#include "ScribeMapi.h"
#include "lgi/common/Store3MimeTree.h"

LMapiMail::LMapiMail(LMapiStore *store) :
	LMapiThing(store),
	From(store),
	Reply(store)
{
	Subject = NULL;
	Flags = 0;
	MsgSize = 0;
	Seg = NULL;
	TxtBody = NULL;
	HtmlBody = NULL;
	From.m = this;
	Reply.m = this;
}

LMapiMail::~LMapiMail()
{
	To.DeleteObjects();
	DeleteObj(Seg);
}

void LMapiMail::Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *Lst)
{
	Entry.Add((uint8_t*)entry->Value.bin.lpb, entry->Value.bin.cb);
	Parent = parent;
	
	if (Lst)
	{
		SPropValue *p = Lst->GetField(PR_CLIENT_SUBMIT_TIME);
		if (p) MapiCastDate(Date, p);
		p = Lst->GetField(PR_SUBJECT);
		if (p) Subject = MapiCastString(p);	

		From.Name = MapiCastString(Lst->GetField(PR_SENT_REPRESENTING_NAME));
		if (!From.Name)
			From.Name = MapiCastString(Lst->GetField(PR_SENDER_NAME));
		From.Email = MapiCastString(Lst->GetField(PR_SENDER_EMAIL_ADDRESS));

		Class = MapiCastString(Lst->GetField(PR_ORIG_MESSAGE_CLASS));
		if (!Class)
			Class = MapiCastString(Lst->GetField(PR_MESSAGE_CLASS));
		bool Post = Class ? _strnicmp(Class, "IPM.Post", 8) == 0 : false;

		int64 f = MapiCastInt(Lst->GetField(PR_MESSAGE_FLAGS));
		if (f & (MSGFLAG_SUBMIT | MSGFLAG_UNSENT) && !Post)
			Flags |= MAIL_CREATED;
		else
			Flags |= MAIL_RECEIVED;	
		if (f & MSGFLAG_HASATTACH)
			Flags |= MAIL_ATTACHMENTS;
		if (f & MSGFLAG_READ)
			Flags |= MAIL_READ;
		
		MsgSize = MapiCastInt(Lst->GetField(PR_MESSAGE_SIZE));
		TxtBody = MapiCastString(Lst->GetField(PR_BODY));
		HtmlBody = MapiCastString(Lst->GetField(PR_BODY_HTML));
	}
}

static int HandleLoads = 0;
static uint64 HandleTs = 0;

LPMESSAGE LMapiMail::Handle()
{
	if (!MapiMsg && Parent && Parent->Handle())
	{
		ULONG Type = 0;
		IUnknown *Item = NULL;
		HRESULT e = Parent->Handle()->OpenEntry(	(ULONG)Entry.Length(),
													(LPENTRYID)&Entry[0],
													NULL,
													MAPI_BEST_ACCESS,
													&Type,
													&Item);
		if (SUCCEEDED(e) && Item)
		{
			HandleLoads++;
			switch (Type)
			{
				case MAPI_MESSAGE:
				{
					Item->QueryInterface(IID_IMessage, (void**)&MapiMsg);
					break;
				}
				default:
				{
					LAssert(0);
					break;
				}
			}
			
			if (Item)
				Item->Release();
			
			if (LCurrentTime() - HandleTs > 500)
			{
				HandleTs = LCurrentTime();
				LgiTrace("HandleLoads = %i\n", HandleLoads);
			}
		}
		else Store->Error("%s:%i - OpenEntry failed with 0x%x\n", _FL, e);
	}
	
	return MapiMsg;
}

Store3CopyImpl(LMapiMail)
{
	return false;
}

const char *LMapiMail::GetStr(int id)
{
	switch (id)
	{
		// Mail fields
		case FIELD_INTERNET_HEADER:
			return MapiGetPropStr(Handle(), PR_TRANSPORT_MESSAGE_HEADERS);
		case FIELD_MIME_TYPE:
			if (!MimeType)
			{
				char *Hdrs = MapiGetPropStr(Handle(), PR_TRANSPORT_MESSAGE_HEADERS);
				LAutoString c(InetGetHeaderField(Hdrs, "Content-Type", -1));
				char *semi = c ? strchr(c, ';') : NULL;
				if (semi) *semi = 0;
				MimeType = c;
				MimeType = MimeType.Strip();
			}
			return MimeType;
		case FIELD_MESSAGE_ID:
			return MapiGetPropStr(Handle(), PR_INTERNET_MESSAGE_ID);
		case FIELD_SUBJECT:
			if (!Subject) Subject = MapiGetPropStr(Handle(), PR_SUBJECT);
			return Subject;
		case FIELD_TEXT:
			if (!TxtBody) TxtBody = MapiGetPropStr(Handle(), PR_BODY);
			return TxtBody;
		case FIELD_ALTERNATE_HTML:
			if (!HtmlBody && Handle())
			{
				IStream *Html = NULL;
				HRESULT res = Handle()->OpenProperty(PR_HTML, &IID_IStream, 0, 0, (IUnknown**) &Html);
				if (SUCCEEDED(res))
				{
					STATSTG s;
					HRESULT res = Html->Stat(&s, STATFLAG_DEFAULT);
					if (SUCCEEDED(res))
					{
						HtmlBody.Set(NULL, (NativeInt)s.cbSize.QuadPart);
						ULONG Rd = 0;
						res = Html->Read(HtmlBody.Get(), (ULONG)HtmlBody.Length(), &Rd);
						if (FAILED(res))
						{
							HtmlBody.Empty();
						}
					}
					Html->Release();
				}
			}
			if (!HtmlBody)
				HtmlBody = MapiGetPropStr(Handle(), PR_BODY_HTML);
			return HtmlBody;
		case FIELD_HTML_CHARSET:
		case FIELD_CHARSET:
			if (!Charset)
			{
				int Cp = (int)MapiGetPropInt(Handle(), PR_INTERNET_CPID);
				Charset = LAnsiToLgiCp(Cp);
				if (!Charset)
					LgiTrace("%s:%i - No charset for cp %i\n", _FL, Cp);
			}
			return Charset;
		case FIELD_LABEL:
			return NULL;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiMail::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_SUBJECT:
		{
			if (MapiSetPropStr(Handle(), PR_SUBJECT, str))
				SetDirty();
			else
				return Store3Error;
			break;
		}
		default:
		{
			LAssert(0);
			return Store3Error;
		}
	}
	
	return Store3Success;
}

int64 LMapiMail::GetInt(int id)
{
	switch (id)
	{
		case FIELD_FLAGS:
			return Flags;
		case FIELD_SIZE:
			return MsgSize;
		case FIELD_DONT_SHOW_PREVIEW:
			return true;
		case FIELD_COLOUR:
			return 0;
		case FIELD_PRIORITY:
			return 0;
		case FIELD_ACCOUNT_ID:
			return Store->GetInt(FIELD_ACCOUNT_ID);
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiMail::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_FLAGS:
		{
			bool ReadChange = ((i & MAIL_READ) != 0) ^ ((Flags & MAIL_READ) != 0);
			Flags = i;			
			if (ReadChange && Handle())
			{
				HRESULT res = MapiMsg->SetReadFlag(Flags & MAIL_READ ? 0 : CLEAR_READ_FLAG);
				if (FAILED(res))
					Store->Error("%s:%i - SetReadFlag failed with %x\n", _FL, res);
			}
			break;
		}
		default:
		{
			LAssert(0);
			return Store3Error;
		}
	}
	
	return Store3Success;
}

const LDateTime *LMapiMail::GetDate(int id)
{
	switch (id)
	{
		case FIELD_DATE_RECEIVED:
		case FIELD_DATE_SENT:
			if (Date.Year() == 0)
				MapiGetPropDate(Date, Handle(), PR_CLIENT_SUBMIT_TIME);
			return &Date;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiMail::SetDate(int id, const LDateTime *i)
{
	switch (id)
	{
		case FIELD_DATE_RECEIVED:
		case FIELD_DATE_SENT:
			if (MapiSetPropDate(Handle(), PR_CLIENT_SUBMIT_TIME, *i))
				return Store3Success;
			break;
		default:
			LAssert(0);
			break;
	}
	
	return Store3Error;
}

LDataPropI *LMapiMail::GetObj(int id)
{
	switch (id)
	{
		case FIELD_FROM:
			return &From;
		case FIELD_REPLY:
			return &Reply;
		case FIELD_MIME_SEG:
			if (!Seg && Handle())
			{
				LPMAPITABLE hAttach = NULL;
				HRESULT res = Handle()->GetAttachmentTable(MAPI_UNICODE, &hAttach);
				if (SUCCEEDED(res))
				{
					LArray<LMapiAttachment*> Segs;
					for (ScribeMapiList Lst(hAttach); Lst.More(); Lst.Next())
					{
						LAutoPtr<LMapiAttachment> a(new LMapiAttachment(Store));
						if (a->Set(this, &Lst))
							Segs.Add(a.Release());
					}
				
					auto Txt = GetStr(FIELD_TEXT);
					if (Txt)
					{
						LAutoPtr<LMapiAttachment> a(new LMapiAttachment(Store));
						if (a && a->Set(Txt, GetStr(FIELD_CHARSET), "text/plain"))
							Segs.Add(a.Release());
					}
					
					auto Html = GetStr(FIELD_ALTERNATE_HTML);
					if (Html)
					{
						LAutoPtr<LMapiAttachment> a(new LMapiAttachment(Store));
						if (a && a->Set(Html, GetStr(FIELD_HTML_CHARSET), "text/html"))
							Segs.Add(a.Release());
					}
					
					Store3MimeTree<LMapiStore, LMapiMail, LMapiAttachment> Tree(this, Seg);
					for (unsigned i=0; i<Segs.Length(); i++)
					{
						Tree.Add(Segs[i]);
					}
					
					Tree.Build();
				}
			}
			return Seg;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiMail::SetObj(int id, LDataPropI *i)
{
	switch (id)
	{
		case FIELD_MIME_SEG:
		default:
			LAssert(0);
			break;
	}
	
	return Store3Error;
}

GDataIt LMapiMail::GetList(int id)
{
	switch (id)
	{
		case FIELD_TO:
		{
			if (To.State == Store3Unloaded)
			{
				LPMAPITABLE Recipients = 0;
				if (Handle() &&
					SUCCEEDED(Handle()->GetRecipientTable(MAPI_UNICODE, &Recipients)) &&
					Recipients)
				{
					for (ScribeMapiList Lst(Recipients); Lst.More(); Lst.Next())
					{
						SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME_W);
						SPropValue *Email1 = Lst.GetField(PR_EMAIL_ADDRESS);
						SPropValue *Email2 = Lst.GetField(PR_SMTP_ADDRESS);
						LAutoPtr<LMapiAddr> a(new LMapiAddr(Store));
						if ((Name || Email1 || Email2) && a)
						{
							a->Name = MapiCastString(Name);
							if (strchr(MapiCastString(Email1), '@'))
								a->Email = MapiCastString(Email1);
							else
								a->Email = MapiCastString(Email2);
							
							SPropValue *Type = Lst.GetField(PR_RECIPIENT_TYPE);
							if (Type)
							{
								int64 Flags = MapiCastInt(Type);
								if (Flags & MAPI_TO)
									a->CC = MAIL_ADDR_TO;
								else if (Flags & MAPI_CC)
									a->CC = MAIL_ADDR_CC;
								else if (Flags & MAPI_BCC)
									a->CC = MAIL_ADDR_BCC;
							}
							
							a->m = this;
							To.Insert(a.Release(), -1, true);
						}
					}
				}

				To.State = Store3Loaded;
			}
			
			return &To;
		}
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiMail::SetRfc822(LStreamI *m)
{
	// IConverterSession does the handling of converting MIME to MAPI (and back)
	LAssert(0);
	return Store3Error;
}

uint32_t LMapiMail::Type()
{
	return MAGIC_MAIL;
}

bool LMapiMail::IsOnDisk()
{
	return true;
}

bool LMapiMail::IsOrphan()
{
	return false;
}

uint64 LMapiMail::Size()
{
	LAssert(0);
	return 0;
}

Store3Status LMapiMail::Save(LDataI *Parent)
{
	LAssert(0);
	return Store3Error;
}

Store3Status LMapiMail::Delete(bool ToTrash)
{
	LArray<LDataI*> del;
	del.Add(this);
	return Store->Delete(del, true);
}

LAutoStreamI LMapiMail::GetStream(const char *file, int line)
{
	LAutoStreamI s;
	LAssert(0);
	return s;
}

////////////////////////////////////////////
LMapiAddr::LMapiAddr(LMapiStore *store)
{
	Store = store;
	CC = 0;
	Status = 0;
	m = NULL;	
}

Store3CopyImpl(LMapiAddr)
{
	CC = (int)p.GetInt(FIELD_CC);
	Name = p.GetStr(FIELD_NAME);
	Email = p.GetStr(FIELD_EMAIL);
	return true;
}

const char *LMapiAddr::GetStr(int id)
{
	if (!m)
	{
		LAssert(0);
		return NULL;
	}
	switch (id)
	{
		case FIELD_NAME:
			return Name;
		case FIELD_EMAIL:
			return Email;
		default:
			LAssert(0);
			break;
	}
	return NULL;
}

Store3Status LMapiAddr::SetStr(int id, const char *str)
{
	if (!m)
	{
		LAssert(0);
		return Store3Error;
	}
	switch (id)
	{
		case FIELD_NAME:
			Name = str;
			return Store3Success;
		case FIELD_EMAIL:
			Email = str;
			return Store3Success;
		default:
			LAssert(0);
			break;
	}
	return Store3Error;
}

int64 LMapiAddr::GetInt(int id)
{
	switch (id)
	{
		case FIELD_CC:
			return CC;
		case FIELD_STATUS:
			return Status;
	}
	
	LAssert(0);
	return -1;
}

Store3Status LMapiAddr::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_CC:
			CC = (int)i;
			break;
		case FIELD_STATUS:
			Status = (int)i;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}
	
	return Store3Success;
}

