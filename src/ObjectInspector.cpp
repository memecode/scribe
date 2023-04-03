#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "ObjectInspector.h"

#define DefFld(type, name)   { type, #name, name }
struct Field
{
	LVariantType Type;
	const char *Name;
	int Id;
};

static Field MailProps[] =
{
	DefFld(GV_INT64, FIELD_STORE_TYPE),
	DefFld(GV_INT64, FIELD_SIZE),
	DefFld(GV_INT64, FIELD_LOADED),
	DefFld(GV_INT64, FIELD_PRIORITY),
	DefFld(GV_INT64, FIELD_FLAGS),
	DefFld(GV_INT64, FIELD_DONT_SHOW_PREVIEW),
	DefFld(GV_INT64, FIELD_ACCOUNT_ID),
	DefFld(GV_INT64, FIELD_COLOUR),

	DefFld(GV_STRING, FIELD_DEBUG),
	DefFld(GV_STRING, FIELD_SUBJECT),
	DefFld(GV_STRING, FIELD_CHARSET),
	DefFld(GV_STRING, FIELD_TEXT),
	DefFld(GV_STRING, FIELD_HTML_CHARSET),
	DefFld(GV_STRING, FIELD_ALTERNATE_HTML),
	DefFld(GV_STRING, FIELD_LABEL),
	DefFld(GV_STRING, FIELD_INTERNET_HEADER),
	DefFld(GV_STRING, FIELD_REFERENCES),
	DefFld(GV_STRING, FIELD_FWD_MSG_ID),
	DefFld(GV_STRING, FIELD_BOUNCE_MSG_ID),
	DefFld(GV_STRING, FIELD_SERVER_UID),
	DefFld(GV_STRING, FIELD_MESSAGE_ID),
	
	DefFld(GV_DATETIME, FIELD_DATE_SENT),
	DefFld(GV_DATETIME, FIELD_DATE_RECEIVED),
};

static Field MimeSegProps[] =
{
	DefFld(GV_INT64, FIELD_STORE_TYPE),
	DefFld(GV_INT64, FIELD_SIZE),

	DefFld(GV_STRING, FIELD_CHARSET),
	DefFld(GV_STRING, FIELD_NAME),
	DefFld(GV_STRING, FIELD_MIME_TYPE),
	DefFld(GV_STRING, FIELD_CONTENT_ID),
	DefFld(GV_STRING, FIELD_INTERNET_HEADER),
	
	DefFld(GV_STREAM, FIELD_ATTACHMENTS_DATA),
};

static Field CalendarProps[] =
{
	DefFld(GV_INT64, FIELD_STORE_TYPE),
	DefFld(GV_INT64, FIELD_CAL_TYPE),
	DefFld(GV_INT64, FIELD_CAL_COMPLETED),
	DefFld(GV_INT64, FIELD_CAL_SHOW_TIME_AS),
	DefFld(GV_INT64, FIELD_CAL_RECUR),
	DefFld(GV_INT64, FIELD_CAL_RECUR_FREQ),
	DefFld(GV_INT64, FIELD_CAL_RECUR_INTERVAL),
	DefFld(GV_INT64, FIELD_CAL_RECUR_END_COUNT),
	DefFld(GV_INT64, FIELD_CAL_RECUR_END_TYPE),
	DefFld(GV_INT64, FIELD_CAL_RECUR_FILTER_DAYS),
	DefFld(GV_INT64, FIELD_CAL_RECUR_FILTER_MONTHS),
	DefFld(GV_INT64, FIELD_CAL_PRIVACY),
	DefFld(GV_INT64, FIELD_COLOUR),
	DefFld(GV_INT64, FIELD_CAL_ALL_DAY),
	DefFld(GV_INT64, FIELD_STATUS),

	DefFld(GV_STRING, FIELD_TO),
	DefFld(GV_STRING, FIELD_ATTENDEE_JSON),
	DefFld(GV_STRING, FIELD_CAL_TIMEZONE),
	DefFld(GV_STRING, FIELD_CAL_SUBJECT),
	DefFld(GV_STRING, FIELD_CAL_LOCATION),
	DefFld(GV_STRING, FIELD_UID),
	DefFld(GV_STRING, FIELD_CAL_REMINDERS),
	DefFld(GV_STRING, FIELD_CAL_RECUR_FILTER_POS),
	DefFld(GV_STRING, FIELD_CAL_RECUR_FILTER_YEARS),
	DefFld(GV_STRING, FIELD_CAL_NOTES),
	
	DefFld(GV_DATETIME, FIELD_CAL_START_UTC),
	DefFld(GV_DATETIME, FIELD_CAL_END_UTC),
	DefFld(GV_DATETIME, FIELD_CAL_RECUR_END_DATE),
	DefFld(GV_DATETIME, FIELD_CAL_LAST_CHECK),
};

class InspectTreeItem : public LTreeItem
{
public:
	LDataI *Thing;
	LDataPropI *Seg;
	class ObjectInspector *Parent;

	InspectTreeItem(ObjectInspector *parent, LDataI *thing);
	InspectTreeItem(ObjectInspector *parent, LDataPropI *seg);
	void Select(bool b);
};

class InspectListItem : public LListItem
{
	LAutoStreamI Stream;

	LStreamI *GetStream()
	{
		if (!Stream && Parent->Seg)
		{
			LDataI *di = dynamic_cast<LDataI*>(Parent->Seg);
			if (di)
				Stream = di->GetStream(_FL);
		}
		return Stream;
	}

public:
	InspectTreeItem *Parent;
	Field Fld;
	
	InspectListItem(InspectTreeItem *parent, Field f)
	{
		Parent = parent;
		Fld = f;
	}
	
	void Select(bool b);
	const char *GetText(int Col);
};

ObjectInspector::ObjectInspector(LViewI *Parent, Thing *obj) : Box(NULL)
{
	m = obj;
	
	Name("Object Inspector");
	
	LRect r(0, 0, 1400, 700);
	SetPos(r);
	MoveToCenter();

	AddView(Box = new LBox(79));
	Box->AddView(Tree = new LTree(80, 0, 0, 200, 200));
	Box->AddView(Lst = new LList(82, 0, 0, 200, 200));
	Lst->AddColumn("Type", 65);
	Lst->AddColumn("Field", 165);
	Lst->AddColumn("Size", 60);
	Box->AddView(Txt = new LTextLog(81));
	
	Box->SetSize(0, LCss::Len(LCss::LenPx, 200));
	Box->SetSize(1, LCss::Len(LCss::LenPx, 300));

	if (Attach(0))
	{
		AttachChildren();
		Visible(true);
		
		InspectTreeItem *Root = new InspectTreeItem(this, m->GetObject());
		if (Root)
		{
			Tree->Insert(Root);
			Root->Select(true);
		}
	}
}

void ObjectInspector::OnPosChange()
{
	if (Box)
		Box->SetPos(GetClient());
}

InspectTreeItem::InspectTreeItem(ObjectInspector *parent, LDataI *thing)
{
	Parent = parent;
	Thing = thing;
	Seg = NULL;
	
	switch (Thing->Type())
	{
		case MAGIC_MAIL:
		{
			SetText("Mail");

			LDataPropI *Root = Thing->GetObj(FIELD_MIME_SEG);
			if (Root)
			{
				Insert(new InspectTreeItem(Parent, Root));
				Expanded(true);
			}
			break;
		}
		case MAGIC_CALENDAR:
		{
			SetText("Calendar");
			break;
		}
		default:
		{
			LAssert(!"Impl me.");
			break;
		}
	}
}

InspectTreeItem::InspectTreeItem(ObjectInspector *parent, LDataPropI *seg)
{
	Parent = parent;
	Seg = seg;
	Thing = NULL;
	
	auto MimeType = Seg->GetStr(FIELD_MIME_TYPE);
	LString s;
	s.Printf("Mime: %s", MimeType);
	SetText(s);
	
	LDataIt Ch = Seg->GetList(FIELD_MIME_SEG);
	if (Ch)
	{
		for (LDataPropI *c = Ch->First(); c; c = Ch->Next())
		{
			Insert(new InspectTreeItem(Parent, c));
		}
		Expanded(true);
	}
}

void InspectTreeItem::Select(bool b)
{
	LTreeItem::Select(b);
	
	if (!b)
		return;

	Parent->Lst->Empty();
	if (Thing)
	{
		switch (Thing->Type())
		{
			case MAGIC_MAIL:
				for (unsigned i=0; i<CountOf(MailProps); i++)
					Parent->Lst->Insert(new InspectListItem(this, MailProps[i]));
				break;
			case MAGIC_CALENDAR:
				for (unsigned i=0; i<CountOf(CalendarProps); i++)
					Parent->Lst->Insert(new InspectListItem(this, CalendarProps[i]));
				break;
			default:
				LAssert(!"Impl me.");
				break;
		}
	}
	else if (Seg)
	{
		for (unsigned i=0; i<CountOf(MimeSegProps); i++)
			Parent->Lst->Insert(new InspectListItem(this, MimeSegProps[i]));
	}
}

void InspectListItem::Select(bool b)
{
	LListItem::Select(b);
	
	if (!b)
		return;
	
	LTextLog *Txt = Parent->Parent->Txt;
	LDataPropI *Obj = Parent->Thing ? Parent->Thing : Parent->Seg;
	LAssert(Obj != NULL);
	
	switch (Fld.Type)
	{
		case GV_INT64:
		{
			int64 Val = Obj->GetInt(Fld.Id);
			LString s;
			s.Printf("Decimal: " LPrintfInt64 "\nHex: 0x" LPrintfHex64 "\n",
				Val, Val);
			Txt->Name(s);
			break;
		}
		case GV_STRING:
		{
			Txt->Name(Obj->GetStr(Fld.Id));
			break;
		}
		case GV_DATETIME:
		{
			auto dt = Obj->GetDate(Fld.Id);
			if (dt)
			{
				char s[64] = "#error";
				dt->Get(s, sizeof(s));
				Txt->Name(s);
			}
			else Txt->Name("#error: NULL datetime.");
			break;
		}
		case GV_STREAM:
		{
			LStreamI *s = GetStream();
			if (s)
			{
				int64 Sz = s->GetSize();
				if (Sz > 0)
				{
					LAutoPtr<char> Buf(new char[(int)Sz+1]);
					if (Buf)
					{
						s->SetPos(0);
						ssize_t r = s->Read(Buf, (int)Sz);
						if (r >= 0)
						{
							Buf[r] = 0;

							if (LIsUtf8(Buf))
								Txt->Name(Buf);
							else
								Txt->Name("Error: Invalid utf-8");
						}
						else Txt->Name("#error: stream read failed.");
					}
					else Txt->Name("#error: memory alloc failed.");
				}
				else Txt->Name("#error: empty stream.");
			}
			else Txt->Name("#error: NULL stream.");
			break;
		}
		default:
		{
			Txt->Name("#error: unknown type.");
			break;
		}
	}
}

const char *InspectListItem::GetText(int Col)
{
	switch (Col)
	{
		case 0:
			switch (Fld.Type)
			{
				case GV_INT64:
					return "Int";
				case GV_STRING:
					return "String";
				case GV_DATETIME:
					return "DateTime";
				case GV_STREAM:
					return "Stream";
				default:
					return "#error: unknown type";
			}
			break;
		case 1:
			return Fld.Name;
		case 2:
		{
			static char s[16];

			switch (Fld.Type)
			{
				case GV_INT64:
					return (char*)"8";
				case GV_STRING:
				{
					LDataPropI *Obj = Parent->Thing ? Parent->Thing : Parent->Seg;
					auto Str = Obj->GetStr(Fld.Id);
					if (!Str)
						return (char*)"NULL";
					sprintf_s(s, sizeof(s), "%i", (int)strlen(Str));
					return s;
				}
				case GV_DATETIME:
				{
					sprintf_s(s, sizeof(s), "%i", (int)sizeof(LDateTime));
					return s;
				}
				case GV_STREAM:
				{
					LStreamI *Str = GetStream();
					if (Str)
					{
						sprintf_s(s, sizeof(s), LPrintfInt64, Str->GetSize());
						return s;
					}						
					break;
				}
				default:
					break;
			}
			break;
		}
	}
	
	return NULL;
}

