#pragma once

#include "resdefs.h"

class FolderTask : public LProgressDlg
{
protected:
	ScribeWnd *App = NULL;
	ScribeFolder *Folder = NULL;
	bool Loading = true;
	
	LString MimeType;
	
	LAutoPtr<LStreamI> Stream;

	ThingType::IoProgress Status;
	ThingType::IoProgressCallback onComplete;

public:
	// Minimum amount of time to do work.
	constexpr static int WORK_SLICE_MS		= 130;
	// This should be larger then WORK_SLICE_MS to allow message loop to process
	constexpr static int PULSE_MS			= 200;

	FolderTask(	ScribeFolder *folder,
				LAutoPtr<LStreamI> stream,
				LString mimeType,
				ThingType::IoProgressCallback cb) :
		LProgressDlg(folder->App),
		Folder(folder),
		Stream(stream),
		MimeType(mimeType),
		onComplete(cb),
		Status(Store3Success)
	{
		App = Folder->App;
		Status.prog = this;
		Ts = LCurrentTime();
		SetParent(Folder->GetTree());		
		SetAlwaysOnTop(true);

		App->OnFolderTask(this, true);

		Folder->WhenLoaded(_FL, [this](auto status)
		{
			Loading = false;
			SetPulse(PULSE_MS);
		});

		if (!Folder->IsLoaded())
			Folder->LoadThings();
	}
	
	virtual ~FolderTask()
	{
		App->OnFolderTask(this, false);
		
		if (onComplete)
			onComplete(&Status, Stream);

		Status.prog = NULL;
	}

	bool OnRequestClose(bool OsClose)
	{
		return true;
	}

	void OnPulse()
	{
		LProgressDlg::OnPulse();

		if (Loading)
			return;

		// We aren't checking IsCancelled() here to allow the
		// TimeSlice implementation a chance to clean up anything relevant
		// before returning false to this caller.
		auto StartTs = LCurrentTime();
		while ((LCurrentTime() - StartTs) < WORK_SLICE_MS)
		{
			if (!TimeSlice())
			{
				Quit();
				break;
			}
		}
	}
	
	/// This should use around WORK_SLICE_MS of time and then
	/// \returns true if more work to do or false if finished.
	/// Do check for IsCancelled() while doing work and return
	/// false.
	virtual bool TimeSlice() = 0;
};

class MboxParser : public LStringPipe
{
	LStreamI *Src = NULL;
	int Hdrs = 0;
	bool NewMsg = true;
	LArray<char> Buf;
	int64 Pos = 0;
	bool Eof = false;
	
	bool IsMessageHdr(LString c)
	{
		// check that it's a from line
		auto parts = c.SplitDelimit(" \r");
		if (parts.Length() >= 7 &&
			parts.Length() <= 9 &&
			parts[0].Equals("From"))
		{
			return true;
		}

		return false;
	}

	struct Blk
	{
		uint8_t *ptr;
		ssize_t size;
	};

	struct Blocks : public LArray<Blk>
	{
		ssize_t Bytes = 0;

		Blocks(LMemQueue *q)
		{
			q->Iterate([this](auto ptr, auto size)
			{
				auto &b = New();
				
				b.ptr = ptr;
				b.size = size;
				Bytes += size;

				return true;
			});
		}

		LString GetLine(size_t idx, size_t offset)
		{
			char buf[256];
			int ch = 0;

			for (size_t i = idx; i < Length(); i++)
			{
				auto &b = (*this)[i];
				auto p = b.ptr + offset;
				auto end = b.ptr + b.size;
				while (p < end)
				{
					if (*p == '\n' || ch == sizeof(buf)-1)
						return LString(buf, ch);

					buf[ch++] = *p++;
				}
			}

			return LString();
		}

		bool ValidateSeparator(LString ln)
		{
			auto p = ln.SplitDelimit();

			if (p.Length() < 7)
				return false;

			if (!p[0].Equals("From"))
				return false;

			if (p[1].Find("@") < 0)
				return false;

			bool hasYear = false;
			bool hasTime = false;
			for (int i=2; i<p.Length(); i++)
			{
				auto &s = p[i];
				if (s.Length() == 4)
				{
					auto val = s.Int();
					if (val >= 1800 && val < 2200)
						hasYear = true;
				}
				else if (s.Find(":") > 0)
				{
					int colons = 0, nonDigits = 0;
					for (auto p = s.Get(); *p; p++)
						if (*p == ':')
							colons++;
						else if (!IsDigit(*p))
							nonDigits++;
					if (colons == 2 && nonDigits == 0)
						hasTime = true;
				}
			}

			return hasYear && hasTime;
		}

		ssize_t FindBoundary(ssize_t start)
		{
			const char *key = "\nFrom ";
			const char *k = key + 1;
			size_t idx = 0;
			ssize_t offset = 0;

			if (Bytes == 0)
				return -1;

			if (start < 0 || start >= Bytes)
			{
				LAssert(!"Start out of range.");
				return -1;
			}

			// Seek to the right starting block...
			while (idx < Length())
			{
				auto &b = (*this)[idx];
				if (start < b.size)
					break;
				start -= b.size;
				offset += b.size;
				idx++;
			}
			
			// Start searching for the key...
			while (idx < Length())
			{
				auto &b = (*this)[idx];
				
				auto end = b.ptr + b.size;
				for (auto p = b.ptr + start; p < end; p++)
				{
					if (*k == *p)
					{
						if (*++k == 0)
						{
							// Found the "From " part, but lets check the rest of the line.
							// Should be in the format:
							//		From sender date more-info
							auto blkAddr = (p - b.ptr) - 4;
							LString ln = GetLine(idx, blkAddr);
							if (ln && ValidateSeparator(ln))
								return offset + blkAddr;
						}
					}
					else k = key;
				}

				offset += b.size;
				idx++;
			}

			return -1;
		}

		LRange FindMsg(MboxParser &parser)
		{
			auto start = FindBoundary(0);
			if (start > 0)
				LgiTrace("%s:%i - Usually the start should be 0, but it's " LPrintfSSizeT "?\n",
					_FL, start);

			if (start >= 0)
			{
				auto end = FindBoundary(start + 5);
				if (end > start)
				{
					return LRange(start, end - start);
				}
				else if (parser.Eof)
				{
					return LRange(start, Bytes);
				}
			}

			return LRange(-1, 0);
		}
	};

public:
	MboxParser(LStreamI *s) : LStringPipe(128 << 10)
	{
		Src = s;
		// _debug = true;
		Buf.Length(128 << 10);
	}

	bool ReadSource()
	{
		auto rd = Src->Read(Buf.AddressOf(), Buf.Length());
		if (rd <= 0)
		{
			// Src stream is empty or in an error state..
			Eof = true;
			return false;
		}

		auto wr = Write(Buf.AddressOf(), Buf.Length());
		if (wr <= 0)
		{
			LgiTrace("%s:%i - Failed to write to local buffer.\n", _FL);
			return false;
		}

		return true;
	}

	LAutoPtr<LStreamI> ReadMessage()
	{
		LAutoPtr<LStreamI> m;

		while (true)
		{
			Blocks blks(this);
			auto r = blks.FindMsg(*this);
			if (r.Start >= 0)
			{
				LMemStream *ms = NULL;

				/*
				LgiTrace("ReadMsg " LPrintfInt64 " %s\n", Pos, r.GetStr());
				auto InitPos = Pos;
				*/

				Pos += r.Len;
				m.Reset(ms = new LMemStream(this, r.Start, r.Len));

				/* Debugging...
				auto key = "The package name is vmware_addons.";
				auto base = ms->GetBasePtr();
				auto result = Strnistr(base, key, m->GetSize());
				if (result)
					LgiTrace("Found the Key @ " LPrintfInt64 "\n", InitPos + (result - base));
				*/
				break;
			}
			else if (!ReadSource())
			{
				r = blks.FindMsg(*this);
				if (r.Start >= 0)
					m.Reset(new LMemStream(this, r.Start, r.Len));
				break;
			}
		}

		return m;
	}
};

class ImportFolderTask : public FolderTask
{
	LDataStoreI::StoreTrans trans;
	LAutoPtr<MboxParser> Parser;
	
public:
	ImportFolderTask(ScribeFolder *fld, LAutoPtr<LStreamI> in, LString mimeType, ThingType::IoProgressCallback cb) :
		FolderTask(fld, in, mimeType, cb)
	{
		SetDescription(LLoadString(IDS_MBOX_READING));
		SetType("K");
		SetScale(1.0/1024.0);
		SetRange(Stream->GetSize());

		trans = fld->GetObject()->GetStore()->StartTransaction();
	}
	
	bool TimeSlice()
	{
		auto Start = LCurrentTime();
		bool Eof = false;

		if (!Parser)
			Parser.Reset(new MboxParser(Stream));
		
		while ( Parser
				&&
				LCurrentTime() - Start < WORK_SLICE_MS
		        &&
		        !IsCancelled())
		{
			auto Msg = Parser->ReadMessage();
			if (Msg)
			{
				Mail *m = dynamic_cast<Mail*>(App->CreateItem(MAGIC_MAIL, Folder, false));
				if (m)
				{
					m->OnAfterReceive(Msg);
					m->SetFlags(MAIL_RECEIVED|MAIL_READ);
					m->Save();
					m->Update();
				}
				else
				{
					Eof = true;
					break;
				}
			}
			else
			{
				Eof = true;
				break;
			}

			Value(Stream->GetPos());
		}
		
		return !Eof && !IsCancelled();
	}
};

class ExportFolderTask : public FolderTask
{
	int Idx = 0;

public:
	ExportFolderTask(	ScribeFolder *folder,
						LAutoPtr<LStreamI> out,
						LString mimeType,
						ThingType::IoProgressCallback cb) :
		FolderTask(folder, out, mimeType, cb)
	{
		bool Mbox = _stricmp(MimeType, sMimeMbox) == 0;

		// Clear the files contents
		Stream->SetSize(0);

		// Setup progress UI
		SetDescription(Mbox ? LLoadString(IDS_MBOX_WRITING) : (char*)"Writing...");
		SetRange(Folder->Items.Length());
		
		switch (Folder->GetItemType())
		{
			case MAGIC_MAIL:
				SetType(LLoadString(IDS_EMAIL));
				break;
			case MAGIC_CALENDAR:
				SetType(LLoadString(IDS_CALENDAR));
				break;
			case MAGIC_CONTACT:
				SetType(LLoadString(IDS_CONTACT));
				break;
			case MAGIC_GROUP:
				SetType("Groups");
				break;
			default:
				SetType("Objects");
				break;
		}

		SetAlwaysOnTop(true);
	}
	
	bool TimeSlice()
	{
		auto Start = LCurrentTime();
		while (	LCurrentTime() - Start < WORK_SLICE_MS
				&&
		        !IsCancelled())
		{
			if (Idx >= (ssize_t)Folder->Items.Length())
				return false;

			// Process all the container's items
			Thing *t = Folder->Items[Idx++];
			if (!t)
				return false;

			LAutoPtr<LStreamI> wrapper(new LProxyStream(Stream));
			if (!t->Export(wrapper, MimeType))
			{
				Status.status = Store3Error;
				Status.errMsg = "Error exporting items.";
				return false;
			}
			
			Value(Idx);
		}
		
		return !IsCancelled();
	}
};



