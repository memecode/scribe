#include "ScribeMapi.h"
#include "Scribe.h"
#include "Calendar.h"

LMapiCalendar::LMapiCalendar(LMapiStore *store) : LMapiThing(store)
{
	ShowAs = CalBusy;
	Priv = CalDefaultPriv;
	Colour = -1;
	Recur = false;
}

LMapiCalendar::~LMapiCalendar()
{
}

void LMapiCalendar::Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *Lst)
{
	if (Entry.Length(entry->Value.bin.cb))
		memcpy(&Entry[0], entry->Value.bin.lpb, entry->Value.bin.cb);
	Parent = parent;
}

LPMESSAGE LMapiCalendar::Handle()
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
		}	
	}
	
	return MapiMsg;
}


LDataPropI &LMapiCalendar::operator =(LDataPropI &p)
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

	return *this;
}

const char *LMapiCalendar::GetStr(int id)
{
	switch (id)
	{
		case FIELD_CAL_SUBJECT:
			if (!Subject)
				Subject = LFromNativeCp(MapiGetPropStr(Handle(), PR_SUBJECT));
			return Subject;
		case FIELD_CAL_LOCATION:
			if (!Location)
				Location = LFromNativeCp(MapiGetPropStr(Handle(), PR_LOCATION));
			return Location;
		case FIELD_CAL_NOTES:
			if (!Notes)
				Notes = LFromNativeCp(MapiGetPropStr(Handle(), PR_BODY));
			return Notes;
		case FIELD_CAL_TIMEZONE:
			return TimeZone;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiCalendar::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_CAL_SUBJECT:
			Subject = str;
			if (MapiSetPropStr(Handle(), PR_SUBJECT, Subject))
				return Store3Success;
			break;
		case FIELD_CAL_LOCATION:
			Location = str;
			if (MapiSetPropStr(Handle(), PR_LOCATION, Location))
				return Store3Success;
			break;
		case FIELD_CAL_NOTES:
			Notes = str;
			if (MapiSetPropStr(Handle(), PR_BODY, Notes))
				return Store3Success;
			break;
		case FIELD_CAL_TIMEZONE:
			TimeZone = str;
			return Store3Success;
		default:
			LAssert(0);
			break;
	}
	
	return Store3Error;
}

int64 LMapiCalendar::GetInt(int id)
{
	switch (id)
	{
		case FIELD_CAL_TYPE:
			return CalEvent;
		case FIELD_CAL_RECUR:
			return Recur;
		case FIELD_COLOUR:
			return Colour;
		case FIELD_CAL_SHOW_TIME_AS:
			return ShowAs;
		case FIELD_CAL_PRIVACY:
			return Priv;

		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiCalendar::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_CAL_RECUR:
			Recur = i != 0;
			return Store3Success;
		case FIELD_COLOUR:
			Colour = i;
			return Store3Success;
		case FIELD_CAL_SHOW_TIME_AS:
			ShowAs = (CalendarShowTimeAs)i;
			return Store3Success;
		case FIELD_CAL_PRIVACY:
			Priv = (CalendarPrivacyType)i;
			return Store3Success;
		default:
			LAssert(0);
			break;
	}
	
	return Store3Error;
}

LDateTime *LMapiCalendar::GetDate(int id)
{
	switch (id)
	{
		case FIELD_CAL_START_UTC:
			MapiGetPropDate(StartDt, Handle(), PR_START_DATE);
			return &StartDt;
		case FIELD_CAL_END_UTC:
			MapiGetPropDate(EndDt, Handle(), PR_END_DATE);
			return &EndDt;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiCalendar::SetDate(int id, const LDateTime *i)
{
	switch (id)
	{
		case FIELD_CAL_START_UTC:
			if (MapiSetPropDate(Handle(), PR_START_DATE, StartDt))
				return Store3Success;
			break;
		case FIELD_CAL_END_UTC:
			if (MapiSetPropDate(Handle(), PR_END_DATE, EndDt))
				return Store3Success;
			break;
		default:
			LAssert(0);
			break;
	}
	
	return Store3Error;
}

LDataPropI *LMapiCalendar::GetObj(int id)
{
	switch (id)
	{
		case FIELD_PARENT:
			return Parent;
	}

	LAssert(0);
	return NULL;
}

LDataIt LMapiCalendar::GetList(int id)
{
	LAssert(0);
	return NULL;
}

LDataI &LMapiCalendar::operator =(LDataI &p)
{
	return *this;
}

uint32_t LMapiCalendar::Type()
{
	return MAGIC_CALENDAR;
}

bool LMapiCalendar::IsOnDisk()
{
	return true;
}

bool LMapiCalendar::IsOrphan()
{
	return false;
}

uint64 LMapiCalendar::Size()
{
	return 0;
}

Store3Status LMapiCalendar::Save(LDataI *Parent)
{
	return Store3Error;
}

Store3Status LMapiCalendar::Delete(bool ToTrash)
{
	return Store3Error;
}
