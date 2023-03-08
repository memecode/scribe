#include "lgi/common/Lgi.h"
#include "Mail3.h"

#define ALL_FIELDS() \
	_("Uid",			"TEXT", FIELD_UID) \
	_("Title",			"TEXT", FIELD_TITLE) \
	_("First",			"TEXT", FIELD_FIRST_NAME) \
	_("Last",			"TEXT", FIELD_LAST_NAME) \
	_("Email",			"TEXT", FIELD_EMAIL) \
	_("AltEmail",		"TEXT", FIELD_ALT_EMAIL) \
	_("Nick",			"TEXT", FIELD_NICK) \
	_("Spouse",			"TEXT", FIELD_SPOUSE) \
	_("Notes",			"TEXT",	FIELD_NOTE) \
	_("TimeZone",		"TEXT",	FIELD_TIMEZONE) \
	_("Plugins",		"TEXT",	FIELD_PLUGIN_ASSOC) \
	\
	_("HomeStreet",		"TEXT", FIELD_HOME_STREET) \
	_("HomeSuburb",		"TEXT", FIELD_HOME_SUBURB) \
	_("HomePostCode",	"TEXT", FIELD_HOME_POSTCODE) \
	_("HomeState",		"TEXT", FIELD_HOME_STATE) \
	_("HomeCountry",	"TEXT", FIELD_HOME_COUNTRY) \
	_("HomePhone",		"TEXT", FIELD_HOME_PHONE) \
	_("HomeMobile",		"TEXT", FIELD_HOME_MOBILE) \
	_("HomeIM",			"TEXT", FIELD_HOME_IM) \
	_("HomeFax",		"TEXT", FIELD_HOME_FAX) \
	_("HomeWebpage",	"TEXT", FIELD_HOME_WEBPAGE)	\
	\
	_("WorkStreet",		"TEXT", FIELD_WORK_STREET) \
	_("WorkSuburb",		"TEXT", FIELD_WORK_SUBURB) \
	_("WorkPostCode",	"TEXT", FIELD_WORK_POSTCODE) \
	_("WorkState",		"TEXT", FIELD_WORK_STATE) \
	_("WorkCountry",	"TEXT", FIELD_WORK_COUNTRY) \
	_("WorkPhone",		"TEXT", FIELD_WORK_PHONE) \
	_("WorkMobile",		"TEXT", FIELD_WORK_MOBILE) \
	_("WorkIM",			"TEXT", FIELD_WORK_IM) \
	_("WorkFax",		"TEXT", FIELD_WORK_FAX) \
	_("WorkWebpage",	"TEXT", FIELD_WORK_WEBPAGE)	\
	_("Company",		"TEXT", FIELD_COMPANY) \
	\
	_("Image",			"BLOB", FIELD_CONTACT_IMAGE) \
	_("JSON",			"TEXT", FIELD_CONTACT_JSON) \
	\
	_("DateModified",	"TEXT", FIELD_DATE_MODIFIED) // UTC


LVariantType FieldToType(int Fld)
{
	switch (Fld)
	{
		case FIELD_DATE_MODIFIED:
			return GV_DATETIME;
		case FIELD_CONTACT_IMAGE:
			return GV_BINARY;
	}

	return GV_STRING;
}

GMail3Def TblContact[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"},

	#define _(SqlName, SqlType, Id) {SqlName, SqlType},
	ALL_FIELDS()	
	#undef _

	{0, 0}
};

LMail3Contact::LMail3Contact(LMail3Store *store) : LMail3Thing(store)
{	
}

LMail3Contact::~LMail3Contact()
{
	f.DeleteObjects();
}

bool LMail3Contact::DbDelete()
{
	char s[256];

	// Delete the contact
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_CONTACT " where Id=" LPrintfInt64, Id);
	LMail3Store::LStatement Del(Store, s);
	if (!Del.Exec())
		return false;

	return true;
}

Store3CopyImpl(LMail3Contact)
{
	f.DeleteObjects();

	#define _(SqlName, SqlType, Id) \
		if (Id != FIELD_CONTACT_IMAGE) \
		{ \
			auto s = p.GetStr(Id); \
			if (s) f.Add(Id, new LString(s)); \
		}
	ALL_FIELDS()	
	#undef _
	const LVariant *v = p.GetVar(FIELD_CONTACT_IMAGE);
	if (v && v->Type == GV_BINARY)
		Image = *v;
	
	return true;
}

bool LMail3Contact::Serialize(LMail3Store::LStatement &s, bool Write)
{
	int i = 0;

	if (Write)
	{
		DateMod.SetNow();
		DateMod.ToUtc();
	}
	else
		f.DeleteObjects();

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);

	#define SERIALIZE_HASHED_GSTR(id, idx) \
		{ \
			if (Write) \
			{ \
				LString *v = f.Find(id); \
				/*LgiTrace("Write %i, %s -> %i\n", id, v.Get(), idx);*/ \
				if (!s.SetStr(idx, v ? v->Get() : NULL)) \
					return false; \
			} \
			else \
			{ \
				const char *v = s.GetStr(idx); \
				/*LgiTrace("Read %i -> %i, %s\n", idx, id, v.Get());*/ \
				if (v) \
					f.Add(id, new LString(v)); \
			} \
			idx++; \
		}

	#define _(SqlName, SqlType, Id) \
		if (FieldToType(Id) == GV_STRING) \
		{ \
			SERIALIZE_HASHED_GSTR(Id, i); \
		}
	ALL_FIELDS()
	#undef _

	if (Write)
	{
		if (Image.Type == GV_BINARY)
			s.SetBinary(i, "Image", &Image);
		i++;
	}
	else
	{
		s.GetBinary(i++, &Image);
	}

	if (Id == 58)
	{
		int asd=0;
	}

	SERIALIZE_DATE(DateMod, i++);
		
	return true;
}

const char *LMail3Contact::GetStr(int id)
{
	LString *s = f.Find(id);
	return s ? s->Get() : NULL;
}

Store3Status LMail3Contact::SetStr(int id, const char *str)
{
	if (str)
	{
		LString *v = f.Find(id);
		if (v)
		{
			*v = str;
			return Store3Success;
		}

		return f.Add(id, new LString(str)) ? Store3Success : Store3Error;
	}
	
	f.Delete(Id);
	return Store3Success;
}

const LDateTime *LMail3Contact::GetDate(int id)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			return &DateMod;
	}

	return NULL;
}

Store3Status LMail3Contact::SetDate(int id, const LDateTime *i)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			DateMod = i;
			return i ? Store3Success : Store3Error;
	}

	return Store3Error;
}

LVariant *LMail3Contact::GetVar(int id)
{
	switch (id)
	{
		case FIELD_CONTACT_IMAGE:
			return &Image;
	}
	
	return NULL;
}

Store3Status LMail3Contact::SetVar(int id, LVariant *i)
{
	if (!i)
		return Store3Error;
	
	switch (id)
	{
		case FIELD_CONTACT_IMAGE:
		{
			Image = *i;
			return Store3Success;
		}
	}
	
	return Store3Error;
}
