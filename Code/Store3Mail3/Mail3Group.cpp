#include "lgi/common/Lgi.h"
#include "Mail3.h"

GMail3Def TblGroup[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"},
	{"Name",			"TEXT"},
	{"Addresses",		"TEXT"},
	{"DateModified",	"TEXT"}, // UTC
	{0, 0}
};

GMail3Group::GMail3Group(GMail3Store *store) : GMail3Thing(store)
{	
}

GMail3Group::~GMail3Group()
{
}

bool GMail3Group::DbDelete()
{
	char s[256];

	// Delete the contact
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_GROUP " where Id=" LPrintfInt64, Id);
	GMail3Store::GStatement Del(Store, s);
	if (!Del.Exec())
		return false;

	return true;
}

Store3CopyImpl(GMail3Group)
{
	Name = p.GetStr(FIELD_GROUP_NAME);
	Group = p.GetStr(FIELD_GROUP_LIST);
	DateMod = p.GetDate(FIELD_DATE_MODIFIED);
	return true;
}

bool GMail3Group::Serialize(GMail3Store::GStatement &s, bool Write)
{
	int i = 0;

	if (Write)
	{
		DateMod.SetNow();
		DateMod.ToUtc();
	}

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);
	SERIALIZE_GSTR(Name, i++);
	SERIALIZE_GSTR(Group, i++);
	SERIALIZE_DATE(DateMod, i++);

	return true;
}

const char *GMail3Group::GetStr(int id)
{
	switch (id)
	{
		case FIELD_GROUP_NAME:
			return Name;
		case FIELD_GROUP_LIST:
			return Group;
	}
	
	return 0;
}

Store3Status GMail3Group::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_GROUP_NAME:
			return Name.Set(str) ? Store3Success : Store3Error;
		case FIELD_GROUP_LIST:
			return Group.Set(str) ? Store3Success : Store3Error;
	}
	
	return Store3Error;
}

const LDateTime *GMail3Group::GetDate(int id)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			return &DateMod;
	}

	return NULL;
}

Store3Status GMail3Group::SetDate(int id, const LDateTime *i)
{
	switch (id)
	{
		case FIELD_DATE_MODIFIED:
			if (!i)
				return Store3Error;
			DateMod = i;
			break;
		default:
			return Store3Error;
	}

	return Store3Success;
}