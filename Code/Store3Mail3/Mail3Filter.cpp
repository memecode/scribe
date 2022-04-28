#include "Mail3.h"

enum Dir
{
	FilterIn = 0x1,
	FilterOut = 0x2,
	FilterInternal = 0x4,
};

GMail3Def TblFilter[] =
{
	{"Id",				"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",		"INTEGER"},
	{"FilterSort",		"INTEGER"},
	{"StopFiltering",	"INTEGER"},
	{"Name",			"TEXT"},
	{"Conditions",		"TEXT"},
	{"Actions",			"TEXT"},
	{"Script",			"TEXT"},
	{"Direction",		"INTEGER"},

	{0, 0}
};

GMail3Filter::GMail3Filter(GMail3Store *store) : GMail3Thing(store)
{
	Index = 0;
	StopFiltering = 0;
	Direction = FilterIn | FilterInternal;
}

GMail3Filter::~GMail3Filter()
{
}

bool GMail3Filter::DbDelete()
{
	char s[256];

	// Delete the filter
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_FILTER " where Id=" LPrintfInt64, Id);
	GMail3Store::GStatement Del(Store, s);
	if (!Del.Exec())
		return false;

	return true;
}

bool GMail3Filter::Serialize(GMail3Store::GStatement &s, bool Write)
{
	int i = 0;

	#undef SERIALIZE_AUTOSTR
	#define SERIALIZE_AUTOSTR(var, idx) \
		{ \
			if (Write) { if (!s.SetStr(idx, var)) return false; } \
			else { var.Reset(NewStr(s.GetStr(idx))); } \
			idx++; \
		}

	SERIALIZE_INT64(Id, i++);
	SERIALIZE_INT64(ParentId, i++);
	SERIALIZE_INT(Index, i++);
	SERIALIZE_INT(StopFiltering, i++);
	SERIALIZE_AUTOSTR(Name, i);
	SERIALIZE_AUTOSTR(ConditionsXml, i);
	SERIALIZE_AUTOSTR(ActionsXml, i);
	SERIALIZE_AUTOSTR(Script, i);
	SERIALIZE_INT(Direction, i++);

	return true;
}

Store3CopyImpl(GMail3Filter)
{
	Index = (int) p.GetInt(FIELD_FILTER_INDEX);
	StopFiltering = (int) p.GetInt(FIELD_STOP_FILTERING);
	Direction = (int) ((p.GetInt(FIELD_FILTER_INCOMING) ? FilterIn : 0) |
				(p.GetInt(FIELD_FILTER_OUTGOING) ? FilterOut : 0) |
				(p.GetInt(FIELD_FILTER_INTERNAL) ? FilterInternal : 0));
	SetStr(FIELD_FILTER_NAME, p.GetStr(FIELD_FILTER_NAME));
	SetStr(FIELD_FILTER_CONDITIONS_XML, p.GetStr(FIELD_FILTER_CONDITIONS_XML));
	SetStr(FIELD_FILTER_ACTIONS_XML, p.GetStr(FIELD_FILTER_ACTIONS_XML));
	SetStr(FIELD_FILTER_SCRIPT, p.GetStr(FIELD_FILTER_SCRIPT));

	return true;
}

const char *GMail3Filter::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FILTER_NAME:
			return Name;
		case FIELD_FILTER_CONDITIONS_XML:
			return ConditionsXml;
		case FIELD_FILTER_ACTIONS_XML:
			return ActionsXml;
		case FIELD_FILTER_SCRIPT:
			return Script;
	}

	LAssert(0);
	return 0;
}

Store3Status GMail3Filter::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FILTER_NAME:
			Name.Reset(NewStr(str));
			return Store3Success;
		case FIELD_FILTER_CONDITIONS_XML:
			ConditionsXml.Reset(NewStr(str));
			return Store3Success;
		case FIELD_FILTER_ACTIONS_XML:
			ActionsXml.Reset(NewStr(str));
			return Store3Success;
		case FIELD_FILTER_SCRIPT:
			Script.Reset(NewStr(str));
			return Store3Success;
	}

	LAssert(0);
	return Store3Error;
}

int64 GMail3Filter::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
		case FIELD_FLAGS:
			return 0;
		case FIELD_FILTER_INDEX:
			return Index;
		case FIELD_STOP_FILTERING:
			return StopFiltering;
		case FIELD_FILTER_INCOMING:
			return (Direction & FilterIn) != 0;
		case FIELD_FILTER_OUTGOING:
			return (Direction & FilterOut) != 0;
		case FIELD_FILTER_INTERNAL:
			return (Direction & FilterInternal) != 0;
		case FIELD_LOADED:
			return Store3Loaded;
	}

	LAssert(0);
	return -1;
}

Store3Status GMail3Filter::SetInt(int id, int64 n)
{
	switch (id)
	{
		case FIELD_FLAGS:
			return Store3Error;
		case FIELD_FILTER_INDEX:
			Index = (int) n;
			return Store3Success;
		case FIELD_STOP_FILTERING:
			StopFiltering = (int) n;
			return Store3Success;
		case FIELD_FILTER_INCOMING:
			if (n)
				Direction |= FilterIn;
			else
				Direction &= ~FilterIn;
			Direction &= FilterIn | FilterOut | FilterInternal;
			return Store3Success;
		case FIELD_FILTER_OUTGOING:
			if (n)
				Direction |= FilterOut;
			else
				Direction &= ~FilterOut;
			Direction &= FilterIn | FilterOut | FilterInternal;
			return Store3Success;
		case FIELD_FILTER_INTERNAL:
			if (n)
				Direction |= FilterInternal;
			else
				Direction &= ~FilterInternal;
			Direction &= FilterIn | FilterOut | FilterInternal;
			return Store3Success;
	}

	LAssert(0);
	return Store3Error;
}

