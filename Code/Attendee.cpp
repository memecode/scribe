/*hdr
**      FILE:           Attendee.cpp
**      AUTHOR:         Matthew Allen
**      DATE:           23/12/2001
**      DESCRIPTION:    Scribe calender support
**
**      Copyright (C) 2003 Matthew Allen
**              fret@memecode.com
*/

#include "Scribe.h"
#include "CalendarView.h"
#include "vCard-vCal.h"
#include "resdefs.h"
#include "LgiRes.h"

//////////////////////////////////////////////////////////////////////////////
ItemFieldDef AttendeeFields[] = {
	{"Name", SdName, 				GV_STRING,	FIELD_ATTENDEE_NAME},
	{"Email", SdEmail, 				GV_STRING,	FIELD_ATTENDEE_EMAIL},
	{"Attendence", SdAttendence, 	GV_INT32,	FIELD_ATTENDEE_ATTENDENCE},
	{"Notes", SdNotes, 				GV_STRING,	FIELD_ATTENDEE_NOTE},
	{"Reponse", SdReponse, 			GV_INT32,	FIELD_ATTENDEE_RESPONSE},
	{0}
};

Attendee::Attendee(ScribeWnd *app, Attendee *a)
{
	Cal = a->Cal;
	App = a->App;
	SetAttendeeType(a->GetAttendeeType());
	Edit = false;
	Addr = new ListAddr(app);
	if (Addr)
	{
		char *s;
		if (a->GetField(FIELD_ATTENDEE_NAME, s))
			Addr->Name = NewStr(s);
		if (a->GetField(FIELD_ATTENDEE_EMAIL, s))
			Addr->Addr = NewStr(s);
	}
}

Attendee::Attendee(Calendar *cal, AttendeeType type, Contact *c)
{
	Cal = cal;
	App = Cal->App;
	Edit = false;
	SetAttendeeType(type);
	Addr = c ? new ListAddr(c) : new ListAddr(cal->App);
}

Attendee::Attendee(Calendar *cal, AttendeeType type, char *Name, char *Email)
{
	Cal = cal;
	App = Cal->App;
	Edit = false;
	SetAttendeeType(type);
	Addr = new ListAddr(cal->App, Email, Name);
}

Attendee::~Attendee()
{
	DeleteObj(Addr);
}

AttendeeType Attendee::GetAttendeeType()
{
	int n = ANew;
	GetField(FIELD_ATTENDEE_ATTENDENCE, n);
	return (AttendeeType)n;
}

void Attendee::SetAttendeeType(AttendeeType t)
{
	SetField(FIELD_ATTENDEE_ATTENDENCE, (int)t);
}

#define Prop(Fld, Var, Wr) \
{ }

void Attendee::OnSerialize(bool Write)
{
	if (Addr)
	{
		Prop(FIELD_ATTENDEE_NAME, Addr->Name, false);
		Prop(FIELD_ATTENDEE_EMAIL, Addr->Addr, false);

		if (!Write)
		{
			Addr->OnFind();
		}
	}
}

void Attendee::Change()
{
	if (Addr)
	{
		Prop(FIELD_ATTENDEE_NAME, Addr->Name, true);
		Prop(FIELD_ATTENDEE_EMAIL, Addr->Addr, true);
	}

	Update();

	LListItem::GetList()->SendNotify(LNotifyItemChange);
}

bool Attendee::GetSelection(List<Attendee> &Attendees)
{
	return LListItem::GetList()->GetSelection(Attendees);
}

void Attendee::OnPaint(ItemPaintCtx &Ctx)
{
	Ctx.Fore = GetAttendeeType() == ANew ? LColour(LC_MED, 24) : Ctx.Fore;
	LListItem::OnPaint(Ctx);
}

char *Attendee::GetText(int i)
{
	switch (i)
	{
		case 0:
		{
			switch (GetAttendeeType())
			{
				default:
					break;
				case AMeetingOrganiser:
				{
					return (char*)"  o";
				}
				case ARequiredAttendee:
				{
					return (char*)"  +";
				}
				case AOptionalAttendee:
				{
					return (char*)"  -";
				}
			}
			break;
		}
		case 1:
		{
			if (GetAttendeeType() == ANew)
			{
				if (Edit)
				{
					Edit = 0;
					return 0;
				}

				return (char*)"(click to add)";
			}

			return Addr->GetText(0);
			break;
		}
	}

	return 0;
}

bool Attendee::SetText(const char *s, int c)
{
	switch (c)
	{
		case 1:
		{
			if (ValidStr(s))
			{
				if (GetAttendeeType() == ANew)
				{
					LListItem::GetList()->Insert(new Attendee(Cal, ANew));
				}

				SetAttendeeType(ARequiredAttendee);
				DeleteArray(Addr->Name);
				DeleteArray(Addr->Addr);
				Addr->Addr = NewStr(s);
				Addr->OnFind();
				Change();
			}
			break;
		}
	}

	return true;
}

int Attendee::GetImage(int i)
{
	if (GetAttendeeType() != ANew)
	{
		if (Addr->Length() > 1)
		{
			return ICON_UNKNOWN;
		}

		return ICON_CONTACT;
	}

	return -1;
}

void Attendee::DeleteSelection()
{
	List<Attendee> Attendees;
	if (GetSelection(Attendees))
	{
		for (auto a: Attendees)
		{
			DeleteObj(a);
		}
	}
}

void Attendee::OnMouseClick(LMouse &m)
{
	if (m.Down())
	{
		int Col = LListItem::GetList()->ColumnAtX(m.x);

		if (m.Left())
		{
			if (Col == 1)
			{
				Edit = true;
				EditLabel(Col);
			}
		}
		else if (m.Right() && GetAttendeeType() != ANew)
		{
			LSubMenu *Sub = new LSubMenu;
			if (Sub)
			{
				if (Addr->Length() > 1)
				{
					int n = 1;
					for (RecipientItem *i: *Addr)
					{
						char s[256];
						sprintf_s(s, sizeof(s), "%s <%s>", i->GetName(), i->GetEmail());
						Sub->AppendItem(s, n++, true);
					}
					Sub->AppendSeparator();
				}

				#define BASE 1000
				#define TypeItem(txt, a) {	LMenuItem *item = Sub->AppendItem(txt, BASE+a, true); \
											if (item && GetAttendeeType()==a) item->Checked(true); }
				TypeItem("Meeting Organiser", AMeetingOrganiser);
				TypeItem("Required Attendee", ARequiredAttendee);
				TypeItem("Optional Attendee", AOptionalAttendee);
				Sub->AppendSeparator();

				Sub->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);
				LMouse m;
				if (LListItem::GetList()->GetMouse(m, true))
				{
					int Cmd;
					switch (Cmd = Sub->Float(LListItem::GetList(), m.x, m.y))
					{
						case IDM_DELETE:
						{
							DeleteSelection();
							break;
						}
						default:
						{
							RecipientItem *i;
							if (Cmd >= BASE)
							{
								SetAttendeeType((AttendeeType)(Cmd - BASE));
								Change();
							}
							else if ((i = (*Addr)[Cmd - 1]))
							{
								Addr->SetWho(i);
								Change();
							}
							break;
						}
					}
				}

				DeleteObj(Sub);
			}
		}
	}
}

bool Attendee::OnKey(LKey &k)
{
	if (k.Down())
	{
		switch (k.vkey)
		{
			case VK_DELETE:
			{
				DeleteSelection();
				return true;
				break;
			}
			case VK_SPACE:
			case VK_RETURN:
			{
				Edit = true;
				EditLabel(1);
				return true;
				break;
			}
		}
	}
	
	return false;
}

LColour Attendee::GetColour()
{
	CalendarSource *s = Source.Length() ? Source[0].Get() : NULL;
	return s ? s->GetColour() : LColour(LC_FOCUS_SEL_BACK, 24);
}

bool Attendee::GetFreeBusy(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &e)
{
	bool Status = false;

	if (Cal && Addr)
	{
		if (!Source.Length())
		{
			if (Cal->App->GetCalendarSources(Source))
			{
				for (unsigned i=0; i<Source.Length(); i++)
				{
				    CalendarSource *s = Source[i];
					if (!s->Match(Addr->Addr))
					{
						Source.DeleteAt(i--, true);
						DeleteObj(s);
					}
				}
			}
		}

		for (unsigned i=0; i<Source.Length(); i++)
		{
			Status |= Source[i]->GetEvents(Start, End, e);
		}
	}

	return Status;
}
