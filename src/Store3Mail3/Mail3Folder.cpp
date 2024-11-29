#include "Mail3.h"

////////////////////////////////////////////////////////////////////////////////
LMail3Def TblFolder[] =
{
	{"Id",			"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",	"INTEGER"},
	{"Name",		"TEXT"},
	{"Unread",		"INTEGER"},
	{"Open",		"INTEGER"},
	{"ItemType",	"INTEGER"},
	{"Sort",		"INTEGER"},
	{"Threaded",	"INTEGER"},
	{"SortIndex",	"INTEGER"},
	{"AccessPerm",	"INTEGER"},
	{0, 0}
};

LMail3Def TblFolderFlds[] =
{
	{"Id",			"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"ParentId",	"INTEGER"},
	{"Field",		"INTEGER"},
	{"Width",		"INTEGER"},
	{0, 0}
};

class TableTypes : public LHashTbl<IntKey<int>, const char*>
{
public:
	TableTypes(int ItemType)
	{
		if (ItemType)
		{
			#define AddTable(Type, Table) \
				if (ItemType == MAGIC_ANY || ItemType == Type) \
					Add(Type, Table);
			AddTable(MAGIC_MAIL, MAIL3_TBL_MAIL);
			AddTable(MAGIC_CONTACT, MAIL3_TBL_CONTACT);
			AddTable(MAGIC_GROUP, MAIL3_TBL_GROUP);
			AddTable(MAGIC_FILTER, MAIL3_TBL_FILTER);
			AddTable(MAGIC_CALENDAR, MAIL3_TBL_CALENDAR);
			
			// If this fails, you've added a new table/object type and need to add it above as well.
			LAssert(Length() > 0);
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
LMail3Folder::LMail3Folder(LMail3Store *store) : LMail3Obj(store)
{
}

LMail3Folder::~LMail3Folder()
{
	Sub.DeleteObjects();
	Items.DeleteObjects();
	Flds.DeleteObjects();
}

bool LMail3Folder::GenSizes()
{
	char Sql[256];
	sprintf_s(Sql, sizeof(Sql), "select Mail.Id, sum(length(data)) from " MAIL3_TBL_MAILSEGS " "
		"inner join " MAIL3_TBL_MAIL " "
		"on " MAIL3_TBL_MAIL ".Id = " MAIL3_TBL_MAILSEGS ".MailId "
		"where Mail.ParentId=" LPrintfInt64 " group by Mail.Id", Id);

	LMail3Store::LStatement s(Store);
	if (!s.Prepare(Sql))
		return false;

    LHashTbl<IntKey<int64>, int64> Sizes;
	while (s.Row())
	{
	    int64 Size = s.GetInt64(1);
	    if (Size >= 0)
	    {
    		Sizes.Add(s.GetInt64(0), Size);
    	}
    	// else LAssert(0); 
	}

	LDataStoreI::StoreTrans Trans = Store->StartTransaction();
	for (unsigned i=0; i<Items.Length(); i++)
	{
	    LMail3Mail *m = dynamic_cast<LMail3Mail*>(Items[i]);
	    if (m)
	    {
	        m->MailSize = Sizes.Find(m->Id);
	        if (m->MailSize >= 0)
	        {
	            sprintf_s(Sql, sizeof(Sql), "update " MAIL3_TBL_MAIL " set Size=" LPrintfInt64 " where Id=" LPrintfInt64, m->MailSize, m->Id);
	            if (!s.Prepare(Sql) ||
	                !s.Exec())
	                return false;
	        }
	        else
	        {
	            m->MailSize = 0;
	        }
	    }
	}

	return true;
}

Store3CopyImpl(LMail3Folder)
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

	Flds.State = Store3Loaded;
	Flds.Empty();
	LDataFolderI *Folder = dynamic_cast<LDataFolderI*>(&p);
	if (Folder)
	{
		LDataIterator<LDataPropI*> &it = Folder->Fields();
		for (LDataPropI *f = it.First(); f; f = it.Next())
		{
			Flds.Insert(new Store3Field(Store,
										(int)f->GetInt(FIELD_ID),
										(int)f->GetInt(FIELD_WIDTH)));
		}
	}

	return true;
}

bool LMail3Folder::Serialize(LMail3Store::LStatement &s, bool Write)
{
	LAssert(ParentId != 0);

	SERIALIZE_INT64(Id, 0);
	SERIALIZE_INT64(ParentId, 1);
	SERIALIZE_STR(Name, 2);
	SERIALIZE_INT(Unread, 3);
	SERIALIZE_INT(Open, 4);
	SERIALIZE_INT(ItemType, 5);
	SERIALIZE_INT(Sort, 6);
	SERIALIZE_INT(Threaded, 7);
	SERIALIZE_INT(SiblingIndex, 8);
	SERIALIZE_INT(AccessPerms, 9);

	char Sql[256];
	if (Write)
	{
		sprintf_s(Sql, sizeof(Sql), "delete from '%s' where ParentId=" LPrintfInt64, MAIL3_TBL_FOLDER_FLDS, Id);

		LDataStoreI::StoreTrans Trans = Store->StartTransaction();
		Store->Check(sqlite3_exec(Store->GetDb(), Sql, 0, 0, 0), Sql);
		LMail3Store::LInsert s(Store, MAIL3_TBL_FOLDER_FLDS);
		if (s.IsOk())
		{
			for (unsigned i=0; i<Flds.Length(); i++)
			{
				Store3Field *f = Flds.a[i];
				s.SetInt64(1, Id);
				s.SetInt(2, f->Id);
				s.SetInt(3, f->Width);
				s.Exec();
				s.Reset();
			}
		}
	}
	else
	{
		if (AccessPerms == -1)
			AccessPerms = 0;

		sprintf_s(Sql, sizeof(Sql), "select * from '%s' where ParentId=" LPrintfInt64, MAIL3_TBL_FOLDER_FLDS, Id);
		
		LMail3Store::LStatement s(Store, Sql);
		if (s.IsOk())
		{
			Flds.DeleteObjects();
			while (s.Row())
			{
				if (auto i = new Store3Field(GetStore(), s.GetInt(2), s.GetInt(3)))
					Flds.Insert(i, -1, true);
			}

			Flds.State = Store3Loaded;
		}
		
	    System = Store3SystemNone;
		if (ItemType == MAGIC_ANY)
		    System = Store3SystemTrash;
		else if (ItemType == MAGIC_CALENDAR)
		    System = Store3SystemCalendar;
		else if (ItemType == MAGIC_CONTACT)
		    System = Store3SystemContacts;
		
		if (ItemType == 0)
		{
			// We need a valid item type?
			if (!Stricmp(Name.Str(), "Spam"))
			{
				ItemType = MAGIC_ANY;
				
				sprintf_s(Sql, sizeof(Sql),
						"update " MAIL3_TBL_FOLDER " set ItemType=%i where Id=" LPrintfInt64,
						ItemType, Id);
				LMail3Store::LStatement s(Store, Sql);
				if (!s.Exec())
				{
					LgiTrace("%s:%i - Failed to fix ItemType.\n", _FL);
				}
			}
		}
	}

	return true;
}

uint32_t LMail3Folder::Type()
{
	return MAGIC_FOLDER;
}

bool LMail3Folder::IsOnDisk()
{
	return Id != 0;
}

bool LMail3Folder::IsOrphan()
{
	return false;
}

uint64 LMail3Folder::Size()
{
	return 0;
}

Store3Status LMail3Folder::Save(LDataI *Folder)
{
	if (Id < 0)
	{
		LMail3Folder *Fld = dynamic_cast<LMail3Folder*>(Folder);
		if (Fld)
		{
			Parent = Fld;
			Parent->Sub.Insert(this);
			ParentId = Fld->Id;
			LAssert(ParentId != 0);
			if (!Write(MAIL3_TBL_FOLDER, true))
				return Store3Error;

			Flds.State = Store3Loaded;
			
			LArray<LDataI*> New;
			New.Add(this);
			Store->OnNew(_FL, Parent, New, -1, false);
			
			return Store3Success;
		}
		else LAssert(!"No parent to save to.");
		return Store3Error;
	}

	return Write(MAIL3_TBL_FOLDER, false) ? Store3Success : Store3Error;
}

bool LMail3Folder::DbDelete()
{
	char Sql[256];
	LDataStoreI::StoreTrans Trans = Store->StartTransaction();

	// Delete folder's fields
	{
		sprintf_s(Sql, sizeof(Sql), "delete from '%s' where ParentId=" LPrintfInt64, MAIL3_TBL_FOLDER_FLDS, Id);
		LMail3Store::LStatement s(Store, Sql);
		if (!s.Exec())
			return false;
	}

	// Delete folder's objects
	if (!DeleteAllChildren())
		return false;

	// Delete folder record
	sprintf_s(Sql, sizeof(Sql), "delete from '%s' where Id=" LPrintfInt64, MAIL3_TBL_FOLDER, Id);
	LMail3Store::LStatement s(Store, Sql);
	if (!s.Exec())
		return false;

	return true;
}

Store3Status LMail3Folder::Delete(bool ToTrash)
{
	LArray<LDataI*> Del;
	Del.Add(this);
	return Store->Delete(Del, ToTrash);
}

LDataStoreI *LMail3Folder::GetStore()
{
	return Store;
}

LAutoStreamI LMail3Folder::GetStream(const char *file, int line)
{
	return LAutoStreamI(0);
}

bool LMail3Folder::SetStream(LAutoStreamI stream)
{
	LAssert(0);
	return false;
}

LDataIterator<LDataFolderI*> &LMail3Folder::SubFolders()
{
	if (Sub.State == Store3Unloaded)
	{
		LAssert(Id != -1);

		char Sql[256];
		sprintf_s(Sql, sizeof(Sql), "select * from '%s' where ParentId=" LPrintfInt64, MAIL3_TBL_FOLDER, Id);
		LMail3Store::LStatement s(Store, Sql);
		if (s.IsOk())
		{
			while (s.Row())
			{
				LMail3Folder *c;
				if ((c = new LMail3Folder(Store)))
				{
					if (c->Serialize(s, false))
					{
						c->Parent = this;
						Sub.Insert(c, -1, true);
					}
					else DeleteObj(c);
				}
			}

			Sub.State = Store3Loaded;
		}
	}

	return Sub;
}

const char *Mail3IdToName(int Id)
{
	switch (Id)
	{
		case FIELD_DATE_SENT:
			return "DateSent";
		case FIELD_DATE_RECEIVED:
			return "DateReceived";
		case FIELD_SUBJECT:
			return "Subject";
		case FIELD_FROM:
			return "FromAddr";
		case FIELD_TO:
			return 0; // field too complex to sort via SQL
		
		case FIELD_FIRST_NAME:
			return "First";
		case FIELD_LAST_NAME:
			return "Last";
		case FIELD_EMAIL:
			return "Email";

		case FIELD_FILTER_INDEX:
			return "FilterSort";
		case FIELD_FILTER_NAME:
			return "Name";

		default:
			LAssert(0);
			break;
	}

	return 0;
}

LDataIterator<LDataI*> &LMail3Folder::Children()
{
	if (Items.State == Store3Unloaded)
	{
		LAssert(Id != -1);
		LHashTbl<IntKey<int64>, LMail3Thing*> Load;
		for (unsigned i=0; i<Items.Length(); i++)
		{
			LMail3Thing *t = Items.a[i];
			Load.Add(t->Id, t);
		}

		char *SortCol = 0;
		TableTypes Tables(ItemType);
		// int Type;
		// for (const char *Tbl = Tables.First(&Type); Tbl; Tbl = Tables.Next(&Type))
		for (auto it : Tables)
		{
			char Sql[256];
			int Ch = sprintf_s(Sql, sizeof(Sql), "select * from '%s' where ParentId=" LPrintfInt64, it.value, Id);
			if (SortCol)
			{
				if (Sort > 0)
					sprintf_s(Sql+Ch, sizeof(Sql)-Ch, " order by %s",  SortCol);
				else
					sprintf_s(Sql+Ch, sizeof(Sql)-Ch, " order by %s desc",  SortCol);
			}

			LMail3Store::LStatement s(Store, Sql);
			if (s.IsOk())
			{
				LDataI *c;
				int NoSize = 0;
				int HasSize = 0;

				while (s.Row())
				{
					int Id = s.GetInt(0);
					if (!Load.Find(Id) &&
						(c = Store->Create(it.key)))
					{
						LMail3Thing *t = dynamic_cast<LMail3Thing*>(c);
						if (t)
						{
							if (t->Serialize(s, false))
							{
								t->Parent = this;
								if (t->Type() == MAGIC_MAIL)
								{
								    LMail3Mail *m = dynamic_cast<LMail3Mail*>(t);
								    LAssert(m != NULL);
								    if (m->MailSize <= 0)
								        NoSize++;
								    else
								        HasSize++;
								}
								Items.Insert(t, -1, true);
							}
							else DeleteObj(t);
						}
						else LAssert(0);
					}
				}
				
				Items.State = Store3Loaded;

				if (NoSize > 0 && HasSize == 0)
				    GenSizes();
			}
		}
	}

	return Items;
}

LDataIterator<LDataPropI*> &LMail3Folder::Fields()
{
	return Flds;
}

Store3Status LMail3Folder::FreeChildren()
{
	LArray<LDataI*> Lst;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		LMail3Thing *t = Items.a[i];
		if (t->UserData)
			Lst.Add(t);
	}
	if (!Store->OnDelete(_FL, this, Lst))
		return Store3Error;
	
	Items.DeleteObjects();
	Items.State = Store3Unloaded;
	return Store3Success;
}

Store3Status LMail3Folder::DeleteAllChildren()
{
	Store3Status Status = Store3Error;
	TableTypes Tbls(ItemType);

	LArray<LDataI*> Lst;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		LMail3Thing *t = Items.a[i];
		if (t->UserData)
		{
			Lst.Add(t);
		}
	}

	if (Lst.Length() == 0)
		return Store3Success;
		
	if (!Store->OnDelete(_FL, this, Lst))
		return Store3Error;

	for (auto it : Tbls)
	{
		LDataStoreI::StoreTrans Trans = Store->StartTransaction();
		char Sql[256];

		switch ((uint32_t)it.key)
		{
			case MAGIC_MAIL:
			{
				Status = Store3Success;

				#if 0
				// Delete mail's segments
				{
					sprintf_s(Sql, sizeof(Sql),
							"delete from " MAIL3_TBL_MAILSEGS " where Id in "
							"("
								"select " MAIL3_TBL_MAILSEGS ".Id from " MAIL3_TBL_MAILSEGS 
								", " MAIL3_TBL_MAIL " where " MAIL3_TBL_MAILSEGS ".ParentId=" MAIL3_TBL_MAIL 
								".Id and " MAIL3_TBL_MAIL ".ParentId=" LPrintfInt64
							");",
							Id);

					LMail3Store::LStatement s(Store, Sql);
					Status &= s.Exec();
				}
				#else
				for (unsigned i=0; i<Items.Length(); i++)
				{
					LMail3Thing *t = Items.a[i];
					if (t->Type() == MAGIC_MAIL)
					{
						sprintf_s(Sql, sizeof(Sql), "delete from " MAIL3_TBL_MAILSEGS " where MailId="  LPrintfInt64, t->Id);
						LMail3Store::LStatement s(Store, Sql);
						if (!s.Exec())
							Status = Store3Error;
					}
				}
				#endif

				// Delete mail
				{
					sprintf_s(Sql, sizeof(Sql), "delete from " MAIL3_TBL_MAIL " where ParentId=" LPrintfInt64, Id);
					LMail3Store::LStatement s(Store, Sql);
						if (!s.Exec())
							Status = Store3Error;
				}
				break;
			}
			default:
			{
				Status = Store3Success;

				sprintf_s(Sql, sizeof(Sql), "delete from %s where ParentId=" LPrintfInt64, it.value, Id);
				LMail3Store::LStatement s(Store, Sql);
				if (!s.Exec())
					Status = Store3Error;
				break;
			}
		}
	}

	return Status;
}

const char *LMail3Folder::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			return Name.Str();
	}

	LAssert(0);
	return 0;
}

Store3Status LMail3Folder::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			Name = str;
			return Store3Success;
	}

	LAssert(0);
	return Store3Error;
}

int64 LMail3Folder::GetInt(int id)
{
	switch (id)
	{
	    case FIELD_SYSTEM_FOLDER:
	        return System;
		case FIELD_SORT:
			return Sort;
		case FIELD_FOLDER_OPEN:
			return Open;
		case FIELD_UNREAD:
			return Unread;
		case FIELD_FOLDER_TYPE:
			return ItemType;
		case FIELD_FOLDER_THREAD:
			return Threaded;
		case FIELD_FOLDER_PERM_READ:
			return ReadPerm;
		case FIELD_FOLDER_PERM_WRITE:
			return WritePerm;
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
		case FIELD_IS_ONLINE:
			return true;
		case FIELD_FOLDER_INDEX:
			return SiblingIndex;
		case FIELD_LOADED:
		    return Items.State == Store3Loaded;
	}

	LAssert(0);
	return -1;
}

Store3Status LMail3Folder::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_SORT:
			Sort = (int)i; return Store3Success;
		case FIELD_FOLDER_OPEN:
			Open = (int)i; return Store3Success;
		case FIELD_UNREAD:
			Unread = (int)i; return Store3Success;
		case FIELD_FOLDER_TYPE:
			ItemType = (int)i; return Store3Success;
		case FIELD_FOLDER_THREAD:
			Threaded = (int)i; return Store3Success;
		case FIELD_FOLDER_PERM_READ:
			ReadPerm = (int16_t)i; return Store3Success;
		case FIELD_FOLDER_PERM_WRITE:
			WritePerm = (int16_t)i; return Store3Success;
		case FIELD_FOLDER_INDEX:
		{
			int NewIdx = (int)i;
			if (SiblingIndex == NewIdx)
				return Store3Error;

			if (Parent)
			{
				Parent->Sub.Delete(this);
				if (NewIdx < SiblingIndex)
					Parent->Sub.a.AddAt(NewIdx, this);
				else
					Parent->Sub.a.AddAt(NewIdx-1, this);

				LDataStoreI::StoreTrans Trans = Store->StartTransaction();
				for (unsigned i=0; i<Parent->Sub.a.Length(); i++)
				{
					auto f = Parent->Sub.a[i];
					if (i != f->SiblingIndex)
					{
						f->SiblingIndex = i;
						f->Save(NULL);
					}
				}
			}
			return Store3Success;
		}
		case FIELD_SYSTEM_FOLDER:
			System = (Store3SystemFolder) i;
			return Store3Success;
		case FIELD_LOADED:
		{
		    if (i)
		    {
		        Children();
		    }
		    else
		    {
    		    // Unload folder...
				Items.DeleteObjects();
				Items.State = Store3Unloaded;
			}
		    return Store3Success;
		}
	}

	LAssert(0);
	return Store3Error;
}

LMail3Folder *LMail3Folder::FindSub(char *Name)
{
	if (!Name)
		return 0;

	for (unsigned i=0; i<Sub.Length(); i++)
	{
		LMail3Folder *s = Sub.a[i];
		if (s->Name.Str() &&
			!_stricmp(s->Name.Str(), Name))
		{
			return s;
		}
	}

	return 0;
}
