#include "ScribeMapi.h"
#include "Scribe.h"
#include "Calendar.h"

// PSETID_Appointment: 00062002-0000-0000-C000-000000000046
static const GUID kPsetidAppointment =
{ 0x00062002, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const LONG kDispidBusyStatus = 0x8205;
// PSETID_Meeting: 6ED8DA90-450B-101B-98DA-00AA003F1305
static const GUID kPsetidMeeting =
{ 0x6ED8DA90, 0x450B, 0x101B, { 0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05 } };
static const LONG kDispidGlobalObjectId = 0x0003;
static const LONG kDispidCleanGlobalObjectId = 0x0023;

#ifndef SENSITIVITY_NORMAL
#define SENSITIVITY_NORMAL ((ULONG)0x00000000)
#endif

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

void LMapiCalendar::Set(SPropValue *entry, LMapiFolder *parent, LMapiList *Lst)
{
	Entry = entry;
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
	SetDate(FIELD_CAL_START_UTC, p.GetDate(FIELD_CAL_START_UTC));
	SetDate(FIELD_CAL_END_UTC, p.GetDate(FIELD_CAL_END_UTC));
	SetStr(FIELD_UID, p.GetStr(FIELD_UID));
	SetStr(FIELD_CAL_TIMEZONE, p.GetStr(FIELD_CAL_TIMEZONE));
	SetStr(FIELD_CAL_SUBJECT, p.GetStr(FIELD_CAL_SUBJECT));
	SetStr(FIELD_CAL_LOCATION, p.GetStr(FIELD_CAL_LOCATION));
	
	SetInt(FIELD_CAL_SHOW_TIME_AS, p.GetInt(FIELD_CAL_SHOW_TIME_AS));
	SetInt(FIELD_CAL_RECUR, p.GetInt(FIELD_CAL_RECUR));
	SetInt(FIELD_CAL_PRIVACY, p.GetInt(FIELD_CAL_PRIVACY));
	SetInt(FIELD_COLOUR, p.GetInt(FIELD_COLOUR));
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
		case FIELD_UID:
			if (!Uid)
			{
				LString bin;
				if (!MapiGetNamedPropBinary(Handle(), kPsetidMeeting, kDispidGlobalObjectId, bin))
					MapiGetNamedPropBinary(Handle(), kPsetidMeeting, kDispidCleanGlobalObjectId, bin);
				if (bin)
					Uid = bin;
			}
			return Uid;
		case FIELD_CAL_TIMEZONE:
			return TimeZone;
		case FIELD_CAL_REMINDERS:
			// FIXME: impl?
			break;
		case FIELD_ATTENDEE_JSON:
			// FIXME: impl?
			break;

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
		case FIELD_UID:
		{
			Uid = str;
			if (!str || !*str)
				return Store3Success;

			bool ok = MapiSetNamedPropBinary(Handle(), kPsetidMeeting, kDispidGlobalObjectId, str, (ULONG)Strlen(str));
			ok &= MapiSetNamedPropBinary(Handle(), kPsetidMeeting, kDispidCleanGlobalObjectId, str, (ULONG)Strlen(str));
			if (ok)
				return Store3Success;
			break;
		}
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
		{
			LONG v = 0;
			if (MapiGetNamedPropLong(Handle(), kPsetidAppointment, kDispidBusyStatus, v))
			{
				if (v >= CalFree && v <= CalOut)
					ShowAs = (CalendarShowTimeAs)v;
			}
			return ShowAs;
		}
		case FIELD_CAL_PRIVACY:
		{
			auto sens = MapiGetPropInt(Handle(), PR_SENSITIVITY);
			if (sens == SENSITIVITY_PRIVATE)
				Priv = CalPrivate;
			else if (sens == SENSITIVITY_NORMAL || sens == SENSITIVITY_PERSONAL || sens == SENSITIVITY_COMPANY_CONFIDENTIAL)
				Priv = CalPublic;
			return Priv;
		}
		case FIELD_CAL_ALL_DAY:
			// FIXME: impl
			return 0;
		case FIELD_READONLY:
			return true;

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
			MapiSetNamedPropLong(Handle(), kPsetidAppointment, kDispidBusyStatus, (LONG)ShowAs);
			return Store3Success;
		case FIELD_CAL_PRIVACY:
			Priv = (CalendarPrivacyType)i;
			if (Priv == CalPrivate)
				MapiSetPropLong(Handle(), PR_SENSITIVITY, SENSITIVITY_PRIVATE);
			else if (Priv == CalPublic)
				MapiSetPropLong(Handle(), PR_SENSITIVITY, SENSITIVITY_NORMAL);
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
	if (!i)
		return Store3Error;

	switch (id)
	{
		case FIELD_CAL_START_UTC:
			StartDt = *i;
			if (MapiSetPropDate(Handle(), PR_START_DATE, *i))
				return Store3Success;
			break;
		case FIELD_CAL_END_UTC:
			EndDt = *i;
			if (MapiSetPropDate(Handle(), PR_END_DATE, *i))
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
	switch (id)
	{
		case FIELD_CAL_ATTACHMENTS:
			// FIXME: impl?
			return nullptr;
	}

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
