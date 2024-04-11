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

LMail3Def TblCalendarFiles[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"}, // ID of the calendar event...

	{"FileName",		"TEXT"},	// FIELD_NAME
	{"MimeType",		"TEXT"},	// FIELD_MIME_TYPE
	{"DateModified",	"TEXT"},	// FIELD_DATE_MODIFIED
	{"Data",			"TEXT"},	// FIELD_ATTACHMENTS_DATA

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
	Attachments.a.DeleteObjects();
	Attachments.State = Store3Unloaded;

	// Delete any attachments.
	auto Sql = LString::Fmt("delete from " MAIL3_TBL_CALENDAR_FILES " where ParentId=" LPrintfInt64, Id);
	LMail3Store::LStatement FileDel(Store, Sql);
	if (!FileDel.Exec())
		return false;

	// Delete the calendar itself
	Sql = LString::Fmt("delete from " MAIL3_TBL_CALENDAR " where Id=" LPrintfInt64, Id);
	LMail3Store::LStatement Del(Store, Sql);
	if (!Del.Exec())
		return false;

	Id = -1;
	return true;
}

LDataIt LMail3Calendar::GetList(int id)
{
	switch (id)
	{
		case FIELD_CAL_ATTACHMENTS:
		{
			if (Attachments.State == Store3Unloaded)
			{
				auto Sql = LString::Fmt("select * from %s where ParentId=" LPrintfInt64, MAIL3_TBL_CALENDAR_FILES, Id);

				LMail3Store::LStatement s(Store, Sql);
				if (s.IsOk())
				{
					while (s.Row())
					{
						if (auto a = new LMail3CalendarFile(Store))
						{
							a->Calendar = this;
							if (a->Serialize(s, false))
								Attachments.a.Add(a);
							else
								delete a;
						}
					}

					Attachments.State = Store3Loaded;
				}
			}
			return &Attachments;
		}
	}

	return NULL;
}

bool LMail3Calendar::SaveAttachments()
{
	if (Id < 0)
	{
		LAssert(!"Needs an ID before you can call this.");
		return false;
	}

	printf("LMail3Calendar::Serialize attachments=%i\n", (int)Attachments.Length());
	for (auto i: Attachments.a)
	{
		printf("LMail3Calendar::Serialize dirty=%i\n", i->IsDirty());
		if (i->IsDirty())
		{
			auto result = i->Save(this);
			printf("LMail3Calendar::Serialize result=%i\n", result);
			if (result != Store3Success)
			{
				LAssert(!"Attachment failed to save.");
				return false;
			}
		}
	}
	
	return true;
}

bool LMail3Calendar::SetId(int64_t id)
{
	bool valid = Id >= 0;

	if (!LMail3Thing::SetId(id))
		return false;
	
	if (!valid)
		SaveAttachments();
	
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
		if (Id >= 0 && !SaveAttachments())
		{
			StoreStatus = Store3Error;
			return false;
		}
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

///////////////////////////////////////////////////////////////////////////////////////////////
LMail3CalendarFile::LMail3CalendarFile(LMail3Store *store) :
	Store(store)
{
}

bool LMail3CalendarFile::CopyProps(LDataPropI &p)
{
	SetStr(FIELD_NAME, p.GetStr(FIELD_NAME));
	SetStr(FIELD_MIME_TYPE, p.GetStr(FIELD_MIME_TYPE));
	SetDate(FIELD_DATE_MODIFIED, p.GetDate(FIELD_DATE_MODIFIED));
	SetStr(FIELD_ATTACHMENTS_DATA, p.GetStr(FIELD_ATTACHMENTS_DATA));

	return true;
}

bool LMail3CalendarFile::Serialize(LMail3Store::LStatement &s, bool Write)
{
	int i = 0;

	if (Write)
		LAssert(ParentId >= 0);

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);

	SERIALIZE_LSTR(FileName, i++); // FIELD_NAME
	SERIALIZE_LSTR(MimeType, i++); // FIELD_MIME_TYPE
	SERIALIZE_DATE(DateModified, i++); // FIELD_DATE_MODIFIED
	SERIALIZE_LSTR(Data, i++); // FIELD_ATTACHMENTS_DATA

	return true;
}

const char *LMail3CalendarFile::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
			return FileName;
		case FIELD_MIME_TYPE:
			return MimeType;
		case FIELD_ATTACHMENTS_DATA:
			return Data;
		case FIELD_SIZE:
			SizeCache = LFormatSize(Data.Length());
			return SizeCache;
	}

	return NULL;
}

Store3Status LMail3CalendarFile::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_NAME:
			FileName = str;
			break;
		case FIELD_MIME_TYPE:
			MimeType = str;
			break;
		case FIELD_ATTACHMENTS_DATA:
			Data = str;
			break;
		default:
			return Store3NotImpl;
	}

	Dirty = true;
	return Store3Success;
}

const LDateTime *LMail3CalendarFile::GetDate(int id)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			return &DateModified;
	}

	return NULL;
}

Store3Status LMail3CalendarFile::SetDate(int id, const LDateTime *i)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			if (i)
				DateModified = *i;
			else
				DateModified.Empty();
			break;
		default:
			return Store3NotImpl;
	}

	Dirty = true;
	return Store3Success;
}

uint64 LMail3CalendarFile::Size()
{
	return sizeof(this) +
		FileName.Length() + 
		MimeType.Length() +
		sizeof(DateModified) +
		Data.Length();
}

Store3Status LMail3CalendarFile::Save(LDataI *Parent)
{
	if (Parent)
	{
		Calendar = dynamic_cast<LMail3Calendar*>(Parent);
		if (!Calendar)
		{
			LAssert(!"Wrong object type!");
			return Store3Error;
		}
		
		if (Calendar->Attachments.IndexOf(this) < 0)
			Calendar->Attachments.Insert(this);
	}
	
	if (!Calendar)
	{
		LAssert(!"Must have parent calendar event.");
		return Store3Error;
	}
	
	ParentId = Calendar->Id;
	if (ParentId < 0)
	{
		// Parent event hasn't got an ID yet... assuming that when it's
		// saved, this object will get saved WITH it.
		return Store3Delayed;
	}

	LAutoPtr<LMail3Store::LStatement> s;

	if (Id >= 0)
		s.Reset(new LMail3Store::LUpdate(Store, MAIL3_TBL_CALENDAR_FILES, Id));
	else
		s.Reset(new LMail3Store::LInsert(Store, MAIL3_TBL_CALENDAR_FILES));

	if (!s || !s->IsOk())
	{
		LAssert(!"Query not valid.");
		return Store3Error;
	}

	if (!Serialize(*s, true))
	{
		LAssert(!"Serialize failed");
		return Store3Error;
	}

	if (!s->Exec())
	{
		LAssert(!"Query failed");
		return Store3Error;
	}

	if (Id < 0)
		Id = s->LastInsertId();

	return Store3Success;
}

Store3Status LMail3CalendarFile::Delete(bool ToTrash)
{
	if (!Calendar)
	{
		LAssert(!"No calendar object?");
		return Store3Error;
	}

	auto idx = Calendar->Attachments.IndexOf(this);
	if (idx < 0)
	{
		LAssert(!"Calendar doesn't have this file?");
		return Store3Error;
	}

	auto Sql = LString::Fmt("delete from " MAIL3_TBL_CALENDAR_FILES " where Id=" LPrintfInt64, Id);
	LMail3Store::LStatement s(Store, Sql);
	if (!s.Exec())
	{
		LAssert(!"Delete query failed");
		return Store3Error;
	}

	return Store3Success;
}

