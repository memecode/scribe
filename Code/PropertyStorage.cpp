#ifndef __PROPERTY_STORAGE_CPP
#define __PROPERTY_STORAGE_CPP

class PropertyObject
{
public:
	int Id;
	StorageObj *Obj;

	PropertyObject(int id, StorageObj *obj)
	{
		Id = id;
		Obj = obj;
	}
};

// Generic property list <-> storage
template <class a>
class PropertyStorage : public a, public ObjProperties
{
	int _ObjType;
	ItemFieldDef *_Fields;

public:
	PropertyStorage(int type, ItemFieldDef *fields)
	{
		_ObjType = type;
		_Fields = fields;
	}

	// Props
	ItemFieldDef *GetFields() { return _Fields; }

	// Overridables
	virtual bool GetObjects(List<PropertyObject> &l) { return false; }

	// GStorageItem implementation
	int Type() { return _ObjType; }

	int Sizeof()
	{
		int Size =	sizeof(ulong) +				// magic
					sizeof(ulong);				// number of fields

		for (bool b=FirstKey(); b; b=NextKey())
		{
			switch (KeyType())
			{
				case OBJ_INT:
				{
					Size += sizeof(int16) +	// Id
							sizeof(int32) +	// Size
							sizeof(int);	// Data
					break;
				}
				case OBJ_FLOAT:
				{
					Size += sizeof(int16) +	// Id
							sizeof(int32) +	// Size
							sizeof(double);	// Data
					break;
				}
				case OBJ_STRING:
				{
					Prop *p = GetProp();
					if (p)
					{
						Size += sizeof(int16) +	// Id
								sizeof(int32) +	// Size
								strlen(p->Value.Cp); // Data
					}
					break;
				}
				case OBJ_BINARY:
				{
					Prop *p = GetProp();
					if (p)
					{
						Size += sizeof(int16) +	// Id
								sizeof(int32) +	// Size
								p->Size; // Data
					}
					break;
				}
			}
		}

		List<PropertyObject> l;
		if (GetObjects(l))
		{
			for (PropertyObject *i=l.First(); i; i=l.Next())
			{
				Size += sizeof(int16) +	// Id
						sizeof(int32) +	// Size
						i->Obj->Sizeof(); // Data
			}
		}

		return Size;
	}

	bool Serialize(LFile &f, bool Write)
	{
		ulong Magic = MAGIC_CALENDAR;

		if (Write)
		{
			// Get sub objects
			List<PropertyObject> l;
			GetObjects(l);

			// Write object header
			f << Magic;
			f << ((ulong) GetPropertyCount() + l.GetItems()); // number of fields following
		
			// Write properties
			for (bool b=FirstKey(); b; b=NextKey())
			{
				Prop *p = GetProp();
				if (p)
				{
					int16 Id = atoi(p->Name);
					int8 Type = p->Type;

					switch (p->Type)
					{
						case OBJ_INT:
						{
							f << Id;
							f << Type;
							f << p->Value.Int;
							break;
						}
						case OBJ_FLOAT:
						{
							f << Id;
							f << Type;
							f << p->Value.Dbl;
							break;
						}
						case OBJ_BINARY:
						case OBJ_STRING:
						{
							f << Id;
							f << Type;
							f << p->Size;
							f.Write(p->Value.Cp, p->Size);
							break;
						}
					}
				}
			}

			// Write objects
			for (PropertyObject *i=l.First(); i; i=l.Next())
			{
				WriteObjField(i->Id, *i->Obj);
			}
		}
		else // Read object
		{
			ulong ObjectId = 0;

			f >> ObjectId;
			if (Magic == ObjectId)
			{
				// Valid object id
				ulong Fields = 0;
				f >> Fields;
				for (int i=0; i<Fields; i++)
				{
					int16 Id;
					int8 Type;
					
					f >> Id;
					f >> Type;

					char IdStr[32];
					sprintf(IdStr, "%i", Id);

					switch (Type)
					{
						case OBJ_STRING:
						{
							int Len;
							f >> Len;
							char *s = new char[Len+1];
							if (s)
							{
								f.Read(s, Len);
								s[Len] = 0;
								Set(IdStr, s);
								DeleteArray(s);
							}
							break;
						}
						case OBJ_BINARY:
						{
							int Len;
							f >> Len;
							uchar *s = new uchar[Len];
							if (s)
							{
								f.Read(s, Len);
								Set(IdStr, s, Len);
								DeleteArray(s);
							}
							break;
						}
						case OBJ_INT:
						{
							int i;
							f >> i;
							Set(IdStr, i);
							break;
						}
						case OBJ_FLOAT:
						{
							double d;
							f >> d;
							Set(IdStr, d);
							break;
						}
						default:
						{
							break;
						}
					}
				}
			}
		}

		return true;
	}
};

#endif
