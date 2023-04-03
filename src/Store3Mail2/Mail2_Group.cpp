#include "Store3Mail2.h"

//////////////////////////////////////////////////////////////////////
GroupData::GroupData(LMail2Store *s) : ThingData(s)
{
	Flags = 0;
	Name = 0;
	Group = 0;
}

GroupData::~GroupData()
{
	DeleteArray(Name);
	DeleteArray(Group);
}

LDataI &GroupData::operator =(LDataI &p)
{
	LAssert(0);
	return *this;
}

void GroupData::Load()
{
	if (IsLoaded)
		return;

	if (Store)
	{
		LFile *f = Store->GotoObject(__FILE__, __LINE__);
		if (f)
		{
			IsLoaded = true;
			Serialize(*f, false);
			DeleteObj(f);
		}
	}
}

char *GroupData::GetStr(int id)
{
	Load();

	switch (id)
	{
		case FIELD_GROUP_NAME:
			return Name;
		case FIELD_GROUP_LIST:
			return Group;
	}

	LAssert(0);
	return 0;
}

bool GroupData::SetStr(int id, const char *str)
{
	Load();

	switch (id)
	{
		case FIELD_GROUP_NAME:
			_Str(Name);
		case FIELD_GROUP_LIST:
			_Str(Group);
	}

	LAssert(0);
	return 0;
}

int64 GroupData::GetInt(int id)
{
	Load();

	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
	}

	LAssert(0);
	return -1;
}

bool GroupData::SetInt(int id, int64 i)
{
	Load();

	LAssert(0);
	return 0;
}

int GroupData::Type()
{
	return MAGIC_GROUP;
}

int GroupData::Sizeof()
{
	int Size =	sizeof(uint32) +	// magic
				sizeof(uint32);		// number of fields

	Size += ThingData::Sizeof(Name);
	Size += ThingData::Sizeof(Group);

	return Size;
}

bool GroupData::Serialize(LFile &f, bool Write)
{
	bool Status = false;
	ulong Magic = Type();

	IsLoaded = true;
	if (Write)
	{
		f << Magic;
		uint32 Fields = 2;
		f << Fields;

		ThingData::Write(f, FIELD_GROUP_NAME, Name);
		ThingData::Write(f, FIELD_GROUP_LIST, Group);

		Status = true;
	}
	else
	{
		uint32 n;
		f >> n;
		if (n == Magic)
		{
			f >> n;
			for (unsigned i=0; i<n; i++)
			{
				int16 Id;
				int8 Type;
				
				f >> Id;
				f >> Type;

				switch (Type)
				{
					case OBJ_STRING:
					{
						uint32 Size;
						f >> Size;
						char *Buf = new char[Size+1];
						if (Buf)
						{
							f.Read(Buf, Size);
							Buf[Size] = 0;
							SetStr(Id, Buf);
							DeleteArray(Buf);
						}
						break;
					}
					case OBJ_BINARY:
					{
						uint32 Size;
						f >> Size;
						if (Size == sizeof(Mail2Date))
						{
							Mail2Date d;
							
							f >> d.Day;
							f >> d.Month;
							f >> d.Year;
							f >> d.Hour;
							f >> d.Minute;
							f >> d.ThouSec;

							LDateTime dt;
							dt.Day(d.Day);
							dt.Month(d.Month);
							dt.Year(d.Year);
							dt.Hours(d.Hour);
							dt.Minutes(d.Minute);
							dt.Seconds(d.ThouSec / 1000);
							dt.Thousands(d.ThouSec % 1000);

							SetDate(Id, &dt);
						}
						break;
					}
					case OBJ_INT:
					{
						int k;
						f >> k;
						SetInt(Id, k);
						break;
					}
				}
			}
		}
		else LAssert(0);
	}

	return Status;
}

