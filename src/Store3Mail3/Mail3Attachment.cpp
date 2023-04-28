#include "Mail3.h"
#include "lgi/common/TextConvert.h"

Mail3BlobStream::Mail3BlobStream(LMail3Store *store, int segid, int size, const char *file, int line, bool Write)
{
	Store = store;
	File = file;
	Line = line;
	b = 0;
	Pos = 0;
	Size = size;
	SegId = segid;
	WriteAccess = Write;
	
	#if MAIL3_TRACK_OBJS
	LMail3Store::SqliteObjs &_d = Store->All.New();
	_d.Stream = this;
	#endif

	OpenBlob();
}

Mail3BlobStream::~Mail3BlobStream()
{
	CloseBlob();
	#if MAIL3_TRACK_OBJS
	Store->RemoveFromAll(this);
	#endif
}

bool Mail3BlobStream::OpenBlob()
{
	bool Res = Store->Check(sqlite3_blob_open(Store->GetDb(),
											NULL, // Database
											"MailSegs",
											"Data",
											SegId,
											WriteAccess,
											&b), 0);
	// if (b) LgiTrace("%s:%i - open blob %p\n", _FL, b);
	return Res;
}

bool Mail3BlobStream::CloseBlob()
{
	if (!b)
		return true;

	// LgiTrace("%s:%i - close blob %p\n", _FL, b);
	int Res = sqlite3_blob_close(b);
	b = NULL;
	return Store->Check(Res, 0);
}

int64 Mail3BlobStream::GetPos()
{
	return Pos;
}

int64 Mail3BlobStream::SetPos(int64 p)
{
	if (p < 0)
		p = 0;
	if (p > Size)
		p = Size;
	return Pos = p;
}

int64 Mail3BlobStream::GetSize()
{
	return Size;
}

int64 Mail3BlobStream::SetSize(int64 sz)
{
	LAssert(!"You can't set the size of a blob.");
	return Size;
}



ssize_t Mail3BlobStream::Read(void *Buf, ssize_t Len, int Flags)
{
	if (!b)
		return 0;

	int64 Remain = Size - Pos;
	int64 Copy = MIN(Remain, Len);
	auto Res = sqlite3_blob_read(b, Buf, (int)Copy, (int)Pos);
	if (Res == SQLITE_ABORT)
	{
		if (!CloseBlob())
			return 0;	
		if (!OpenBlob())
			return 0;	
		Res = sqlite3_blob_read(b, Buf, (int)Copy, (int)Pos);
	}
	
	if (!Store->Check(Res, 0))
		return 0;

	Pos += Copy;
	return (ssize_t) Copy;
}

ssize_t Mail3BlobStream::Write(const void *Buf, ssize_t Len, int Flags)
{
	if (!b)
		return 0;

	auto Res = sqlite3_blob_write(b, Buf, (int)Len, (int)Pos);
	if (Res == SQLITE_ABORT)
	{
		if (!CloseBlob())
			return 0;	
		if (!OpenBlob())
			return 0;	
		Res = sqlite3_blob_write(b, Buf, (int)Len, (int)Pos);
	}

	if (!Store->Check(Res, 0))
		return 0;
	
	Pos += Len;
	return Len;
}

///////////////////////////////////////////////////////////////////////////////////////
LMail3Attachment::LMail3Attachment(LMail3Store *store) :
	Store3Attachment<LMail3Store, LMail3Mail, LMail3Attachment>(store)
{
	SegId = -1;
	BlobSize = 0;
	InMemoryOnly = false;
}

LMail3Attachment::~LMail3Attachment()
{
	_Delete();
}

void LMail3Attachment::SetInMemoryOnly(bool b)
{
	InMemoryOnly = b;
	for (unsigned i=0; i<Children.Length(); i++)
	{
		Children.a[i]->SetInMemoryOnly(b);
	}
}

uint64 LMail3Attachment::Size()
{
	int64 HeaderSz = Headers.Length();
	int64 NameSz = Name.Length();
	int64 MimeSz = MimeType.Length();
	int64 ContentIdSz = ContentId.Length();
	int64 CharsetSz = Charset.Length();
	int64 ImportSz = Import ? Import->GetSize() : 0;
	
	return	HeaderSz +
			NameSz +
			MimeSz +
			ContentIdSz +
			CharsetSz +
			BlobSize +
			sizeof(BlobSize) + 
			sizeof(SegId) +
			ImportSz;
}

uint64 LMail3Attachment::SizeChildren()
{
	uint64 s = 0;
	for (unsigned i=0; i<Children.Length(); i++)
	{
		LMail3Attachment *c = Children.a[i];
		s += c->Size();
		s += c->SizeChildren();
	}
	return s;	
}

LMail3Attachment *LMail3Attachment::Find(int64 Id)
{
	if (SegId == Id)
		return this;

	for (unsigned i=0; i<Children.Length(); i++)
	{
		LMail3Attachment *r = Children.a[i]->Find(Id);
		if (r)
			return r;
	}

	return 0;
}

Store3CopyImpl(LMail3Attachment)
{
	Headers = p.GetStr(FIELD_INTERNET_HEADER);
	if (Headers)
	{
		// Source supports headers...
		ParseHeaders();
	}
	else
	{
		// Copy over whatever fields we can...
		Name = p.GetStr(FIELD_NAME);
		MimeType = p.GetStr(FIELD_MIME_TYPE);
		ContentId = p.GetStr(FIELD_CONTENT_ID);
		Charset = p.GetStr(FIELD_CHARSET);
	}
	
	LDataI *Data = dynamic_cast<LDataI*>(&p);
	if (Data)
	{
		LAutoStreamI tmp = Data->GetStream(_FL);
		SetStream(tmp);
	}
	return true;
}

char *LMail3Attachment::GetHeaders()
{
	if (!Headers)
	{
		LStringPipe p;
		if (!MimeType)
		{
			LAssert(!"MimeType is required.");
			return NULL;
		}
		p.Print("Content-Type: %s", MimeType?MimeType.Get():(char*)"text/plain");
		if (Charset)
			p.Print("; charset=%s", Charset.Get());
		if (Name)
			p.Print("; name=\"%s\"", Name.Get());
		p.Print("\r\n");
		if (ContentId)
		{
			p.Print("Content-Id: <%s>\r\n", ContentId.Strip("<>").Get());
			if (Name)
				p.Print("Content-Disposition: inline; filename=\"%s\"\r\n", Name.Get());
		}
		Headers = p.NewLStr();		
	}

	return Headers;
}

bool LMail3Attachment::ParseHeaders()
{
	auto Ct = LGetHeaderField(Headers, "Content-Type");
	char *Colon = Ct ? strchr(Ct, ';') : 0;
	if (Colon)
	{
		while (Colon > Ct.Get() && strchr(" \t\r\n", Colon[-1])) Colon--;

		LAutoString Cs(InetGetSubField(Colon, "charset"));
		if (Cs)
		{
			Charset = Cs.Get();
			LAssert(!strchr(Charset, '>'));
		}
		
		*Colon = 0;
	}

	MimeType = Ct;
	
	return true;
}

bool LMail3Attachment::Load(LMail3Store::LStatement &s, int64 &ParentId)
{
	SegId    = s.GetInt64(0);
	ParentId = s.GetInt64(2);	
	Headers  = s.GetStr(3);
	ParseHeaders();
	
	BlobSize = s.GetSize(4);
	Dirty = false;

	#ifdef _DEBUG
	// This was a hack to fix a dumb bug in a dev build... so so dumb.
	if (Headers)
	{
		auto cdcd = Strstr(Headers.Get(), "\xcd\xcd");
		if (cdcd)
		{
			*cdcd = 0;
			Dirty = true;
		}
	}	
	#endif

	LAssert(!Mail || Kit == Mail->Store);

	return true;
}

Store3Status LMail3Attachment::Save(LDataI *NewParent)
{
	if (NewParent)
	{
		// Check hierarchy
		LMail3Attachment *NewSeg = dynamic_cast<LMail3Attachment*>(NewParent);
		if (NewSeg)
		{
			// This propagates the in mem only setting down the tree of nodes
			InMemoryOnly = NewSeg->InMemoryOnly;
			if (NewSeg != Parent)
				AttachTo(NewSeg);
		}
		else
		{
			LMail3Mail *NewMail = dynamic_cast<LMail3Mail*>(NewParent);
			if (NewMail)
			{
				if (NewMail != Mail)
					AttachTo(NewMail);
			}
		}
	}

	if (Mail)
	{
		// Mark the mail size dirty.
		Mail->MailSize = -1;

		LAssert(Kit == Mail->Store);
	}

	if (!InMemoryOnly)
	{
		if (Mail && Mail->Id > 0)
		{
			if (SegId <= 0)
			{
				LMail3Attachment *Parent = GetParent();

				LMail3Store::LInsert Ins(Kit, MAIL3_TBL_MAILSEGS);
				Ins.SetInt64(1, Mail->Id);
				Ins.SetInt64(2, Parent ? Parent->SegId : -1);
				Ins.SetStr(3, GetHeaders());
				if (Import)
					Ins.SetStream(4, "Data", Import);
				if (!Ins.Exec())
					return Store3Error;

				SegId = Ins.LastInsertId();
			}
			else if (Dirty)
			{
				LMail3Attachment *Parent = GetParent();

				LMail3Store::LUpdate Up(Kit, MAIL3_TBL_MAILSEGS, SegId, Import ? 0 : (char*)"Data");
				Up.SetInt64(0, SegId);
				Up.SetInt64(1, Mail->Id);
				Up.SetInt64(2, Parent ? Parent->SegId : -1);
				Up.SetStr(3, GetHeaders());
				if (Import)
					Up.SetStream(4, "Data", Import);
				if (!Up.Exec())
					return Store3Error;

			}

 			Import.Reset();
			Dirty = false;
		}
		else
		{
			Dirty = true;
		}
	}

	return Store3Success;
}

void LMail3Attachment::OnSave()
{
	if (!Mail)
	{
		LAssert(!"Segment is not attached to a mail!");
		return;
	}

	if (Dirty || SegId <= 0)
	{
		Save();
	}

	for (unsigned i=0; i<Children.Length(); i++)
	{
		Children.a[i]->OnSave();
	}
}

const char *LMail3Attachment::GetStr(int id)
{
	switch (id)
	{
		case FIELD_CHARSET:
		{
		    if (!Charset)
		    {
			    // Maybe a parent segment has a charset?
			    for (LMail3Attachment *p = GetParent(); p; p = p->GetParent())
			    {
			        auto Cs = p->GetStr(FIELD_CHARSET);
			        if (Cs)
			            return Cs;
			    }
			}

			return Charset;
		}
		case FIELD_NAME:
		{
			if (!Name)
			{
				auto t = LGetHeaderField(Headers, "Content-Disposition");				
				auto n = LDecodeRfc2047(LGetSubField(t, "filename"));
				if (n)
					Name = n.Get();
				else
				{
					auto ct = LGetHeaderField(Headers, "Content-Type");
					if (ct)
					{
						if (n = LDecodeRfc2047(LGetSubField(ct, "name")))
							Name = n.Get();
					}
				}
			}

			return Name;
			break;
		}
		case FIELD_MIME_TYPE:
		{
			if (!MimeType)
			{
				auto t = LGetHeaderField(Headers, "Content-Type");
				if (t)
				{
					// Trim off any sub-feilds.
					auto c = strchr(t, ';');
					if (c)
					{
						while (strchr(" \t\r\n", c[-1]))
							c--;
						*c = 0;
						MimeType.Set(t, c - t.Get());
					}
				}
			}

			return MimeType;
			break;
		}
		case FIELD_CONTENT_ID:
		{
			if (!ContentId)
			{
				LAutoString Id(InetGetHeaderField(Headers, "Content-Id"));
				ContentId = LString(Id.Get()).Strip("<>");
			}

			return ContentId;
			break;
		}
		case FIELD_INTERNET_HEADER:
			return Headers;
	}

	LAssert(!"Unknown id.");
	return NULL;
}

Store3Status LMail3Attachment::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_INTERNET_HEADER:
        	Headers = str;
			ParseHeaders();
			break;
		case FIELD_NAME:
			Name = str;
			Headers.Empty();
			break;
		case FIELD_MIME_TYPE:
			MimeType = str;

			// FIXME: If we have headers from an incoming mail, this is an error to delete them here.
			LAssert(Headers.Get() == NULL);

			Headers.Empty();
			break;
		case FIELD_CONTENT_ID:
			ContentId = LString(str).Strip("<>");
			Headers.Empty();
			break;
		case FIELD_CHARSET:
			Charset = str;
			if (Charset.Find(">") >= 0)
				LAssert(!"Invalid char");
			
			Headers.Empty();
			break;
		default:
			LAssert(!"Unknown id.");
			return Store3Error;
	}

	return Store3Success;
}

int64 LMail3Attachment::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
		case FIELD_SIZE:
			return BlobSize;
	}

	LAssert(!"Unknown id.");
	return false;
}

Store3Status LMail3Attachment::SetInt(int id, int64 i)
{
	LAssert(!"Unknown id.");
	return Store3Error;
}

Store3Status LMail3Attachment::Delete(bool ToTrash)
{
	if (InMemoryOnly)
		return Store3Success;

	if (SegId <= 0)
		return Store3Error;

	LString Sql;
	Sql.Printf("delete from " MAIL3_TBL_MAILSEGS " where Id=" LPrintfInt64, SegId);
	LMail3Store::LStatement s(Kit, Sql);
	if (!s.Exec())
		return Store3Error;

	SegId = -1;
	return Store3Success;
}

LAutoStreamI LMail3Attachment::GetStream(const char *file, int line)
{
	LAutoStreamI Ret;

	if (Import)
		Ret.Reset(new LProxyStream(Import));
	else if (SegId > 0 && BlobSize > 0)
		Ret.Reset(new Mail3BlobStream(Kit, (int)SegId, (int)BlobSize, file, line));

	return Ret;
}

bool LMail3Attachment::SetStream(LAutoStreamI s)
{
	Import = s;
	Dirty = true;
	BlobSize = Import ? Import->GetSize() : 0;
	
	if (Mail)
		Mail->ResetCaches();
	
	return true;
}
