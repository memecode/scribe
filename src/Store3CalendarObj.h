// This is a generic implementation of the Store3 interface for a calendar object
// It just doesn't do the storage part, which can be implemented by the subclass.
#pragma once

#include "lgi/common/Json.h"

template<typename Parent>
class Store3CalendarFile : public Parent
{
protected:
	LString FileName;
	LString MimeType;
	LDateTime DateModified;
	LString Uri; // Either 'Uri' or 'Data' must be valid
	LString Data; // Store the data inline

	LString SizeCache;
	bool Dirty = false;

public:
	bool IsDirty() { return Dirty; }
	uint32_t Type() override { return MAGIC_CALENDAR_FILE; }

	bool CopyProps(LDataPropI &p) override
	{
		SetStr(FIELD_NAME, p.GetStr(FIELD_NAME));
		SetStr(FIELD_MIME_TYPE, p.GetStr(FIELD_MIME_TYPE));
		SetDate(FIELD_DATE_MODIFIED, p.GetDate(FIELD_DATE_MODIFIED));
		SetStr(FIELD_URI, p.GetStr(FIELD_URI));
		SetStr(FIELD_ATTACHMENTS_DATA, p.GetStr(FIELD_ATTACHMENTS_DATA));
		return true;
	}
	
	// Impl LDataPropI
	const char *GetStr(int id) override
	{
		switch (id)
		{
			case FIELD_NAME:
				return FileName;
			case FIELD_MIME_TYPE:
				return MimeType;
			case FIELD_URI:
				return Uri;
			case FIELD_ATTACHMENTS_DATA:
				return Data;
			case FIELD_SIZE:
				SizeCache = LFormatSize(Data.Length());
				return SizeCache;
		}

		return NULL;
	}

	Store3Status SetStr(int id, const char *str) override
	{
		switch (id)
		{
			case FIELD_NAME:
				FileName = str;
				break;
			case FIELD_MIME_TYPE:
				MimeType = str;
				break;
			case FIELD_URI:
				Uri = str;
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

	const LDateTime *GetDate(int id) override
	{
		switch (id)
		{
			case FIELD_DATE_MODIFIED:
				return &DateModified;
		}

		return NULL;
	}

	Store3Status SetDate(int id, const LDateTime *i) override
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

	// Impl LDataI
	LAutoStreamI GetStream(const char *file, int line) override
	{
		LAutoStreamI s;
		if (Data)
			s.Reset(new LMemStream(Data.Get(), Data.Length(), false));
		return s;
	}

	uint64 Size() override
	{
		return sizeof(this) +
			FileName.Length() + 
			MimeType.Length() +
			sizeof(DateModified) +
			Uri.Length() +
			Data.Length();
	}

	// Stubs
	int64 GetInt(int id) override { return 0; }
	Store3Status SetInt(int id, int64 i) override { return Store3NotImpl; }
	const LVariant *GetVar(int id) override { return NULL; }
	Store3Status SetVar(int id, LVariant *i) override { return Store3NotImpl; }
	LDataPropI *GetObj(int id) override { return NULL; }
	Store3Status SetObj(int id, LDataPropI *i) override { return Store3NotImpl; }
	LDataIt GetList(int id) override { EmptyVirtual(NULL); }
};


template<typename Parent>
class Store3CalendarObj : public Parent
{
	LString ToCache;

protected:
	int CalType = 0;		// FIELD_CAL_TYPE
	LString To;				// FIELD_TO
	CalendarPrivacyType CalPriv = CalDefaultPriv; // FIELD_CAL_PRIVACY
	int Completed = 0;		// FIELD_CAL_COMPLETED
	LDateTime Start;		// FIELD_CAL_START_UTC
	LDateTime  End;			// FIELD_CAL_END_UTC
	LString TimeZone;		// FIELD_CAL_TIMEZONE
	LString Subject;		// FIELD_CAL_SUBJECT
	LString Location;		// FIELD_CAL_LOCATION
	LString Uid;			// FIELD_UID
	bool AllDay = false;	// FIELD_CAL_ALL_DAY
	int64 StoreStatus = Store3Success; // FIELD_STATUS - ie the current Store3Status
	LString EventStatus;	// FIELD_CAL_STATUS
	LDateTime Modified;		// FIELD_DATE_MODIFIED

	LString Reminders;		// FIELD_CAL_REMINDERS
	LDateTime LastCheck;	// FIELD_CAL_LAST_CHECK
	int ShowTimeAs = 0;		// FIELD_CAL_SHOW_TIME_AS

	int Recur = 0;			// FIELD_CAL_RECUR
	int RecurFreq = 0;		// FIELD_CAL_RECUR_FREQ
	int RecurInterval = 0;	// FIELD_CAL_RECUR_INTERVAL
	LDateTime RecurEnd;		// FIELD_CAL_RECUR_END_DATE
	int RecurCount = 0;		// FIELD_CAL_RECUR_END_COUNT
	int RecurEndType = 0;	// FIELD_CAL_RECUR_END_TYPE
	LString RecurPos;		// FIELD_CAL_RECUR_FILTER_POS

	int FilterDays = 0;		// FIELD_CAL_RECUR_FILTER_DAYS
	int FilterMonths = 0;	// FIELD_CAL_RECUR_FILTER_MONTHS
	LString FilterYears;	// FIELD_CAL_RECUR_FILTER_YEARS
	LString Notes;			// FIELD_CAL_NOTES

	LColour Colour;			// FIELD_COLOUR

public:
	uint32_t Type() override { return MAGIC_CALENDAR; }
	const char *GetClass() override { return "Store3CalendarObj"; }
	
	bool CopyProps(LDataPropI &p) override
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

	const char *GetStr(int id) override
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
					for (auto i: j.GetArray(LString()))
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
	
	Store3Status SetStr(int id, const char *str) override
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

	int64 GetInt(int id) override
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

	Store3Status SetInt(int id, int64 val) override
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
				if (val > 0)
					Colour.Set((uint32_t)val, 32);
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

	const LDateTime *GetDate(int id) override
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

	Store3Status SetDate(int id, const LDateTime *t) override
	{
		if (!t)
			return Store3Error;
		if (t->GetTimeZone())
		{
			LStackTrace("LMail3Calendar::SetDate error: date has timezone.\n");
			LAssert(t->GetTimeZone()==0);
		}

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

	// LDataI impl:
	uint64 Size() override
	{
		return sizeof(*this) +
			ToCache.Length() +
			To.Length() +
			TimeZone.Length() +
			Subject.Length() +
			Location.Length() +
			Uid.Length() +
			Reminders.Length();
	}

	/* Impl this is subclass:
		bool IsOnDisk() override { return false; }
		bool IsOrphan() override { return false; }
		Store3Status Save(LDataI *Obj = NULL) override { return Store3NotImpl; }
		Store3Status Delete(bool ToTrash = true) override { return Store3NotImpl; }
		LAutoStreamI GetStream(const char *file, int line) override { return LAutoStreamI(); }
	*/
};
