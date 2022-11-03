#include "Store3Mail2.h"
#include "Calendar.h"

/*
	int CalType; // FIELD_CAL_TYPE
	int Completed; // FIELD_CAL_COMPLETED
	int ReminderTime; // FIELD_CAL_REMINDER_TIME
	int ReminderAction; // FIELD_CAL_REMINDER_ACTION
	int ShowTimeAs; // FIELD_CAL_SHOW_TIME_AS
	int Recur; // FIELD_CAL_RECUR
	int RecurFreq; // FIELD_CAL_RECUR_FREQ
	int RecurInterval; // FIELD_CAL_RECUR_INTERVAL
	int RecurCount; // FIELD_CAL_RECUR_END_COUNT
	int RecurEndType; // FIELD_CAL_RECUR_END_TYPE
	int FilterDays; // FIELD_CAL_RECUR_FILTER_DAYS
	int FilterMonths; // FIELD_CAL_RECUR_FILTER_MONTHS

	char *Subject; // FIELD_CAL_SUBJECT
	char *Location; // FIELD_CAL_LOCATION
	char *ReminderArg; // FIELD_CAL_REMINDER_ARG
	char *RecurPos; // FIELD_CAL_RECUR_FILTER_POS
	char *FilterYears; // FIELD_CAL_RECUR_FILTER_YEARS
	char *Notes; // FIELD_CAL_NOTES
	char *Uid; // FIELD_UID

	LDateTime Start; // FIELD_CAL_START_UTC
	LDateTime  End; // FIELD_CAL_END_UTC
	LDateTime RecurEnd; // FIELD_CAL_RECUR_END_DATE
*/

#define AllCalendarFields() \
	_Macro(Subject, FIELD_CAL_SUBJECT) \
	_Macro(CalType, FIELD_CAL_TYPE) \
	_Macro(Completed, FIELD_CAL_COMPLETED) \
	_Macro(ReminderTime, FIELD_CAL_REMINDER_TIME) \
	_Macro(ReminderAction, FIELD_CAL_REMINDER_ACTION) \
	_Macro(ShowTimeAs, FIELD_CAL_SHOW_TIME_AS) \
	_Macro(Recur, FIELD_CAL_RECUR) \
	_Macro(RecurFreq, FIELD_CAL_RECUR_FREQ) \
	_Macro(RecurInterval, FIELD_CAL_RECUR_INTERVAL) \
	_Macro(RecurCount, FIELD_CAL_RECUR_END_COUNT) \
	_Macro(RecurEndType, FIELD_CAL_RECUR_END_TYPE) \
	_Macro(FilterDays, FIELD_CAL_RECUR_FILTER_DAYS) \
	_Macro(FilterMonths, FIELD_CAL_RECUR_FILTER_MONTHS) \
	_Macro(Location, FIELD_CAL_LOCATION) \
	_Macro(ReminderArg, FIELD_CAL_REMINDER_ARG) \
	_Macro(RecurPos, FIELD_CAL_RECUR_FILTER_POS) \
	_Macro(FilterYears, FIELD_CAL_RECUR_FILTER_YEARS) \
	_Macro(Notes, FIELD_CAL_NOTES) \
	_Macro(Uid, FIELD_UID) \
	_Macro(Start, FIELD_CAL_START_UTC) \
	_Macro(End, FIELD_CAL_END_UTC) \
	_Macro(RecurEnd, FIELD_CAL_RECUR_END_DATE)

//////////////////////////////////////////////////////////////////////
CalendarData::CalendarData(LMail2Store *s) : ThingData(s)
{
	CalType = CAL_EVENT;
	Completed = false;
	Subject = 0;
	Location = 0;
	ReminderTime = 0;
	ReminderAction = 0;
	ReminderArg = 0;
	ShowTimeAs = 0;
	Recur = 0;
	RecurFreq = 0;
	RecurInterval = 0;
	RecurCount = 0;
	RecurEndType = 0;
	RecurPos = 0;
	FilterDays = 0;
	FilterMonths = 0;
	FilterYears = 0;
	Notes = 0;
	TimeZone = 0;
	Uid = 0;
}

CalendarData::~CalendarData()
{
	DeleteArray(TimeZone);
	DeleteArray(Subject);
	DeleteArray(Location);
	DeleteArray(Uid);
	DeleteArray(ReminderArg);
	DeleteArray(RecurPos);
	DeleteArray(FilterYears);
	DeleteArray(Notes);
}

LDataI &CalendarData::operator =(LDataI &p)
{
	SetStr(FIELD_CAL_SUBJECT, p.GetStr(FIELD_CAL_SUBJECT));
	SetStr(FIELD_CAL_LOCATION, p.GetStr(FIELD_CAL_LOCATION));
	SetStr(FIELD_CAL_REMINDER_ARG, p.GetStr(FIELD_CAL_REMINDER_ARG));
	SetStr(FIELD_CAL_RECUR_FILTER_POS, p.GetStr(FIELD_CAL_RECUR_FILTER_POS));
	SetStr(FIELD_CAL_RECUR_FILTER_YEARS, p.GetStr(FIELD_CAL_RECUR_FILTER_YEARS));
	SetStr(FIELD_CAL_NOTES, p.GetStr(FIELD_CAL_NOTES));
	SetStr(FIELD_CAL_TIMEZONE, p.GetStr(FIELD_CAL_TIMEZONE));
	SetStr(FIELD_UID, p.GetStr(FIELD_UID));

	SetInt(FIELD_CAL_RECUR_FILTER_MONTHS, p.GetInt(FIELD_CAL_RECUR_FILTER_MONTHS));
	SetInt(FIELD_CAL_RECUR_FILTER_DAYS, p.GetInt(FIELD_CAL_RECUR_FILTER_DAYS));
	SetInt(FIELD_CAL_TYPE, p.GetInt(FIELD_CAL_TYPE));
	SetInt(FIELD_CAL_COMPLETED, p.GetInt(FIELD_CAL_COMPLETED));
	SetInt(FIELD_CAL_REMINDER_TIME, p.GetInt(FIELD_CAL_REMINDER_TIME));
	SetInt(FIELD_CAL_REMINDER_ACTION, p.GetInt(FIELD_CAL_REMINDER_ACTION));
	SetInt(FIELD_CAL_SHOW_TIME_AS, p.GetInt(FIELD_CAL_SHOW_TIME_AS));
	SetInt(FIELD_CAL_RECUR, p.GetInt(FIELD_CAL_RECUR));
	SetInt(FIELD_CAL_RECUR_FREQ, p.GetInt(FIELD_CAL_RECUR_FREQ));
	SetInt(FIELD_CAL_RECUR_INTERVAL, p.GetInt(FIELD_CAL_RECUR_INTERVAL));
	SetInt(FIELD_CAL_RECUR_END_COUNT, p.GetInt(FIELD_CAL_RECUR_END_COUNT));
	SetInt(FIELD_CAL_RECUR_END_TYPE, p.GetInt(FIELD_CAL_RECUR_END_TYPE));

	SetDate(FIELD_CAL_START_UTC, p.GetDate(FIELD_CAL_START_UTC));
	SetDate(FIELD_CAL_END_UTC, p.GetDate(FIELD_CAL_END_UTC));
	SetDate(FIELD_CAL_RECUR_END_DATE, p.GetDate(FIELD_CAL_RECUR_END_DATE));

	return *this;
}

void CalendarData::Load()
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

char *CalendarData::GetStr(int id)
{
	Load();

	switch (id)
	{
		case FIELD_CAL_SUBJECT:
			return Subject;
		case FIELD_CAL_LOCATION:
			return Location;
		case FIELD_CAL_REMINDER_ARG:
			return ReminderArg;
		case FIELD_CAL_RECUR_FILTER_POS:
			return RecurPos;
		case FIELD_CAL_RECUR_FILTER_YEARS:
			return FilterYears;
		case FIELD_CAL_NOTES:
			return Notes;
		case FIELD_CAL_TIMEZONE:
			return TimeZone;
		case FIELD_UID:
			return Uid;
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

bool CalendarData::SetStr(int id, const char *str)
{
	Load();

	switch (id)
	{
		case FIELD_CAL_TIMEZONE:
			_Str(TimeZone);
		case FIELD_CAL_SUBJECT:
			_Str(Subject);
		case FIELD_CAL_LOCATION:
			_Str(Location);
		case FIELD_CAL_REMINDER_ARG:
			_Str(ReminderArg);
		case FIELD_CAL_RECUR_FILTER_POS:
			_Str(RecurPos);
		case FIELD_CAL_RECUR_FILTER_YEARS:
			_Str(FilterYears);
		case FIELD_CAL_NOTES:
			_Str(Notes);
		case FIELD_UID:
			_Str(Uid);
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

int64 CalendarData::GetInt(int id)
{
	Load();

	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
		case FIELD_CAL_RECUR_FILTER_MONTHS:
			return FilterMonths;
		case FIELD_CAL_RECUR_FILTER_DAYS:
			return FilterDays;
		case FIELD_CAL_TYPE:
			return CalType;
		case FIELD_CAL_COMPLETED:
			return Completed;
		case FIELD_CAL_REMINDER_TIME:
			return ReminderTime;
		case FIELD_CAL_REMINDER_ACTION:
			return ReminderAction;
		case FIELD_CAL_SHOW_TIME_AS:
			return ShowTimeAs;
		case FIELD_CAL_RECUR:
			return Recur;
		case FIELD_CAL_RECUR_FREQ:
			return RecurFreq;
		case FIELD_CAL_RECUR_INTERVAL:
			return RecurInterval;
		case FIELD_CAL_RECUR_END_COUNT:
			return RecurCount;
		case FIELD_CAL_RECUR_END_TYPE:
			return RecurEndType;
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return -1;
}

bool CalendarData::SetInt(int id, int64 i)
{
	Load();

	switch (id)
	{
		case FIELD_CAL_RECUR_FILTER_MONTHS:
			FilterMonths = (int)i;
			return true;
		case FIELD_CAL_RECUR_FILTER_DAYS:
			FilterDays = (int)i;
			return true;
		case FIELD_CAL_TYPE:
			CalType = (int)i;
			return true;
		case FIELD_CAL_COMPLETED:
			Completed = (int)i;
			return true;
		case FIELD_CAL_REMINDER_TIME:
			ReminderTime = (int)i;
			return true;
		case FIELD_CAL_REMINDER_ACTION:
			ReminderAction = (int)i;
			return true;
		case FIELD_CAL_SHOW_TIME_AS:
			ShowTimeAs = (int)i;
			return true;
		case FIELD_CAL_RECUR:
			Recur = (int)i;
			return true;
		case FIELD_CAL_RECUR_FREQ:
			RecurFreq = (int)i;
			return true;
		case FIELD_CAL_RECUR_INTERVAL:
			RecurInterval = (int)i;
			return true;
		case FIELD_CAL_RECUR_END_COUNT:
			RecurCount = (int)i;
			return true;
		case FIELD_CAL_RECUR_END_TYPE:
			RecurEndType = (int)i;
			return true;
		case FIELD_UID:
		{
			char s[64];
			sprintf_s(s, sizeof(s), LGI_PrintfInt64, i);
			return SetStr(FIELD_UID, s);
		}
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

LDateTime *CalendarData::GetDate(int id)
{
	Load();

	switch (id)
	{
		case FIELD_CAL_START_UTC:
			return Start.Year() ? &Start : 0;
		case FIELD_CAL_END_UTC:
			return End.Year() ? &End : 0;
		case FIELD_CAL_RECUR_END_DATE:
			return RecurEnd.Year() ? &RecurEnd : 0;
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

bool CalendarData::SetDate(int id, LDateTime *t)
{
	if (!t)
		return false;

	Load();

	switch (id)
	{
		case FIELD_CAL_START_UTC:
			Start = *t;
			return true;
		case FIELD_CAL_END_UTC:
			End = *t;
			return true;
		case FIELD_CAL_RECUR_END_DATE:
			RecurEnd = *t;
			return true;
	}

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

GDataIt CalendarData::GetList(int id)
{
	Load();

	LgiTrace("%s:%i - Unknown id %i\n", _FL, id);
	LAssert(0);
	return 0;
}

int CalendarData::Type()
{
	return MAGIC_CALENDAR;
}

int CalendarData::Sizeof()
{
	int Size =	sizeof(uint32) +	// magic
				sizeof(uint32);		// number of fields

	#define _Macro(var, id) { 	int s = ThingData::Sizeof(var); Size += s; \
								/*LgiTrace("sizeof(%i)=%i\n", id, s);*/ }
	AllCalendarFields();
	#undef _Macro

	return Size;
}

bool CalendarData::Serialize(LFile &f, bool Write)
{
	bool Status = true;
	uint32 Magic = Type();

	if (Write)
	{
		f << Magic;
		uint32 Fields = 22;
		f << Fields;

		#define _Macro(var, id) { int w = ThingData::Write(f, id, var); \
								  /*LgiTrace("write(%i)=%i\n", id, w);*/ }
		AllCalendarFields();
		#undef _Macro
	}
	else
	{
		uint32 n;
		f >> n;
		if (n == Magic)
		{
			f >> n;
			for (uint32 i=0; i<n; i++)
			{
				int16 Id;
				int8 Type;
				
				f >> Id;
				f >> Type;

				switch (Type)
				{
					case OBJ_STRING:
					{
						uint32 Size;
						f >> Size;
						char *Buf = new char[Size+1];
						if (Buf)
						{
							f.Read(Buf, Size);
							Buf[Size] = 0;
							SetStr(Id, Buf);
							DeleteArray(Buf);
						}
						break;
					}
					case OBJ_BINARY:
					{
						uint32 Size;
						f >> Size;
						if (Size == sizeof(Mail2Date))
						{
							Mail2Date d;
							
							f >> d.Day;
							f >> d.Month;
							f >> d.Year;
							f >> d.Hour;
							f >> d.Minute;
							f >> d.ThouSec;

							LDateTime dt;
							dt.Day(d.Day);
							dt.Month(d.Month);
							dt.Year(d.Year);
							dt.Hours(d.Hour);
							dt.Minutes(d.Minute);
							dt.Seconds(d.ThouSec / 1000);
							dt.Thousands(d.ThouSec % 1000);

							SetDate(Id, &dt);
						}
						break;
					}
					case OBJ_INT:
					{
						int k;
						f >> k;
						SetInt(Id, k);
						break;
					}
				}
			}
		}
		else LAssert(0);
	}

	return Status;
}

