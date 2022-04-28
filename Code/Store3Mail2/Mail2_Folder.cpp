#include "Store3Mail2.h"

#define OLD_FIELD_FOLDER_TYPE		1000
#define OLD_FIELD_FOLDER_NAME		1001
#define OLD_FIELD_UNREAD			1002
#define OLD_FIELD_SORT				1003

//////////////////////////////////////////////////////////////////////
FolderData::FolderData(GMail2Store *store)
{
	Debug = false;
	Kit = store;
	Sort = 0;
	UnRead = 0;
	Name = 0;
	Index = -1;
	Open = true;
	ReadAccess = WriteAccess = PermRequireNone;
	Threaded = false;
	ItemType = MAGIC_NONE;
	System = Store3SystemNone;
}

FolderData::~FolderData()
{
	// LStackTrace("%p::~FolderData\n", this);
	DeleteArray(Name);
}

LDataI &FolderData::operator =(LDataI &p)
{
	SetStr(FIELD_FOLDER_NAME, p.GetStr(FIELD_FOLDER_NAME));

	SetInt(FIELD_SORT, p.GetInt(FIELD_SORT));
	SetInt(FIELD_FOLDER_TYPE, p.GetInt(FIELD_FOLDER_TYPE));
	SetInt(FIELD_UNREAD, p.GetInt(FIELD_UNREAD));
	SetInt(FIELD_FOLDER_INDEX, p.GetInt(FIELD_FOLDER_INDEX));
	SetInt(FIELD_FOLDER_OPEN, p.GetInt(FIELD_FOLDER_OPEN));
	SetInt(FIELD_FOLDER_THREAD, p.GetInt(FIELD_FOLDER_THREAD));
	SetInt(FIELD_FOLDER_PERM_READ, p.GetInt(FIELD_FOLDER_PERM_READ));
	SetInt(FIELD_FOLDER_PERM_WRITE, p.GetInt(FIELD_FOLDER_PERM_WRITE));

	Field.Empty();
	LDataFolderI *Folder = dynamic_cast<LDataFolderI*>(&p);
	if (Folder)
	{
		GDataIterator<LDataPropI*> &it = Folder->Fields();
		for (LDataPropI *f = it.First(); f; f = it.Next())
		{
			Mail2Field *n = new Mail2Field(GetStore());
			if (n)
			{
				n->Id = (int)f->GetInt(FIELD_ID);
				n->Width = (int)f->GetInt(FIELD_WIDTH);
				Field.Insert(n);
			}
		}
	}

	return *this;
}

Store3Status FolderData::FreeChildren()
{
	// We can't really delete just the data items without taking out
	// the entire folder heirarchy as well, because they are in a list
	// together.
	return Store3Error;
}

Store3Status FolderData::DeleteAllChildren()
{
	Kit->Callback->OnDelete(this, (LArray<LDataI*>&)Things.a);

	if (Store)
		Store->DeleteAllChildren();
	else
		return Store3Error;

	Things.Empty();
	return Store3Success;
}

Store3Status FolderData::Save(LDataI *Folder)
{
	if (!Store)
	{
		FolderData *f = dynamic_cast<FolderData*>(Folder);
		if (f && f->Store)
		{
			StorageItem *n = f->Store->CreateSub(this);
			if (n)
			{
				f->Sub.Insert(this);
				Store = n;
				return Store3Success;
			}
			// else we have been deleted.
		}
	}
	else
	{
		return Store->Save() ? Store3Success : Store3Error;
	}

	LAssert(0);
	return Store3Error;
}

Store3Status FolderData::Delete()
{
	Store3Status Status = Store3Error;
	if (Store)
	{
		LArray<LDataI*> Lst;
		Lst.Add(this);
		if (!Kit->Callback || Kit->Callback->OnDelete(GetParent(), Lst))
		{
			Status = Store->GetTree()->SeparateItem(Store) ? Store3Success : Store3Error;
		}
	}
	else LAssert(0);

	return Status;
}

bool FolderData::Load(bool Flds, bool Thgs)
{
	if (!Store)
		return false;

	GHashTbl<void*, LDataI*> AlreadyLoaded;
	if (Thgs && Things.State == Unloaded && Things.Length() > 0)
	{
		// Some objects already in the container... happens when something
		// is moved into a folder that isn't loaded yet.
		for (unsigned i=0; i<Things.Length(); i++)
		{
			LgiTrace("Things.a[%i]->Store=%p\n", i, Things.a[i]->Store);
			AlreadyLoaded.Add(Things.a[i]->Store, Things[i]);
		}
	}

	for (StorageItem *c = Store->GetChild(); c; c = c->GetNext())
	{
		int Type = c->GetType();
		if (Type == MAGIC_FOLDER_OLD ||
			Type == MAGIC_FOLDER)
		{
			if (Flds && Sub.State == Unloaded)
			{
				FolderData *n = new FolderData(Kit);
				if (n)
				{
					n->Store = c;
					c->Object = n;
					Sub.Insert(n);

					LAutoPtr<LFile> f(c->GotoObject(_FL));
					if (f)
					{
						n->Serialize(*f, false);
					}
				}
			}
		}
		else if (Thgs && Things.State == Unloaded)
		{
			LDataI *di = AlreadyLoaded.Find(c);
			if (!di)
			{
				if ((di = Kit->Create(c->GetType())))
				{
					ThingData *n = dynamic_cast<ThingData*>(di);
					LAssert(n != NULL);
					if (n)
					{
						n->IsLoaded = false;
						n->Store = c;
						c->Object = n;

						Things.Insert(n);
					}
					else
					{
						DeleteObj(di);
					}
				}
			}
		}
	}

	if (Flds)
		Sub.State = Loaded;
	if (Thgs)
		Things.State = Loaded;

	return true;
}

GDataIterator<LDataFolderI*> &FolderData::SubFolders()
{
	Load(true, false);
	return Sub;
}

GDataIterator<LDataI*> &FolderData::Children()
{
	Load(false, true);
	return Things;
}

GDataIterator<LDataPropI*> &FolderData::Fields()
{
	return Field;
}

int FolderData::Type()
{
	return MAGIC_FOLDER;
}

char *FolderData::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
		{
			return Name;
		}
	}

	LAssert(0);
	return 0;
}

bool FolderData::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			_Str(Name);
	}

	LAssert(0);
	return false;
}

int64 FolderData::GetInt(int id)
{
	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
	    case FIELD_SYSTEM_FOLDER:
	        return System;
		case FIELD_SORT:
			return Sort;
		case FIELD_FOLDER_TYPE:
			return ItemType;
		case FIELD_UNREAD:
			return UnRead;
		case FIELD_FOLDER_OPEN:
			return Open;
		case FIELD_FOLDER_THREAD:
			return Threaded;
		case FIELD_FOLDER_PERM_READ:
			return ReadAccess;
		case FIELD_FOLDER_PERM_WRITE:
			return WriteAccess;
		case FIELD_FOLDER_INDEX:
			return Index;
	}

	LAssert(0);
	return -1;
}

bool FolderData::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_SORT:
			Sort = i;
			return true;
		case FIELD_FOLDER_TYPE:
			ItemType = i;
			return true;
		case FIELD_UNREAD:
			UnRead = i;
			return true;
		case FIELD_FOLDER_OPEN:
			Open = i;
			return true;
		case FIELD_FOLDER_THREAD:
			Threaded = i;
			return true;
		case FIELD_FOLDER_PERM_READ:
			ReadAccess = (ScribePerm)i;
			return true;
		case FIELD_FOLDER_PERM_WRITE:
			WriteAccess = (ScribePerm)i;
			return true;
		case FIELD_FOLDER_INDEX:
			Index = i;
			return true;
		case FIELD_SYSTEM_FOLDER:
			System = (Store3SystemFolder)i;
			return true;
	}

	LAssert(0);
	return false;
}

int FolderData::Sizeof()
{
	int Size =	sizeof(ulong) + // magic
				sizeof(ulong) + // # of items
				SizeIntField(ItemType) +
				SizeIntField(UnRead) + 
				SizeIntField(Sort) +
				SizeStrField(Name) +
				SizeIntField(Open) +
				SizeIntField((int&)ReadAccess) + 
				SizeIntField((int&)WriteAccess) + 
				SizeIntField(Threaded) + 
				SizeIntField(Index);
				
	for (unsigned i=0; i<Field.Length(); i++)
	{
		LAssert(Field[i] != NULL);
		Size += SizeObjField(*Field.a[i]);
	}

	if (Debug)
		LgiTrace("FolderData::Sizeof %s = %i (Fields=%i)\n", Name, Size, Field.Length());

	return Size;
}

bool FolderData::Serialize(LFile &f, bool Write)
{
	ulong Magic = MAGIC_FOLDER;
	
	if (Write)
	{
		int CalcSize = Sizeof();
		int64 Start = f.GetPos();
		int Fields = 9 + Field.Length();

		f << Magic;
		f << Fields;

		if (Debug)
			LgiTrace("FolderData::Write Fields=%i\n", Fields);

		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_TYPE, f.GetPos()-Start);
		WriteIntField(FIELD_FOLDER_TYPE, ItemType);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_NAME, f.GetPos()-Start);
		WriteStrField(FIELD_FOLDER_NAME, Name);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_UNREAD, f.GetPos()-Start);
		WriteIntField(FIELD_UNREAD, UnRead);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_SORT, f.GetPos()-Start);
		WriteIntField(FIELD_SORT, Sort);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_OPEN, f.GetPos()-Start);
		WriteIntField(FIELD_FOLDER_OPEN, Open);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_PERM_READ, f.GetPos()-Start);
		WriteIntField(FIELD_FOLDER_PERM_READ, (int&)ReadAccess);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_PERM_WRITE, f.GetPos()-Start);
		WriteIntField(FIELD_FOLDER_PERM_WRITE, (int&)WriteAccess);
		if (Debug) LgiTrace("    Writing %i @ " LGI_PrintfInt64 "\n", FIELD_FOLDER_THREAD, f.GetPos()-Start);
		WriteIntField(FIELD_FOLDER_THREAD, Threaded);
		WriteIntField(FIELD_FOLDER_INDEX, Index);

		for (unsigned i=0; i<Field.Length(); i++)
		{
			Mail2Field *o = Field.a[i];
			LAssert(o != NULL);
			if (Debug) LgiTrace("    Writing field %i @ " LGI_PrintfInt64 "\n", FIELD_COLUMN, f.GetPos()-Start);
			WriteObjField(FIELD_COLUMN, *o);
		}

		int64 End = f.GetPos();

		if (Debug)
			LgiTrace("FolderData::Write %s = %i (Fields=%i)\n", Name, (int)(End-Start), Field.Length());

		LAssert(End - Start == CalcSize);
	}
	else
	{
		int64 Base = f.GetPos();

		DeleteArray(Name);
		f >> Magic;

		if (Magic == MAGIC_FOLDER_OLD)
		{
			f >> ItemType;
			f >> UnRead;
			
			Name = ReadStr(f PassDebugArgs);

			if (!Store->EndOfObj(f))
			{
				goto FolderFieldListReader;
			}
		}
		else if (Magic == MAGIC_FOLDER)
		{
			FolderFieldListReader:
			Sort = 0;
			Field.DeleteObjects();

			ulong FieldCount = 0;
			f >> FieldCount;

			for (uint i=0; i < FieldCount; i++)
			{
				int64 Start = f.GetPos();
				short FieldId = 0;
				ulong FieldSize = 0;

				f >> FieldId;
				switch (FieldId)
				{
					ReadIntField(OLD_FIELD_FOLDER_TYPE, ItemType); // deprecated field ID
					ReadStrField(OLD_FIELD_FOLDER_NAME, Name); // deprecated field ID
					ReadIntField(OLD_FIELD_UNREAD, UnRead); // deprecated field ID
					ReadIntField(OLD_FIELD_SORT, Sort); // deprecated field ID

					ReadIntField(FIELD_FOLDER_TYPE, ItemType);
					ReadStrField(FIELD_FOLDER_NAME, Name);
					ReadIntField(FIELD_UNREAD, UnRead);
					ReadIntField(FIELD_SORT, Sort);
					ReadIntField(FIELD_FOLDER_INDEX, Index);

					ReadIntField(FIELD_FOLDER_OPEN, Open);
					ReadIntField(FIELD_FOLDER_PERM_READ, (int&)ReadAccess);
					ReadIntField(FIELD_FOLDER_PERM_WRITE, (int&)WriteAccess);
					ReadIntField(FIELD_FOLDER_THREAD, Threaded);
					case FIELD_COLUMN:
					{
						f >> FieldSize;
						
						Mail2Field *Item = new Mail2Field(GetStore());
						if (Item)
						{
							if (Item->Serialize(f, Write))
							{
								Field.Insert(Item);
							}
							else
							{
								printf("Couldn't read column in folder '%s'.\n", Name);
								DeleteObj(Item);
								SetDirty();
							}
						}
						else
						{
							f.Seek(FieldSize, SEEK_CUR);
						}
						break;
					}
					case 0:
					{
						// Truncated object?
						i = FieldCount;

						if (Store)
							Store->Save();
						break;
					}
					default:
					{
						// skip over unknown chunk
						f >> FieldSize;
						f.Seek(FieldSize, SEEK_CUR);
						if (Debug)
						{
							LgiTrace("Skipping field, size=%i\n", FieldSize);
						}
						break;
					}
				}

				#if 0
				if (Name && _stricmp(Name, "Trash") == 0)
					Debug = true;
				#endif
				if (Debug)
				{
					LgiTrace("Field %i, pos=" LGI_PrintfInt64 "-" LGI_PrintfInt64 ", size=%i, Offset=" LGI_PrintfInt64 "\n", FieldId, Start, f.GetPos(), (int)(f.GetPos()-Start), f.GetPos()-Base);
				}
			}

			// FIXME GetFolderPerms(ScribeReadAccess);
			// FIXME GetFolderPerms(ScribeWriteAccess);
		}
		else return false;
	}

	return f.GetStatus();
}

