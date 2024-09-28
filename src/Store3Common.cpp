#include <time.h>

#include "lgi/common/Lgi.h"
#include "lgi/common/Mail.h"
#include "lgi/common/TextConvert.h"

#include "Store3Common.h"
#include "ScribeInc.h"
#include "DomType.h"

LHashTbl<IntKey<int>,LDataStoreI*> LDataStoreI::Map;

//////////////////////////////////////////////////////////////////////////////////////
const char *Store3ItemTypeToMime(Store3ItemTypes type)
{
	switch (type)
	{
		case MAGIC_MAIL:       return "message/rfc822";
		case MAGIC_CONTACT:    return "text/vcard";
		case MAGIC_ATTACHMENT: return "application/octet-stream";
		case MAGIC_CALENDAR:   return "text/vcalendar";
		case MAGIC_FILTER:     return "text/x-email-filter";
		default:
			LAssert(!"Unknown type");
			break;
	}
	
	return NULL;
}

CalendarType ParseCalendarType(const char *type)
{
	if (type && ToLower(*type) == 'v')
		type++;
	#define _(name) if (!Stricmp(type, #name)) return Cal##name;
	_(Event) _(Todo) _(Journal) _(Request) _(Reply)
	#undef _
	return CalTypeMax;
}

const char *ToString(CalendarType type)
{
	switch (type)
	{
		case CalEvent:   return "Event";
		case CalTodo:    return "Todo";
		case CalJournal: return "Journal";
		case CalRequest: return "Request";
		case CalReply:   return "Reply";
		default:         return NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////////////
LDataUserI::~LDataUserI()
{
	if (Object)
		Object->UserData = NULL;
}

bool LDataUserI::SetObject(LDataI *o, bool InDestuctor, const char *File, int Line)
{
	if (o == Object)
		return true;

	if (!o && ObjectLock)
	{
		LAssert(!"Object getting set to NULL.");
	}

	if (Object)
	{
		Object->UserData = NULL;
		if (!InDestuctor && Object->IsOrphan())
			delete Object;
		Object = NULL;
	}

	Object = o;
	if (File)
		SetterRef.Printf("%s:%i", File, Line);
	else
		SetterRef.Empty();

	if (Object)
		Object->UserData = this;

	return true;
}

//////////////////////////////////////////////////////////////////////////////
bool LDataI::ParseHeaders()
{
	// Reload from headers...
	LString InetHdrs = GetStr(FIELD_INTERNET_HEADER);
	auto Subject = LDecodeRfc2047(LGetHeaderField(InetHdrs, "subject"));
	if (LIsUtf8(Subject))
		SetStr(FIELD_SUBJECT, Subject);

	// From
	auto s = LDecodeRfc2047(LGetHeaderField(InetHdrs, "from"));
	if (LIsUtf8(s))
	{
		auto from = GetObj(FIELD_FROM);
		DecodeAddrName(s, [&](auto name, auto email)
		{
			from->SetStr(FIELD_NAME, name);
			from->SetStr(FIELD_EMAIL, email);
		},	NULL);
	}

	s = LDecodeRfc2047(LGetHeaderField(InetHdrs, "reply-to"));
	if (LIsUtf8(s))
	{
		auto replyTo = GetObj(FIELD_REPLY);
		DecodeAddrName(s, [&](auto name, auto email)
		{
			replyTo->SetStr(FIELD_NAME, name);
			replyTo->SetStr(FIELD_EMAIL, email);
		},	NULL);
	}

	// Parse To and CC headers.
	s = LDecodeRfc2047(LGetHeaderField(InetHdrs, "to"));
	if (s.IsUtf8())
		ParseAddresses(s, MAIL_ADDR_TO);
	s = LDecodeRfc2047(LGetHeaderField(InetHdrs, "cc"));
	if (s.IsUtf8())
		ParseAddresses(s, MAIL_ADDR_CC);

	// Data
	if ((s = LGetHeaderField(InetHdrs, "date")))
	{
		LDateTime dt;
		if (dt.Decode(s))
		{
			dt.ToUtc();
			SetDate(FIELD_DATE_SENT, &dt);
		}
	}

	return true;
}

bool LDataI::ParseAddresses(const char *Str, int CC)
{
	LString::Array Addr;
	TokeniseStrList(Str, Addr, ",");

	auto store = GetStore();
	auto to = GetList(FIELD_TO);
	if (!to || !store)
		return false;

	for (auto &RawAddr: Addr)
	{
		LAutoPtr<LDataPropI> a(to->Create(store));
		if (!a)
			return false;

		auto sa = dynamic_cast<Store3Addr*>(a.Get());
		LAssert(sa != NULL);
		if (!sa)
			return false;

		DecodeAddrName(RawAddr, sa->Name, sa->Addr, 0);
		sa->CC = CC;
		to->Insert(a.Release());
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////
Store3Addr::Store3Addr(LDataStoreI *store, LDataPropI *i)
{
	LAssert(store != NULL);
	Store = store;
	CC = 0;
	if (i)
		CopyProps(*i);
}

Store3Addr::~Store3Addr()
{
}

size_t Store3Addr::Sizeof()
{
	return	sizeof(*this) +
			Addr.Length() +
			Name.Length();
}

void Store3Addr::SetStore(LDataStoreI *s)
{
	Store = s;
	LAssert(Store != NULL);
}

void Store3Addr::Empty()
{
	Name.Empty();
	Addr.Empty();
	CC = 0;
}

Store3CopyImpl(Store3Addr)
{
	Empty();
	Name = p.GetStr(FIELD_NAME);
	Addr = p.GetStr(FIELD_EMAIL);
	CC = (int)p.GetInt(FIELD_CC);
	return true;
}

const char *Store3Addr::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
			return Name;
		case FIELD_EMAIL:
			return Addr;
	}

	return 0;
}

Store3Status Store3Addr::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_NAME:
		{
			Name = str;
			break;
		}
		case FIELD_EMAIL:
		{
			Addr = str;
			break;
		}
		default:
			return Store3Error;
	}

	return Store3Success;
}

bool Store3Addr::GetVariant(const char *n, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(n);
	switch (Fld)
	{
		case SdName: // Type: String
		{
			Value = Name;
			break;
		}
		case SdEmail: // Type: String
		{
			Value = Addr;
			break;
		}
		case SdDomain: // Type: String
		{
			char *At = Addr ? strchr(Addr, '@') : NULL;
			if (At)
				Value = At + 1;
			else
				Value.Empty();
			break;
		}
		case SdText: // Type: String
		{
			char s[512];
			LString EscName;
			if (Name)
				EscName = LString::Escape(Name, -1, "\'");
			
			if (Name && Addr)
				sprintf_s(s, sizeof(s), "\"%s\" <%s>", EscName.Get(), (char*)Addr);
			else if (Name)
				sprintf_s(s, sizeof(s), "\"%s\"", EscName.Get());
			else if (Addr)
				sprintf_s(s, sizeof(s), "<%s>", (char*)Addr);
			else
				return false;

			s[sizeof(s)-1] = 0;
			Value = s;
			break;
		}
		case SdContact: // Type: Contact
		{
			LDataEventsI *e = Store->GetEvents();
			if (!e)
				return false;

			LArray<LDom*> Matches;
			if (!e->Match(Store, this, MAGIC_CONTACT, Matches))
				return false;

			Value = Matches[0];
			break;
		}
		case SdGroups: // Type: String[]
		{
			LDataEventsI *e = Store->GetEvents();
			if (!e)
				return false;

			LArray<LDom*> Matches;
			if (e->Match(Store, this, MAGIC_GROUP, Matches))
			{				
				Value.SetList();
				for (unsigned i=0; i<Matches.Length(); i++)
				{
					LAutoPtr<LVariant> v(new LVariant);
					if (Matches[i]->GetValue("Name", *v))
						Value.Value.Lst->Insert(v.Release());
				}
			}
			else
				Value.Empty();
			break;
		}
		default:
		{
			LAssert(!"Not a supported field.");
			return false;
		}
	}

	return true;
}

bool Store3Addr::SetVariant(const char *n, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(n);
	switch (Fld)
	{
		case SdName: // Type: String
		{
			Name = Value.Str();
			break;
		}
		case SdEmail: // Type: String
		{
			Addr = Value.Str();
			break;
		}
		case SdText: // Type: String
		{
			DecodeAddrName(Value.Str(), [this](LString name, LString addr){
				Name = LString::UnEscape(name);
				Addr = addr;
			}, NULL);
			break;
		}
		default:
		{
			LAssert(!"Not a valid field");
			return false;
		}
	}
	
	return true;
}

int64 Store3Addr::GetInt(int id)
{
	switch (id)
	{
		case FIELD_CC:
			return CC;
	}
	return -1;
}

Store3Status Store3Addr::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_CC:
			CC = (int)i;
			break;
		default:
			return Store3Error;
	}
	return Store3Success;
}

///////////////////////////////////////////////////////////////////////////////////
Store3Field::Store3Field(LDataStoreI *Store, int id, int width)
{
	Id = id;
	Width = width;
}

const char *Store3Field::GetStr(int id)
{
	return 0;
}

int64 Store3Field::GetInt(int id)
{
	switch (id)
	{
		case FIELD_ID:
			return Id;
		case FIELD_WIDTH:
			return Width;
	}

	return -1;
}

Store3Status Store3Field::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_ID:
			Id = (int)i;
			break;
		case FIELD_WIDTH:
			Width = (int)i;
			break;
		default:
			return Store3Error;
	}

	return Store3Success;
}

////////////////////////////////////////////////////////////////////////////////
// Mime conversion
bool Store3ToLMime(LMime *Out, LDataPropI *InInterface)
{
	auto In = dynamic_cast<LDataI*>(InInterface);
	if (!Out || !In)
	{
		LAssert(0);
		return false;
	}
	
	auto Type = In->Type();
	if (Type == MAGIC_MAIL)
	{
		auto Sub = In->GetList(FIELD_MIME_SEG);
		auto Child = Sub->First();
		if (Child)
		{
			if (!Store3ToLMime(Out, Child))
				return false;
		}
		else LAssert(0);
	}
	else if (Type == MAGIC_ATTACHMENT)
	{
		auto Hdrs = In->GetStr(FIELD_INTERNET_HEADER);
		if (Hdrs)
		{
			if (!Out->SetHeaders(Hdrs))
			{
				LAssert(0);
				return false;
			}
		}
		else
		{
			// No headers???
		}

		auto Charset = In->GetStr(FIELD_CHARSET);
		if (Charset)
			Out->SetCharset(Charset);
		
		auto Data = In->GetStream(_FL);
		if (Data)
		{
			if (!Out->SetData(true, Data.Release()))
			{
				LAssert(0);
				return false;
			}
		}

		LDataIt Sub = In->GetList(FIELD_MIME_SEG);
		for (LDataPropI *Child = Sub->First(); Child; Child = Sub->Next())
		{
			LMime *NewSeg = Out->NewChild();
			if (NewSeg)
			{
				if (Store3ToLMime(NewSeg, Child))
					Out->Insert(NewSeg);
				else
					return false;
			}
			else
			{
				LAssert(0);
				return false;
			}
		}
	}
	else
	{
		LAssert(!"Incorrect object type.");
		return false;
	}
		
	return true;
}

bool LMimeToStore3(LDataPropI *Out, LMime *In, bool InMemOnly)
{
	LDataI *DataOut = dynamic_cast<LDataI*>(Out);
	if (!DataOut || !In)
	{
		LAssert(0);
		return false;
	}
	
	if (!Out->SetStr(FIELD_INTERNET_HEADER, In->GetHeaders()))
	{
		LAssert(0);
		return false;
	}
	
	if (In->GetLength() > 0)
	{
		LAutoStreamI Data(new LMemStream(In->GetData(), -1, -1));
		if (!Data)
		{
			LAssert(0);
			return false;
		}			
		if (!DataOut)
		{
			LAssert(0);
			return false;
		}
		DataOut->SetStream(Data);
	}

	for (int i=0; i<In->Length(); i++)
	{
		LDataI *cOut = DataOut->GetStore()->Create(MAGIC_ATTACHMENT);
		if (!cOut)
			return false;
		
		LMime *cIn = (*In)[i];
		if (!LMimeToStore3(cOut, cIn))
			return false;
		
		Store3Status s = cOut->Save(DataOut);
		if (s == Store3Error)
		{
			LAssert(0);
			return false;
		}
	}

	return true;
}


////////////////////////////////////////////////////////////////////////////////////////
LString HeadersFromStream(LStreamI *Msg)
{
	LString s;
	int Block = 1024;
	
	Msg->SetPos(0);
	for (ssize_t Pos = 0; Pos < (512 << 10); )
	{
		if (!s.Length(Pos + Block))
			break;
		
		ssize_t Rd = Msg->Read(s.Get() + Pos, s.Length() - Pos);
		if (Rd <= 0)
		{
			s.Empty();
			break;
		}
		
		Pos += Rd;
		s.Length(Pos);

		ptrdiff_t EndOfHeader = s.Find("\r\n\r\n");
		if (EndOfHeader > 0)
		{
			s.Length(EndOfHeader);
			break;
		}
	}
	
	return s;
}

////////////////////////////////////////////////////////////////////////////////////////
LString CreateMboxHeader(LDataI *Object)
{
	LString s;
	LDataPropI *From;
	if (!Object ||
		!(From = Object->GetObj(FIELD_FROM)))
	{
		LAssert(0);
		return s;
	}

	// generate from header
	s.Printf("From %s ", From->GetStr(FIELD_EMAIL));
	
	struct tm Ft;	
	ZeroObj(Ft);
	LDateTime Rec = *Object->GetDate(FIELD_DATE_RECEIVED);
	if (!Rec.Year())
		Rec.SetNow();
		
	Ft.tm_sec = Rec.Seconds();			/* seconds after the minute - [0,59] */
	Ft.tm_min = Rec.Minutes();			/* minutes after the hour - [0,59] */
	Ft.tm_hour = Rec.Hours();			/* hours since midnight - [0,23] */
	Ft.tm_mday = Rec.Day();				/* day of the month - [1,31] */
	Ft.tm_mon = Rec.Month() - 1;		/* months since January - [0,11] */
	Ft.tm_year = Rec.Year() - 1900;		/* years since 1900 */
	Ft.tm_wday = Rec.DayOfWeek();

	char Temp[64];
	strftime(Temp, sizeof(Temp), "%a %b %d %H:%M:%S %Y", &Ft);
	s += Temp;
	s += "\r\n";
	
	return s;
}

