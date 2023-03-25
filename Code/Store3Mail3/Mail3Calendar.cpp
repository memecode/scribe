#include "Mail3.h"
#include "lgi/common/Json.h"

LMail3Def TblCalendar[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"},

	{"CalType",			"INTEGER"}, // FIELD_CAL_TYPE
	{"Completed",		"INTEGER"}, // FIELD_CAL_COMPLETED
	{"Start",			"TEXT"},	// FIELD_CAL_START_UTC
	{"End",				"TEXT"},	// FIELD_CAL_END_UTC
	{"TimeZone",		"TEXT"},	// FIELD_CAL_TIMEZONE
	{"Subject",			"TEXT"},	// FIELD_CAL_SUBJECT
	{"Location",		"TEXT"},	// FIELD_CAL_LOCATION
	{"Uid",				"TEXT"},	// FIELD_UID
	{"ShowTimeAs",		"INTEGER"}, // FIELD_CAL_SHOW_TIME_AS
	{"Recur",			"INTEGER"}, // FIELD_CAL_RECUR
	{"RecurFreq",		"INTEGER"}, // FIELD_CAL_RECUR_FREQ
	{"RecurInterval",	"INTEGER"}, // FIELD_CAL_RECUR_INTERVAL
	{"RecurEnd",		"TEXT"},	// FIELD_CAL_RECUR_END_DATE
	{"RecurCount",		"INTEGER"}, // FIELD_CAL_RECUR_END_COUNT
	{"RecurEndType",	"INTEGER"}, // FIELD_CAL_RECUR_END_TYPE
	{"RecurPos",		"TEXT"},	// FIELD_CAL_RECUR_FILTER_POS
	{"FilterDays",		"INTEGER"}, // FIELD_CAL_RECUR_FILTER_DAYS
	{"FilterMonths",	"INTEGER"}, // FIELD_CAL_RECUR_FILTER_MONTHS
	{"FilterYears",		"TEXT"},	// FIELD_CAL_RECUR_FILTER_YEARS
	{"Notes",			"TEXT"},	// FIELD_CAL_NOTES
	{"Colour",			"INTEGER"},	// FIELD_COLOUR
	{"Guests",			"TEXT"},	// FIELD_TO
	{"Reminders",		"TEXT"},	// FIELD_CAL_REMINDERS
	{"LastCheck",		"TEXT"},	// FIELD_CAL_LAST_CHECK
	{"AllDay",			"INTEGER"},	// FIELD_CAL_ALL_DAY
	{"Status",			"STATUS"},	// FIELD_CAL_STATUS
	{"DateModified",	"TEXT"},	// FIELD_DATE_MODIFIED

	{0, 0}
};

LMail3Calendar::LMail3Calendar(LMail3Store *store) : LMail3Thing(store)
{
}

LMail3Calendar::~LMail3Calendar()
{
}

bool LMail3Calendar::DbDelete()
{
	char s[256];

	// Delete the calendar itself
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_CALENDAR " where Id=" LPrintfInt64, Id);
	LMail3Store::LStatement Del(Store, s);
	if (!Del.Exec())
		return false;

	return true;
}

bool LMail3Calendar::Serialize(LMail3Store::LStatement &s, bool Write)
{
	int i = 0;

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);

	SERIALIZE_INT(CalType, i++); // FIELD_CAL_TYPE
	SERIALIZE_INT(Completed, i++); // FIELD_CAL_COMPLETED
	SERIALIZE_DATE(Start, i++); // FIELD_CAL_START_UTC
	SERIALIZE_DATE(End, i++); // FIELD_CAL_END_UTC
	SERIALIZE_LSTR(TimeZone, i++); // FIELD_CAL_TIMEZONE
	SERIALIZE_LSTR(Subject, i++); // FIELD_CAL_SUBJECT
	SERIALIZE_LSTR(Location, i++); // FIELD_CAL_LOCATION
	SERIALIZE_LSTR(Uid, i++); // FIELD_UID
	SERIALIZE_INT(ShowTimeAs, i++); // FIELD_CAL_SHOW_TIME_AS
	SERIALIZE_INT(Recur, i++); // FIELD_CAL_RECUR
	SERIALIZE_INT(RecurFreq, i++); // FIELD_CAL_RECUR_FREQ
	SERIALIZE_INT(RecurInterval, i++); // FIELD_CAL_RECUR_INTERVAL
	SERIALIZE_DATE(RecurEnd, i++); // FIELD_CAL_RECUR_END_DATE
	SERIALIZE_INT(RecurCount, i++); // FIELD_CAL_RECUR_END_COUNT
	SERIALIZE_INT(RecurEndType, i++); // FIELD_CAL_RECUR_END_TYPE
	SERIALIZE_LSTR(RecurPos, i++); // FIELD_CAL_RECUR_FILTER_POS
	SERIALIZE_INT(FilterDays, i++); // FIELD_CAL_RECUR_FILTER_DAYS
	SERIALIZE_INT(FilterMonths, i++); // FIELD_CAL_RECUR_FILTER_MONTHS
	SERIALIZE_LSTR(FilterYears, i++); // FIELD_CAL_RECUR_FILTER_YEARS
	SERIALIZE_LSTR(Notes, i++); // FIELD_CAL_NOTES
	SERIALIZE_COLOUR(Colour, i++); // FIELD_COLOUR
	SERIALIZE_LSTR(To, i++); // FIELD_TO
	SERIALIZE_LSTR(Reminders, i++); // FIELD_CAL_REMINDER_TIME
	SERIALIZE_DATE(LastCheck, i++); // FIELD_CAL_LAST_CHECK
	SERIALIZE_BOOL(AllDay, i++); // FIELD_CAL_ALL_DAY
	SERIALIZE_LSTR(EventStatus, i++); // FIELD_CAL_STATUS

	if (Write)
	{
		StoreStatus = Store3Success;
	}
	else if (To.Get())
	{
		char c = To.Get()[0];
		if (c != '[' && c != '{')
		{
			// Convert old style comma separated field to JSON
			LString s = To;
			To.Empty();
			SetStr(FIELD_TO, s);
		}
	}

	
	return true;
}

Store3CopyImpl(LMail3Calendar)
{
	SetInt(FIELD_CAL_TYPE, p.GetInt(FIELD_CAL_TYPE));
	SetInt(FIELD_CAL_COMPLETED, p.GetInt(FIELD_CAL_COMPLETED));
	SetDate(FIELD_CAL_START_UTC, p.GetDate(FIELD_CAL_START_UTC));
	SetDate(FIELD_CAL_END_UTC, p.GetDate(FIELD_CAL_END_UTC));
	SetStr(FIELD_CAL_TIMEZONE, p.GetStr(FIELD_CAL_TIMEZONE));
	SetStr(FIELD_CAL_SUBJECT, p.GetStr(FIELD_CAL_SUBJECT));
	SetStr(FIELD_CAL_LOCATION, p.GetStr(FIELD_CAL_LOCATION));
	SetStr(FIELD_UID, p.GetStr(FIELD_UID));
	SetStr(FIELD_CAL_REMINDERS, p.GetStr(FIELD_CAL_REMINDERS));
	SetInt(FIELD_CAL_SHOW_TIME_AS, p.GetInt(FIELD_CAL_SHOW_TIME_AS));
	SetInt(FIELD_CAL_RECUR, p.GetInt(FIELD_CAL_RECUR));
	SetInt(FIELD_CAL_RECUR_FREQ, p.GetInt(FIELD_CAL_RECUR_FREQ));
	SetInt(FIELD_CAL_RECUR_INTERVAL, p.GetInt(FIELD_CAL_RECUR_INTERVAL));
	SetDate(FIELD_CAL_RECUR_END_DATE, p.GetDate(FIELD_CAL_RECUR_END_DATE));
	SetInt(FIELD_CAL_RECUR_END_COUNT, p.GetInt(FIELD_CAL_RECUR_END_COUNT));
	SetInt(FIELD_CAL_RECUR_END_TYPE, p.GetInt(FIELD_CAL_RECUR_END_TYPE));
	SetStr(FIELD_CAL_RECUR_FILTER_POS, p.GetStr(FIELD_CAL_RECUR_FILTER_POS));
	SetInt(FIELD_CAL_RECUR_FILTER_DAYS, p.GetInt(FIELD_CAL_RECUR_FILTER_DAYS));
	SetInt(FIELD_CAL_RECUR_FILTER_MONTHS, p.GetInt(FIELD_CAL_RECUR_FILTER_MONTHS));
	SetStr(FIELD_CAL_RECUR_FILTER_YEARS, p.GetStr(FIELD_CAL_RECUR_FILTER_YEARS));
	SetStr(FIELD_CAL_NOTES, p.GetStr(FIELD_CAL_NOTES));
	SetDate(FIELD_CAL_LAST_CHECK, p.GetDate(FIELD_CAL_LAST_CHECK));
	SetDate(FIELD_DATE_MODIFIED, p.GetDate(FIELD_DATE_MODIFIED));

	return true;
}

const char *LMail3Calendar::GetStr(int id)
{
	switch (id)
	{
		case FIELD_TO:
		{
			if (!ToCache)
			{
				LJson j(To);
				LString::Array Out;
				Out.SetFixedLength(false);
				for (auto i: j.GetArray(NULL))
				{
					auto Nm = i.Get("name");
					auto Em = i.Get("email");
					Out.New().Printf("\'%s\' <%s>", Nm.Get(), Em.Get());
				}
				ToCache = LString(",").Join(Out);
			}
			return ToCache;
		}
		case FIELD_ATTENDEE_JSON:
			return To;
		case FIELD_CAL_TIMEZONE:
			return TimeZone;
		case FIELD_CAL_SUBJECT:
			return Subject;
		case FIELD_CAL_LOCATION:
			return Location;
		case FIELD_UID:
			return Uid;
		case FIELD_CAL_REMINDERS:
			return Reminders;
		case FIELD_CAL_RECUR_FILTER_POS:
			return RecurPos;
		case FIELD_CAL_RECUR_FILTER_YEARS:
			return FilterYears;
		case FIELD_CAL_NOTES:
			return Notes;
		case FIELD_CAL_STATUS:
			return EventStatus;
	}

	LAssert(0);
	return 0;
}

Store3Status LMail3Calendar::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_TO:
		{
			LJson j;
			auto In = LString(str).SplitDelimit(",");
			int i = 0;
			for (auto s: In)
			{
				LAutoString Name, Addr;
				DecodeAddrName(s, Name, Addr, NULL);
				if (Addr)
				{
					LString a;
					a.Printf("[%i].email", i);
					j.Set(a, Addr);

					a.Printf("[%i].name", i);
					if (Name)
						j.Set(a, Name);
					i++;
				}				
			}
			To = j.GetJson();
			ToCache = str;
			break;
		}
		case FIELD_ATTENDEE_JSON:
			To = str;
			ToCache.Empty();
			break;
		case FIELD_CAL_TIMEZONE:
			TimeZone = str;
			break;
		case FIELD_CAL_SUBJECT:
			Subject = str;
			break;
		case FIELD_CAL_LOCATION:
			Location = str;
			break;
		case FIELD_UID:
			Uid = str;
			break;
		case FIELD_CAL_REMINDERS:
			Reminders = str;
			break;
		case FIELD_CAL_RECUR_FILTER_POS:
			RecurPos = str;
			break;
		case FIELD_CAL_RECUR_FILTER_YEARS:
			FilterYears = str;
			break;
		case FIELD_CAL_NOTES:
			Notes = str;
			break;
		case FIELD_CAL_STATUS:
			EventStatus = str;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 LMail3Calendar::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
		case FIELD_CAL_TYPE:
			return CalType;
		case FIELD_CAL_COMPLETED:
			return Completed;
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
		case FIELD_CAL_RECUR_FILTER_DAYS:
			return FilterDays;
		case FIELD_CAL_RECUR_FILTER_MONTHS:
			return FilterMonths;
		case FIELD_CAL_PRIVACY:
			return CalPriv;
		case FIELD_COLOUR:
			if (Colour.IsValid())
				return Colour.c32();
			return -1;
		case FIELD_CAL_ALL_DAY:
			return AllDay;
		case FIELD_STATUS:
			return StoreStatus;
	}

	LAssert(0);
	return -1;
}

Store3Status LMail3Calendar::SetInt(int id, int64 val)
{
	int n = (int)val;
	switch (id)
	{
		case FIELD_CAL_TYPE:
			CalType = n;
			break;
		case FIELD_CAL_COMPLETED:
			Completed = n;
			break;
		case FIELD_CAL_SHOW_TIME_AS:
			ShowTimeAs = n;
			break;
		case FIELD_CAL_RECUR:
			Recur = n;
			break;
		case FIELD_CAL_RECUR_FREQ:
			RecurFreq = n;
			break;
		case FIELD_CAL_RECUR_INTERVAL:
			RecurInterval = n;
			break;
		case FIELD_CAL_RECUR_END_COUNT:
			RecurCount = n;
			break;
		case FIELD_CAL_RECUR_END_TYPE:
			RecurEndType = n;
			break;
		case FIELD_CAL_RECUR_FILTER_DAYS:
			FilterDays = n;
			break;
		case FIELD_CAL_RECUR_FILTER_MONTHS:
			FilterMonths = n;
			break;
		case FIELD_CAL_PRIVACY:
			CalPriv = (CalendarPrivacyType)n;
			break;
		case FIELD_COLOUR:
			if (n > 0)
				Colour.Set(n, 32);
			else
				Colour.Empty();
			break;
		case FIELD_CAL_ALL_DAY:
			AllDay = val != 0;
			break;
		case FIELD_STATUS:
			StoreStatus = val;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

const LDateTime *LMail3Calendar::GetDate(int id)
{
	switch (id)
	{
		case FIELD_CAL_START_UTC:
			return &Start;
		case FIELD_CAL_END_UTC:
			return &End;
		case FIELD_CAL_RECUR_END_DATE:
			return &RecurEnd;
		case FIELD_CAL_LAST_CHECK:
			return &LastCheck;
		case FIELD_DATE_MODIFIED:
			return &Modified;
	}

	return NULL;
}

Store3Status LMail3Calendar::SetDate(int id, const LDateTime *t)
{
	if (t)
		LAssert(t->GetTimeZone()==0);

	switch (id)
	{
		case FIELD_CAL_START_UTC:
			if (t)
				Start = *t;
			else
				Start.Year(0);
			break;
		case FIELD_CAL_END_UTC:
			if (t)
				End = *t;
			else
				End.Year(0);
			break;
		case FIELD_CAL_RECUR_END_DATE:
			if (t)
				RecurEnd = *t;
			else
				RecurEnd.Year(0);
			break;
		case FIELD_CAL_LAST_CHECK:
			if (t)
				LastCheck = *t;
			else
				LastCheck.Empty();
			break;
		case FIELD_DATE_MODIFIED:
			if (t)
				Modified = *t;
			else
				Modified.Empty();
			break;
		default:
			return Store3NotImpl;
	}

	return Store3Success;
}

