#include "Store3Mail2.h"
#include "resdefs.h"

AttachmentData::AttachmentData(LMail2Store *store) : Store3Attachment<LMail2Store, MailData, AttachmentData>(store)
{
	Content = CONTENT_NONE;
	DataSize = 0;
	Name = 0;
	MimeType = 0;
	ContentId = 0;
	Import = 0;
	Charset = 0;
	MailBody = 0;
	MailCharset = 0;
	PlaceHolder = false;
	IsLoaded = false;
}

AttachmentData::~AttachmentData()
{
	DeleteArray(Name);
	DeleteArray(MimeType);
	DeleteArray(ContentId);
	DeleteArray(Charset);
	DeleteObj(Import);

	_Delete();
}

LArray<AttachmentData*> AttachmentData::GetChildren()
{
	LArray<AttachmentData*> arr;
	for (unsigned i=0; i<Children.Length(); i++)
		arr.Add(dynamic_cast<AttachmentData*>(Children[i]));
	return arr;
}

void AttachmentData::SetExtern(char **body, char **charset)
{
	MailBody = body;
	MailCharset = charset;
}

void AttachmentData::OnSave()
{
	if (!PlaceHolder && Mail && Mail->Store && (Import != 0 || Dirty))
	{
		if (!Store)
		{
			// Create a new node on the tree...
			StorageItem *it = Mail->Store->CreateSub(this);
			if (it)
			{
				IsLoaded = true;
				Store = it;
			}
		}
		else if (Dirty)
		{
			// Update the existing node on the tree...
			LAssert(!"Impl me.");
		}
	}

	for (unsigned i=0; i<Children.Length(); i++)
	{
		Children.a[i]->OnSave();
	}
}

int AttachmentData::Sizeof()
{
	int Of =	sizeof(ulong) +		// magic
				sizeof(Content) +	// Content type
				sizeof(DataSize) +	// Size
				SizeofStr(Name) +	// File name
				DataSize;			// File itself

	if (MimeType)
	{
		Of += SizeStrField(MimeType);
	}
	if (ContentId)
	{
		Of += SizeStrField(ContentId);
	}
	if (Charset)
	{
		Of += SizeStrField(Charset);
	}

	return Of;
}

bool AttachmentData::Serialize(LFile &f, bool Write)
{
	ulong Magic = MAGIC_ATTACHMENT;

	if (Write)
	{
		f << Magic;
		f << Content;

		/*
		// Check we can open the file...
		while (!In.Open(ImportName, O_READ))
		{
			char Msg[256];
			sprintf(Msg, LLoadString(IDS_ERROR_CANT_READ), ImportName);
			LAlert Dlg(	Parent,
						AppName,
						Msg,
						LLoadString(IDS_RETRY),
						LLoadString(IDS_CANCEL));
			int Result = Dlg.DoModal();
			if (Result == 2)
			{
				break;
			}
		}
		*/

		f << DataSize;
		WriteStr(f, Name);

		if (Import)
		{
			// import from the file
			uint64 Last = LCurrentTime();

			// LgiTrace("Write: Attachment data @ %I64i\n", f.GetPos());
			int64 CurSize = Import->GetSize();
			if (CurSize != DataSize)
			{
				LAssert(!"Size of import object changed?!?");
			}

			Import->SetPos(0);

			int BufSize = 64 << 10;
			uchar *Buf = new uchar[BufSize];
			if (Buf)
			{
				int s = DataSize;
				while (s > 0)
				{
					int r = min(s, BufSize);
					r = Import->Read(Buf, r);
					if (r > 0)
					{
						f.Write(Buf, r);
						s -= r;
					}
					else break;
				}
			}

			DeleteArray(Buf);
			DeleteObj(Import);
		}
		else
		{
			// skip over data on hard disk
			f.Seek(DataSize, SEEK_CUR);
		}

		// new style fields
		if (MimeType)	WriteStrField(FIELD_MIME_TYPE, MimeType);
		if (ContentId)	WriteStrField(FIELD_CONTENT_ID, ContentId);
		if (Charset)	WriteStrField(FIELD_CHARSET, Charset);
	}
	else
	{
		f >> Magic;
		if (Magic == MAGIC_ATTACHMENT)	// The versions before v1.25 didn't
										// set this correctly, but that is so old
										// now, I've removed the hack to allow
										// the attachment in.
		{
			f >> Content;
			f >> DataSize;
			DeleteArray(Name);
			Name = ReadStr(f PassDebugArgs);

			int64 StartPos = f.GetPos();

			f.Seek(DataSize, SEEK_CUR);
			
			int64 ExtraPos = f.GetPos();

			// read list of new-style fields
			bool Done = false;
			bool Eob = false;

			while (	!(Eob = Store->EndOfObj(f)) &&
					!Done)
			{
				short FieldId = 0;
				ulong FieldSize = 0;

				f >> FieldId;
				switch (FieldId)
				{
					ReadStrField(FIELD_MIME_TYPE, MimeType);
					ReadStrField(FIELD_CONTENT_ID, ContentId);
					ReadStrField(FIELD_CHARSET, Charset);
					default:
					{
						// Error: unknown chunk
						SetDirty();
						Done = true;
						break;
					}
				}
			}
		}
		else return false;
	}

	Dirty = false;

	return f.GetStatus();
}

LDataI &AttachmentData::operator =(LDataI &p)
{
	SetStr(FIELD_MIME_TYPE, p.GetStr(FIELD_MIME_TYPE));
	SetStr(FIELD_NAME, p.GetStr(FIELD_NAME));
	SetStr(FIELD_CONTENT_ID, p.GetStr(FIELD_CONTENT_ID));
	SetStr(FIELD_CHARSET, p.GetStr(FIELD_CHARSET));
	SetStr(FIELD_INTERNET_HEADER, p.GetStr(FIELD_INTERNET_HEADER));

    GAutoStreamI tmp = p.GetStream(_FL);
	SetStream(tmp);

	return *this;
}

bool AttachmentData::IsOnDisk()
{
	return Store != 0;
}

bool AttachmentData::IsOrphan()
{
	return Mail == 0;
}

uint64 AttachmentData::Size()
{
	return Sizeof();
}

Store3Status AttachmentData::Save(LDataI *NewParent)
{
	if (NewParent)
	{
		// Check heirarchy
		AttachmentData *NewSeg = dynamic_cast<AttachmentData*>(NewParent);
		if (NewSeg)
		{
			if (NewSeg != Parent)
			{
				Detach();
				AttachTo(NewSeg);
			}
		}
		else
		{
			MailData *NewMail = dynamic_cast<MailData*>(NewParent);
			if (NewMail)
			{
				if (NewMail != Mail)
				{
					Detach();
					AttachTo(NewMail);
				}
			}
		}
	}

	if (!PlaceHolder)
	{
		if (!Store)
		{
			if (Mail && Mail->Store)
			{
				StorageItem *it = Mail->Store->CreateSub(this);
				if (it)
				{
					Store = it;
					Dirty = false;
				}
			}
		}
		else
		{
			LAutoPtr<LFile> f(Store->GotoObject(_FL));
			if (f && Serialize(*f, true))
				Dirty = false;
		}
	}	

	return Store3Success;
}

Store3Status AttachmentData::Delete()
{
	if (MailBody)
	{
		if (MailCharset)
			DeleteArray(*MailCharset);
		DeleteArray(*MailBody);
		return Store3Success;
	}

	if (Store)
	{
		if (Kit->DeleteItem(Store))
		{
			Store = 0;
			return Store3Success;
		}
	}

	return Store3Error;
}

class BodyStream : public LStreamI
{
	int Pos;
	char **Ptr;

public:
	BodyStream(char **p)
	{
		Pos = 0;
		Ptr = p;
	}

	int64 GetSize()
	{
		return *Ptr ? strlen(*Ptr) : 0;
	}

	int Write(const void *b, int s, int f)
	{
		DeleteArray(*Ptr);
		*Ptr = NewStr((char*)b, s);
		return s;
	}

	int Read(void *b, int s, int f)
	{
		if (!*Ptr)
			return -1;

		int Len = strlen(*Ptr);
		int c = min(s, Len - Pos);
		memcpy(b, *Ptr + Pos, c);
		Pos += c;
		return c;
	}
};

GAutoStreamI AttachmentData::GetStream(const char *file, int line)
{
	GAutoStreamI Ret;

	if (Import)
	{
		// In memory object, no data offset...
		Ret.Reset(new LProxyStream(Import));
	}
	else if (MailBody)
	{
		Ret.Reset(new BodyStream(MailBody));
	}
	else if (Store)
	{
		Load();

		// On disk object...
		LStreamI *s = Store ? Store->GotoObject(file, line) : 0;
		if (s)
		{
			// Need to skip over the header data...
			int Offset = 12 + SizeofStr(Name);
			LSubFilePtr *Sub = dynamic_cast<LSubFilePtr*>(s);
			if (Sub)
			{
				int64 Start = 0, Len = 0;
				Sub->GetSub(Start, Len);
				Sub->SetSub(Start + Offset, DataSize);
				Ret.Reset(s);
			}
			else DeleteObj(s);
		}
	}

	return Ret;
}

bool AttachmentData::SetStream(GAutoStreamI s)
{
	DeleteObj(Import);

	if (s)
	{
		size_t Sz = (size_t)s->GetSize();

		if (Sz > 0xffffffffL)
			return false;

		if (MailBody)
		{
			DeleteArray(*MailBody);
			if ((*MailBody = new char[Sz+1]))
			{
				int64 r = s->Read(*MailBody, Sz);
				LAssert(r == Sz);
				if (r > 0)
					(*MailBody)[r] = 0;
				else
				{
					DeleteArray(*MailBody);
					return false;
				}
			}
			else return false;

			s.Reset();
			Dirty = false;
		}
		else
		{
			DataSize = Sz;
			Import = s.Release();
		}
		return true;
	}

	return false;
}

void AttachmentData::Load()
{
	if (IsLoaded)
		return;

	if (Store)
	{
		IsLoaded = true;
		LFile *f = Store->GotoObject(_FL);
		if (f)
		{
			Serialize(*f, false);
			DeleteObj(f);
		}
	}
}

int64 AttachmentData::GetInt(int id)
{
	Load();

	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
		case FIELD_SIZE:
			return DataSize;
	}

	LAssert(0);
	return -1;
}

bool AttachmentData::SetInt(int id, int64 i)
{
	Load();

	switch (id)
	{
		case FIELD_SIZE:
			DataSize = (uint32)i;
			return true;
	}

	LAssert(0);
	return 0;
}

char *AttachmentData::GetStr(int id)
{
	Load();

	switch (id)
	{
		case FIELD_NAME:
			return Name;
		case FIELD_MIME_TYPE:
			return MimeType;
		case FIELD_CONTENT_ID:
			return ContentId;
		case FIELD_CHARSET:
			return MailCharset ? *MailCharset : Charset;
		case FIELD_INTERNET_HEADER:
		{
			// Synthesize some headers from the various parts we've got.
			if (!InternetHeaderCache)
			{
				LStringPipe p;
				char *Cs = MailCharset ? *MailCharset : Charset;

				char *Type = MimeType ? MimeType : (char*)"text/plain";
				p.Print("Content-Type: %s", Type);
				if (Cs)
					p.Print("; charset=%s", Cs);
				if (Name)
					p.Print("; name=\"%s\"", Name);
				p.Print("\r\n");
				if (ContentId)
					p.Print("Content-ID: <%s>\r\n", ContentId);

				const char *Enc = 0;
				if (Content == CONTENT_BASE64)
					Enc = "base64";
				else if (Content == CONTENT_QUOTED_PRINTABLE)
					Enc = "quoted-printable";
				if (Enc)
					p.Print("Content-Transfer-Encoding: %s\r\n", Enc);

				InternetHeaderCache.Reset(p.NewStr());
			}
			return InternetHeaderCache;
			break;
		}
	}

	LAssert(0);
	return 0;
}

bool AttachmentData::SetStr(int id, const char *str)
{
	Load();

	switch (id)
	{
		case FIELD_INTERNET_HEADER:
		{
			LAutoString ContentTransferEncoding(InetGetHeaderField(str, "Content-Transfer-Encoding"));
			if (ContentTransferEncoding)
			{
				if (strnistr(ContentTransferEncoding, "base64", 1000))
					Content = CONTENT_BASE64;
				else if (strnistr(ContentTransferEncoding, "quoted-printable", 1000))
					Content = CONTENT_QUOTED_PRINTABLE;
			}
			return TRUE;
			break;
		}
		case FIELD_NAME:
		{
			_Str(Name);
		}
		case FIELD_MIME_TYPE:
		{
			DeleteArray(MimeType);
			MimeType = NewStr(str);
			PlaceHolder = IsMultipart();
			return true;
		}
		case FIELD_CONTENT_ID:
		{
			_Str(ContentId);
		}
		case FIELD_CHARSET:
		{
			if (MailCharset)
			{
				DeleteArray(*MailCharset);
				*MailCharset = NewStr(str);
			}
			else
			{
				DeleteArray(Charset);
				Charset = NewStr(str);
			}
			return true;
		}
	}

	LAssert(0);
	return 0;
}
