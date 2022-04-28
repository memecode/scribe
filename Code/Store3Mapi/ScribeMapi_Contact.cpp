#include "ScribeMapi.h"
#include "Scribe.h"

uint32_t MapiContactEmailTags[] =
{
	PR_LAST_MODIFIER_NAME, // 0x3FFA001F
	0x800A001F, // PR_EMS_AB_AUTOREPLY_MESSAGE
	
	0x8035001F, // PR_EMS_AB_EXTENSION_ATTRIBUTE_9
	0x8036001F,
	0x8057001F,
	
	0x80C7001F,
	0x80Ca001F,
	0x80Cd001F,
	
	0x805d001e,
	0x8060001e,
	0x81ae001e,
	0x81b0001e,
	0	
};

GMapiContact::GMapiContact(GMapiStore *store) : GMapiThing(store)
{
}

GMapiContact::~GMapiContact()
{
}

void GMapiContact::Set(SPropValue *entry, GMapiFolder *parent, ScribeMapiList *lst)
{
	if (Entry.Length(entry->Value.bin.cb))
		memcpy(&Entry[0], entry->Value.bin.lpb, entry->Value.bin.cb);
	Parent = parent;
}

LPMESSAGE GMapiContact::Handle()
{
	if (!MapiMsg && Parent && Parent->Handle())
	{
		ULONG Type = 0;
		IUnknown *Item = NULL;
		HRESULT e = Parent->Handle()->OpenEntry(	Entry.Length(),
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

LDataPropI &GMapiContact::operator =(LDataPropI &p)
{
	LAssert(0);
	return *this;
}

void GMapiContact::ReadEmails()
{
	if (!PrimaryEmail)
	{
		LString::Array Addrs;
		for (unsigned i=0; MapiContactEmailTags[i]; i++)
		{
			LString s = MapiGetPropStr(Handle(), MapiContactEmailTags[i]);
			if (s && strchr(s, '@'))
			{
				bool Has = false;
				for (unsigned n=0; n<Addrs.Length(); n++)
				{
					if (!_stricmp(Addrs[n], s))
					{
						Has = true;
						break;
					}
				}
				if (!Has)
					Addrs.New() = s;
			}
		}

		if (Addrs.Length())
		{
			LString Sep(",");
			PrimaryEmail = Addrs[0];
			Addrs.DeleteAt(0, true);
			AltEmails = Sep.Join(Addrs);
		}
	}
}

char *GMapiContact::CacheGetStr(LString &s, uint32_t Prop)
{
	s = MapiGetPropStr(Handle(), Prop);
	return s;
}

const char *GMapiContact::GetStr(int id)
{
	switch (id)
	{
		case FIELD_TITLE:
			return CacheGetStr(Title, PR_DISPLAY_NAME_PREFIX);
		case FIELD_POSITION:
			return CacheGetStr(Position, PR_TITLE);
		case FIELD_FIRST_NAME:
			return CacheGetStr(First, PR_GIVEN_NAME);
		case FIELD_LAST_NAME:
			return CacheGetStr(Last, PR_SURNAME);
		case FIELD_NICK:
			return CacheGetStr(Nick, PR_NICKNAME);
		case FIELD_SPOUSE:
			return CacheGetStr(Spouse, PR_SPOUSE_NAME);
		
		case FIELD_EMAIL:
			ReadEmails();
			return PrimaryEmail;
		case FIELD_ALT_EMAIL:
			ReadEmails();
			return AltEmails;
		
		case FIELD_HOME_STREET:
			return CacheGetStr(Home.Street, PR_HOME_ADDRESS_STREET);
		case FIELD_HOME_SUBURB:
			return CacheGetStr(Home.Suburb, PR_HOME_ADDRESS_CITY);
		case FIELD_HOME_POSTCODE:
			return CacheGetStr(Home.Postcode, PR_HOME_ADDRESS_POSTAL_CODE);
		case FIELD_HOME_STATE:
			return CacheGetStr(Home.State, PR_HOME_ADDRESS_STATE_OR_PROVINCE);
		case FIELD_HOME_COUNTRY:
			return CacheGetStr(Home.Country, PR_HOME_ADDRESS_COUNTRY);
		case FIELD_HOME_WEBPAGE:
			return CacheGetStr(Home.Url, PR_PERSONAL_HOME_PAGE);

		case FIELD_WORK_STREET:
			return CacheGetStr(Work.Street, PR_BUSINESS_ADDRESS_STREET);
		case FIELD_WORK_SUBURB:
			return CacheGetStr(Work.Suburb, PR_BUSINESS_ADDRESS_CITY);
		case FIELD_WORK_POSTCODE:
			return CacheGetStr(Work.Postcode, PR_BUSINESS_ADDRESS_POSTAL_CODE);
		case FIELD_WORK_STATE:
			return CacheGetStr(Work.State, PR_BUSINESS_ADDRESS_STATE_OR_PROVINCE);
		case FIELD_WORK_COUNTRY:
			return CacheGetStr(Work.Country, PR_BUSINESS_ADDRESS_COUNTRY);
		case FIELD_WORK_WEBPAGE:
			return CacheGetStr(Work.Url, PR_BUSINESS_HOME_PAGE);
		case FIELD_COMPANY:
			return CacheGetStr(Company, PR_COMPANY_NAME);

		case FIELD_HOME_PHONE:
			return CacheGetStr(HomePh.Number, PR_HOME_TELEPHONE_NUMBER);
		case FIELD_HOME_FAX:
			return CacheGetStr(HomePh.Fax, PR_HOME_FAX_NUMBER);
		case FIELD_HOME_MOBILE:
			return CacheGetStr(HomePh.Mobile, PR_MOBILE_TELEPHONE_NUMBER);
		case FIELD_HOME_IM:
			return NULL;
			
		case FIELD_WORK_PHONE:
			return CacheGetStr(WorkPh.Number, PR_BUSINESS_TELEPHONE_NUMBER);
		case FIELD_WORK_FAX:
			return CacheGetStr(WorkPh.Fax, PR_BUSINESS_FAX_NUMBER);
		case FIELD_WORK_MOBILE:
			return CacheGetStr(WorkPh.Mobile, PR_CELLULAR_TELEPHONE_NUMBER);
		case FIELD_WORK_IM:
			return NULL;

		case FIELD_NOTE:
			return CacheGetStr(Notes, PR_BODY);
		case FIELD_TIMEZONE:
			return NULL;

		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status GMapiContact::SetStr(int id, const char *str)
{
	return Store3Error;
}

int64 GMapiContact::GetInt(int id)
{
	return -1;
}

Store3Status GMapiContact::SetInt(int id, int64 i)
{
	return Store3Error;
}

const LDateTime *GMapiContact::GetDate(int id)
{
	return NULL;
}

Store3Status GMapiContact::SetDate(int id, const LDateTime *i)
{
	return Store3Error;
}

LDataPropI *GMapiContact::GetObj(int id)
{
	return NULL;
}

GDataIt GMapiContact::GetList(int id)
{
	return NULL;
}

LDataI &GMapiContact::operator =(LDataI &p)
{
	LAssert(0);
	return *this;
}

uint32_t GMapiContact::Type()
{
	return MAGIC_CONTACT;
}

bool GMapiContact::IsOnDisk()
{
	return true;
}

bool GMapiContact::IsOrphan()
{
	return false;
}

uint64 GMapiContact::Size()
{
	return 0;
}

Store3Status GMapiContact::Save(LDataI *Parent)
{
	return Store3Error;
}

Store3Status GMapiContact::Delete(bool ToTrash)
{
	return Store3Error;
}

