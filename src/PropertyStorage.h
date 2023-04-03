#ifndef __PROPERTY_STORAGE_H
#define __PROPERTY_STORAGE_H

struct ItemFieldDef;

// Generic property list <-> storage
template <class a>
class PropertyStorage : public ObjProperties
{
	uint32 _ObjType;
	ItemFieldDef *_Fields;
	a *Object;

public:
	bool SetDirty(bool b = true) { return false; }

	PropertyStorage(int type, ItemFieldDef *fields)
	{
		_ObjType = type;
		_Fields = fields;
	}

	// Props
	ItemFieldDef *GetFields() { return _Fields; }
	ItemFieldDef *GetField(int Id)
	{
		for (int i=0; _Fields[i].Name(); i++)
		{
			if (_Fields[i].Id() == Id)
				return _Fields+i;
		}
		return 0;
	}

	// Overridables
	virtual bool GetObjects(List<LDataI> &l) { return false; }
	virtual LDataI *NewObject(int Type) { return 0; }
	virtual void OnSerialize(bool Write) {}

	// GStorageItem implementation
	int Type() { return _ObjType; }

	int Sizeof()
	{
		int Size =	sizeof(uint32) +				// magic
					sizeof(uint32);				// number of fields

		for (bool b=FirstKey(); b; b=NextKey())
		{
			switch (KeyType())
			{
				case OBJ_INT:
				{
					Size += sizeof(int16) +	// Id
							sizeof(int8) +	// Type
							sizeof(int32);	// Data
					break;
				}
				case OBJ_FLOAT:
				{
					Size += sizeof(int16) +	// Id
							sizeof(int8) +	// Type
							sizeof(double);	// Data
					break;
				}
				case OBJ_STRING:
				{
					Prop *p = GetProp();
					if (p)
					{
						Size += sizeof(int16) +			// Id
								sizeof(int8) +			// Type
								sizeof(int32) +			// Size
								strlen(p->Value.Cp);	// Data
					}
					break;
				}
				case OBJ_BINARY:
				{
					Prop *p = GetProp();
					if (p)
					{
						Size += sizeof(int16) +	// Id
								sizeof(int8) +	// Type
								sizeof(int32) +	// Size
								p->Size;		// Data
					}
					break;
				}
			}
		}

		return Size;
	}

	void SerializeUi(LView *Parent, bool Load)
	{
		for (ItemFieldDef *f = _Fields; f->FieldId; f++)
		{
			if (f->CtrlId > 0)
			{
				char Id[32];
				sprintf(Id, "%i", f->FieldId);

				if (Load)
				{
					// Props -> UI
					switch (f->Type)
					{
						case GV_STRING:
						{
							char *s = 0;
							Get(Id, s);
							Parent->SetCtrlName(f->CtrlId, s?s:(char*)"");
							break;
						}
						case GV_INT32:
						{
							int i = 0;
							Get(Id, i);
							Parent->SetCtrlValue(f->CtrlId, i);
							break;
						}
						case GV_DATETIME:
						{
							LDateTime d;
							if (d.Serialize(this, Id, false))
							{
								ItemFieldDef *Def = GetField(f->FieldId);
								if (Def && Def->UtcConvert)
									d.SetTimeZone(0, false);
								d.ToLocal();

								char Str[256];
								d.Get(Str, sizeof(Str));
								Parent->SetCtrlName(f->CtrlId, Str);
							}
							else
							{
								Parent->SetCtrlName(f->CtrlId, "");
							}
							break;
						}
						default:
						{
							LAssert(0);
							break;
						}
					}
				}
				else
				{
					// UI -> Props
					switch (f->Type)
					{
						case GV_STRING:
						{
							char *s = Parent->GetCtrlName(f->CtrlId);
							if (ValidStr(s))
							{
								Set(Id, s);
							}
							else
							{
								DeleteKey(Id);
							}
							break;
						}
						case GV_INT32:
						{
							int i = Parent->GetCtrlValue(f->CtrlId);
							Set(Id, i);
							break;
						}
						case GV_DATETIME:
						{
							char *s = Parent->GetCtrlName(f->CtrlId);
							if (ValidStr(s))
							{
								LDateTime d;
								if (d.Set(s))
								{
									ItemFieldDef *Def = GetField(f->FieldId);
									if (Def && Def->UtcConvert)
										d.ToUtc();

									d.Serialize(this, Id, true);
								}
							}
							else
							{
								DeleteKey(Id);
							}
							break;
						}
						default:
						{
							LAssert(0);
							break;
						}
					}
				}
			}
		}
	}
};

#endif
