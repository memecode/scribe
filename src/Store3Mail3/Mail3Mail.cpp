#include "Mail3.h"
#include "lgi/common/Mail.h"
#include "lgi/common/Store3MimeTree.h"
#include "lgi/common/StreamConcat.h"
#include "lgi/common/TextConvert.h"
#include "ScribeUtils.h"

LMail3Def TblMail[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"},

	// Integers
		{"Priority",		"INTEGER"},
		{"Flags",			"INTEGER"},
		{"AccountId",		"INTEGER"},
		{"MarkColour",		"INTEGER"},

	// Strings
		{"Subject",			"TEXT"},
		{"ToAddr",			"TEXT"},
		{"FromAddr",		"TEXT"},
		{"Reply",			"TEXT"},
		{"Label",			"TEXT"},
		{"InternetHeader",	"TEXT"},
		{"MessageID",		"TEXT"},
		{"Ref",				"TEXT"},
		{"FwdMsgId",		"TEXT"},
		{"BounceMsgId",		"TEXT"},
		{"ServerUid",		"TEXT"},

	// Date/times
		{"DateReceived",	"TEXT"},
		{"DateSent",		"TEXT"},

    // Other meta data
		{"Size",    		"INTEGER"},

	{0, 0}
};

LMail3Def TblMailSegs[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"MailId",			"INTEGER"},
	{"ParentId",		"INTEGER"},
	{"Headers",			"TEXT"},
	{"Data",			"BLOB"},
	
	{0,					0},
};

bool Mail3_InsertSeg(LMail3Store::LInsert &Ins, LMime *m, int64 ParentId, int64 ParentSeg, int64 &MailSize)
{
	Ins.SetInt64(1, ParentId);
	Ins.SetInt64(2, ParentSeg);
	Ins.SetStr(3, m->GetHeaders());

	LStreamI *MimeData = m->GetData();
	int64 Sz = MimeData ? MimeData->GetSize() : 0;
	if (Sz > 0)
		MailSize += Sz;
	
	Ins.SetStream(4, "Data", MimeData);

	if (!Ins.Exec())
		return false;

	ParentSeg = Ins.LastInsertId();
	Ins.Reset();

	for (int i=0; i<m->Length(); i++)
	{
		LMime *c = (*m)[i];
		if (!Mail3_InsertSeg(Ins, c, ParentId, ParentSeg, MailSize))
			return false;
	}

	return true;
}

bool Mail3_CopySegs(LMail3Mail *m, LMail3Attachment *parent, LDataI *in)
{
	const char *Mt = in->GetStr(FIELD_MIME_TYPE);
	if (!Mt)
	{
		return false;
	}
	
	LMail3Attachment *out = new LMail3Attachment(m->Store);
	if (!out)
		return false;

	out->CopyProps(*in);
	if (parent)
		out->AttachTo(parent);
	else
		out->AttachTo(m);

	LDataIt It = in->GetList(FIELD_MIME_SEG);
	for (LDataPropI *c = It->First(); c; c = It->Next())
	{
		LDataI *child = dynamic_cast<LDataI*>(c);
		if (child && !Mail3_CopySegs(m, out, child))
			return false;
	}

	return true;
}

LMail3Mail::LMail3Mail(LMail3Store *store) :
	LMail3Thing(store),
	From(store),
	Reply(store)
{
	To.State = Store3Loaded;
}

LMail3Mail::~LMail3Mail()
{
	To.DeleteObjects();
	DeleteObj(Seg);
}

#define DEBUG_COPY_PROPS		0

Store3CopyImpl(LMail3Mail)
{
#if DEBUG_COPY_PROPS
LProfile Prof("Store3CopyProps");
#endif
	Priority = (int)p.GetInt(FIELD_PRIORITY);
	Flags = (int)p.GetInt(FIELD_FLAGS);

#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif

	SetStr(FIELD_INTERNET_HEADER, p.GetStr(FIELD_INTERNET_HEADER));
	MessageID = p.GetStr(FIELD_MESSAGE_ID);
	Subject = p.GetStr(FIELD_SUBJECT);
	MarkColour = (int)p.GetInt(FIELD_COLOUR);
	AccountId = (int)p.GetInt(FIELD_ACCOUNT_ID);
	SetStr(FIELD_LABEL, p.GetStr(FIELD_LABEL));

#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
	LDataPropI *i = p.GetObj(FIELD_FROM);
	if (i) From.CopyProps(*i);
	i = p.GetObj(FIELD_REPLY);
	if (i) Reply.CopyProps(*i);
	
#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
	const LDateTime *d = p.GetDate(FIELD_DATE_RECEIVED);
	if (d) DateReceived = *d;
	d = p.GetDate(FIELD_DATE_SENT);
	if (d) DateSent = *d;
	
#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
	To.DeleteObjects();
	LDataIt pTo = p.GetList(FIELD_TO);
	for (unsigned n=0; n<pTo->Length(); n++)
	{
		To.Insert(new Store3Addr(GetStore(), (*pTo)[n]), -1, true);
	}
	To.State = Store3Loaded;

#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
	LDataI *Root = dynamic_cast<LDataI*>(p.GetObj(FIELD_MIME_SEG));
	if (Root)
	{
		// Must commit ourselves now and get an Id if needed
#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
		if (Id > 0 || Write(MAIL3_TBL_MAIL, true))
		{
		    // Clear existing segment if present...
		    LAssert(!Seg || Seg->GetId() < 0);
			DeleteObj(Seg);

#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
			// Save all the segments out
			Mail3_CopySegs(this, NULL, Root);
			
#if DEBUG_COPY_PROPS
Prof.Add(_FL);
#endif
			OnSave();
		}
	}

	return true;
}

void LMail3Mail::SetStore(LMail3Store *s)
{
	if (Id < 0)
	{
		LMail3Thing::SetStore(s);
		From.SetStore(s);
		Reply.SetStore(s);
		if (Seg)
			Seg->SetMail(this);
	}
	else LAssert(!"Object is already commited to another store.");
}

bool SafeAddrTokenize(const char *In, List<char> &Out, bool Debug = false)
{
	if (!In)
		return false;
	
	// Find all the characters of interest...
	LArray<const char*> a;
	for (const char *c = In; *c; c++)
	{
		if (strchr("\':,<@>", *c))
			a.Add(c);
	}
	
	// Now do pattern matching. We may encounter weird numbers of single
	// quotes due to recipient names having un-escaped single quotes.
	const char *Last = In;
	LAutoString Str;
	for (ssize_t i=0; i<(ssize_t)a.Length(); i++)
	{
		// Check for '<' '@', '>' pattern
		if (i < (ssize_t)a.Length() - 4 &&
			*a[i] == '<' &&
			*a[i+1] == '@' &&
			*a[i+2] == '>' &&
			*a[i+3] == ':' &&
			*a[i+4] == ',')
		{
			const char *e = a[i+4];
			Str.Reset(NewStr(Last, e-Last));
			Last = e + 1;
		}
		else if (i < (ssize_t)a.Length() - 3 &&
			*a[i] == '<' &&
			*a[i+1] == '@' &&
			*a[i+2] == '>' &&
			*a[i+3] == ':')
		{
			const char *e = a[i+3];
			while (*e) e++;
			Str.Reset(NewStr(Last, e-Last));
			Last = e;
		}
		else if (i < (ssize_t)a.Length() - 2 &&
			*a[i] == '<' &&
			*a[i+1] == '>' &&
			*a[i+2] == ':')
		{
			// This case handles a group name without a '@' in it...
			const char *e = a[i+2];
			while (*e) e++;
			Str.Reset(NewStr(Last, e-Last));
			Last = e;
		}
		else break;
		
		if (Str)
		{
			Out.Insert(Str.Release());
		}
	}
	
	return true;
}

#if 1
#define DEBUG_SERIALIZE(...)	 if (Debug) LgiTrace(__VA_ARGS__)
#else
#define DEBUG_SERIALIZE(...)
#endif

bool LMail3Mail::Serialize(LMail3Store::LStatement &s, bool Write)
{
	static LVariant vTo, vFrom, vReply;
	bool Debug = false;
	
	// Save the objects to strings
	if (Write)
	{
		if (MailSize < 0 && Seg)
		{
			MailSize = (uint32_t) (Seg->Size() + Seg->SizeChildren());
		}
		
		if (!From.GetValue("Text", vFrom))
			vFrom.Empty();
		if (!Reply.GetValue("Text", vReply))
			vReply.Empty();

		LStringPipe p;
		int Done = 0;
		for (unsigned i=0; i<To.Length(); i++)
		{
			LVariant v;
			Store3Addr *a = To.a[i];
			if (a->GetValue("Text", v) && v.Str())
			{
			    const char *Comma = Done ? ", " : "";
				DEBUG_SERIALIZE("ToText='%s'\n", v.Str());
				p.Print("%s%s:%i", Comma, v.Str(), a->CC);
				Done++;
			}
		}
		vTo.OwnStr(p.NewStr());
		DEBUG_SERIALIZE("vTo='%s'\n", vTo.Str());
	}

	int i = 0;
	LVariant NullInternetHeader; // The internet header is now stored
								// In the first segment ONLY. But it
								// wasn't worth removing the field in
								// the DB.

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);

	SERIALIZE_INT(Priority, i++);
	SERIALIZE_INT(Flags, i++);
	SERIALIZE_INT(AccountId, i++);
	SERIALIZE_INT(MarkColour, i++);

	SERIALIZE_STR(Subject, i++);
	SERIALIZE_STR(vTo, i++);
	SERIALIZE_STR(vFrom, i++);
	SERIALIZE_STR(vReply, i++);
	SERIALIZE_STR(Label, i++);
	SERIALIZE_STR(NullInternetHeader, i++);
	SERIALIZE_STR(MessageID, i++);
	SERIALIZE_STR(References, i++);
	SERIALIZE_STR(FwdMsgId, i++);
	SERIALIZE_STR(BounceMsgId, i++);
	SERIALIZE_STR(ServerUid, i++);

	SERIALIZE_DATE(DateReceived, i++);
	SERIALIZE_DATE(DateSent, i++);

	SERIALIZE_INT64(MailSize, i++);

	// Load the objects from the strings
	if (!Write)
	{
		From.SetValue("Text", vFrom);
		Reply.SetValue("Text", vReply);

		To.DeleteObjects();
		List<char> Addr;
		char *ToStr = vTo.Str();
		//DEBUG_SERIALIZE("ReadTo='%s'\n", ToStr);
		int Colons = 0;
		int Commas = 0;
		for (char *c = ToStr; c && *c; c++)
		{
			if (*c == ':')
				Colons++;
			else if (*c == ',')
				Commas++;
		}
		if (Commas == 1 && Colons > 1)
		{
			// Broken data
			char *c = ToStr;
			while (*c)
			{
				while (*c && (strchr(WhiteSpace, *c) || *c == ','))
					c++;
					
				char *e = strchr(c, '>');
				if (!e)
					break;
				e++;
				if (*e == ':')
					e++;
				while (IsDigit(*e))
					e++;
				
				char *a = NewStr(c, e - c);
				if (!a)
					break;
					
				Addr.Insert(a);
				c = e;
			}
		}
		else
		{
			// Correct data? Hopefully...
			#if 1
			SafeAddrTokenize(vTo.Str(), Addr);
			#else
			TokeniseStrList(vTo.Str(), Addr, ",");
			#endif
		}
		
		for (auto r: Addr)
		{
			Store3Addr *a = new Store3Addr(GetStore());
			if (a)
			{
				char *CcType = strrchr(r, ':');
				if (CcType)
					*CcType++ = 0;

				LAutoString Name, Addr;
				DecodeAddrName(r, Name, Addr, 0);
				
				a->SetStr(FIELD_NAME, Name);
				a->SetStr(FIELD_EMAIL, Addr);
				a->CC = CcType ? atoi(CcType) : 0;
				
				To.Insert(a, -1, true);
			}
		}
		Addr.DeleteArrays();
		To.State = Store3Loaded;
	}

	return true;
}

uint32_t LMail3Mail::Type()
{
	return MAGIC_MAIL;
}

size_t Sizeof(LVariant &v)
{
	int s = 0;
	switch (v.Type)
	{
		default:
			break;
		case GV_STRING:
			return (int)strlen(v.Str());
		case GV_WSTRING:
			return StrlenW(v.WStr()) * sizeof(char16);
	}
	return s + sizeof(v);
}

size_t Sizeof(DIterator<LDataPropI, Store3Addr, LMail3Store> &i)
{
	size_t s = sizeof(i);
	for (unsigned n=0; n<i.Length(); n++)
		s += i.a[n]->Sizeof();

	return s;
}

uint64 LMail3Mail::Size()
{
	if (MailSize < 0 && Seg)
	{
		MailSize = (uint32_t) (Seg->Size() + Seg->SizeChildren());
	}

	return	MailSize + // Size of mime segments...
			sizeof(Priority) +
			sizeof(Flags) +
			sizeof(AccountId) +
			Sizeof(Subject) +
			Sizeof(To) +
			From.Sizeof() +
			Reply.Sizeof() +
			Sizeof(Label) +
			// Sizeof(InternetHeader) +
			Sizeof(MessageID) +
			Sizeof(References) +
			Sizeof(FwdMsgId) +
			Sizeof(BounceMsgId) +
			Sizeof(ServerUid) +
			sizeof(DateReceived) +
			sizeof(DateSent);
}

bool LMail3Mail::DbDelete()
{
	return Store->DeleteMailById(Id);
}

LDataStoreI *LMail3Mail::GetStore()
{
	return Store;
}

LAutoStreamI LMail3Mail::GetStream(const char *file, int line)
{
	LAutoStreamI Ret;
	const char *TmpPath = Store->GetStr(FIELD_TEMP_PATH);
	
	if (Flags & MAIL_STORED_FLAT)
	{
		// Construct from the single segment...
		LString Sql;
		Sql.Printf("select * from '%s' where MailId=" LPrintfInt64, MAIL3_TBL_MAILSEGS, Id);
		LMail3Store::LStatement s(Store, Sql);
		if (s.Row())
		{
			LStreamConcat *Sc;
			int64 SegId = s.GetInt64(0);
			char *Hdr	= s.GetStr(3);
			
			if (Hdr && Ret.Reset(Sc = new LStreamConcat))
			{
				size_t HdrLen	= strlen(Hdr);
				int BlobSize	= s.GetSize(4);

				Sc->Add(new LMemStream(Hdr, HdrLen));
				Sc->Add(new LMemStream("\r\n\r\n", 4));
				Sc->Add(new Mail3BlobStream(Store, (int)SegId, BlobSize, file, line));
				
				LFile f;
				if (f.Open("c:\\temp\\email.eml", O_WRITE))
				{
					LCopyStreamer Cp(64<<10);
					Cp.Copy(Sc, &f);
					f.Close();
					
					Sc->SetPos(0);
				}
			}
		}
	}
	else if (Ret.Reset(new LTempStream(TmpPath)))
	{
		// Encode from multiple segments...
		LoadSegs();
		if (Seg)
		{
			LMime Mime(TmpPath);
			if (Store3ToLMime(&Mime, Seg))
			{
				if (!Mime.Text.Encode.Push(Ret))
				{
					LgiTrace("%s:%i - Mime encode failed.\n", _FL);
					Ret.Reset();					
				}
			}
			else
			{
				LgiTrace("%s:%i - Store3ToGMime failed.\n", _FL);
				Ret.Reset();
			}
		}
		else
		{
			LgiTrace("%s:%i - No segment to encode.\n", _FL);
			Ret.Reset();
		}
	}
	
	return Ret;
}

bool LMail3Mail::SetStream(LAutoStreamI stream)
{
	if (!stream)
		return false;

	DeleteObj(Seg);

	// MIME parse the stream and store it to segments.
	LMime Mime;
	if (Mime.Text.Decode.Pull(stream))
	{
		Seg = new LMail3Attachment(Store);
		if (Seg)
		{
			// This stops the objects being written to disk.
			// Which would mean we have multiple copies of the same
			// data on disk. This setting also propagates down the
			// tree automatically as GMimeToStore3 saves new child
			// notes to their parents.
			Seg->SetInMemoryOnly(true);
			Seg->AttachTo(this);
			
			GMimeToStore3(Seg, &Mime);
		}
	}

	return false;
}

bool LMail3Mail::FindSegs(const char *MimeType, LArray<LMail3Attachment*> &Results, bool Create)
{
	LoadSegs();
	
	if ((!Seg || !Seg->FindSegs(MimeType, Results)) && Create)
	{
		LMail3Attachment *a = new LMail3Attachment(Store);
		if (!a)
			return false;
		a->SetStr(FIELD_MIME_TYPE, MimeType);

		Store3MimeTree<LMail3Store, LMail3Mail, LMail3Attachment> Tree(this, Seg);
		Tree.Add(a);
		if (!Tree.Build())
			return false;
		
		Results.Add(a);
	}
	
	/* 
	for (unsigned i=0; i<Results.Length(); i++)
	{
		LMail3Attachment *a = Results[i];
		if (!a->GetStr(FIELD_MIME_TYPE))
		{
			// Ugh, a bug has caused a bunch of NULL mime-types..
			a->SetStr(FIELD_MIME_TYPE, MimeType);
		}
	}
	*/

	return Results.Length() > 0;
}

LMail3Attachment *LMail3Mail::GetAttachment(int64 Id)
{
	return Seg ? Seg->Find(Id) : 0;
}

struct Pair
{
	int64 Parent;
	LMail3Attachment *Seg;
};

int LMail3Mail::GetAttachments(LArray<LMail3Attachment*> *Lst)
{
	if (!Seg)
		return -1;

	int Count = 0;
	if (Seg->IsMultipart())
	{
		LDataIt It = Seg->GetList(FIELD_MIME_SEG);
		if (It)
		{
			for (LDataPropI *i=It->First(); i; i=It->Next())
			{
				const char *Mt = i->GetStr(FIELD_MIME_TYPE);
				const char *Name = i->GetStr(FIELD_NAME);
				if
				(
					(
						Mt &&
						_stricmp("text/plain", Mt) &&
						_stricmp("text/html", Mt)
					)
					||
					ValidStr(Name)
				)
				{
					Count++;
					if (Lst)
					{
						LMail3Attachment *a = dynamic_cast<LMail3Attachment*>(i);
						if (a)
							Lst->Add(a);
					}
				}
			}
		}
	}

	return Count;
}

void LMail3Mail::LoadSegs()
{
	if (Id > 0 && !Seg)
	{
		if (Flags & MAIL_STORED_FLAT)
		{
			// Decompression flat MIME storage to tree
			
			// GetStream will do the hard work of joining the message back
			// into RFC822 format.
			LAutoStreamI Rfc822 = GetStream(_FL);
			if (!Rfc822)
				return;

			// Then we MIME parse it and store it to segments.
			SetStream(Rfc822);
		}
		else
		{
			LArray<Pair> Others;

			char Sql[256];
			sprintf_s(Sql, sizeof(Sql), "select * from '%s' where MailId=" LPrintfInt64, MAIL3_TBL_MAILSEGS, Id);
			LMail3Store::LStatement s(Store, Sql);
			while (s.Row())
			{
				Pair p;
				p.Seg = new LMail3Attachment(Store);
				if (p.Seg->Load(s, p.Parent))
				{
					if (p.Parent <= 0)
					{
						if (Seg)
						{
							// hmmm, this shouldn't happen
							p.Seg->AttachTo(Seg);
						}
						else
						{
							p.Seg->AttachTo(this);
						}
					}
					else
					{
						Others.Add(p);
					}
				}
			}

			while (Seg && Others.Length())
			{
				ssize_t StartSize = Others.Length();

				for (unsigned i=0; i<Others.Length(); i++)
				{
					LMail3Attachment *p = Seg->Find(Others[i].Parent);
					if (p)
					{
						Others[i].Seg->AttachTo(p);
						Others.DeleteAt(i--);
					}
				}

				if (StartSize == Others.Length())
				{
					// No segments could be attached? Must have borked up parent id's
					while (Others.Length())
					{
						// Just attach them somewhere... anywhere...!
						if (Seg)
							Others[0].Seg->AttachTo(Seg);
						else
							Others[0].Seg->AttachTo(this);
						Others.DeleteAt(0);
					}
				}
			}

			if (Seg)
			{
				int Attached = GetAttachments();
				int NewFlags;
				if (Attached > 0)
					NewFlags = Flags | MAIL_ATTACHMENTS;
				else
					NewFlags = Flags & ~MAIL_ATTACHMENTS;
				if (NewFlags != Flags)
				{
					Flags = NewFlags;
					Save();
				}
			}
		}
	}
}

static auto DefaultCharset = "windows-1252";
			
const char *LMail3Mail::GetStr(int id)
{
	switch (id)
	{
		case FIELD_DEBUG:
		{
			static char s[64];
			sprintf_s(s, sizeof(s), "Mail3.Id=" LPrintfInt64, Id);
			return s;
		}
		case FIELD_SUBJECT:
		{
			if (!LIsUtf8(Subject.Str()))
			{
				auto cs = DetectCharset(Subject.Str());
				LAutoString conv;
				if (conv.Reset((char*)LNewConvertCp("utf-8", Subject.Str(), cs ? cs.Get() : DefaultCharset)))
					Subject = conv;
			}

			#if 0
			int32 ch;
			LgiTrace("Subj:");
			for (LUtf8Ptr p(Subject.Str()); ch = p; p++)
				LgiTrace(" u+%x", ch);
			LgiTrace("\n");
			#endif

			return Subject.Str();
		}
		case FIELD_CHARSET:
		{
			return "utf-8";
		}
		case FIELD_TEXT:
		{
			LArray<LMail3Attachment*> Results;
			if (!TextCache && FindSegs("text/plain", Results))
			{
				LStringPipe p;
				for (auto seg: Results)
				{
					if (seg->GetStr(FIELD_NAME))
						continue;

					auto s = seg->GetStream(_FL);
					if (!s)
						continue;

					LString Charset = seg->GetStr(FIELD_CHARSET);

					LAutoString Buf;
					auto Size = s->GetSize();
					if (Size <= 0)
						continue;

					Buf.Reset(new char[Size+1]);
					s->Read(&Buf[0], Size);
					Buf[Size] = 0;

					if (!Charset)
						Charset = DetectCharset(Buf.Get());
					if (!Charset)
						Charset = DefaultCharset;

					LAutoString Utf8;
					if (!Stricmp(Charset.Get(), "utf-8") && LIsUtf8(Buf))
						Utf8 = Buf;
					else
						Utf8.Reset((char*)LNewConvertCp("utf-8", Buf, Charset, Size));

					if (Results.Length() > 1)
					{
						if (p.GetSize())
							p.Print("\n---------------------------------------------\n");
						p.Push(Utf8);
					}
					else
					{
						TextCache = Utf8;
						return TextCache;
					}
				}

				TextCache.Reset(p.NewStr());
			}
			return TextCache;
		}
		case FIELD_HTML_CHARSET:
		{
			return "utf-8";
		}
		case FIELD_ALTERNATE_HTML:
		{
			LArray<LMail3Attachment*> Results;
			if (!HtmlCache && FindSegs("text/html", Results))
			{
				LMemQueue Blocks(1024);
				int64 Total = 0;
				for (unsigned i=0; i<Results.Length(); i++)
				{
					if (!Results[i]->GetStr(FIELD_NAME))
					{
						LAutoStreamI s = Results[i]->GetStream(_FL);
						if (s)
						{
							int64 Size = s->GetSize();
							if (Size > 0)
							{
								auto Charset = Results[i]->GetStr(FIELD_CHARSET);
							
								char *Buf = new char[(size_t)Size+1];
								ssize_t Rd = s->Read(Buf, (int)Size);
								if (Rd > 0)
								{
									Buf[Rd] = 0;
									
									if (Charset && _stricmp("utf-8", Charset) != 0)
									{
										char *Tmp = (char*)LNewConvertCp("utf-8", Buf, Charset, (int)Size);
										if (Tmp)
										{
											DeleteArray(Buf);
											Buf = Tmp;
											Size = strlen(Tmp);
										}
									}
									
									Blocks.Write(Buf, (int)Size);
									Total += Size;
									DeleteArray(Buf);
								}
							}
						}
					}
				}
				
				HtmlCache.Reset((char*)Blocks.New(1));				
			}
			return HtmlCache;
		}

		case FIELD_LABEL:
			return Label.Str();
		case FIELD_INTERNET_HEADER:
		{
			auto Root = GetObj(FIELD_MIME_SEG);
			if (Root)
				return Root->GetStr(id);
			else
				return NULL;
		}
		case FIELD_REFERENCES:
			return References.Str();
		case FIELD_FWD_MSG_ID:
			return FwdMsgId.Str();
		case FIELD_BOUNCE_MSG_ID:
			return BounceMsgId.Str();
		case FIELD_SERVER_UID:
			return ServerUid.Str();
		case FIELD_MESSAGE_ID:
		{
			if (!MessageID.Str())
			{
				LAutoString Header(InetGetHeaderField(GetStr(FIELD_INTERNET_HEADER), "Message-ID"));
				if (Header)
				{
					auto Ids = ParseIdList(Header);
					MessageID = Ids[0];
				}
			}
			return MessageID.Str();
		}
		case FIELD_UID:
		{
			IdCache.Printf(LPrintfInt64, Id);
			return IdCache;
		}
	}

	LAssert(0);
	return 0;
}

void LMail3Mail::OnSave()
{
	if (Seg)
	{
		LDataStoreI::StoreTrans Trans = Store->StartTransaction();
		Seg->OnSave();
	}
}

void LMail3Mail::ParseAddresses(char *Str, int CC)
{
	List<char> Addr;
	TokeniseStrList(Str, Addr, ",");
	
	for (auto RawAddr: Addr)
	{
		LAutoPtr<LDataPropI> a(To.Create(Store));
		if (a)
		{
			Store3Addr *sa = dynamic_cast<Store3Addr*>(a.Get());
			LAssert(sa != NULL);
			if (sa)
			{
				DecodeAddrName(RawAddr, sa->Name, sa->Addr, 0);
				sa->CC = CC;
				To.Insert(a.Release());
			}
		}
	}
	Addr.DeleteArrays();
}

void LMail3Mail::ResetCaches()
{
	TextCache.Reset();
	HtmlCache.Reset();
	SizeCache.Reset();
}

const char *LMail3Mail::InferCharset()
{
	if (!InferredCharset)
	{
		// Sometimes mailers don't follow the rules... *cough*outlook*cough*
		// So lets play the "guess the charset" game...
	
		// Maybe one of the segments has a charset?
		InferredCharset = Seg->GetStr(FIELD_CHARSET);		
		if (!InferredCharset)
		{
			// What about 'Content-Language'?
			auto InetHdrs = GetStr(FIELD_INTERNET_HEADER);
			if (InetHdrs)
			{
				LAutoString ContentLang(InetGetHeaderField(InetHdrs, "Content-Language"));
				if (ContentLang)
				{
					LLanguage *Lang = LFindLang(ContentLang);
					if (Lang)
						InferredCharset = Lang->Charset;
				}
			}
		}
	}
		
	return InferredCharset;
}

bool LMail3Mail::Utf8Check(LAutoString &v)
{
	if (!LIsUtf8(v.Get()))
	{
		const char *Cs = InferCharset();
		if (Cs)
		{
			LAutoString Value((char*) LNewConvertCp("utf-8", v, Cs, -1));
			if (Value)
			{
				v = Value;
				return true;
			}
		}
	}

	return false;
}

bool LMail3Mail::Utf8Check(LVariant &v)
{
	if (!LIsUtf8(v.Str()))
	{
		const char *Cs = InferCharset();
		if (Cs)
		{
			LAutoString Value((char*) LNewConvertCp("utf-8", v.Str(), Cs, -1));
			if (Value)
			{
				v.OwnStr(Value.Release());
				return true;
			}
		}
	}

	return false;
}

bool LMail3Mail::ParseHeaders()
{
	// Reload from headers...
	auto InetHdrs = GetStr(FIELD_INTERNET_HEADER);
	Subject.OwnStr(DecodeRfc2047(InetGetHeaderField(InetHdrs, "subject")));
	Utf8Check(Subject);

	// From
	LAutoString s(DecodeRfc2047(InetGetHeaderField(InetHdrs, "from")));
	Utf8Check(s);
	From.Empty();
	DecodeAddrName(s, From.Name, From.Addr, NULL);

	s.Reset(DecodeRfc2047(InetGetHeaderField(InetHdrs, "reply-to")));
	Utf8Check(s);
	Reply.Empty();
	DecodeAddrName(s, Reply.Name, Reply.Addr, NULL);

	// Parse To and CC headers.
	To.DeleteObjects();
	if (s.Reset(DecodeRfc2047(InetGetHeaderField(InetHdrs, "to"))))
	{
		Utf8Check(s);
		ParseAddresses(s, MAIL_ADDR_TO);
	}
	if (s.Reset(DecodeRfc2047(InetGetHeaderField(InetHdrs, "cc"))))
	{
		Utf8Check(s);
		ParseAddresses(s, MAIL_ADDR_CC);
	}

	// Data
	if (s.Reset(InetGetHeaderField(InetHdrs, "date")))
	{
	    DateSent.Decode(s);
		DateSent.ToUtc();
	}
				
	DeleteObj(Seg);
	ResetCaches();
	
	return true;
}

Store3Status LMail3Mail::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_SUBJECT:
			Subject = str;
			break;

		case FIELD_TEXT:
		{
			TextCache.Reset();
			LArray<LMail3Attachment*> Results;
			if (FindSegs("text/plain", Results, str != 0))
			{
				for (unsigned i=0; i<Results.Length(); i++)
				{
					if (!Results[i]->GetStr(FIELD_NAME))
					{
					    LAutoStreamI s(str ? new LMemStream((char*)str, strlen(str)) : 0);
						Results[i]->SetStream(s);
						break;
					}
				}
			}
			break;
		}
		case FIELD_CHARSET:
		{
			LArray<LMail3Attachment*> Results;
			if (FindSegs("text/plain", Results, str != 0))
			{
				for (unsigned i=0; i<Results.Length(); i++)
				{
					if (!Results[i]->GetStr(FIELD_NAME))
					{
						Results[i]->SetStr(FIELD_CHARSET, str);
						break;
					}
				}
			}
			break;
		}
		case FIELD_ALTERNATE_HTML:
		{
			HtmlCache.Reset();
			LArray<LMail3Attachment*> Results;
			if (FindSegs("text/html", Results, str != 0))
			{
				for (unsigned i=0; i<Results.Length(); i++)
				{
					if (!Results[i]->GetStr(FIELD_NAME))
					{
						if (str)
						{
							LAutoStreamI tmp(new LMemStream((char*)str, strlen(str)));
							Results[i]->SetStream(tmp);
						}
						else
						{
							auto s = Results[i];
							s->Delete();
							delete s;
						}
						break;
					}
				}
			}
			break;
		}
		case FIELD_HTML_CHARSET:
		{
			LArray<LMail3Attachment*> Results;
			if (FindSegs("text/html", Results, str != 0))
			{
				const char *Charset = str && *str == '>' ? str + 1 : str;
				for (unsigned i=0; i<Results.Length(); i++)
				{
					if (!Results[i]->GetStr(FIELD_NAME))
					{
						if (Results[i]->SetStr(FIELD_CHARSET, Charset))
							return Store3Success;
						break;
					}
				}
			}

			return Store3Error;
		}

		case FIELD_LABEL:
			Label = str;
			break;
		case FIELD_INTERNET_HEADER:
		{
			LoadSegs();
			
			if (!ValidStr(str))
				// There is no point continuing as it will just attach an empty
				// attachment which eventually asserts in the save code.
				//		e.g. LMail3Attachment::GetHeaders()
				break;

			if (!Seg)
			{
				// This happens when the user re-sends an email and it creates
				// a new empty email to copy the old sent email into.
				LMail3Attachment *a = new LMail3Attachment(Store);
				if (!a)
				{
					LAssert(0);
					return Store3Error;
				}
				
				a->AttachTo(this);
			}
			
			Seg->SetStr(id, str);
			break;
		}
		case FIELD_MESSAGE_ID:
			MessageID = str;
			break;
		case FIELD_REFERENCES:
			References = str;
			break;
		case FIELD_FWD_MSG_ID:
			FwdMsgId = str;
			break;
		case FIELD_BOUNCE_MSG_ID:
			BounceMsgId = str;
			break;
		case FIELD_SERVER_UID:
			ServerUid = str;
			break;

		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 LMail3Mail::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
		case FIELD_SIZE:
			return Size();
		case FIELD_LOADED:
			return Store3Loaded;
		case FIELD_PRIORITY:
			return Priority;
		case FIELD_FLAGS:
			return Flags;
		case FIELD_DONT_SHOW_PREVIEW:
			return false;
		case FIELD_ACCOUNT_ID:
			return AccountId;
		case FIELD_COLOUR:
			return (uint64_t)MarkColour;
		case FIELD_SERVER_UID:
			return -1;
	}

	LAssert(0);
	return -1;
}

Store3Status LMail3Mail::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_LOADED:
			return Store3Success;
		case FIELD_PRIORITY:
			Priority = (int)i;
			return Store3Success;
		case FIELD_FLAGS:
			Flags = (int)i;
			return Store3Success;
		case FIELD_ACCOUNT_ID:
			AccountId = (int)i;
			return Store3Success;
		case FIELD_COLOUR:
			if (i < 0)
				MarkColour = Rgba32(0, 0, 0, 0); // transparent
			else
				MarkColour = (uint32_t)i;
			return Store3Success;
	}

	LAssert(0);
	return Store3NotImpl;
}

const LDateTime *LMail3Mail::GetDate(int id)
{
	switch (id)
	{
		case FIELD_DATE_RECEIVED:
			return &DateReceived;
		case FIELD_DATE_SENT:
			return &DateSent;
	}

	LAssert(0);
	return 0;
}

Store3Status LMail3Mail::SetDate(int id, const LDateTime *t)
{
	switch (id)
	{
		case FIELD_DATE_RECEIVED:
			if (t)
				DateReceived = *t;
			else
				DateReceived.Year(0);
			return Store3Success;
		case FIELD_DATE_SENT:
			if (t)
				DateSent = *t;
			else
				DateSent.Year(0);
			return Store3Success;
	}

	LAssert(0);
	return Store3NotImpl;
}

LDataPropI *LMail3Mail::GetObj(int id)
{
	switch (id)
	{
		case FIELD_FROM:
			return &From;
		case FIELD_REPLY:
			return &Reply;
		case FIELD_MIME_SEG:
		{
			LoadSegs();

			/* This causes replies to have the wrong format for "text/plain"
			if (!Seg)
			{
				LMail3Attachment *a = new LMail3Attachment(Store);
				if (a)
				{
					a->SetStr(FIELD_MIME_TYPE, sMultipartMixed);
					a->AttachTo(this);
				}
			}
			*/

			return Seg;
		}
	}

	LAssert(0);
	return 0;
}

Store3Status LMail3Mail::SetObj(int id, LDataPropI *i)
{
	switch (id)
	{
		case FIELD_MIME_SEG:
		{
			if (Seg)
			{
				Seg->SetMail(NULL);
				Seg = NULL;
			}
			
			LMail3Attachment *a = dynamic_cast<LMail3Attachment*>(i);
			if (!a)
			{
				LAssert(!"Incorrect object...");
				return Store3Error;
			}
			
			Seg = a;
			Seg->SetMail(this);
			break;
		}
		case FIELD_HTML_RELATED:
		{
			LMail3Attachment *a = i ? dynamic_cast<LMail3Attachment*>(i) : NULL;
			
			LoadSegs();
			if (Seg)
			{
				Store3MimeTree<LMail3Store, LMail3Mail, LMail3Attachment> Tree(this, Seg);
				
				if (a)
					Tree.MsgHtmlRelated.Add(a);
				else
					Tree.MsgHtmlRelated.Empty();
				
				if (!Tree.Build())
					return Store3Error;
					
				MailSize = -1;
				Seg->Save(this);
			}
			break;
		}
		default:
			LAssert(0);
			return Store3NotImpl;
	}
	
	return Store3Success;
}

LDataIt LMail3Mail::GetList(int id)
{
	switch (id)
	{
		case FIELD_TO:
			return &To;
	}

	LAssert(0);
	return 0;
}

class LSubStream : public LStreamI
{
	// The source stream to read from
	LStreamI *s;
	
	// The start position in the source stream
	int64 Start;
	
	// The length of the sub-stream
	int64 Len;
	
	// The current position in the sub-stream
	// (relative to the start of the sub-stream, not the parent stream)
	int64 Pos;

public:
	LSubStream(LStreamI *stream, int64 start = 0, int64 len = -1)
	{
		s = stream;
		Pos = 0;
		Start = MAX(0, start);
		
		int64 MaxLen = s->GetSize() - Start;
		if (MaxLen < 0)
			MaxLen = 0;
		Len = len < 0 && s ? MaxLen : MIN(MaxLen, len); 
	}

	bool IsOpen() { return true; }
	int Close()
	{
		s = NULL;
		Start = Len = Pos = 0;
		return true;
	}
	
	int64 GetSize()
	{
		return s ? Len : -1;
	}
	
	int64 SetSize(int64 Size)
	{
		// Can't set size
		return GetSize();
	}

	int64 GetPos()
	{
		return s ? Pos : -1;
	}
	
	int64 SetPos(int64 p)
	{
		if (p < 0) p = 0;
		if (p >= Len) p = Len;
		return Pos = p;
	}
	
	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0)
	{
		ssize_t r = 0;
		if (s && Buffer)
		{
			int64 Remaining = Len - Pos;
			ssize_t Common = MIN(Size, (int)Remaining);
			
			int64 SrcPos = Start + Pos;
			int64 ActualPos = s->SetPos(SrcPos);
			if (ActualPos == SrcPos)
			{			
				r = s->Read(Buffer, Common, Flags);
				if (r > 0)
				{
					Pos += r;
				}
			}
			else LAssert(0);
		}
		return r;
	}
	
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		LAssert(!"Not implemented.");
		return 0;
	}
	
	LStreamI *Clone() { return new LSubStream(s, Start, Len); }
};

Store3Status LMail3Mail::SetRfc822(LStreamI *m)
{
	Store3Status Status = Store3Error;

	if (m)
	{
		// Save all the segments out
		LDataStoreI::StoreTrans Trans = Store->StartTransaction();

		if (Id < 0)
		{
			// Must commit ourselves now and get an Id
			if (!Write(MAIL3_TBL_MAIL, true))
				return Store3Error;
		}
		else
		{
			char s[256];
			sprintf_s(s, sizeof(s), "delete from %s where MailId=" LPrintfInt64, MAIL3_TBL_MAILSEGS, Id);
			LMail3Store::LStatement Del(Store, s);
			if (!Del.Exec())
				return Store3Error;
		}

		DeleteObj(Seg);

		/*
		Normally the message is parsed into MIME segments and stored parsed and decoded into 
		the MailSegs table. If the message is encrypted or signed that could the client can't
		verify the message later because the specifics of MIME encoding varies between clients.
		
		So if the message is:
		
			Signed and/or Encrypted:	There is only one MAILSEG record with all the headers of the 
										root MIME segment, and the body stream has all the data of
										the RFC822 image.
			
			Otherwise:					Normal MIME parsing is done, storing the message in different
										MAILSEG records. Which was the previous behaviour.

		First the headers of the root MIME node are read to see what the Content-Type is.
		Then a decision about how to store the node is made.
		*/
		LString Hdrs = HeadersFromStream(m);
		LAutoString Type(InetGetHeaderField(Hdrs, "Content-Type"));

		Flags &= ~MAIL_STORED_FLAT;
		if (Type)
		{
			LString s = Type.Get();
			ptrdiff_t Colon = s.Find(";");
			if (Colon > 0)
				s.Length((uint32_t)Colon);
			s = s.Strip().Lower();
			if (s == sMultipartEncrypted ||
				s == sMultipartSigned)
			{
				Flags |= MAIL_STORED_FLAT;
			}
		}

		LMail3Store::LInsert Ins(Store, MAIL3_TBL_MAILSEGS);
		MailSize = 0;

		LMime Mime;
		if (Flags & MAIL_STORED_FLAT)
		{
			// Don't parse MIME into a tree.
			Mime.SetHeaders(Hdrs);
			Mime.SetData(true, new LSubStream(m, Hdrs.Length()+4));
			if (Mail3_InsertSeg(Ins, &Mime, Id, -1, MailSize))
				Status = Store3Success;
		}
		else
		{
			// Do normal parsing in to MIME tree.
			if (Mime.Text.Decode.Pull(m) &&
				Mail3_InsertSeg(Ins, &Mime, Id, -1, MailSize))
				Status = Store3Success;
		}
	}

	return Status;
}
