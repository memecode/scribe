#include "Store3Mail2.h"
#include "LMap.h"

#define ForAllContactFields(var) ItemFieldDef *var = 0; for (var = ContactFieldDefs; var->Option && var->FieldId; var++)

static bool Init = false;
static const char *IdToOpt[FIELD_TIMEZONE+1];
#define GetOpt(id) ( (id >= 0 && id < CountOf(IdToOpt) ) ? IdToOpt[id] : 0 )

//////////////////////////////////////////////////////////////////////
ContactData::ContactData(LMail2Store *s) : ThingData(s)
{
	if (!Init)
	{
		Init = true;
		ZeroObj(IdToOpt);
		for (ItemFieldDef *d = ContactFieldDefs; d->FieldId; d++)
		{
			if (d->FieldId < CountOf(IdToOpt))
			{
				IdToOpt[d->FieldId] = d->Option;
			}
			else LAssert(0);
		}
	}
}

ContactData::~ContactData()
{
	AltEmail.DeleteArrays();
	Plugins.DeleteArrays();
}

LDataI &ContactData::operator =(LDataI &p)
{
	ForAllContactFields(i)
	{
		SetStr(i->FieldId, p.GetStr(i->FieldId));
	}

	return *this;
}

void ContactData::Load()
{
	if (IsLoaded)
		return;

	if (Store)
	{
		IsLoaded = true;
		LFile *f = Store->GotoObject(_FL);
		if (f)
		{
			Serialize(*f, false);
			DeleteObj(f);
		}
	}
}

char *ContactData::GetStr(int id)
{
	Load();

	const char *Opt = GetOpt(id);
	if (Opt)
	{
		char *s;
		return Get(Opt, s) ? s : 0;
	}

	if (id == FIELD_ALT_EMAIL)
	{
		LStringPipe p;
		for (unsigned i=0; i<AltEmail.Length(); i++)
		{
			p.Print("%s%s", i?",":"", AltEmail[i]);
		}

		AltEmailCache.Reset(p.NewStr());
		return AltEmailCache;
	}
	else if (id == FIELD_PLUGIN_ASSOC)
	{
		return 0;
	}

	LAssert(0);
	return 0;
}

bool ContactData::SetStr(int id, const char *str)
{
	Load();

	const char *Opt = GetOpt(id);
	if (Opt)
	{
		return Set(Opt, str);
	}

	if (id == FIELD_ALT_EMAIL)
	{
		AltEmail.DeleteArrays();
		LToken t(str, ",");
		for (unsigned i=0; i<t.Length(); i++)
		{
			AltEmail.Add(NewStr(t[i]));
		}
		return true;
	}
	else if (id == FIELD_PLUGIN_ASSOC)
	{
		return false;
	}

	LAssert(0);
	return 0;
}

int64 ContactData::GetInt(int id)
{
	Load();

	const char *Opt = GetOpt(id);
	if (Opt)
	{
		int i;
		if (Get(Opt, i))
			return i;
	}

	return -1;
}

bool ContactData::SetInt(int id, int64 i)
{
	Load();

	const char *Opt = GetOpt(id);
	if (Opt)
	{
		return Set(Opt, (int)i);
	}

	LAssert(0);
	return 0;
}

GDataIt ContactData::GetList(int id)
{
	Load();

	LAssert(0);
	return 0;
}

int ContactData::Type()
{
	return MAGIC_CONTACT;
}

int ContactData::SizeofField(const char *Name)
{
	char *c;
	if (Get(Name, c) && ValidStr(c))
	{
		return sizeof(short) + SizeofStr(c);
	}

	return 0;
}

int ContactData::Sizeof()
{
	int Size = sizeof(ulong) + sizeof(ulong);

	ForAllContactFields(Fld)
	{
		Size += SizeofField(Fld->Option);
	}

	for (unsigned i=0; i<Plugins.Length(); i++)
	{
		Size += SizeStrField(Plugins[i]);
	}

	for (unsigned i=0; i<AltEmail.Length(); i++)
	{
		Size += SizeStrField(AltEmail[i]);
	}

	return Size;
}

bool ContactData::Serialize(LFile &f, bool Write)
{
	bool Status = true;
	if (Write)
	{
		int Items = 0;
		{
			ForAllContactFields(Fld)
			{
				char *s = 0;
				if (Get(Fld->Option, s) && ValidStr(s)) Items++;
			}
		}

		f << (ulong) MAGIC_CONTACT;
		f << (ulong) (Items + Plugins.Length() + AltEmail.Length());

		LMap<int,int> Done;
		ForAllContactFields(Fld)
		{
			char *c;
			if (Get(Fld->Option, c) && ValidStr(c))
			{
				#if defined(_DEBUG) && defined(WIN32) && !defined(_WIN64)
				if (Done[Fld->Id()])
				{
					_asm int 3 // Um, you've already used that ID
				}
				Done[Fld->Id()] = 1;
				#endif

				f << ((short) Fld->FieldId);
				WriteStr(f, c);
			}
		}

		for (unsigned n=0; n<Plugins.Length(); n++)
		{
			WriteStrField(FIELD_PLUGIN_ASSOC, Plugins[n]);
		}

		for (unsigned n=0; n<AltEmail.Length(); n++)
		{
			WriteStrField(FIELD_ALT_EMAIL, AltEmail[n]);
		}
	}
	else
	{
		Plugins.DeleteArrays();
		AltEmail.DeleteArrays();

		ulong Magic = 0;
		f >> Magic;
		if (Magic == MAGIC_CONTACT)
		{
			ulong Fields = 0;
			f >> Fields;
			for (unsigned i=0; i<Fields; i++)
			{
				short Id;
				f >> Id;
				if (Id == FIELD_PLUGIN_ASSOC)
				{
					// Plugin Association Field
					char *s = ReadStr(f PassDebugArgs);
					if (s)
					{
						Plugins.Add(s);
					}
				}
				else if (Id == FIELD_ALT_EMAIL)
				{
					// Alt Email Field
					char *s = ReadStr(f PassDebugArgs);
					if (s)
					{
						AltEmail.Add(s);
					}
				}
				else
				{
					// General contact field
					ForAllContactFields(fld)
					{
						if (fld->FieldId == Id)
						{
							// process field
							char *c = ReadStr(f PassDebugArgs);
							if (c && strlen(c) > 0)
							{
								Set(fld->Option, c);
								DeleteArray(c);
							}
							fld = 0;
							break;
						}
					}

					if (fld)
					{
						// didn't find field
						int Size = 0;
						f >> Size;
						if (Size <= 0)
						{
							break;
						}
						else
						{
							f.Seek(Size, SEEK_CUR);
						}
					}
				}
			}
		}
		else
		{
			Status = false;
		}
	}
	
	return Status;
}

