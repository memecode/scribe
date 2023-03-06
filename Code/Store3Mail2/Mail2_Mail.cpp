#include "Store3Mail2.h"

#include "../MailBuf.cpp"
#include "INetTools.h"

const char sAlternative[] = "multipart/alternative";

MailData::MailData(LMail2Store *s) :
	ThingData(s),
	From(s),
	Reply(s)
{
	Flags = 0;
	MarkColour = 0;
	AccountId = 0;
	Priority = MAIL_PRIORITY_NORMAL;

	Label = 0;
	FwdMsgId = 0;
	ServerUid = 0;
	Subject = 0;
	Body = 0;
	BodyCharset = 0;
	Html = 0;
	HtmlCharset = 0;
	MessageID = 0;
	InternetHeader = 0;
	References = 0;
	Seg = 0;
	BounceMessageID = NULL;
}

MailData::~MailData()
{
	Empty();
	DeleteObj(Seg);
}

void MailData::Empty()
{
	DeleteArray(Subject);
	DeleteArray(Body);
	DeleteArray(BodyCharset);
	DeleteArray(Html);
	DeleteArray(HtmlCharset);
	DeleteArray(Label);
	DeleteArray(InternetHeader);
	DeleteArray(MessageID);
	DeleteArray(BounceMessageID);
	DeleteArray(References);
	DeleteArray(ServerUid);
	From.Empty();
	To.DeleteObjects();
}

bool MailData::ParseHeaders()
{
	DeleteArray(Subject);
	DeleteArray(Label);
	DeleteArray(MessageID);
	DeleteArray(BounceMessageID);
	DeleteArray(References);
	DeleteArray(ServerUid);
	From.Empty();
	To.DeleteObjects();

	Subject = DecodeRfc2047(InetGetHeaderField(InternetHeader, "Subject"));
	MessageID = DecodeRfc2047(InetGetHeaderField(InternetHeader, "Message-Id"));
    LAssert(!MessageID || !strchr(MessageID, '\n'));

	LAutoString s(InetGetHeaderField(InternetHeader, "Date"));
	if (s)
		DateSent.Decode(s);
	else
		DateSent.Empty();
	
	if (s.Reset(InetGetHeaderField(InternetHeader, "X-Priority")))
		Priority = atoi(s);

	if (s.Reset(InetGetHeaderField(InternetHeader, "X-Color")))
	{
		char *Hash = strchr(s, '#');
		if (Hash)
		{
			int Rgb = htoi(Hash+1);
			MarkColour = Rgb32((Rgb >> 16) & 0xff, (Rgb >> 8) & 0xff, Rgb & 0xff);
		}
	}

	if (s.Reset(InetGetHeaderField(InternetHeader, "Disposition-Notification-To")))
		Flags |= MAIL_READ_RECEIPT;	

	if (s.Reset(DecodeRfc2047(InetGetHeaderField(InternetHeader, "From"))))
		DecodeAddrName(s, From.Name, From.Addr, 0);
	
	if (s.Reset(DecodeRfc2047(InetGetHeaderField(InternetHeader, "To"))))
	{
		List<char> Addr;
		TokeniseStrList(s, Addr, ",");
		for (char *RawAddr = Addr.First(); RawAddr; RawAddr = Addr.Next())
		{
			LAutoPtr<LDataPropI> a(To.Create(Kit));
			if (a)
			{
				Store3Addr *sa = dynamic_cast<Store3Addr*>(a.Get());
				LAssert(sa != NULL);
				if (sa)
				{
					DecodeAddrName(RawAddr, sa->Name, sa->Addr, 0);
					To.Insert(a.Release());
				}
			}
		}
		Addr.DeleteArrays();
	}
	
	return true;
}

static char *StreamToString(LStreamI *s)
{
	int64 Len = s->GetSize();
	char *Str = new char[(size_t)Len+1];
	if (Str)
	{
		int Read = s->Read(Str, (int)Len);
		if (Read <= 0)
		{
			DeleteArray(Str);
		}
		else Str[Read] = 0;
	}
	return Str;
}

void CopyAttachments(MailData *Mail, LDataI *Source, AttachmentData *&Mixed, int Depth, bool InMultipart = false)
{
	char *Mt = Source->GetStr(FIELD_MIME_TYPE);

	if (InMultipart &&
		Mt &&
		_strnicmp(Mt, "text/", 5))
	{
		// Copy attachment

		if (!Mixed &&
			(Mixed = new AttachmentData(Mail->Kit)))
		{
			Mixed->SetStr(FIELD_MIME_TYPE, "multipart/mixed");
			Mixed->AttachTo(Mail);
		}

		if (Mixed)
		{
			AttachmentData *a = new AttachmentData(Mail->Kit);
			if (a)
			{
				*a = *Source;
				a->AttachTo(Mixed);
			}
			else LAssert(!"Alloc error");
		}
		else LAssert(!"No mixed segment to attach to.");
	}
	else if (Depth == 0)
	{
		char *Charset = Source->GetStr(FIELD_CHARSET);
		GAutoStreamI Data = Source->GetStream(_FL);
		if (Data)
		{
			if (Mt && !_stricmp(Mt, "text/html"))
			{
				// Html body
				Mail->Html = StreamToString(Data);
				Mail->HtmlCharset = NewStr(Charset);
			}
			else
			{
				// Text body
				Mail->Body = StreamToString(Data);
				Mail->BodyCharset = NewStr(Charset);
			}
		}
		// else this can happen with an empty mime segment.
	}
	
	InMultipart =	Mt &&
					!_strnicmp(Mt, "multipart/", 10) &&
					_stricmp(Mt, sAlternative);

	// Iterate over tree of segments
	LDataIt Children = Source->GetList(FIELD_MIME_SEG);
	for (LDataPropI *i=Children->First(); i; i=Children->Next())
	{
		LDataI *d = dynamic_cast<LDataI*>(i);
		if (d)
			CopyAttachments(Mail, d, Mixed, Depth + 1, InMultipart);
	}
}

void PrintMimeTree(AttachmentData *a, const char *msg, int depth = 1)
{
	if (a)
	{
		#ifdef _DEBUG
		if (msg)
			LgiTrace("MimeTree: %s\n", msg);
		
		char m[256];
		memset(m, ' ', depth * 4);
		m[depth * 4] = 0;
		LgiTrace("%s%s\n", m, a->GetStr(FIELD_MIME_TYPE));
		
		LArray<AttachmentData*> c = a->GetChildren();
		for (unsigned i=0; i<c.Length(); i++)
		{
			PrintMimeTree(c[i], NULL, depth + 1);
		}

		if (msg)
			LgiTrace("\n");
		#endif
	}
}

LDataI &MailData::operator =(LDataI &p)
{
	Empty();
	DeleteObj(Seg);

	Priority = (int)p.GetInt(FIELD_PRIORITY);
	SetInt(FIELD_FLAGS, p.GetInt(FIELD_FLAGS));
	
	InternetHeader = NewStr(p.GetStr(FIELD_INTERNET_HEADER));
	MessageID = NewStr(p.GetStr(FIELD_MESSAGE_ID));
	BounceMessageID = NewStr(p.GetStr(FIELD_BOUNCE_MSG_ID));
	Subject = NewStr(p.GetStr(FIELD_SUBJECT));
	Body = NewStr(p.GetStr(FIELD_TEXT));
	BodyCharset = NewStr(p.GetStr(FIELD_CHARSET));
	Html = NewStr(p.GetStr(FIELD_ALTERNATE_HTML));
	HtmlCharset = NewStr(p.GetStr(FIELD_HTML_CHARSET));
	
	LDataPropI *i = p.GetObj(FIELD_FROM);
	if (i) (Store3Addr&)From = *i;
	i = p.GetObj(FIELD_REPLY);
	if (i) (Store3Addr&)Reply = *i;
	
	LDateTime *d = p.GetDate(FIELD_DATE_RECEIVED);
	if (d) DateReceived = *d;
	d = p.GetDate(FIELD_DATE_SENT);
	if (d) DateSent = *d;
	
	To.DeleteObjects();
	
	unsigned n;
	LDataIt pTo = p.GetList(FIELD_TO);
	for (n=0; n<pTo->Length(); n++)
	{
		To.Insert(new Mail2Addr(GetStore(), (*pTo)[n]));
	}

	DeleteObj(Seg);
	LDataI *Root = dynamic_cast<LDataI*>(p.GetObj(FIELD_MIME_SEG));
	if (Root)
	{
		AttachmentData *Mixed = 0;
		CopyAttachments(this, Root, Mixed, 0);		
	}

	// PrintMimeTree(Seg, "Before");
	RebuildMimeTree();
	// PrintMimeTree(Seg, "After");
	return *this;
}

bool MailData::RebuildMimeTree()
{
	LAutoString ContentType(InetGetHeaderField(InternetHeader, "Content-Type"));
	if (ContentType)
	{
		char *Colon = strchr(ContentType, ';');
		if (Colon)
		{
			while (strchr(" \t", Colon[-1])) Colon--;
			*Colon = 0;
		}

		if (!Seg &&
			(Seg = new AttachmentData(Kit)))
		{
			Seg->SetStr(FIELD_MIME_TYPE, ContentType);
		}
	}

	int NewFlags = Flags;
	if (Store)
	{
		if (!Seg)
		{
			if ((Seg = new AttachmentData(Kit)))
			{
				Seg->SetStr(FIELD_MIME_TYPE, "multipart/mixed");
			}
		}

		if (Seg)
		{
			for (StorageItem *c = Store->GetChild(); c; c = c->GetNext())
			{
				if (c->GetType() == MAGIC_ATTACHMENT)
				{
					AttachmentData *a = new AttachmentData(Kit);
					if (a)
					{
						a->IsLoaded = false;
						a->Store = c;
						c->Object = a;
						a->AttachTo(Seg);

						NewFlags |= MAIL_ATTACHMENTS;
					}
				}
			}
		}
	}
	
	if (Seg)
	{
		AttachmentData *TextSeg = 0;
		AttachmentData *HtmlSeg = 0;

		if (Seg->IsPlainText())
		{
			TextSeg = Seg;
		}
		else if (Seg->IsHtml())
		{
			HtmlSeg = Seg;
		}
		else
		{
			if (Body && Html)
			{
				AttachmentData *Alt;
				AttachmentData *ChildAlt = Seg->FindChildByMimeType(sAlternative);
				
				if (Seg->IsAlternative())
				{
					Alt = Seg;
				}
				else if (ChildAlt)
				{
					Alt = ChildAlt;
				}
				else if ((Alt = new AttachmentData(Kit)))
				{
					Alt->SetPlaceholder();
					Alt->AttachTo(Seg);
				}

				if (Alt)
				{
					Alt->SetStr(FIELD_MIME_TYPE, sAlternative);

					if ((TextSeg = new AttachmentData(Kit)))
					{
						TextSeg->AttachTo(Alt);
					}
					if ((HtmlSeg = new AttachmentData(Kit)))
					{
						HtmlSeg->AttachTo(Alt);
					}
				}
			}
			else if (Body)
			{
				if ((TextSeg = new AttachmentData(Kit)))
				{
					TextSeg->AttachTo(Seg);
				}
			}
			else if (Html)
			{
				if ((HtmlSeg = new AttachmentData(Kit)))
				{
					HtmlSeg->AttachTo(Seg);
				}
			}
		}

		if (TextSeg)
		{
			TextSeg->SetStr(FIELD_MIME_TYPE, "text/plain");
			TextSeg->SetExtern(&Body, &BodyCharset);
		}
		if (HtmlSeg)
		{
			HtmlSeg->SetStr(FIELD_MIME_TYPE, "text/html");
			HtmlSeg->SetExtern(&Html, &HtmlCharset);
		}
	}

	if (NewFlags != Flags)
	{
		Flags = NewFlags;
		SetDirty();
	}
	
	return true;
}

bool MailData::Load()
{
	if (IsLoaded)
		return true;

	if (!Store)
		return false;

	LFile *f = Store->GotoObject(_FL);
	if (f)
	{
		Serialize(*f, false);
		DeleteObj(f);
	}

	RebuildMimeTree();

	return IsLoaded = true;
}

Store3Status MailData::Save(LDataI *Folder)
{
	if (!ThingData::Save(Folder))
		return Store3Error;

	if (Seg)
		Seg->OnSave();

	if (Store->GetChild())
		Flags |= MAIL_ATTACHMENTS;
	else
		Flags &= ~MAIL_ATTACHMENTS;

	return Store3Success;
}

int MailData::Type()
{
	return MAGIC_MAIL;
}

char *MailData::GetStr(int id)
{
	Load();

	switch (id)
	{
		case FIELD_SUBJECT:
			return Subject;
		case FIELD_TEXT:
			return Body;
		case FIELD_CHARSET:
			return BodyCharset;
		case FIELD_ALTERNATE_HTML:
			return Html;
		case FIELD_HTML_CHARSET:
			return HtmlCharset;
		case FIELD_LABEL:
			return Label;
		case FIELD_INTERNET_HEADER:
			return InternetHeader;
		case FIELD_REFERENCES:
			return References;
		case FIELD_FWD_MSG_ID:
			return FwdMsgId;
		case FIELD_SERVER_UID:
			return ServerUid;
		case FIELD_MESSAGE_ID:
		{
			if (!MessageID && InternetHeader)
			{
				char *Header = InetGetHeaderField(InternetHeader, "Message-ID");
				if (Header)
				{
					List<char> Ids;
					ParseIdList(Header, Ids);
					MessageID = Ids.First();
					Ids.Delete(MessageID);
					Ids.DeleteArrays();
					DeleteArray(Header);
				}
			}
			return MessageID;
		}
		case FIELD_BOUNCE_MSG_ID:
			return BounceMessageID;
	}

	LAssert(0);
	return 0;
}

bool MailData::SetStr(int id, const char *str)
{
	Load();

	switch (id)
	{
		case FIELD_SUBJECT:
			_Str(Subject);
		case FIELD_TEXT:
			_Str(Body);
		case FIELD_CHARSET:
			_Str(BodyCharset);
		case FIELD_ALTERNATE_HTML:
			_Str(Html);
		case FIELD_HTML_CHARSET:
			_Str(HtmlCharset);
		case FIELD_LABEL:
			_Str(Label);
		case FIELD_MESSAGE_ID:
			_Str(MessageID);
		case FIELD_REFERENCES:
			_Str(References);
		case FIELD_FWD_MSG_ID:
			_Str(FwdMsgId);
		case FIELD_SERVER_UID:
			_Str(ServerUid);
		case FIELD_INTERNET_HEADER:
			DeleteArray(InternetHeader);
			InternetHeader = NewStr(str);
			ParseHeaders();
			return (InternetHeader != 0) == (str != 0);
		case FIELD_BOUNCE_MSG_ID:
			_Str(BounceMessageID);
			break;
			
	}

	LAssert(0);
	return false;
}

int64 MailData::GetInt(int id)
{
	Load();

	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
		case FIELD_LOADED:
			return Store3Loaded;
		case FIELD_SIZE:
		{
			int s = Sizeof();

			if (Store)
			{
				for (StorageItem *i = Store->GetChild(); i; i = i->GetNext())
				{
					s += i->GetObjectSize();
				}
			}

			return s;
		}
		case FIELD_PRIORITY:
			return Priority;
		case FIELD_FLAGS:
			return Flags;
		case FIELD_DONT_SHOW_PREVIEW:
			return false;
		case FIELD_ACCOUNT_ID:
			return AccountId;
		case FIELD_MARK_COLOUR:
			return MarkColour;
	}

	LAssert(0);
	return -1;
}

bool MailData::SetInt(int id, int64 i)
{
	Load();

	switch (id)
	{
		case FIELD_DEBUG:
			Debug = i != 0;
			return true;
		case FIELD_PRIORITY:
			Priority = i;
			return true;
		case FIELD_FLAGS:
			Flags = i;
			return true;
		case FIELD_ACCOUNT_ID:
			AccountId = i;
			return true;
		case FIELD_MARK_COLOUR:
			MarkColour = i;
			return true;
	}

	LAssert(0);
	return 0;
}

LDateTime *MailData::GetDate(int id)
{
	Load();

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

bool MailData::SetDate(int id, LDateTime *t)
{
	Load();

	switch (id)
	{
		case FIELD_DATE_RECEIVED:
			if (t)
				DateReceived = *t;
			else
				DateReceived.Year(0);
			return true;
		case FIELD_DATE_SENT:
			if (t)
				DateSent = *t;
			else
				DateSent.Year(0);
			return true;
	}

	LAssert(0);
	return 0;
}

LDataPropI *MailData::GetObj(int id)
{
	Load();

	switch (id)
	{
		case FIELD_FROM:
			return &From;
		case FIELD_REPLY:
			return &Reply;
		case FIELD_MIME_SEG:
		{
			if (!Seg)
			{
				AttachmentData *a = new AttachmentData(Kit);
				if (a)
				{
					a->SetStr(FIELD_MIME_TYPE, "multipart/mixed");
					a->AttachTo(this);
				}
			}

			return Seg;
			break;
		}
	}

	LAssert(0);
	return 0;
}

LDataIt MailData::GetList(int id)
{
	Load();

	switch (id)
	{
		case FIELD_TO:
			return &To;
	}

	LAssert(0);
	return 0;
}

int MailData::Sizeof()
{
	int Size = sizeof(uint32);				// magic
	Size += sizeof(uint32);					// number of fields

	// This causes the msg id to be parsed out of the headers if it hasn't already
	//  FIXME GetMsgId();

	// Required fields
	Size += SizeIntField(Flags);
	Size += SizeIntField(Priority);
	Size += SizeIntField(MarkColour);
	Size += SizeObjField(From);
	Size += SizeObjField(Reply);
	Size += SizeObjField(DateReceived);
	Size += SizeObjField(DateSent);
	
	// Optional fields
	Size += (Subject		? SizeStrField(Subject) : 0);
	Size += (Body			? SizeStrField(Body) : 0);
	Size += (MessageID		? SizeStrField(MessageID) : 0);
	Size += (BounceMessageID ? SizeStrField(BounceMessageID) : 0);
	Size += (InternetHeader ? SizeStrField(InternetHeader) : 0);
	Size += (BodyCharset	? SizeStrField(BodyCharset) : 0);
	Size += (Html			? SizeStrField(Html) : 0);
	Size += (Label			? SizeStrField(Label) : 0);
	Size += (References		? SizeStrField(References) : 0);
	Size += (FwdMsgId		? SizeStrField(FwdMsgId) : 0);
	Size += (ServerUid		? SizeStrField(ServerUid) : 0);
	Size += (HtmlCharset	? SizeStrField(HtmlCharset) : 0);
	Size += (AccountId		? SizeIntField(AccountId) : 0);

	// List fields
	for (LDataPropI *a = To.First(); a; a = To.Next())
	{
		Mail2Addr *la = dynamic_cast<Mail2Addr *>(a);
		LAssert(la != NULL);
		Size += SizeObjField(*la);
	}

	return Size;
}

bool MailData::Serialize(LFile &Stream, bool Write)
{
	uint32 Magic = Type();

	if (Write)
	{
		LFile &f = Stream;
		f << Magic;

		// number of fields following
		f << (uint32)(7 +
					((Subject)			? 1 : 0) +
					((Body)				? 1 : 0) +
					((MessageID)		? 1 : 0) +
					((BounceMessageID)	? 1 : 0) +
					((InternetHeader)	? 1 : 0) +
					((BodyCharset)		? 1 : 0) +
					((Html)				? 1 : 0) +
					((Label)			? 1 : 0) +
					((References)		? 1 : 0) +
					((FwdMsgId)			? 1 : 0) +
					((ServerUid)		? 1 : 0) +
					((HtmlCharset)		? 1 : 0) +
					((AccountId)		? 1 : 0) +
					To.Length()); 

		WriteIntField(FIELD_FLAGS, Flags);
		WriteIntField(FIELD_PRIORITY, Priority);
		WriteIntField(FIELD_MARK_COLOUR, MarkColour);
		WriteObjField(FIELD_FROM, From);
		WriteObjField(FIELD_REPLY, Reply);
		WriteDateField(FIELD_DATE_RECEIVED, DateReceived, false);
		WriteDateField(FIELD_DATE_SENT, DateSent, false);
		if (Subject)		WriteStrField(FIELD_SUBJECT, Subject);
		if (Body)			WriteStrField(FIELD_TEXT, Body);
		if (MessageID)		WriteStrField(FIELD_MESSAGE_ID, MessageID);
		if (BounceMessageID) WriteStrField(FIELD_BOUNCE_MSG_ID, BounceMessageID);
		if (InternetHeader)	WriteStrField(FIELD_INTERNET_HEADER, InternetHeader);
		if (BodyCharset)	WriteStrField(FIELD_CHARSET, BodyCharset);
		if (Html)			WriteStrField(FIELD_ALTERNATE_HTML, Html);
		if (Label)			WriteStrField(FIELD_LABEL, Label);
		if (References)		WriteStrField(FIELD_REFERENCES, References);
		if (FwdMsgId)		WriteStrField(FIELD_FWD_MSG_ID, FwdMsgId);
		if (ServerUid)		WriteStrField(FIELD_SERVER_UID, ServerUid);
		if (HtmlCharset)	WriteStrField(FIELD_HTML_CHARSET, HtmlCharset);
		if (AccountId)		WriteIntField(FIELD_ACCOUNT_ID, AccountId);

		for (LDataPropI *a = To.First(); a; a = To.Next())
		{
			Mail2Addr *la = dynamic_cast<Mail2Addr *>(a);
			switch (la->CC)
			{
				case MAIL_ADDR_CC:
				{
					WriteObjField(FIELD_CC, *la);
					break;
				}
				case MAIL_ADDR_BCC:
				{
					WriteObjField(FIELD_BCC, *la);
					break;
				}
				default:
				{
					WriteObjField(FIELD_TO, *la);
					break;
				}
			}
		}
	}
	else
	{
		#ifndef MAC

		int64 OldPos = Stream.GetPos();
		MailBuf f(Stream, Store->GetObjectSize());

		#else

		LFile &f = Stream;

		#endif

		f >> Magic;
		if (Magic == MAGIC_MAIL)
		{
			short OldCodePage = -1;
			ulong Fields = 0;
			f >> Fields;

			LAssert(InternetHeader == 0);

			ulong i = 0;
			uint16 FieldId;
			int32 FieldSize;
			for (; i<Fields; i++)
			{
				if (f.Eof())
				{
					break;
				}

				f >> FieldId;

				// char *Ptr = Buf.GetPtr();
				switch (FieldId)
				{
					ReadObjField(FIELD_FROM, From);
					ReadObjField(FIELD_REPLY, Reply);
					ReadDateField(FIELD_DATE_RECEIVED, DateReceived, false);
					ReadDateField(FIELD_DATE_SENT, DateSent, false);
					ReadStrField(FIELD_SUBJECT, Subject);
					ReadStrField(FIELD_MESSAGE_ID, MessageID);
					ReadStrField(FIELD_BOUNCE_MSG_ID, BounceMessageID);
					ReadStrField(FIELD_INTERNET_HEADER, InternetHeader);
					ReadIntField(FIELD_FLAGS, Flags);
					ReadIntField(FIELD_PRIORITY, Priority);
					ReadIntField(FIELD_MARK_COLOUR, MarkColour);
					ReadStrField(FIELD_LABEL, Label);
					ReadIntField(FIELD_CODE_PAGE, OldCodePage);
					ReadStrField(FIELD_REFERENCES, References);
					ReadStrField(FIELD_FWD_MSG_ID, FwdMsgId);
					ReadStrField(FIELD_SERVER_UID, ServerUid);
					ReadIntField(FIELD_ACCOUNT_ID, AccountId);
					ReadStrField(FIELD_TEXT, Body);
					ReadStrField(FIELD_CHARSET, BodyCharset);
					ReadStrField(FIELD_ALTERNATE_HTML, Html);
					ReadStrField(FIELD_HTML_CHARSET, HtmlCharset);

					case FIELD_TO:
					case FIELD_CC:
					case FIELD_BCC:
					{
						int32 FieldSize;
						f >> FieldSize;
						
						Mail2Addr *a = new Mail2Addr(GetStore());
						if (a)
						{
							if (a->Serialize(f, Write))
							{
								// a->OnFind(&Contact::Everyone, true);

								switch (FieldId)
								{
									case FIELD_CC:
									{
										a->CC = MAIL_ADDR_CC;
										break;
									}
									case FIELD_BCC:
									{
										a->CC = MAIL_ADDR_BCC;
										break;
									}
									default:
									{
										a->CC = MAIL_ADDR_TO;
										break;
									}
								}

								To.Insert(a);
							}
							else
							{
								printf("%s:%i - ListAddr::Serialize failed.\n", __FILE__, __LINE__);
								DeleteObj(a);
							}
						}
						break;
					}
					case FIELD_ADDRESSED_TO:
					{
						int32 Size;
						f >> Size;
						f.Seek(Size, SEEK_CUR);
						break;
					}
					default:
					{
						f >> FieldSize;
						f.Seek(FieldSize, SEEK_CUR);
						// FieldSize = *((int32*&)Ptr)++;
						// Ptr += FieldSize;

						// printf("%s:%i - Invalid field id '%i' when reading message.\n", __FILE__, __LINE__, FieldId);
						// i = Fields;
						break;
					}
				}
			}

			if (!f.GetStatus() || Fields < 1)
			{
				printf("%s:%i - read %lu of %i fields, status=%i\n",
					__FILE__, __LINE__,
					i, (int)Fields, f.GetStatus());
			}
			
			if (OldCodePage >= 0)
			{
				// Convert to a new charset string instead of a INT
				const char *OldCp[] =
				{
					"us-ascii",
					"iso-8859-1",
					"iso-8859-2",
					"iso-8859-3",
					"iso-8859-4",
					"iso-8859-5",
					"iso-8859-6",
					"iso-8859-7",
					"iso-8859-8",
					"iso-8859-9",
					"iso-8859-15",
					"windows-1250",
					"windows-1252",
					"utf-8"
				};

				if (OldCodePage < CountOf(OldCp))
				{
					BodyCharset = NewStr(OldCp[OldCodePage]);
				}
			}
		}
		else
		{
			return false;
		}
	}

	return Stream.GetStatus();
}

bool ConnectMime(MailData *Mail, AttachmentData *Parent, LMime *Mime)
{
	if (!Mail || !Mime)
		return false;

	AttachmentData *a = new AttachmentData(Mail->Kit);
	if (!a)
		return false;

	LAutoString Mt(Mime->GetMimeType());
	LAutoString Charset(Mime->GetCharset());
	LAutoString FileName(DecodeRfc2047(Mime->GetFileName()));
	LAutoString ContentId(Mime->Get("Content-Id"));

	a->SetStr(FIELD_MIME_TYPE, Mt);
	a->SetStr(FIELD_NAME, FileName);
	if (ContentId)
	{
		LAutoString Tmp(TrimStr(ContentId, "<>"));
		a->SetStr(FIELD_CONTENT_ID, Tmp);
	}

	if (a->IsMultipart())
	{
		a->SetPlaceholder();
		a->SetStr(FIELD_CHARSET, Charset);
	}
	else
	{
		// Single non multipart body
		if (a->IsPlainText() && !Mail->Body)
		{
			a->SetExtern(&Mail->Body, &Mail->BodyCharset);
		}
		else if (a->IsHtml() && !Mail->Html)
		{
			a->SetExtern(&Mail->Html, &Mail->HtmlCharset);
		}

		a->SetStr(FIELD_CHARSET, Charset);
		
		GAutoStreamI dt(Mime->GetData(true));
		a->SetStream(dt);
	}

	if (Parent)
	{
		a->AttachTo(Parent);
	}
	else
	{
		a->AttachTo(Mail);
	}

	for (int i=0; i<Mime->Length(); i++)
	{
		LMime *c = (*Mime)[i];
		if (!ConnectMime(Mail, a, c))
			return false;
	}

	return true;
}

bool MailData::SetMime(LAutoPtr<LMime> m)
{
	// Convert a parsed email into the right fields
	bool Status = false;

	if (!m)
		return false;

	LVariant AutoDeleteExe;
	Kit->Callback->GetOptions()->GetValue(OPT_AutoDeleteExe, AutoDeleteExe);
	if ((Status = ConnectMime(this, 0, m)))
	{	
		// ParseHeaders();
	}
	
	return Status;
}
