/*hdr
**      FILE:           Calendar.cpp
**      AUTHOR:         Matthew Allen
**      DATE:           23/11/2001
**      DESCRIPTION:    Scribe calender support
**
**      Copyright (C) 2001 Matthew Allen
**              fret@memecode.com
*/

#include "Scribe.h"

#include "lgi/common/vCard-vCal.h"
#include "lgi/common/Combo.h"
#include "lgi/common/DateTimeCtrls.h"
#include "lgi/common/TabView.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/Edit.h"
#include "lgi/common/ColourSelect.h"
#include "lgi/common/Button.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/Json.h"

#include "CalendarView.h"
#include "PrintContext.h"
#include "../Resources/resdefs.h"
#include "resource.h"
#include "AddressSelect.h"
#include "ObjectInspector.h"

#define MAX_RECUR			1024
#define DEBUG_REMINDER		0
#define DEBUG_DATES			0

//////////////////////////////////////////////////////////////////////////////
ItemFieldDef CalendarFields[] = {
	{"Start", SdStart, 					GV_DATETIME,	FIELD_CAL_START_UTC,		IDC_START_DATE,			0, true},
	{"End", SdEnd, 						GV_DATETIME,	FIELD_CAL_END_UTC,			IDC_END_DATE,			0, true},
	{"Subject", SdSubject, 				GV_STRING,		FIELD_CAL_SUBJECT,			IDC_SUBJECT},
	{"Location", SdLocation, 			GV_STRING,		FIELD_CAL_LOCATION,			IDC_LOCATION},
	{"Show Time As", SdShowTimeAs,		GV_INT32,		FIELD_CAL_SHOW_TIME_AS,		IDC_AVAILABLE_TYPE},
	{"All Day", SdAllDay,				GV_BOOL,		FIELD_CAL_ALL_DAY,			IDC_ALL_DAY},

	// TRUE if the calendar event recurs
	{"Recur", SdRecur, 					GV_INT32,		FIELD_CAL_RECUR,			-1},
	
	// Base time unit of recurring event. See enum CalRecurFreq: days, weeks, months, years.
	{"Recur Freq", SdRecurFreq, 		GV_INT32,		FIELD_CAL_RECUR_FREQ,		-1},

	// Number of FIELD_CAL_RECUR_FREQ units of time between recurring events. (Minimum is '1')
	{"Recur Interval", SdRecurInterval, GV_INT32,		FIELD_CAL_RECUR_INTERVAL,	-1},

	// Bitfield of days, Bit 0 = Sunday, Bit 1 = Monday, Bit 2 = Teusday etc.
	{"Filter Days", SdFilterDays, 		GV_INT32,		FIELD_CAL_RECUR_FILTER_DAYS, -1},

	// Bitfield of months, Bit 0 = Janurary, Bit 1 = feburary, Bit 2 = March etc.
	{"Filter Months", SdFilterMonths,	GV_INT32,		FIELD_CAL_RECUR_FILTER_MONTHS, -1},

	// String of year numbers separated by commas: "2010,2014,2018"
	{"Filter Years", SdFilterYears,		GV_STRING,		FIELD_CAL_RECUR_FILTER_YEARS, -1},
	
	// Position in month, "1" means the first matching day of the month. Multiple indexes can be joined with ','
	// like: "1,3"
	{"Filter Pos", SdFilterPos, 		GV_STRING,		FIELD_CAL_RECUR_FILTER_POS, -1},

	// See the CalRecurEndType enumeration
	{"Recur End Type", SdRecurEndType,	GV_INT32,		FIELD_CAL_RECUR_END_TYPE,	-1},
	
	// If FIELD_CAL_RECUR_END_TYPE==CalEndOnDate then this specifies the date to end
	{"Recur End Date", SdRecurEndDate,	GV_DATETIME,	FIELD_CAL_RECUR_END_DATE,	-1},
	
	// If FIELD_CAL_RECUR_END_TYPE==CalEndOnCount then this specifies the count of events
	{"Recur End Count", SdRecurEndCount, GV_INT32,		FIELD_CAL_RECUR_END_COUNT,	-1},

	// The timezone the start and end are referencing
	{"Timezone", SdTimeZone, 			GV_STRING,		FIELD_CAL_TIMEZONE,			IDC_TIMEZONE},

	// User defined notes for the object
	{"Notes", SdNotes, 					GV_STRING,		FIELD_CAL_NOTES,			IDC_DESCRIPTION},

	// See the CalendarType enumeration
	{"Type", SdType, 					GV_INT32,		FIELD_CAL_TYPE,				-1},
	
	{"Reminders", SdReminders,			GV_STRING,		FIELD_CAL_REMINDERS,		-1},
	{"LastCheck", SdLastCheck,			GV_DATETIME,	FIELD_CAL_LAST_CHECK,		-1},

	{0}
};

//////////////////////////////////////////////////////////////////////////////
const char *sReminderType[] = { "Email", "Popup", "ScriptCallback" };
const char *sReminderUnits[] = { "Minutes", "Hours", "Days", "Weeks" };

class ReminderItem : public LListItem
{
	CalendarReminderType Type;
	float Value;
	CalendarReminderUnits Units;
	LString Param;

public:
	ReminderItem(CalendarReminderType type, float value, CalendarReminderUnits units, LString param)
	{
		Type = type;
		Value = value;
		Units = units;
		Param = param;
		
		Update();
		SetText("x", 1);
	}
	
	ReminderItem(LString s)
	{
		if (!SetString(s))
		{
			// Default
			Type = CalPopup;
			Value = 1.0;
			Units = CalMinutes;
			Param.Empty();
		}
		
		Update();
		SetText("x", 1);
	}
	
	CalendarReminderType GetType() { return Type; }
	float GetValue() { return Value; }
	CalendarReminderUnits GetUnits() { return Units; }
	LString GetParam() { return Param; }
	
	LString GetString()
	{
		// See also FIELD_CAL_REMINDERS
		LString s;
		s.Printf("%g,%i,%i,%s", Value, Units, Type, Param?LUrlEncode(Param, ",\r\n").Get():"");
		return s;
	}
	
	bool SetString(LString s)
	{
		// See also FIELD_CAL_REMINDERS
		LString::Array a = s.Split(",");
		if (a.Length() < 3)
			return false;
		
		Value = (float) a[0].Float();
		Units = (CalendarReminderUnits) a[1].Int();
		Type = (CalendarReminderType) a[2].Int();
		if (a.Length() > 3)
			Param = LUrlDecode(a[3]);
		
		return true;		
	}
	
	void Update()
	{
		char s[300];
		if (Param)
			sprintf_s(s, sizeof(s), "%s '%s' @ %g %s", sReminderType[Type], Param.Get(), Value, sReminderUnits[Units]);
		else
			sprintf_s(s, sizeof(s), "%s @ %g %s", sReminderType[Type], Value, sReminderUnits[Units]);
		SetText(s);
	}
	
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
	{
		LColour old = Ctx.Fore;
		if (i == 1)
			Ctx.Fore.Rgb(255, 0, 0);
		LListItem::OnPaintColumn(Ctx, i, c);
		Ctx.Fore = old;
	}
	
	void OnMouseClick(LMouse &m)
	{
		if (m.Down() && m.Left())
		{
			if (GetList()->ColumnAtX(m.x) == 1)
			{
				delete this;
				return;
			}
		}
		LListItem::OnMouseClick(m);
	}
};

////////////////////////////////////////////////////////////////////////////////////
static int DefaultCalenderFields[] = 
{
	FIELD_CAL_SUBJECT,
	FIELD_CAL_START_UTC,
	FIELD_CAL_END_UTC,
	0,
};
	
#define MINUTE		1 // we're in minutes...
#define HOUR		(60 * MINUTE)
#define DAY			(24 * HOUR)

int ReminderOffsets[] =
{
	0,
	15	* MINUTE,
	30	* MINUTE,
	1	* HOUR,
	2	* HOUR,
	3	* HOUR,
	4	* HOUR,
	5	* HOUR,
	6	* HOUR,
	7	* HOUR,
	8	* HOUR,
	9	* HOUR,
	10	* HOUR,
	11	* HOUR,
	12	* HOUR,
	1	* DAY,
	2	* DAY
};

//////////////////////////////////////////////////////////////////////////////
List<Calendar> Calendar::Reminders;

int Calendar::DayStart = -1;
int Calendar::DayEnd = -1;
int Calendar::WorkDayStart = -1;
int Calendar::WorkDayEnd = -1;
int Calendar::WorkWeekStart = -1;
int Calendar::WorkWeekEnd = -1;

void InitCalendarView()
{
	Calendar::DayStart = 6;
	Calendar::DayEnd = 23;

	Calendar::WorkDayStart = -1;
	Calendar::WorkDayEnd = -1;
	Calendar::WorkWeekStart = -1;
	Calendar::WorkWeekEnd = -1;

	auto s = LAppInst->GetConfig("Scribe.Calendar.WorkDayStart");
	if (s) Calendar::WorkDayStart = (int)s.Int();

	s = LAppInst->GetConfig("Scribe.Calendar.WorkDayEnd");
	if (s) Calendar::WorkDayEnd = (int)s.Int();

	s = LAppInst->GetConfig("Scribe.Calendar.WorkWeekStart");
	if (s) Calendar::WorkWeekStart = (int)s.Int() + 1;
	
	s = LAppInst->GetConfig("Scribe.Calendar.WorkWeekEnd");
	if (s) Calendar::WorkWeekEnd = (int)s.Int() + 1;

	if (Calendar::WorkDayStart < 0)
		Calendar::WorkDayStart = 9;
	if (Calendar::WorkDayEnd < 0)
		Calendar::WorkDayEnd = 18;
	if (Calendar::WorkWeekStart < 0)
		Calendar::WorkWeekStart = 1;
	if (Calendar::WorkWeekEnd < 0)
		Calendar::WorkWeekEnd = 5;
}

void Calendar::CheckReminders()
{
	LDateTime Now;
	Now.SetNow();
	LDateTime Then;
	Then = Now;
	Then.AddDays(1);

	#if DEBUG_REMINDER
	LgiTrace("%s:%i - Reminders.Len=%i, Now=%s, Then=%s\n", _FL, Reminders.Length(), Now.Get().Get(), Then.Get().Get());
	#endif
	for (auto c: Reminders)
	{
		auto App = c->App;
		LHashTbl<StrKey<char>,bool> Added;
		Thing *ReminderThing = NULL;
		Mail *ReminderEmail = NULL;

		// Is a reminder on the entry?
		if (!c->GetObject())
			continue;
		
		LString Rem = c->GetObject()->GetStr(FIELD_CAL_REMINDERS);
		if (!Rem)
			continue;
	
		const char *Subj = NULL, *Notes = NULL;
		c->GetField(FIELD_CAL_SUBJECT, Subj);
		c->GetField(FIELD_CAL_NOTES, Notes);

		LArray<TimePeriod> Times;
		if (!c->GetTimes(Now, Then, Times))
		{
			#if DEBUG_REMINDER
			LgiTrace("    No times for '%s'\n", Subj);
			#endif
			continue;
		}
		
		LDataI *Obj = c->GetObject();
		LDateTime LastCheck = *Obj->GetDate(FIELD_CAL_LAST_CHECK);
		if (LastCheck.IsValid())
			LastCheck.ToLocal();

		LString::Array r = Rem.SplitDelimit("\n");
		for (unsigned i=0; i<r.Length(); i++)
		{
			ReminderItem ri(r[i]);
			for (unsigned n=0; n<Times.Length(); n++)
			{
				TimePeriod &t = Times[n];
				LDateTime ts = t.s;
				
				#if DEBUG_REMINDER
				LgiTrace("    ts init = %s\n", ts.Get().Get());
				#endif
				
				switch (ri.GetUnits())
				{
					case CalMinutes:
						ts.AddMinutes((int64) -ri.GetValue());
						break;
					case CalHours:
						ts.AddHours((int64) -ri.GetValue());
						break;
					case CalDays:
						ts.AddDays((int64) -ri.GetValue());
						break;
					case CalWeeks:
						ts.AddDays((int64) -(ri.GetValue()*7));
						break;
					default:
						ts.Empty();
						break;
				}

				#if DEBUG_REMINDER
				LgiTrace("    ts after = %s\n", ts.Get().Get());
				#endif
				
				if (ts.IsValid())
				{
					#if DEBUG_REMINDER
					bool a = !LastCheck.IsValid()
							||
							ts > LastCheck;
					bool b = ts <= Now;

					LgiTrace("    last check = %s\n", LastCheck.Get().Get());
					LgiTrace("    now = %s\n", Now.Get().Get());
					LgiTrace("    %i %i\n", a, b);
					#endif

					if
					(
						(
							!LastCheck.IsValid()
							||
							ts > LastCheck
						)
						&&
						ts <= Now
					)
					{
						// Save the last check field now
						auto NowUtc = Now.Utc();
						Obj->SetDate(FIELD_CAL_LAST_CHECK, &NowUtc);
						c->SetDirty();

						// Fire the event
						switch (ri.GetType())
						{
							case CalEmail:
							{
								ScribeAccount *Acc = App->GetCurrentAccount();
								if (!Acc || !Acc->Identity.IsValid())
								{
									LView *Ui = c->GetUI();
									LgiMsg(Ui ? Ui : c->App, "No from account to use for sending event notifications.", AppName, MB_OK);
									break;
								}

								auto Email = Acc->Identity.Email();
								auto Name = Acc->Identity.Name();
								LgiTrace("%s:%i - Using account ('%s' '%s') for the event reminder 'from' address.\n", _FL, Email.Str(), Name.Str());

								if (!ReminderThing)
									ReminderThing = App->CreateThingOfType(MAGIC_MAIL);
								
								if (!ReminderEmail)
								{
									ReminderEmail = ReminderThing ? ReminderThing->IsMail() : NULL;
									if (ReminderEmail)
									{
										LString s;
										s.Printf("Calendar Notification: %s", Subj);
										ReminderEmail->SetSubject(s);

										s.Printf("The event '%s' is due at: %s\n"
												"\n"
												"%s\n"
												"\n"
												"(This email was generated by Scribe)",
												Subj,
												t.s.Get().Get(),
												Notes ? Notes : "");
										ReminderEmail->SetBody(s);

										auto Frm = ReminderEmail->GetFrom();
										Frm->SetStr(FIELD_EMAIL, Email.Str());
										Frm->SetStr(FIELD_NAME, Name.Str());
									}
								}

								if (ReminderEmail)
								{
									auto To = ReminderEmail->GetTo();

									// Add any custom parameter email address:
									LString Param = ri.GetParam();
									if (LIsValidEmail(Param))
									{
										auto Recip = To->Create(c->GetObject()->GetStore());
										if (Recip)
										{
											Added.Add(Param, true);
											Recip->SetStr(FIELD_EMAIL, Param);
											To->Insert(Recip);
										}
									}

									// Add all the guests
									LString sGuests = c->GetObject()->GetStr(FIELD_TO);
									LString::Array Guests = sGuests.SplitDelimit(",");
									for (auto Guest: Guests)
									{
										LAutoString e, n;
										DecodeAddrName(Guest, n, e, NULL);

										#if DEBUG_REMINDER
										LgiTrace("Attendee=%s,%s\n", n.Get(), e.Get());
										#endif

										if (LIsValidEmail(e.Get()) &&
											!Added.Find(e))
										{
											auto Recip = To->Create(c->GetObject()->GetStore());
											if (Recip)
											{
												if (n) Recip->SetStr(FIELD_NAME, n);
												Recip->SetStr(FIELD_EMAIL, e);
												To->Insert(Recip);
												Added.Add(e, true);
											}
										}
										else
										{
											#if DEBUG_REMINDER
											LgiTrace("Attendee not valid or added already: %s\n", e.Get());
											#endif
										}
									}
								}
								break;
							}
							case CalPopup:
							{
								if (LgiMsg(	0, // this causes the dialog to be ontop of everything else
											LLoadString(IDS_EVENT_DUE),
											AppName,
											MB_YESNO | MB_SYSTEMMODAL,
											Subj) == IDYES)
								{
									// Open the calendar entry
									c->DoUI();
								}
								break;
							}
							case CalScriptCallback:
							{
								break;
							}
							default:
							{
								LgiTrace("%s:%i - Unknown reminder type.\n", _FL);
								break;
							}
						}
					}
				}
			}
		}

		if (ReminderEmail)
		{
			ReminderEmail->CreateMailHeaders();
			ReminderEmail->Update();
			ReminderEmail->Send(true);
		}
	}
}

#define MIN_1		((int64)LDateTime::Second64Bit * 60)
#define HOUR_1		(MIN_1 * 60)
#define DAY_1		(HOUR_1 * 24)
#define YEAR_1		(DAY_1 * 365)

const char *RelativeTime(LDateTime &Then)
{
	static char s[256];
	static const int Id[] =
	{
		IDS_CAL_LDAY_SUN,
		IDS_CAL_LDAY_MON,
		IDS_CAL_LDAY_TUE,
		IDS_CAL_LDAY_WED,
		IDS_CAL_LDAY_THU,
		IDS_CAL_LDAY_FRI,
		IDS_CAL_LDAY_SAT,
	};
	
	LDateTime Now;
	Now.SetNow();

	char Val[64];
	uint64 n, t;
	Now.Get(n);
	Then.Get(t);
	int64_t Diff = (int64)t - (int64)n;

	int Yrs = 0;
	int Months = 0;
	int Days = 0;
	int Hrs = 0;
	int Mins = 0;
	LDateTime i = Now;
	int Inc = Then > Now ? 1 : -1;
	char DirIndcator = Then > Now ? '+' : '-';
	while (ABS(Diff) >= YEAR_1)
	{
		Yrs++;
		i.Year(i.Year()+Inc);
		i.Get(n);
		Diff = (int64)t - (int64)n;
	}

	int TotalDays = 0;
	if (ABS(Diff) > DAY_1)
	{
		TotalDays = Days = (int) (Diff / DAY_1);

		while (true)
		{
			LDateTime first = i;
			first.AddMonths(Inc);
			if (	
				(Inc < 0 && first > Then) // Tracking back in time..
				||
				(Inc > 0 && Then > first) // Forward in time..
			)
			{
				Months += Inc;
				i = first;
			}
			else break;
		}

		if (Months)
		{
			uint64 remaining;
			i.Get(remaining);
			Diff = (int64_t)t - (int64_t)remaining;
			Days = (int) (Diff / DAY_1);
		}

		Diff -= (int64) Days * DAY_1;
	}
	if (ABS(Diff) > HOUR_1)
	{
		Hrs = (int) (Diff / HOUR_1);
		Diff -= (int64) Hrs * HOUR_1;
	}
	if (ABS(Diff) > MIN_1)
	{
		Mins = (int) (Diff / MIN_1);
		Diff -= (int64) Mins * MIN_1;
	}
		
	if (Yrs)
	{
		// Years + months
		sprintf_s(Val, sizeof(Val), "%c%iy %im", DirIndcator, abs(Yrs), abs(Months));
	}
	else if (Months)
	{
		// Months + days
		sprintf_s(Val, sizeof(Val), "%c%im %id", DirIndcator, abs(Months), abs(Days));
	}
	else if (Days)
	{
		if (abs(Days) >= 7)
		{
			// Weeks + days...
			sprintf_s(Val, sizeof(Val), "%c%iw %id", DirIndcator, abs(Days)/7, abs(Days)%7);
		}
		else
		{
			// Days + hours...
			sprintf_s(Val, sizeof(Val), "%c%id %ih", DirIndcator, abs(Days), abs(Hrs));
		}
	}
	else if (Hrs)
	{
		// Hours + min
		sprintf_s(Val, sizeof(Val), "%c%ih %im", DirIndcator, abs(Hrs), abs(Mins));
	}
	else
	{
		// Mins
		sprintf_s(Val, sizeof(Val), "%c%im", DirIndcator, abs(Mins));
	}

	if (Yrs != 0 || Months != 0)
	{
		sprintf_s(s, sizeof(s), "%s", Val);
		return s;
	}

	auto NowDay = n / DAY_1;
	auto ThenDay = t / DAY_1;
	auto DaysDiff = (int64_t)ThenDay - (int64_t)NowDay;

	int Ch = 0;
	if (NowDay == ThenDay)
		Ch = sprintf_s(s, sizeof(s), "%s", LLoadString(IDS_TODAY));
	else if (DaysDiff == -1)
		Ch = sprintf_s(s, sizeof(s), "%s", LLoadString(IDS_YESTERDAY));
	else if (DaysDiff == 1)
		Ch = sprintf_s(s, sizeof(s), "%s", LLoadString(IDS_TOMORROW));
	else if (DaysDiff > 1 && DaysDiff < 7)
		Ch = sprintf_s(s, sizeof(s), LLoadString(IDS_THIS_WEEK), LLoadString(Id[Then.DayOfWeek()]));
	else if (DaysDiff >= 7 && DaysDiff < 14)
		Ch = sprintf_s(s, sizeof(s), LLoadString(IDS_NEXT_WEEK), LLoadString(Id[Then.DayOfWeek()]));
	else
	{
		sprintf_s(s, sizeof(s), "%s", Val);
		return s;
	}
	
	sprintf_s(s+Ch, sizeof(s)-Ch, ", %s", Val);	
	return s;	
}

int CalSorter(TimePeriod *a, TimePeriod *b)
{
	return a->s.Compare(&b->s);
}

bool Calendar::SummaryOfToday(ScribeWnd *App, LVariant &v)
{
	bool Status = false;

    LArray<CalendarSource*> Sources;
	if (App && App->GetCalendarSources(Sources))
	{
		LDateTime Now;
		Now.SetNow();
		LDateTime Next = Now;
		Next.AddMonths(1);

		LArray<TimePeriod> e;
		for (unsigned i=0; i<Sources.Length(); i++)
		{
			Sources[i]->GetEvents(Now, Next, e);
		}
		
		if (e.Length())
		{
			e.Sort(CalSorter);

			LStringPipe p;

			p.Print("<table cellspacing=3 style='background:white;'>\n");
			for (unsigned n=0; n<e.Length() && n < 3; n++)
			{
				TimePeriod &tp = e[n];
				Calendar *c = tp.c;

				char Fore[32] = "black";
				char Back[32] = "#c0c0c0";
				const char *Subject = "...";

				c->GetField(FIELD_CAL_SUBJECT, Subject);

				LColour Base32 = c->GetColour();
				auto Edge = Base32.Mix(LColour(L_WORKSPACE), 0.85f);
				sprintf_s(Back, sizeof(Back), "#%2.2x%2.2x%2.2x", Edge.r(), Edge.g(), Edge.b());
				sprintf_s(Fore, sizeof(Fore), "#%2.2x%2.2x%2.2x", Base32.r(), Base32.g(), Base32.b());

				p.Print("\t<tr><td style='padding:2px;color:%s;background:%s;border:1px solid %s;'>\n"
						"\t\t<a style='color:%s;font-size:13px;' href='thing://%p'>%s</a>\n",
						Fore,
						Back,
						Fore,
						Fore,
						(Thing*)c,
						Subject);

				char Str[256];
				const char *Rel = RelativeTime(tp.s);
				if (Rel)
				{
					p.Print("\t\t<br/>%s\n", Rel);
				}

				tp.s.Get(Str, sizeof(Str));
				p.Print("\t\t<br/>%s\n", Str);

				tp.e.Get(Str, sizeof(Str));
				p.Print("\t\t-<br/>%s\n", Str);
			}
			p.Print("</table>\n");

			char *Str = p.NewStr();
			if (Str)
			{
				v = Str;
				DeleteArray(Str);
				Status = true;
			}
		}
		else
		{
			char s[256];
			sprintf_s(s, sizeof(s), "<font color='#808080'>%s</font>", LLoadString(IDS_NO_EVENTS));
			v = s;
			Status = true;
		}
	}

	return Status;
}

void Calendar::OnSerialize(bool Write)
{
	// Update reminder list..
	Reminders.Delete(this);

	const char *Rem = NULL;
	if (GetField(FIELD_CAL_REMINDERS, Rem) && ValidStr(Rem))
	{
		Reminders.Insert(this);
	}

	SetImage(GetCalType() == CalTodo ? ICON_TODO : ICON_CALENDAR);

	if (Write && TodoView)
	{
		TodoView->Update();
		TodoView->Resort();
	}
}

//////////////////////////////////////////////////////////////////////////////
Calendar::Calendar(ScribeWnd *app, LDataI *object) : Thing(app, object)
{
	DefaultObject(object);
	Ui = 0;
	TodoView = 0;
	Source = 0;
	SetImage(ICON_CALENDAR);
}

Calendar::~Calendar()
{
	DeleteObj(TodoView);
	Reminders.Delete(this);
}

bool Calendar::GetTimes(LDateTime StartLocal, LDateTime EndLocal, LArray<TimePeriod> &Times)
{
	if (Calendar::DayStart < 0)
		InitCalendarView();

	ssize_t StartLen = Times.Length();
	LDateTime StartUtc = StartLocal;
	LDateTime EndUtc = EndLocal;
	StartUtc.ToUtc();
	EndUtc.ToUtc();
	// int StartTz = StartLocal.GetTimeZone();

	TimePeriod w;
	w.s = StartUtc;
	w.e = EndUtc;

	const char *Subj = NULL;
	GetField(FIELD_CAL_SUBJECT, Subj);

	TimePeriod BaseUtc;
	LArray<TimePeriod> Periods;
	if (GetField(FIELD_CAL_START_UTC, BaseUtc.s))
	{
		if (!GetField(FIELD_CAL_END_UTC, BaseUtc.e))
		{
			BaseUtc.e = BaseUtc.s;
			BaseUtc.e.AddHours(1);
		}

		LArray<LDateTime::GDstInfo> Dst;
		LDateTime::GetDaylightSavingsInfo(Dst, BaseUtc.s, &EndUtc);
		LAssert(Dst.Length() > 0);

		Periods.Add(BaseUtc);

		LDateTime BaseS = BaseUtc.s;
		LDateTime::DstToLocal(Dst, BaseS);
		auto BaseTz = BaseS.GetTimeZone();

		// Process recur rules
		int Recur = 0;
		if (GetField(FIELD_CAL_RECUR, Recur) &&
			Recur)
		{
			LDateTime Diff = BaseUtc.e - BaseUtc.s;

			int FilterFreq = -1;
			int FilterInterval = 0;
			int FilterDay = 0;
			int FilterMonth = 0;
			const char *FilterYear = 0;
			const char *FilterPos = 0;
			int EndType = 0;
			LDateTime EndDate;
			int EndCount = 0;
			
			GetField(FIELD_CAL_RECUR_FREQ, FilterFreq);
			GetField(FIELD_CAL_RECUR_INTERVAL, FilterInterval);
			GetField(FIELD_CAL_RECUR_FILTER_DAYS, FilterDay);
			GetField(FIELD_CAL_RECUR_FILTER_MONTHS, FilterMonth);
			GetField(FIELD_CAL_RECUR_FILTER_YEARS, FilterYear);
			GetField(FIELD_CAL_RECUR_FILTER_POS, FilterPos);
			GetField(FIELD_CAL_RECUR_END_TYPE, EndType);
			GetField(FIELD_CAL_RECUR_END_DATE, EndDate);
			GetField(FIELD_CAL_RECUR_END_COUNT, EndCount);

			LDateTime CurUtc = BaseUtc.s;
			const char *Error = 0;
			int Count = 0;
			while (!Error)
			{
				// Advance the current date by interval * freq
				switch (FilterFreq)
				{
					case CalFreqDays:
						CurUtc.AddDays(FilterInterval);
						break;
					case CalFreqWeeks:
						CurUtc.AddDays(FilterInterval * 7);
						break;
					case CalFreqMonths:
						CurUtc.AddMonths(FilterInterval);
						break;
					case CalFreqYears:
						CurUtc.Year(CurUtc.Year() + FilterInterval);
						break;
					default:
						Error = "Invalid freq.";
						break;
				}
				
				if (Error || CurUtc > EndUtc) break;
				
				// Check against end conditions
				bool IsEnded = CurUtc > EndUtc;

				switch (EndType)
				{
					case CalEndNever:
						break;
					case CalEndOnCount: // count
						IsEnded = Count >= EndCount - 1;
						break;
					case CalEndOnDate: // date
						IsEnded = CurUtc > EndDate;
						break;
					default: // error
						IsEnded = true;
						break;
				}

				if (IsEnded) break;

				// Check against filters
				LDateTime CurLocal = CurUtc;
				LDateTime::DstToLocal(Dst, CurLocal);
				
				// This fixes the current time when it's in a different daylight saves zone.
				// Otherwise you get events one hour or whatever out of position after DST starts
				// or ends during the recurring set.
				int DiffMins = BaseTz - CurLocal.GetTimeZone();
				CurLocal.AddMinutes(DiffMins);

				bool Show = true;
				if (FilterDay)
				{
					int Day = CurLocal.DayOfWeek();
					for (int i=0; i<7; i++)
					{
						int Bit = 1 << i;
						if (Day == i &&
							(FilterDay & Bit) == 0)
						{
							Show = false;
							break;
						}
					}
				}

				if (Show && FilterMonth)
				{
					for (int i=0; i<12; i++)
					{
						int Bit = 1 << i;
						if ((CurLocal.Month() == i + 1) &&
							(FilterMonth & Bit) == 0)
						{
							Show = false;
							break;
						}
					}
				}

				if (Show && ValidStr(FilterYear))
				{
					LToken t(FilterYear, " ,;:");
					Show = false;
					for (unsigned i=0; i<t.Length(); i++)
					{
						if (atoi(t[i]) == CurLocal.Year())
						{
							Show = true;
							break;
						}
					}
				}

				if (Show && ValidStr(FilterPos))
				{
					LDateTime Sm = CurLocal;
					Sm.Day(1);
					int Off = Sm.DayOfWeek();
					int Idx = (CurLocal.Day() + Off) / 7;

					LToken t(FilterPos, " ,;:");
					Show = false;
					for (unsigned i=0; i<t.Length(); i++)
					{
						if (atoi(t[i]) == Idx)
						{
							Show = true;
							break;
						}
					}
				}

				// Add to the periods to output
				if (Show)
				{
					if (Periods.Length() >= MAX_RECUR)
						break;

					TimePeriod &p = Periods.New();
					p.s = CurLocal;
					p.e = CurLocal;
					p.e.AddHours(Diff.Hours());
					p.e.AddMinutes(Diff.Minutes());
				}

				Count++;
			}
		}

		// Now process periods into multiday segments if needed
		for (unsigned k=0; k<Periods.Length(); k++)
		{
			TimePeriod &n = Periods[k];
			
			if (!n.s.IsSameDay(n.e))
			{
				for (LDateTime i = n.s; true; i.AddDays(1))
				{
					if (i.IsSameDay(n.s))
					{
						// Start day
						TimePeriod t;
						t.s = n.s;
						t.e = n.s;
						int h = t.e.Hours();
						if (h > Calendar::WorkDayEnd)
						{
							t.e.Hours(23);
							t.e.Minutes(59);
							t.e.Seconds(59);
						}
						else
						{
							t.e.Hours(Calendar::WorkDayEnd);
						}

						if (t.Overlap(w))
						{
							t.c = this;
							Times.Add(t);
						}
					}
					else if (i.IsSameDay(n.e))
					{
						// End day
						TimePeriod t;
						t.s = n.e;
						t.e = n.e;
						int h = t.s.Hours();
						if (h < Calendar::WorkDayStart)
						{
							t.s.Hours(0);
							t.s.Minutes(0);
							t.s.Seconds(0);
						}
						else
						{
							t.s.Hours(Calendar::WorkDayStart);
						}

						if (t.Overlap(w))
						{
							t.c = this;
							Times.Add(t);
						}
						break;
					}
					else
					{
						// Middle day
						TimePeriod t;
						t.s = i;
						t.s.Hours(Calendar::WorkDayStart);
						t.s.Minutes(0);
						t.s.Seconds(0);
						t.e = i;
						t.e.Hours(Calendar::WorkDayEnd);
						t.e.Minutes(0);
						t.e.Seconds(0);

						if (t.Overlap(w))
						{
							t.c = this;
							Times.Add(t);
						}
					}
				}
			}
			else if (n.Overlap(w))
			{
				n.c = this;
				Times.Add(n);
			}
		}
	}

	return (int)Times.Length() > StartLen;
}

CalendarType Calendar::GetCalType()
{
	CalendarType Type = CalEvent;
	GetField(FIELD_CAL_TYPE, (int&)Type);
	return Type;
}

void Calendar::SetCalType(CalendarType Type)
{
	SetField(FIELD_CAL_TYPE, (int)Type);
	SetImage(Type == CalTodo ? ICON_TODO : ICON_CALENDAR);
}

LColour Calendar::GetColour()
{
	if (GetObject())
	{
		int64 c = GetObject()->GetInt(FIELD_COLOUR);
		if (c >= 0)
		{
			return LColour((uint32_t)c, 32);
		}
	}

	if (Source)
		return Source->GetColour();

	return LColour(L_LOW);
}

void Calendar::OnPaintView(LSurface *pDC, LFont *Font, LRect *Pos, TimePeriod *Period)
{
	LRect p = *Pos;
	const char *Title = "...";
	LDateTime Now;

	float Sx = 1.0;
	float Sy = 1.0;
	if (pDC->IsPrint())
	{
		auto DcDpi = pDC->GetDpi();
		auto SrcDpi = LScreenDpi();
		Sx = (float)DcDpi.x / SrcDpi.x;
		Sy = (float)DcDpi.y / SrcDpi.y;
	}
	float Scale = Sx < Sy ? Sx : Sy;

	Now.SetNow();
	GetField(FIELD_CAL_SUBJECT, Title);
	bool Delayed = GetObject() ? GetObject()->GetInt(FIELD_STATUS) == Store3Delayed : false;

	LColour Grey(192, 192, 192);
	auto View = GetView();
	if (View && View->Selection.HasItem(this))
	{
		// selected
		LColour f = L_FOCUS_SEL_FORE;
		LColour b = L_FOCUS_SEL_BACK;
		if (Delayed)
		{
			f = f.Mix(Grey);
			b = b.Mix(Grey);
		}

		pDC->Colour(f);
		pDC->Box(&p);
		p.Inset(1, 1);
		pDC->Colour(b);
		Font->Colour(f, b);
	}
	else
	{
		LColour Text(0x80, 0x80, 0x80);
		LColour Base = GetColour();
		LColour Qtr = GdcMixColour(Base, LColour(L_WORKSPACE), 0.1f);
		LColour Half = GdcMixColour(Base, LColour(L_WORKSPACE), 0.4f);

		// not selected
		LColour f, b;

		if (Period && Now < Period->s)
		{
			// future entry (full colour)
			f = Base;
			b = Half;
		}
		else
		{
			// historical entry (half strength colour)
			f = Half;
			b = Qtr;
		}

		if (Delayed)
		{
			f = f.Mix(Grey);
			b = b.Mix(Grey);
		}

		pDC->Colour(f);
		pDC->Box(&p);
		p.Inset(1, 1);
		pDC->Colour(b);
		Font->Colour(Text, b);
	}

	pDC->Rectangle(&p);
	p.Inset((int)SX(1), (int)SY(1));

	Font->Transparent(false);
	LDisplayString ds(Font, Title);
	float Ht = p.Y() > 0 ? (float)ds.Y() / p.Y() : 1.0f;
	if (Ht < 0.75f)
		p.Inset((int)SX(3), (int)SY(3));
	else if (Ht < 0.95f)
		p.Inset((int)SX(1), (int)SY(1));
	ds.Draw(pDC, p.x1, p.y1, &p);

	ViewPos.Union(Pos);
}

Thing &Calendar::operator =(Thing &Obj)
{
	if (Obj.GetObject() &&
		GetObject())
	{
		GetObject()->CopyProps(*Obj.GetObject());
	}

	return *this;
}

bool Calendar::operator ==(Thing &t)
{
	Calendar *c = t.IsCalendar();
	if (!c)
		return false;

	{
		LDateTime a, b;
		if (GetField(FIELD_CAL_START_UTC, a) && c->GetField(FIELD_CAL_START_UTC, b) && a != b)
			return false;

		if (GetField(FIELD_CAL_END_UTC, a) && c->GetField(FIELD_CAL_END_UTC, b) && a != b)
			return false;
	}

	{
		const char *a, *b;
		if (GetField(FIELD_CAL_SUBJECT, a) && c->GetField(FIELD_CAL_SUBJECT, b) && Stricmp(a, b))
			return false;

		if (GetField(FIELD_CAL_LOCATION, a) && c->GetField(FIELD_CAL_LOCATION, b) && Stricmp(a, b))
			return false;
	}

	return true;
}

int Calendar::Compare(LListItem *Arg, ssize_t FieldId)
{
	Calendar *c1 = this;
	Calendar *c2 = dynamic_cast<Calendar*>(Arg);
	if (c1 && c2)
	{
		switch (FieldId)
		{
			case FIELD_CAL_START_UTC:
			case FIELD_CAL_END_UTC:
			{
				LDateTime d1, d2;
				if (!c1->GetField((int)FieldId, d1))
					d1.SetNow();

				if (!c2->GetField((int)FieldId, d2))
					d2.SetNow();

				return d1.Compare(&d2);
				break;
			}
			case FIELD_CAL_SUBJECT:
			case FIELD_CAL_LOCATION:
			{
				const char *s1 = "", *s2 = "";
				if (c1->GetField((int)FieldId, s1) &&
					c2->GetField((int)FieldId, s2))
				{
					return _stricmp(s1, s2);
				}
				break;
			}
		}
	}

	return 0;
}

uint32_t Calendar::GetFlags()
{
	return 0;
}

ThingUi *Calendar::DoUI(MailContainer *c)
{
	if (!Ui)
	{
		Ui = new CalendarUi(this);
	}
	return Ui;
}

ThingUi *Calendar::GetUI()
{
	return Ui;
}

bool Calendar::SetUI(ThingUi *ui)
{
	if (Ui)
		Ui->Item = NULL;
	Ui = dynamic_cast<CalendarUi*>(ui);
	if (Ui)
		Ui->Item = this;
	return true;
}

void Calendar::OnMouseClick(LMouse &m)
{
	if (m.Down())
	{
		if (m.Left())
		{
			if (m.Double())
			{
				DoUI();
			}
		}
		else if (m.Right())
		{
			auto View = GetView();
			DoContextMenu(m, View ? (LView*)View : (LView*)App);
		}
	}
}

void Calendar::OnCreate()
{
	for (auto v: CalendarView::CalendarViews)
		v->OnContentsChanged();
}

bool Calendar::OnDelete()
{
	bool IsTodo = GetCalType() == CalTodo;
	bool Status = Thing::OnDelete();
	
	if (IsTodo)
		DeleteObj(TodoView);

	for (auto v: CalendarView::CalendarViews)
		v->OnContentsChanged();
	
	return Status;
}

bool Calendar::GetParentSelection(LList *Lst, List<LListItem> &s)
{
	if (Lst)
	{
		return Lst->GetSelection(s);
	}

	if (auto View = GetView())
	{
		LArray<Calendar*> &a = View->GetSelection();
		for (unsigned i=0; i<a.Length(); i++)
		{
			s.Insert(dynamic_cast<LListItem*>(a[i]));
		}
		return true;
	}

	return false;
}

#define IDM_MOVE_TO 4000

void Calendar::DoContextMenu(LMouse &m, LView *Parent)
{
    LSubMenu Sub;
	LScriptUi s(&Sub);
	if (s.Sub)
	{
		s.Sub->AppendItem(LLoadString(IDS_OPEN), IDM_OPEN);
		s.Sub->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE);
		s.Sub->AppendItem(LLoadString(IDS_EXPORT), IDM_EXPORT);
		s.Sub->AppendSeparator();
		s.Sub->AppendItem(LLoadString(IDS_INSPECT), IDM_INSPECT);

		CalendarView *Cv = dynamic_cast<CalendarView*>(Parent);
		if (Cv)
		{
			s.Sub->AppendSeparator();

			for (unsigned n = 0; n<CalendarSource::GetSources().Length(); n++)
			{
				char m[256];
				CalendarSource *Cs = CalendarSource::GetSources().ItemAt(n);
				sprintf_s(m, sizeof(m), "Move to '%s'\n", Cs->GetName());
				s.Sub->AppendItem(m, IDM_MOVE_TO + n++, Source != Cs);
			}
		}

		LArray<LScriptCallback*> Callbacks;
		if (App->GetScriptCallbacks(LThingContextMenu, Callbacks))
		{
			LScriptArguments Args(NULL);

			Args[0] = new LVariant(App);
			Args[1] = new LVariant(this);
			Args[2] = new LVariant(&s);

			for (unsigned i=0; i<Callbacks.Length(); i++)
				App->ExecuteScriptCallback(*Callbacks[i], Args);

			Args.DeleteObjects();
		}

		m.ToScreen();
		int Result = s.Sub->Float(App, m.x, m.y);
		switch (Result)
		{
			default:
			{
				if (Cv)
				{
					int Idx = Result - IDM_MOVE_TO;
					if (Idx >= 0 && Idx < (int)CalendarSource::GetSources().Length())
					{
						CalendarSource *Dst = CalendarSource::GetSources().ItemAt(Idx);
						if (Dst)
						{
							FolderCalendarSource *Fsrc = dynamic_cast<FolderCalendarSource*>(Dst);
							if (Fsrc)
							{
								const char *Path = Fsrc->GetPath();
								ScribeFolder *DstFolder = App->GetFolder(Path);
								if (DstFolder)
								{
									LArray<Thing*> Items;
									Items.Add(this);
									DstFolder->MoveTo(Items);
									if (Items[0] != (Thing*)this)
										return;

									Source = Dst;
									Cv->Invalidate();
								}
							}
							else LAssert(!"Impl me.");
						}
					}
				}

				// Handle any installed callbacks for menu items
				for (unsigned i=0; i<s.Callbacks.Length(); i++)
				{
					LScriptCallback &Cb = s.Callbacks[i];
					if (Cb.Param == Result)
					{
						LScriptArguments Args(NULL);
						Args[0] = new LVariant(App);
						Args[1] = new LVariant(this);
						Args[2] = new LVariant(Cb.Param);
						
						App->ExecuteScriptCallback(Cb, Args);
					}
				}
				break;
			}
			case IDM_OPEN:
			{
				DoUI();
				break;
			}
			case IDM_DELETE:
			{
				LVariant ConfirmDelete;
				App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

				if (!ConfirmDelete.CastInt32() ||
					LgiMsg(Parent, LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
				{
					List<LListItem> Del;

					LList *PList = dynamic_cast<LList*>(Parent);
					if (GetParentSelection(PList ? PList : GetList(), Del))
					{
						for (auto i: Del)
						{
							Thing *t = dynamic_cast<Thing*>(i);
							if (t)
							{
								t->OnDelete();
							}
							else
							{
								CalendarTodoItem *Todo = dynamic_cast<CalendarTodoItem*>(i);
								if (Todo)
								{
									Calendar *c = Todo->GetTodo();
									c->OnDelete();
								}
							}
						}
					}
					else
					{
						Thing *t = this;
						t->OnDelete();
					}
				}
				break;
			}
			case IDM_EXPORT:
			{
				ExportAll(GetList(), sMimeVCalendar, NULL);
				break;
			}
			case IDM_INSPECT:
			{
				new ObjectInspector(App, this);
				break;
			}
		}
	}
}

const char *Calendar::GetText(int i)
{
	static char s[64];
	
	if (!GetObject())
	{
		LAssert(!"No storage object");
		return 0;
	}
	
	int Field = 0;
	if (FieldArray.Length())
	{
		if (i >= 0 && i < (int) FieldArray.Length())
			Field = FieldArray[i];
	}
	else if (i >= 0 && i < CountOf(DefaultCalenderFields))
	{
		Field = DefaultCalenderFields[i];
	}

	if (!Field)
		return 0;

	ItemFieldDef *Def = GetFieldDefById(Field);
	if (!Def)
	{
		LAssert(!"Where is the field def?");
		return 0;
	}

	switch (Def->Type)
	{
		case GV_STRING:
		{
			return GetObject()->GetStr(Field);
			break;
		}
		case GV_INT32:
		{
			sprintf_s(s, sizeof(s), LPrintfInt64, GetObject()->GetInt(Field));
			return s;
			break;
		}
		case GV_DATETIME:
		{
			// Get any effective timezone for this event.
			LString Tz = GetObject()->GetStr(FIELD_CAL_TIMEZONE);

			auto dt = GetObject()->GetDate(Field);
			if (dt && dt->IsValid())
			{
				LDateTime tmp = *dt;
				
				#if DEBUG_DATES
				printf("GetText.UTC %i = %s\n", i, tmp.Get().Get());
				#endif
				
				bool UseLocal = true;
				tmp.SetTimeZone(0, false);

				if (Tz.Get())
				{
					bool HasPt = false, HasDigit = false;
					char *e = Tz.Get();
					while (strchr(" \t\r\n-.+", *e) || IsDigit(*e))
					{
						if (*e == '.') HasPt = true;
						if (IsDigit(*e)) HasDigit = true;
						e++;
					}
					if (HasDigit)
					{
						if (HasPt)
						{
							double TzHrs = Tz.Float();
							double i, f = modf(TzHrs, &i);
							int Mins = (int) ((i * 60) + (f * 60));
							tmp.AddMinutes(Mins);
						}
						else
						{
							int64 i = Tz.Int();
							int a = (int)ABS(i);
							int Mins = (int) (((a / 100) * 60) + (a % 100));
							tmp.AddMinutes(i < 0 ? -Mins : Mins);
						}
						UseLocal = false;
					}
				}

				if (UseLocal)
					tmp.ToLocal();

				#if DEBUG_DATES
				printf("GetText.Local %i = %s\n", i, tmp.Get().Get());
				#endif

				tmp.Get(s, sizeof(s));
				return s;
			}
			break;
		}
		default:
		{
			LAssert(0);
			break;
		}
	}

	return 0;
}

int *Calendar::GetDefaultFields()
{
	return DefaultCalenderFields;
}

const char *Calendar::GetFieldText(int Field)
{
	return 0;
}

bool Calendar::Overlap(Calendar *c)
{
	if (c)
	{
		LDateTime Ts, Te, Cs, Ce;
		if (GetField(FIELD_CAL_START_UTC, Ts) &&
			c->GetField(FIELD_CAL_START_UTC, Cs))
		{
			if (!GetField(FIELD_CAL_END_UTC, Te))
			{
				Te = Ts;
				Te.AddHours(1);
			}

			if (!c->GetField(FIELD_CAL_END_UTC, Ce))
			{
				Ce = Cs;
				Ce.AddHours(1);
			}
			
			if ((Ce <= Ts) || (Cs >= Te))
			{
				return false;
			}
			
			return true;			
		}
	}
	
	return false;
}

bool Calendar::Save(ScribeFolder *Folder)
{
	bool Status = false;

	// Check the dates are the right way around
	LDateTime Start, End;
	if (GetField(FIELD_CAL_START_UTC, Start) &&
		GetField(FIELD_CAL_END_UTC, End))
	{
		if (End < Start)
		{
			SetField(FIELD_CAL_START_UTC, End);
			SetField(FIELD_CAL_END_UTC, Start);
		}
	}

	auto ChangeEvent = [this](bool Status)
	{
		auto View = GetView();
		if (View && Status)
		{
			View->OnContentsChanged(Source);
			OnSerialize(true);
		}
	};

	if (GetObject() &&
		GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Webdav)
	{
		auto ParentObj = Folder ? Folder->GetObject() : NULL;
		Store3Status s = GetObject()->Save(ParentObj);
		Status = s > Store3Error;
		if (Status)
			SetDirty(false);

		ChangeEvent(Status);
	}
	else
	{
		if (!Folder)
		{
			Folder = GetFolder();
		}
		if (!Folder && App)
		{
			Folder = App->GetFolder(FOLDER_CALENDAR);
		}

		// FIXME: This can't wait for WriteThing to finish it's call back...
		Status = true;
		if (Folder)
		{
			Folder->WriteThing(this, [this, ChangeEvent](auto Status)
			{
				if (Status > Store3Error)
					SetDirty(false);
				ChangeEvent(Status);
			});
		}
		else ChangeEvent(Status);
	}

	return Status;
}

// Import/Export
bool Calendar::GetFormats(bool Export, LString::Array &MimeTypes)
{
	MimeTypes.Add(sMimeVCalendar);
	return MimeTypes.Length() > 0;
}

Thing::IoProgress Calendar::Import(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeVCalendar) &&
		Stricmp(mimeType, sMimeICalendar))
		IoProgressNotImpl();


	VCal vCal;
	if (!vCal.Import(GetObject(), stream))
		IoProgressError("vCal import failed.");

	IoProgressSuccess();
}

Thing::IoProgress Calendar::Export(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeVCalendar))
		IoProgressNotImpl();

	VCal vCal;
	if (!vCal.Export(GetObject(), stream))
		IoProgressError("vCal export failed.");

	IoProgressSuccess();
}

char *Calendar::GetDropFileName()
{
	if (!DropFileName)
	{
		const char *Name = 0;
		GetField(FIELD_CAL_SUBJECT, Name);
		DropFileName.Reset(MakeFileName(Name ? Name : "Cal", "ics"));
	}
	return DropFileName;
}

bool Calendar::GetDropFiles(LString::Array &Files)
{
	bool Status = false;

	if (GetDropFileName())
	{
		if (!LFileExists(DropFileName))
		{
			LAutoPtr<LFile> F(new LFile);
			if (F->Open(DropFileName, O_WRITE))
			{
				F->SetSize(0);
				Export(AutoCast(F), sMimeVCalendar);
			}
		}

		if (LFileExists(DropFileName))
		{
			Files.Add(DropFileName.Get());
			Status = true;
		}
	}

	return Status;
}

void Calendar::OnPrintHeaders(ScribePrintContext &Context)
{
	LDisplayString *ds = Context.Text(LLoadString(IDS_CAL_EVENT));
	LRect &r = Context.MarginPx;
	int Line = ds->Y();
	LDrawListSurface *Page = Context.Pages.Last();
	Page->Rectangle(r.x1, Context.CurrentY + (Line * 5 / 10), r.x2, Context.CurrentY + (Line * 6 / 10));
	Context.CurrentY += Line;
}

void Calendar::OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages)
{
	// Print document
	for (ItemFieldDef *Fld = CalendarFields; Fld->FieldId; Fld++)
	{
		LString Value;
		switch (Fld->Type)
		{
			case GV_STRING:
			{
				Value = GetObject()->GetStr(Fld->FieldId);
				break;
			}
			case GV_DATETIME:
			{
				auto Dt = GetObject()->GetDate(Fld->FieldId);
				if (Dt)
				{
					char s[64] = "<error>";
					Dt->Get(s, sizeof(s));
					Value = s;					
				}
				break;
			}
			default:
				break;
		}
		
		if (Value)
		{
			LString f;
			const char *Name = LLoadString(Fld->FieldId);
			f.Printf("%s: %s", Name ? Name : Fld->DisplayText, Value.Get());
			// LDisplayString *ds = Context.Text(f);
		}
	}
}

bool Calendar::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		// String variables
		case SdSubject: // Type: String
			Value = GetObject()->GetStr(FIELD_CAL_SUBJECT);
			break;
		case SdTo: // Type: String
			Value = GetObject()->GetStr(FIELD_TO);
			break;
		case SdLocation: // Type: String
			Value = GetObject()->GetStr(FIELD_CAL_LOCATION);
			break;
		case SdUid: // Type: String
			Value = GetObject()->GetStr(FIELD_UID);
			break;
		case SdReminders: // Type: String
			Value = GetObject()->GetStr(FIELD_CAL_REMINDERS);
			break;
		case SdNotes: // Type: String
			Value = GetObject()->GetStr(FIELD_CAL_NOTES);
			break;
		case SdStatus: // Type: String
			Value = GetObject()->GetStr(FIELD_CAL_STATUS);
			break;

		// Int variables
		case SdType: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_TYPE);
			break;
		case SdCompleted: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_COMPLETED);
			break;
		case SdShowTimeAs: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_SHOW_TIME_AS);
			break;
		case SdRecur: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR);
			break;
		case SdRecurFreq: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_FREQ);
			break;
		case SdRecurInterval: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_INTERVAL);
			break;
		case SdRecurEndCount: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_END_COUNT);
			break;
		case SdRecurEndType: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_END_TYPE);
			break;
		case SdRecurFilterDays: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_FILTER_DAYS);
			break;
		case SdRecurFilterMonths: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_RECUR_FILTER_MONTHS);
			break;
		case SdPrivacy: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_PRIVACY);
			break;
		case SdAllDay: // Type: Int32
			Value = GetObject()->GetInt(FIELD_CAL_ALL_DAY);
			break;
		case SdColour: // Type: Int64
			Value = GetObject()->GetInt(FIELD_COLOUR);
			break;

		// Date time fields
		case SdStart: // Type: DateTime
			Value = GetObject()->GetDate(FIELD_CAL_START_UTC);
			break;
		case SdEnd: // Type: DateTime
			Value = GetObject()->GetDate(FIELD_CAL_END_UTC);
			break;

		default:
			return false;
	}

	return true;
}

bool Calendar::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		// String variables
		case SdSubject:
			Value = GetObject()->SetStr(FIELD_CAL_SUBJECT, Value.Str());
			break;
		case SdTo:
			Value = GetObject()->SetStr(FIELD_TO, Value.Str());
			break;
		case SdLocation:
			Value = GetObject()->SetStr(FIELD_CAL_LOCATION, Value.Str());
			break;
		case SdUid:
			Value = GetObject()->SetStr(FIELD_UID, Value.Str());
			break;
		case SdReminders:
			Value = GetObject()->SetStr(FIELD_CAL_REMINDERS, Value.Str());
			break;
		case SdNotes:
			Value = GetObject()->SetStr(FIELD_CAL_NOTES, Value.Str());
			break;
		case SdStatus:
			Value = GetObject()->SetStr(FIELD_CAL_STATUS, Value.Str());
			break;

		// Int variables
		case SdType:
			Value = GetObject()->SetInt(FIELD_CAL_TYPE, Value.CastInt32());
			break;
		case SdCompleted:
			Value = GetObject()->SetInt(FIELD_CAL_COMPLETED, Value.CastInt32());
			break;
		case SdShowTimeAs:
			Value = GetObject()->SetInt(FIELD_CAL_SHOW_TIME_AS, Value.CastInt32());
			break;
		case SdRecur:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR, Value.CastInt32());
			break;
		case SdRecurFreq:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_FREQ, Value.CastInt32());
			break;
		case SdRecurInterval:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_INTERVAL, Value.CastInt32());
			break;
		case SdRecurEndCount:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_END_COUNT, Value.CastInt32());
			break;
		case SdRecurEndType:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_END_TYPE, Value.CastInt32());
			break;
		case SdRecurFilterDays:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_FILTER_DAYS, Value.CastInt32());
			break;
		case SdRecurFilterMonths:
			Value = GetObject()->SetInt(FIELD_CAL_RECUR_FILTER_MONTHS, Value.CastInt32());
			break;
		case SdPrivacy:
			Value = GetObject()->SetInt(FIELD_CAL_PRIVACY, Value.CastInt32());
			break;
		case SdColour:
			Value = GetObject()->SetInt(FIELD_COLOUR, Value.CastInt32());
			break;
		case SdAllDay:
			Value = GetObject()->SetInt(FIELD_CAL_ALL_DAY, Value.CastInt32());
			break;

		// Date time fields
		case SdStart:
			if (Value.Type == GV_DATETIME)
				Value = GetObject()->SetDate(FIELD_CAL_START_UTC, Value.Value.Date);
			else if (Value.Str())
			{
				LDateTime dt;
				dt.Set(Value.Str());
				GetObject()->SetDate(FIELD_CAL_START_UTC, &dt);
			}
			break;
		case SdEnd:
			if (Value.Type == GV_DATETIME)
				Value = GetObject()->SetDate(FIELD_CAL_END_UTC, Value.Value.Date);
			else if (Value.Str())
			{
				LDateTime dt;
				dt.Set(Value.Str());
				GetObject()->SetDate(FIELD_CAL_END_UTC, &dt);
			}
			break;

		default:
			return false;
	}

	return true;
}

bool Calendar::CallMethod(const char *MethodName, LVariant *ReturnValue, LArray<LVariant*> &Args)
{
	// ScribeDomType Fld = StrToDom(MethodName);
	
	return Thing::CallMethod(MethodName, ReturnValue, Args);
}

//////////////////////////////////////////////////////////////////////////////
bool SerializeUi(ItemFieldDef *Defs, LDataI *Object, LViewI *View, bool ToUi)
{
	if (!Object || !Defs || !View)
		return false;

	if (ToUi)
	{
		for (ItemFieldDef *d = Defs; d->FieldId; d++)
		{
			if (d->CtrlId <= 0)
				continue;

			switch (d->Type)
			{
				case GV_STRING:
				{
					auto s = Object->GetStr(d->FieldId);
					View->SetCtrlName(d->CtrlId, s);
					break;
				}
				case GV_INT32:
				{
					int64 i = Object->GetInt(d->FieldId);
					View->SetCtrlValue(d->CtrlId, i);
					break;
				}
				case GV_DATETIME:
				{
					char s[64] = "";
					auto dt = Object->GetDate(d->FieldId);
					if (dt && dt->Year())
						dt->Get(s, sizeof(s));
					View->SetCtrlName(d->CtrlId, s);
					break;
				}
				default:
				{
					LAssert(0);
					break;
				}
			}
		}
	}
	else
	{
		for (ItemFieldDef *d = Defs; d->FieldId; d++)
		{
			if (d->CtrlId <= 0)
				continue;

			switch (d->Type)
			{
				case GV_STRING:
				{
					const char *s = View->GetCtrlName(d->CtrlId);
					Object->SetStr(d->FieldId, s);
					break;
				}
				case GV_INT32:
				{
					int i = (int)View->GetCtrlValue(d->CtrlId);
					Object->SetInt(d->FieldId, i);
					break;
				}
				case GV_DATETIME:
				{
					const char *s = View->GetCtrlName(d->CtrlId);
					LDateTime dt;
					dt.Set(s);
					Object->SetDate(d->FieldId, &dt);
					break;
				}
				default:
				{
					LAssert(0);
					break;
				}
			}
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////
class LEditDropDown : public LEdit
{
public:
	enum DropType
	{
		DropNone,
		DropDate,
		DropTime,
	};

protected:
	DropType Type;
	LAutoPtr<LPopup> Popup;

public:
	LEditDropDown(int id) : LEdit(id, 0, 0, 60, 20, NULL)
	{
		Type = DropNone;
		SetObjectName(Res_Custom);
	}
	
	void SetType(DropType type) { Type = type; }
	const char *GetClass() { return "LEditDropDown"; }
	
	void OnMouseClick(LMouse &m)
	{
		LEdit::OnMouseClick(m);
		
		if (Focus() && Popup && !Popup->Visible())
		{
			Popup->Visible(true);
		}
	}
	
	void OnFocus(bool f)
	{
		if (f)
		{
			if (!Popup)
			{
				if (Type == DropDate)
					Popup.Reset(new LDatePopup(this));
				else if (Type == DropTime)
					Popup.Reset(new LTimePopup(this));
				
				if (Popup)
				{
					Popup->TakeFocus(false);
					
					LPoint p(0, Y());
					PointToScreen(p);
					LRect r = Popup->GetPos();
					r.Offset(p.x - r.x1, p.y - r.y1);
					Popup->SetPos(r);
				}
			}
			
			if (Popup)
				Popup->Visible(true);
		}
	}

	int OnNotify(LViewI *Wnd, LNotification n)
	{
		/*
		LgiTrace("OnNotify %s, %i\n", Wnd->GetClass(), Flags);
		if (Wnd == (LViewI*)Popup)
		{
			GDatePopup *Date;
			if ((Date = dynamic_cast<GDatePopup*>(Popup.Get())))
			{
				LDateTime Ts = Date->Get();
				char s[256];
				Ts.GetDate(s, sizeof(s));
				Name(s);
			}
			else LgiTrace("%s:%i - Incorrect pop up type.\n", _FL);
		}
		*/
		
		return 0;
	}

	void OnChildrenChanged(LViewI *Wnd, bool Attaching)
	{
		if (Wnd == (LViewI*)Popup.Get() && !Attaching)
		{
			// This gets called in the destructor of the Popup, so we should
			// lose the pointer to it.
			Popup.Release();
		}
	}
};

struct LEditDropDownFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (!_stricmp(Class, "LEditDropDown"))
		{
			return new LEditDropDown(-1);
		}
		return NULL;
	}
}	EditDropDownFactory;

//////////////////////////////////////////////////////////////////////////////
class LRecurDlg : public LDialog
{
	CalendarUi *Ui;
	LEditDropDown *EndOnDate;
	LCombo *Repeats;
	bool AcceptNotify;
	
public:
	LRecurDlg(CalendarUi *ui)
	{
		EndOnDate = NULL;
		Repeats = NULL;
		AcceptNotify = true;
		Ui = ui;
		SetParent(ui);		
		if (LoadFromResource(IDD_CAL_RECUR))
		{
			if (GetViewById(IDC_ON_DATE, EndOnDate))
			{
				EndOnDate->SetType(LEditDropDown::DropDate);
				
				LDateTime dt;
				dt.SetNow();
				char s[64];
				dt.GetDate(s, sizeof(s));
				EndOnDate->Name(s);
			}
			
			if (GetViewById(IDC_REPEATS, Repeats))
			{
				Repeats->Insert(LLoadString(IDS_DAY));
				Repeats->Insert(LLoadString(IDS_WEEK));
				Repeats->Insert(LLoadString(IDS_MONTH));
				Repeats->Insert(LLoadString(IDS_YEAR));
				Repeats->Value(1);
			}
			
			Radio(IDC_NEVER);
			SetCtrlValue(IDC_EVERY, 1);
			Serialize(false);
		}
	}
	
	~LRecurDlg()
	{
	}
	
	void Serialize(bool Write)
	{
		int DayCtrls[] = {
			IDC_SUNDAY,
			IDC_MONDAY,
			IDC_TUESDAY,
			IDC_WEDNESDAY,
			IDC_THURSDAY,
			IDC_FRIDAY,
			IDC_SATURDAY
		};
		
		Calendar *c = Ui->GetCal();
		LDataI *o = c->GetObject();
		if (Write)
		{
			// Dlg -> Object
			int64 v = Repeats->Value();
			o->SetInt(FIELD_CAL_RECUR_FREQ, v);
			v = GetCtrlValue(IDC_EVERY);
			o->SetInt(FIELD_CAL_RECUR_INTERVAL, v);
			
			int DayFilter = 0;
			for (int i=0; i<7; i++)
			{
				if (GetCtrlValue(DayCtrls[i]))
					DayFilter |= 1 << i;
			}
			o->SetInt(FIELD_CAL_RECUR_FILTER_DAYS, DayFilter);
			
			if (GetCtrlValue(IDC_NEVER))
			{
				o->SetInt(FIELD_CAL_RECUR_END_TYPE, CalEndNever);
			}
			else if (GetCtrlValue(IDC_AFTER))
			{
				o->SetInt(FIELD_CAL_RECUR_END_TYPE, CalEndOnCount);
				o->SetInt(FIELD_CAL_RECUR_END_COUNT, GetCtrlValue(IDC_AFTER_COUNT));
			}
			else if (GetCtrlValue(IDC_ON))
			{
				o->SetInt(FIELD_CAL_RECUR_END_TYPE, CalEndOnDate);
				
				LDateTime e;
				e.SetDate(GetCtrlName(IDC_ON_DATE));
				e.SetTime("11:59:59");
				o->SetDate(FIELD_CAL_RECUR_END_DATE, &e);
			}
			else LAssert(0);
		}
		else
		{
			// Object -> Dlg
			int64 v = o->GetInt(FIELD_CAL_RECUR_FREQ);
			Repeats->Value(v);
			OnRepeat(v);
			v = o->GetInt(FIELD_CAL_RECUR_INTERVAL);
			SetCtrlValue(IDC_EVERY, MAX(v, 1));

			int DayFilter = (int)o->GetInt(FIELD_CAL_RECUR_FILTER_DAYS);
			for (int i=0; i<7; i++)
			{
				SetCtrlValue(DayCtrls[i], (DayFilter & (1 << i)) ? 1 : 0);
			}

			CalRecurEndType EndType = (CalRecurEndType) o->GetInt(FIELD_CAL_RECUR_END_TYPE);
			if (EndType == CalEndOnCount)
			{
				Radio(IDC_AFTER);
				SetCtrlValue(IDC_AFTER_COUNT, o->GetInt(FIELD_CAL_RECUR_END_COUNT));
			}
			else if (EndType == CalEndOnDate)
			{
				Radio(IDC_ON);
				
				auto e = o->GetDate(FIELD_CAL_RECUR_END_DATE);
				if (e)
					SetCtrlName(IDC_ON_DATE, e->GetDate());
			}
			else // Default to never...
			{
				Radio(IDC_NEVER);
			}			
		}
	}

	void Radio(int Id)
	{
		AcceptNotify = false;
		SetCtrlValue(IDC_NEVER, Id == IDC_NEVER);

		SetCtrlValue(IDC_AFTER, Id == IDC_AFTER);
		SetCtrlEnabled(IDC_AFTER_COUNT, Id == IDC_AFTER);
		SetCtrlEnabled(IDC_OCCURRENCES, Id == IDC_AFTER);

		SetCtrlValue(IDC_ON, Id == IDC_ON);
		SetCtrlEnabled(IDC_ON_DATE, Id == IDC_ON);
		AcceptNotify = true;
	}
	
	void OnRepeat(int64 Val)
	{
		LViewI *v;
		if (!GetViewById(IDC_TIME_TYPE, v))
			return;
		switch (Val)
		{
			case 0:
				v->Name(LLoadString(IDS_DAYS));
				break;
			case 1:
				v->Name(LLoadString(IDS_WEEKS));
				break;
			case 2:
				v->Name(LLoadString(IDS_MONTHS));
				break;
			case 3:
				v->Name(LLoadString(IDS_YEARS));
				break;
		}
		v->SendNotify(LNotifyTableLayoutRefresh);
	}
	
	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		if (!AcceptNotify)
			return 0;
			
		switch (Ctrl->GetId())
		{
			case IDC_NEVER:
			case IDC_AFTER:
			case IDC_ON:
				Radio(Ctrl->GetId());
				break;
			
			case IDC_REPEATS:
				OnRepeat(Ctrl->Value());
				break;
			
			case IDOK:
				Serialize(true);
				// Fall through
			case IDCANCEL:
				EndModal(Ctrl->GetId() == IDOK);
				break;
		}
		
		return 0;
	}
};

///////////////////////////////////////////////////////////////////////////////
struct CalendarUiPriv
{
	CalendarUi *Ui;
	LEditDropDown *StartDate, *StartTime;
	LEditDropDown *EndDate, *EndTime;
	LEdit *Entry;
	AddressBrowse *Browse;
	LList *Guests;
	LList *Reminders;
	LCombo *ReminderType;
	LCombo *ReminderUnit;
	LCombo *CalSelect;
	LColourSelect *Colour;
	
	CalendarUiPriv(CalendarUi *ui)
	{
		Ui = ui;
		StartDate = StartTime = NULL;
		EndDate = EndTime = NULL;
		CalSelect = NULL;
		Entry = NULL;
		Browse = NULL;
		Guests = NULL;
		Colour = NULL;
		Reminders = NULL;
		ReminderType = NULL;
		ReminderUnit = NULL;
	}
	
	void OnGuest()
	{
		const char *g = Ui->GetCtrlName(IDC_GUEST_ENTRY);
		if (!g)
			return;
		
		Mailto mt(Ui->App, g);
		for (auto a: mt.To)
		{
			ListAddr *la = new ListAddr(Ui->App, a);
			if (la)
			{
				Guests->Insert(la);
			}
		}
		
		Ui->SetCtrlName(IDC_GUEST_ENTRY, "");
	}
};

CalendarUi::CalendarUi(Calendar *item) : ThingUi(item, LLoadString(IDS_CAL_EVENT))
{
	NotifyOn = false;
	Item = item;
	d = new CalendarUiPriv(this);
	#if WINNATIVE
	CreateClassW32("Event", LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_EVENT)));
	#endif
	if (!Attach(NULL))
		LAssert(0);
	else
	{
		LoadFromResource(IDD_CAL, this);
		AttachChildren();

		if (!SerializeState(Item->App->GetOptions(), OPT_CalendarEventPos, true))
		{
			LRect p(100, 100, 900, 700);
			SetPos(p);
			MoveToCenter();
		}
		
		if (GetViewById(IDC_START_DATE, d->StartDate))
		{
			d->StartDate->SetType(LEditDropDown::DropDate);
		}
		if (GetViewById(IDC_START_TIME, d->StartTime))
		{
			d->StartTime->SetType(LEditDropDown::DropTime);
		}
		if (GetViewById(IDC_END_DATE, d->EndDate))
		{
			d->EndDate->SetType(LEditDropDown::DropDate);
		}
		if (GetViewById(IDC_END_TIME, d->EndTime))
		{
			d->EndTime->SetType(LEditDropDown::DropTime);
		}
		
		GetViewById(IDC_GUEST_ENTRY, d->Entry);
		if (GetViewById(IDC_GUESTS, d->Guests))
		{
			d->Guests->AddColumn(LLoadString(IDS_ADDRESS), 120);
			d->Guests->AddColumn(LLoadString(IDS_NAME), 120);
			d->Guests->ShowColumnHeader(false);
		}
		if (GetViewById(IDC_REMINDERS, d->Reminders))
		{
			d->Reminders->AddColumn(LLoadString(FIELD_CAL_REMINDER_TIME), 200);
			d->Reminders->AddColumn(LLoadString(IDC_DELETE), 20);
			d->Reminders->ShowColumnHeader(false);
		}
		if (GetViewById(IDC_REMINDER_TYPE, d->ReminderType))
		{
			d->ReminderType->Insert(LLoadString(IDS_EMAIL));
			d->ReminderType->Insert(LLoadString(IDS_POPUP));
			d->ReminderType->Insert("Script");
			d->ReminderType->Value(1);
		}
		SetCtrlValue(IDC_REMINDER_VALUE, 10);
		if (GetViewById(IDC_REMINDER_UNIT, d->ReminderUnit))
		{
			d->ReminderUnit->Insert(LLoadString(IDS_MINUTES));
			d->ReminderUnit->Insert(LLoadString(IDS_HOURS));
			d->ReminderUnit->Insert(LLoadString(IDS_DAYS));
			d->ReminderUnit->Insert(LLoadString(IDS_WEEKS));
			d->ReminderUnit->Value(0);
		}
		SetCtrlValue(IDC_AVAILABLE_TYPE, 1);
		if (GetViewById(IDC_COLOUR, d->Colour))
		{
			LArray<LColour> Colours;
			Colours.Add(LColour(84, 132, 237, 255));
			Colours.Add(LColour(164, 189, 252, 255));
			Colours.Add(LColour(70, 214, 219, 255));
			Colours.Add(LColour(122, 231, 191, 255));
			Colours.Add(LColour(81, 183, 73, 255));
			Colours.Add(LColour(251, 215, 91, 255));
			Colours.Add(LColour(255, 184, 120, 255));
			Colours.Add(LColour(255, 136, 124, 255));
			Colours.Add(LColour(220, 33, 39, 255));
			Colours.Add(LColour(219, 173, 255, 255));
			Colours.Add(LColour(225, 225, 225, 255));
			d->Colour->SetColourList(&Colours);
			d->Colour->Value(0);
		}
		if (GetViewById(IDC_CALENDAR, d->CalSelect))
		{
			LArray<CalendarSource*> Sources;
			if (Item->App->GetCalendarSources(Sources))
			{
				for (unsigned i=0; i<Sources.Length(); i++)
				{
					CalendarSource *cs = Sources[i];
					d->CalSelect->Insert(cs->GetName());					
				}
			}
		}
		
		LView *s;
		if (GetViewById(IDC_SUBJECT, s))
			s->Focus(true);
		LButton *btn;
		if (GetViewById(IDC_SAVE, btn))
			btn->Default(true);
		
		OnLoad();
		
		Visible(true);
		NotifyOn = true;
	}

	LViewI *v;
	if (GetViewById(IDC_REMINDER_TYPE, v))
	{
		LNotification note;
		OnNotify(v, note);
	}

	RegisterHook(this, LKeyEvents);
}

CalendarUi::~CalendarUi()
{
	if (Item)
	{
		if (Item->App)
			SerializeState(Item->App->GetOptions(), OPT_CalendarEventPos, false);
		Item->SetUI(NULL);
	}
}

bool CalendarUi::OnViewKey(LView *v, LKey &k)
{
	if (k.CtrlCmd())
	{
		switch (k.c16)
		{
			case 's':
			case 'S':
			{
				if (k.Down())
					OnSave();
				return true;
			}
			case 'w':
			case 'W':
			{
				if (k.Down())
				{
					OnSave();
					Quit();
				}
				return true;
			}
		}

		if (k.vkey == LK_RETURN)
		{
			if (!k.Down())
			{
				OnSave();
				Quit();
			}

			return true;
		}
	}

	return false;
}

int CalendarUi::OnCommand(int Cmd, int Event, OsView Window)
{
	return 0;
}

void CalendarUi::OnPosChange()
{
	LWindow::OnPosChange();
}

void CalendarUi::CheckConsistancy()
{
	auto St = CurrentStart();
	auto En = CurrentEnd();
	if (En < St)
	{
		En = St;
		En.AddMinutes(30);

		SetCtrlName(IDC_END_DATE, En.GetDate().Get());
		SetCtrlName(IDC_END_TIME, En.GetTime().Get());
	}
}

LDateTime CalendarUi::CurrentStart()
{
	LDateTime dt;
	dt.SetDate(GetCtrlName(IDC_START_DATE));
	dt.SetTime(GetCtrlName(IDC_START_TIME));
	return dt;
}

LDateTime CalendarUi::CurrentEnd()
{
	LDateTime dt;
	dt.SetDate(GetCtrlName(IDC_END_DATE));
	dt.SetTime(GetCtrlName(IDC_END_TIME));
	return dt;
}

void CalendarUi::UpdateStartRelative()
{
	LDateTime dt = CurrentStart();
	if (dt.IsValid())
	{
		const char *s = RelativeTime(dt);
		SetCtrlName(IDC_START_REL, s);
	}
}

void CalendarUi::UpdateEndRelative()
{
	LDateTime dt = CurrentEnd();
	if (dt.IsValid())
	{
		const char *s = RelativeTime(dt);
		SetCtrlName(IDC_END_REL, s);
	}
}

void CalendarUi::UpdateRelative()
{
	CheckConsistancy();
	UpdateStartRelative();
	UpdateEndRelative();
}

int CalendarUi::OnNotify(LViewI *Ctrl, LNotification n)
{
	if (!NotifyOn)
		return false;

	switch (Ctrl->GetId())
	{
		case IDC_START_DATE:
		case IDC_START_TIME:
		{
			CheckConsistancy();
			UpdateStartRelative();
			break;
		}
		case IDC_END_DATE:
		case IDC_END_TIME:
		{
			CheckConsistancy();
			UpdateEndRelative();
			break;
		}
		case IDC_REMINDER_TYPE:
		{
			LViewI *Label, *Param;
			if (GetViewById(IDC_PARAM_LABEL, Label) &&
				GetViewById(IDC_REMINDER_PARAM, Param))
			{
				Param->Name(NULL);
				switch (Ctrl->Value())
				{
					case CalEmail:
						Label->Name("optional email:");
						Param->Enabled(true);
						break;
					default:
					case CalPopup:
						Label->Name(NULL);
						Param->Enabled(false);
						break;
					case CalScriptCallback:
						Label->Name("callback fn:");
						Param->Enabled(true);
						break;
				}
			}
			break;
		}
		case IDC_ALL_DAY:
		{
			int64 AllDay = Ctrl->Value();
			LDataI *o = Item->GetObject();

			char s[256] = "";
			auto dt = o->GetDate(FIELD_CAL_START_UTC);
			if (dt)
			{
				LDateTime tmp = *dt;
				tmp.ToLocal(true);

				tmp.GetTime(s, sizeof(s));
				SetCtrlName(IDC_START_TIME, AllDay ? "" : s);
				SetCtrlEnabled(IDC_START_TIME, !AllDay);
			}

			dt = o->GetDate(FIELD_CAL_END_UTC);
			if (dt)
			{
				LDateTime tmp = *dt;
				tmp.ToLocal(true);

				tmp.GetTime(s, sizeof(s));
				SetCtrlName(IDC_END_TIME, AllDay ? "" : s);
				SetCtrlEnabled(IDC_END_TIME, !AllDay);
			}
			break;
		}
		case IDC_SUBJECT:
		{
			SetCtrlEnabled(IDC_SAVE, ValidStr(Ctrl->Name()));
			break;
		}
		case IDC_SHOW_LOCATION:
		{
			LString Loc = GetCtrlName(IDC_LOCATION);
			if (ValidStr(Loc))
			{
				for (char *c = Loc; *c; c++)
				{
					if (Strchr(WhiteSpace, *c))
						*c = '+';
				}
			
				LString Uri;
				Uri.Printf("http://maps.google.com/?q=%s", Loc.Get());
				LExecute(Uri);
			}
			break;
		}
		case IDC_GUESTS:
		{
			if (n.Type == LNotifyItemInsert)
			{
				d->Guests->ResizeColumnsToContent();
			}
			else if (n.Type == LNotifyDeleteKey)
			{
				List<ListAddr> la;
				d->Guests->GetSelection(la);
				la.DeleteObjects();
			}
			break;
		}
		case IDC_GUEST_ENTRY:
		{
			if (d->Entry)
			{
				if (ValidStr(d->Entry->Name()))
				{
					if (!d->Browse)
					{
						d->Browse = new AddressBrowse(Item->App, d->Entry, d->Guests, NULL);
					}
				}
				if (d->Browse)
				{
					d->Browse->OnNotify(d->Entry, n);
				}
				if (n.Type == LNotifyReturnKey)
				{
					d->OnGuest();
				}
			}
			break;
		}
		case IDC_REPEAT:
		{
			if (!Ctrl->Value())
				break;

			auto Dlg = new LRecurDlg(this);
			Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
			{
				if (ctrlId)
					SetCtrlValue(IDC_REPEAT, 0);
				delete dlg;
			});
			break;
		}
		case IDC_TIMEZONE:
		{
			auto Tz = Item->GetObject()->GetStr(FIELD_CAL_TIMEZONE);
			auto Dlg = new LInput(this, Tz, "Time zone:", "Calendar Event Timezone");
			Dlg->DoModal([this, Dlg](auto dlg, auto Result)
			{
				if (Result)
				{
					Item->GetObject()->SetStr(FIELD_CAL_TIMEZONE, Dlg->GetStr());
					Item->SetDirty();
				}
				delete dlg;
			});
			break;
		}
		case IDC_REMINDER_ADD:
		{
			const char *v = GetCtrlName(IDC_REMINDER_VALUE);
			const char *param = GetCtrlName(IDC_REMINDER_PARAM);
			d->Reminders->Insert
			(
				new ReminderItem
				(
					(CalendarReminderType) d->ReminderType->Value(),
					v ? (float)atof(v) : 1.0f,
					(CalendarReminderUnits) d->ReminderUnit->Value(),
					param
				)
			);
			d->Reminders->ResizeColumnsToContent();
			break;
		}
		case IDC_ADD_GUEST:
		{
			d->OnGuest();
			break;
		}
		case IDC_SAVE:
		{
			LViewI *v;
			if (GetViewById(IDC_GUEST_ENTRY, v) &&
				v->Focus())
			{
				d->OnGuest();
				break;
			}
			else
			{
				OnSave();
				// Fall through
			}
		}
		case IDCANCEL:
		{
			Quit();
			break;
		}
	}
	
	return 0;
}

void CalendarUi::OnLoad()
{
	// Sanity check
	if (!Item) { LAssert(!"No item."); return; }
	LDataI *o = Item->GetObject();
	if (!o) { LAssert(!"No object."); return; }
	
	// Copy object values into UI
	int64 AllDay = false;
	auto CalSubject = o->GetStr(FIELD_CAL_SUBJECT);
	SetCtrlName(IDC_SUBJECT, CalSubject);
	SetCtrlName(IDC_LOCATION, o->GetStr(FIELD_CAL_LOCATION));
	SetCtrlName(IDC_DESCRIPTION, o->GetStr(FIELD_CAL_NOTES));
	SetCtrlValue(IDC_ALL_DAY, AllDay = o->GetInt(FIELD_CAL_ALL_DAY));

	if (d->Guests)
	{
		LJson j(o->GetStr(FIELD_ATTENDEE_JSON));
		for (auto g: j.GetArray(NULL))
		{
			LAutoPtr<ListAddr> la(new ListAddr(App));
			if (la)
			{
				la->sAddr = g.Get("email");
				la->sName = g.Get("name");
				d->Guests->Insert(la.Release());
			}
		}
		d->Guests->ResizeColumnsToContent();
	}
	
	if (d->Reminders)
	{
		d->Reminders->Empty();
		
		LString Rem = o->GetStr(FIELD_CAL_REMINDERS);
		LString::Array a = Rem.SplitDelimit("\n");
		for (unsigned i=0; i<a.Length(); i++)
		{
			ReminderItem *ri = new ReminderItem(CalPopup, 0, CalMinutes, NULL);
			if (ri)
			{
				if (ri->SetString(a[i]))
					d->Reminders->Insert(ri);
				else
					delete ri;
			}
		}

		d->Reminders->ResizeColumnsToContent();
	}

	CalendarShowTimeAs Show = (CalendarShowTimeAs)o->GetInt(FIELD_CAL_SHOW_TIME_AS);
	switch (Show)
	{
		case CalFree:
			SetCtrlValue(IDC_AVAILABLE_TYPE, 0);
			break;
		case CalTentative:
			SetCtrlValue(IDC_AVAILABLE_TYPE, 1);
			break;
		default:
		case CalBusy:
			SetCtrlValue(IDC_AVAILABLE_TYPE, 2);
			break;
	}
	CalendarPrivacyType Priv = (CalendarPrivacyType)o->GetInt(FIELD_CAL_PRIVACY);
	switch (Priv)
	{
		default:
			SetCtrlValue(IDC_PRIVACY_TYPE, 0);
			break;
		case CalPublic:
			SetCtrlValue(IDC_PRIVACY_TYPE, 1);
			break;
		case CalPrivate:
			SetCtrlValue(IDC_PRIVACY_TYPE, 2);
			break;
	}
	int64 Col = o->GetInt(FIELD_COLOUR);
	if (Col >= 0)
		d->Colour->Value(Col);

	for (unsigned i=0; i<CalendarSource::GetSources().Length(); i++)
	{
		CalendarSource *src = CalendarSource::GetSources().ItemAt(i);
		LString s_path = src->GetName();
		LString i_path;
		if (Item->GetFolder())
			i_path = Item->GetFolder()->GetPath();
		
		if (s_path &&
			i_path &&
			_stricmp(s_path, i_path) == 0)
		{
			SetCtrlValue(IDC_CALENDAR, i);
			break;
		}
	}
	
	char s[256] = "";
	auto dt = o->GetDate(FIELD_CAL_START_UTC);
	if (dt)
	{
		#if DEBUG_DATES
		printf("Load.Start.UTC=%s\n", dt->Get().Get());
		#endif
		LDateTime tmp = *dt;
		tmp.SetTimeZone(0, false);
		tmp.ToLocal(true);
		#if DEBUG_DATES
		printf("Load.Start.Local=%s\n", tmp.Get().Get());
		#endif

		tmp.GetDate(s, sizeof(s));
		SetCtrlName(IDC_START_DATE, s);
		
		tmp.GetTime(s, sizeof(s));
		SetCtrlName(IDC_START_TIME, AllDay ? "" : s);
		SetCtrlEnabled(IDC_START_TIME, !AllDay);
	}

	dt = o->GetDate(FIELD_CAL_END_UTC);
	if (dt)
	{
		#if DEBUG_DATES
		printf("Load.End.UTC=%s\n", dt->Get().Get());
		#endif
		LDateTime tmp = *dt;
		tmp.SetTimeZone(0, false);
		tmp.ToLocal(true);
		#if DEBUG_DATES
		printf("Load.End.Local=%s\n", tmp.Get().Get());
		#endif

		tmp.GetDate(s, sizeof(s));
		SetCtrlName(IDC_END_DATE, s);

		tmp.GetTime(s, sizeof(s));
		SetCtrlName(IDC_END_TIME, AllDay ? "" : s);
		SetCtrlEnabled(IDC_END_TIME, !AllDay);
	}

	SetCtrlValue(IDC_REPEAT, o->GetInt(FIELD_CAL_RECUR));

	LViewI *ctrl;
	if (GetViewById(IDC_SUBJECT, ctrl))
	{
		LNotification note(LNotifyValueChanged);
		OnNotify(ctrl, note);
	}
	
	if (ValidStr(CalSubject))
	{
		LString s;
		s.Printf("%s - %s", LLoadString(IDS_CAL_EVENT), CalSubject);
		Name(s);
	}

	UpdateRelative();
}

void CalendarUi::OnSave()
{
	// Sanity check
	if (!Item) { LAssert(!"No item."); return; }
	LDataI *o = Item->GetObject();
	if (!o) { LAssert(!"No object."); return; }
	
	// Save UI values into object
	bool AllDay = false;
	o->SetStr(FIELD_CAL_SUBJECT, GetCtrlName(IDC_SUBJECT));
	o->SetStr(FIELD_CAL_LOCATION, GetCtrlName(IDC_LOCATION));
	o->SetStr(FIELD_CAL_NOTES, GetCtrlName(IDC_DESCRIPTION));
	o->SetInt(FIELD_CAL_ALL_DAY, AllDay = (GetCtrlValue(IDC_ALL_DAY) != 0));
	
	if (d->Guests)
	{
		List<ListAddr> All;
		if (d->Guests->GetAll(All))
		{
			LString::Array a;
			LString Sep(", ");
			for (auto la: All)
			{
				LString e;
				if (la->Serialize(e, true))
					a.Add(e);
			}
			
			LString Guests = Sep.Join(a);
			o->SetStr(FIELD_TO, Guests);
		}
	}
	
	if (d->Reminders)
	{
		List<ReminderItem> All;
		if (d->Reminders->GetAll(All))
		{
			LString::Array a;
			LString Sep("\n");
			for (auto ri: All)
			{
				a.Add(ri->GetString());
			}
			
			LString Reminders = Sep.Join(a);
			o->SetStr(FIELD_CAL_REMINDERS, Reminders);
		}
		else
		{
			o->SetStr(FIELD_CAL_REMINDERS, "");
		}
	}
	
	int64 Show = GetCtrlValue(IDC_AVAILABLE_TYPE);
	switch (Show)
	{
		case 0:
			o->SetInt(FIELD_CAL_SHOW_TIME_AS, CalFree);
			break;
		case 1:
			o->SetInt(FIELD_CAL_SHOW_TIME_AS, CalTentative);
			break;
		default:
		case 2:
			o->SetInt(FIELD_CAL_SHOW_TIME_AS, CalBusy);
			break;
	}

	int64 Priv = GetCtrlValue(IDC_PRIVACY_TYPE);
	switch (Priv)
	{
		default:
		case 0:
			o->SetInt(FIELD_CAL_PRIVACY, CalDefaultPriv);
			break;
		case 1:
			o->SetInt(FIELD_CAL_PRIVACY, CalPublic);
			break;
		case 2:
			o->SetInt(FIELD_CAL_PRIVACY, CalPrivate);
			break;
	}

	auto Col = d->Colour->Value();
	o->SetInt(FIELD_COLOUR, Col > 0 ? Col : -1);

	/* FIXME: change calendar item location if the user selects a different cal
	for (unsigned i=0; i<d->Sources.Length(); i++)
	{
		CalendarSource *src = d->Sources[i];
		LAutoString s_path(src->GetPath());
		LAutoString i_path;
		if (Item->GetFolder())
			i_path = Item->GetFolder()->GetPath();
		
		if (s_path &&
			i_path &&
			_stricmp(s_path, i_path) == 0)
		{
			SetCtrlValue(IDC_CALENDAR, i);
			break;
		}
	}
	*/
	
	LDateTime dt;
	dt.SetDate(GetCtrlName(IDC_START_DATE));
	dt.SetTime(AllDay ? "0:0:0" : GetCtrlName(IDC_START_TIME));
	#if DEBUG_DATES
	printf("Start.Local=%s\n", dt.Get().Get());
	#endif
	dt.ToUtc(true);
	#if DEBUG_DATES
	printf("Start.UTC=%s\n", dt.Get().Get());
	#endif
	o->SetDate(FIELD_CAL_START_UTC, &dt);

	dt.Empty();
	dt.SetDate(GetCtrlName(IDC_END_DATE));
	dt.SetTime(AllDay ? "11:59:59" : GetCtrlName(IDC_END_TIME));
	#if DEBUG_DATES
	printf("End.Local=%s\n", dt.Get().Get());
	#endif
	dt.ToUtc(true);
	#if DEBUG_DATES
	printf("End.UTC=%s\n", dt.Get().Get());
	#endif
	o->SetDate(FIELD_CAL_END_UTC, &dt);

	o->SetInt(FIELD_CAL_RECUR, GetCtrlValue(IDC_REPEAT));

	Item->Update();
	Item->Save();
}

//////////////////////////////////////////////////////////////
int LDateTimeViewBase = 1000;
class LDateTimeView : public LLayout, public ResObject
{
    LEdit *Edit;
    LDateDropDown *Date;
    LTimeDropDown *Time;
    
public:
    LDateTimeView() : ResObject(Res_Custom)
    {
        AddView(Edit = new LEdit(10, 0, 0, 60, 20, NULL));
        AddView(Date = new LDateDropDown());
        AddView(Time = new LTimeDropDown());

        Edit->SetId(LDateTimeViewBase++);

        Date->SetNotify(Edit);
        Date->SetId(LDateTimeViewBase++);

        Time->SetNotify(Edit);
        Time->SetId(LDateTimeViewBase++);
    }
    
    bool OnLayout(LViewLayoutInfo &Inf)
    {
        if (!Inf.Width.Max)
        {
            Inf.Width.Max = 220;
            Inf.Width.Min = 150;
        }
        else if (!Inf.Height.Max)
        {
            Inf.Height.Max = Inf.Height.Min = LSysFont->GetHeight() + 6;
        }
        else return false;
        return true;
    }
    
    void OnCreate()
    {
        AttachChildren();
    }
    
    void OnPosChange()
    {
        LRect c = GetClient();
        // int cy = c.Y();
        // int py = Y();
        
        LRect tc = c;
        tc.x1 = tc.x2 - 20;
        LRect dc = c;
        dc.x2 = tc.x1 - 1;
        dc.x1 = dc.x2 - 20;
        Date->SetPos(dc);
        Time->SetPos(tc);
        c.x2 = dc.x1 - 1;
        Edit->SetPos(c);
    }
    
    const char *Name()
    {
		return Edit->Name();
    }
    
    bool Name(const char *s)
    {
		return Edit->Name(s);
    }
};

class LDateTimeViewFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
	    if (!_stricmp(Class, "LDateTimeView"))
	        return new LDateTimeView;
	    return NULL;
	}

public:
} DateTimeViewFactory;
