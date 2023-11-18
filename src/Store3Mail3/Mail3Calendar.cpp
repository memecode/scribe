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

LMail3Calendar::LMail3Calendar(LMail3Store *store)
{
	Store = store;
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

