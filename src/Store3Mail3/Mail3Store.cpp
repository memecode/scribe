#include "Mail3.h"
#include "resdefs.h"
#include "lgi/common/SubProcess.h"
#include <wchar.h>
#include "lgi/common/LgiRes.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Store3MimeTree.h"

#define SanityCheck() if (!IsOk()) return NULL;

///////////////////////////////////////////////////////////////////////////////////
LMail3Thing::~LMail3Thing()
{
	if (Parent)
		Parent->Items.a.Delete(this);
}

Store3Status LMail3Thing::Delete(bool ToTrash)
{
	LArray<LDataI*> Del;
	Del.Add(this);
	return Store->Delete(Del, ToTrash);
}

Store3Status LMail3Thing::Save(LDataI *Folder)
{
	LMail3Folder *Fld = dynamic_cast<LMail3Folder*>(Folder);
	if (Folder && !Fld)
	{
		LAssert(!"Not the right folder type");
		LgiTrace("%s:%i - Not the right folder type.\n", _FL);
		return Store3Error;
	}
	
	if (Fld && Fld->Store != Store)
	{
		SetStore(Fld->Store);
	}
	
	const char *Table = GetTable();
	bool Create = false;
	bool IsNewMail = Type() == MAGIC_MAIL && TestFlag(GetInt(FIELD_FLAGS), MAIL_NEW);
	LArray<LDataI*> NewItems;

	if (Id < 0)
	{
		if (Fld)
		{
			// Make sure the folder is loaded...
			Fld->Children();

			Parent = Fld;
			ParentId = Fld->Id;
			LAssert(Parent->Items.IndexOf(this, true) < 0);
			
			// LgiTrace("%s:%i - Saving %i to %s (%i items)\n", _FL, (int)Id, Parent->GetStr(FIELD_FOLDER_NAME), Parent->Items.Length());
			Parent->Items.Insert(this, -1, true);
			
			Create = true;

			NewItems.Add(this);

			if (IsNewMail)
				SetInt(FIELD_FLAGS, GetInt(FIELD_FLAGS) & ~MAIL_NEW);
		}
		else
		{
			LAssert(!"No parent to save to.");
			LgiTrace("%s:%i - No parent to save to.\n", _FL);
			return Store3Error;
		}
	}
	else if (Fld)
	{
		// Make sure the folder is loaded...
		Fld->Children();

		ParentId = Fld->Id;
		if (Parent != Fld)
		{
			LDataFolderI *Old = Parent;
			if (Parent)
			{
				// Remove this item from the current parent folder...
				LAssert(Parent->Items.IndexOf(this) >= 0);
				Parent->Items.Delete(this);
			}
			
			// Add it to the new one...
			Parent = Fld;
			LAssert(Parent->Items.IndexOf(this, true) < 0);

// LgiTrace("%s:%i - Saving %i to %s (%i items)\n", _FL, (int)Id, Parent->GetStr(FIELD_FOLDER_NAME), Parent->Items.Length());
			Parent->Items.Insert(this, -1, true);

			LArray<LDataI*> a;
			a.Add(this);
			if (Old)
				Store->OnMove(_FL, Parent, Old, a);
			else
				NewItems.Add(this);

			if (IsNewMail)
				SetInt(FIELD_FLAGS, GetInt(FIELD_FLAGS) & ~MAIL_NEW);
		}
	}
	
	bool Status = Write(Table, Create);
	if (Status)
	{
		OnSave();
		if (NewItems.Length())
			Store->OnNew(_FL, Parent, NewItems, -1, IsNewMail);
	}
	else
	{
		LgiTrace("%s:%i - Write failed.\n", _FL);
	}

	return Status ? Store3Success : Store3Error;
}

LDataPropI *LMail3Thing::GetObj(int id)
{
	switch (id)
	{
		case FIELD_PARENT:
			return Parent;
	}
	LAssert(0);
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////
LMail3Idx Mail3Indexes[] =
{
	{"MailFolderIdx", MAIL3_TBL_MAIL, "ParentId"},
	{"MailSegMailIdx", MAIL3_TBL_MAILSEGS, "MailId"},
	{"MailSegParentIdx", MAIL3_TBL_MAILSEGS, "ParentId"},
};

LMail3Store::LMail3Store(const char *Mail3Folder, LDataEventsI *callback, bool Create) :
	TableStatus(0, Store3Error)
{
	Format = Mail3v1;
	Folder = Mail3Folder;
	Callback = callback;
	Db = 0;
	Transaction = 0;
	Root = 0;
	OpenStatus = Store3Success;
	Fields.Add(MAIL3_TBL_FOLDER, TblFolder);
	Fields.Add(MAIL3_TBL_FOLDER_FLDS, TblFolderFlds);
	Fields.Add(MAIL3_TBL_MAIL, TblMail);
	Fields.Add(MAIL3_TBL_MAILSEGS, TblMailSegs);
	Fields.Add(MAIL3_TBL_CONTACT, TblContact);
	Fields.Add(MAIL3_TBL_GROUP, TblGroup);
	Fields.Add(MAIL3_TBL_FILTER, TblFilter);
	Fields.Add(MAIL3_TBL_CALENDAR, TblCalendar);
	Fields.Add(MAIL3_TBL_CALENDAR_FILES, TblCalendarFiles);

	bool Exist = LDirExists(Mail3Folder);
	if (!Create && !Exist)
	{
		ErrorMsg.Printf(LLoadString(IDS_ERROR_FILE_DOESNT_EXIST), Mail3Folder);
	}
	else if (!Exist && !FileDev->CreateFolder(Mail3Folder))
	{
		ErrorMsg.Printf(LLoadString(IDS_ERROR_CANT_CREATE_FOLDER), Mail3Folder);
	}
	else
	{
		OpenDb();
	}

	#if _DEBUG
	LMail3Mail m(this);
	Store3MimeTree<LMail3Store, LMail3Mail, LMail3Attachment> Tree(&m, m.Seg);
	if (!Tree.UnitTests(this, &m))
	{
		LgiTrace("%s:%i - Error: Store3MimeTree unit tests failed\n", _FL);
		LAssert(!"Store3MimeTree unit tests failed.\n");
	}
	#endif
}

LMail3Store::~LMail3Store()
{
	CloseDb();
	DeleteObj(Root);
}

LMail3Folder *LMail3Store::GetSystemFolder(int Type)
{
	if (!Callback || !Root)
		return NULL;

	LVariant Path;
	if (Callback->GetSystemPath(FOLDER_INBOX, Path))
	{
		auto p = LString(Path.Str()).Split("/");
		for (auto f: Root->Sub.a)
		{
			auto Nm = f->GetStr(FIELD_FOLDER_NAME);
			if (p.Last().Equals(Nm))
			{
				return f;
			}
		}
	}
	
	return NULL;
}

LDataPropI *LMail3Store::GetObj(int id)
{
	switch (id)
	{
		case FIELD_INBOX:
			return GetSystemFolder(FOLDER_INBOX);
		case FIELD_OUTBOX:
			return GetSystemFolder(FOLDER_OUTBOX);
		case FIELD_SENT:
			return GetSystemFolder(FOLDER_SENT);
		case FIELD_TRASH:
			return GetSystemFolder(FOLDER_TRASH);
	}
	
	return NULL;
}

bool LMail3Store::OpenDb()
{
	char f[MAX_PATH_LEN];
	
	StatusMsg.Empty();
	
	LMakePath(f, sizeof(f), Folder, MAIL3_DB_FILE);
	DbFile = f;
	if (!Check(sqlite3_open(f, &Db), 0))
	{
		ErrorMsg.Printf(LLoadString(IDS_ERROR_CANT_CREATE_DB), f);
		return false;
	}
	else
	{
		// const char *Tbl;
		// for (LMail3Def *Flds=Fields.First(&Tbl); Flds; Flds=Fields.Next(&Tbl))
		for (auto it : Fields)
		{
			Store3Status s = CheckTable(it.key, it.value);
			if (s == Store3Missing &&
				UpdateTable(it.key, it.value, s))
			{
				s = Store3Success;
			}

			TableStatus.Add(it.key, s);
			if (s == Store3Error ||
				(s == Store3UpgradeRequired && OpenStatus != Store3Error))
				OpenStatus = s;
		}
		
		LArray<LAutoString> DelNames;
		{
			LStatement Tables(	this,
								"SELECT name FROM sqlite_master "
								"WHERE type='table' "
								"ORDER BY name;");
			while (Tables.Row())
			{
				char *Name = Tables.GetStr(0);
				if (Name && strchr(Name, '_') && !stristr(Name, "sqlite"))
					DelNames.New().Reset(NewStr(Name));
			}
		}
		for (unsigned i=0; i<DelNames.Length(); i++)
		{
			char Sql[256];
			sprintf_s(Sql, sizeof(Sql), "drop table %s", DelNames[i].Get());
			LStatement Del(this, Sql);
			if (!Del.Exec())
			{
				LAssert(!"Drop table failed.");
				return false;
			}
		}

		LDataStoreI::StoreTrans Trn = StartTransaction();
		for (int i=0; i<CountOf(Mail3Indexes); i++)
		{
			sprintf_s(f, sizeof(f),
					"create index if not exists %s on %s(%s)",
					Mail3Indexes[i].IdxName,
					Mail3Indexes[i].Table,
					Mail3Indexes[i].Column);
			LStatement s(this, f);
			s.Exec();
		}
	}
	
	return true;
}

bool LMail3Store::CloseDb()
{
	DeleteObj(Transaction);
	if (Db)
	{
		int r = sqlite3_close(Db);
		if (r == SQLITE_BUSY)
		{
			sqlite3_stmt *n;
			while ((n = sqlite3_next_stmt(Db, 0)))
			{
				LgiTrace("close n=%p\n", n);
				sqlite3_finalize(n);
			}
			LAssert(!"Unfinalized statements");
			Db = NULL;
			return false;
		}
		else Check(r, 0);

		Db = NULL;
	}
	
	return true;
}

LMail3Store::Mail3Trans::Mail3Trans(LMail3Store *s, LMail3Store::Mail3Trans **ptr)
	: LDataStoreI::LDsTransaction(s), Trans(s)
{
	Ptr = ptr;
	*Ptr = this;
}

LMail3Store::Mail3Trans::~Mail3Trans()
{
	*Ptr = 0;
}

LDataStoreI::StoreTrans LMail3Store::StartTransaction()
{
	StoreTrans t;
	if (!Transaction)
	{
		t.Reset(new Mail3Trans(this, &Transaction));
	}
	return t;
}

void LMail3Store::OnNew(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
{
	if (!Callback)
		return;
	Callback->SetContext(File, Line);
	Callback->OnNew(parent, new_items, pos, is_new);
}

bool LMail3Store::OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnChange(items, FieldHint);
}

bool LMail3Store::OnMove(const char *File, int Line, LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnMove(new_parent, old_parent, items);
}

bool LMail3Store::OnDelete(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &items)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnDelete(parent, items);
}

const char *LMail3Store::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
			return DbFile;
		case FIELD_ERROR:
			return ErrorMsg;
		case FIELD_STATUS:
			return StatusMsg;
		case FIELD_TEMP_PATH:
			return TempPath;
		case FIELD_STORE_TYPE:
			return "LMail3Store";
	}
	
	return NULL;
}

Store3Status LMail3Store::SetStr(int id, const char *s)
{
	switch (id)
	{
		case FIELD_ERROR:
			ErrorMsg = s;
			break;
		case FIELD_STATUS:
			StatusMsg = s;
			break;
		case FIELD_TEMP_PATH:
			TempPath = s;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}
	
	return Store3Success;
}

int64 LMail3Store::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STATUS:
		{
			if (OpenStatus != Store3Success)
				return OpenStatus;

			return IsOk() ? Store3Success : Store3Error;
		}
		case FIELD_READONLY:
			return false;
		case FIELD_VERSION:
			return 3;
		case FIELD_FORMAT:
			return Format;
		case FIELD_STORE_TYPE:
			return Store3Sqlite;
	}

	return -1;
}

Store3Status LMail3Store::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_FORMAT:
			break;
	}

	return Store3Error;
}

bool LMail3Store::Check(int Code, const char *Sql)
{
	if (Code == SQLITE_OK ||
		Code == SQLITE_DONE)
		return true;

	const char *Err = sqlite3_errmsg(Db);
	LgiTrace("%s:%i - Sqlite error %i: %s\n%s",
		_FL,
		Code, Err,
		Sql?Sql:(char*)"",
		Sql?"\n":"");

	// LAssert(!"Db Error");

	#if MAIL3_TRACK_OBJS
	for (int i=0; i<All.Length(); i++)
	{
		SqliteObjs &s = All[i];
		if (s.Stat)
		{
			int asd=0;
		}
		else if (s.Stream)
		{
			int asd=0;
		}
	}
	#endif

	return false;
}

uint64 LMail3Store::Size()
{
	return LFileSize(DbFile);
}

LDataI *LMail3Store::Create(int Type)
{
	switch ((uint32_t)Type)
	{
		case MAGIC_FOLDER:
			return new LMail3Folder(this);
		case MAGIC_MAIL:
			return new LMail3Mail(this);
		case MAGIC_FILTER:
			return new LMail3Filter(this);
		case MAGIC_CALENDAR:
			return new LMail3Calendar(this);
		case MAGIC_CALENDAR_FILE:
			return new LMail3CalendarFile(this);
		case MAGIC_ATTACHMENT:
			return new LMail3Attachment(this);

		case MAGIC_CONTACT:
			return new LMail3Contact(this);
		case MAGIC_GROUP:
			return new LMail3Group(this);
	}

	LAssert(0);
	return 0;
}

LDataFolderI *LMail3Store::GetRoot(bool create)
{
	SanityCheck()

	if (!Root)
	{
		LStatement s(this, "select * from '" MAIL3_TBL_FOLDER "' where ParentId is null");
		if (s.IsOk())
		{
			if (s.Row())
			{
				// Load existing root folder...
				if ((Root = new LMail3Folder(this)))
				{
					Root->Items.State = Store3Loaded;
					Root->Serialize(s, false);
				}
			}
			else
			{
				// Create new root folder...
				if ((Root = new LMail3Folder(this)))
				{
					Root->Items.State = Store3Loaded;
					Root->Write(MAIL3_TBL_FOLDER, true);
				}
			}
		}
	}

	return Root;
}

#define PROFILE_MOVE		0
#if PROFILE_MOVE
	#define PROF_MOVE(...)	prof.Add(__VA_ARGS__)
#else
	#define PROF_MOVE(...)
#endif

Store3Status LMail3Store::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	Store3Status Status = Store3Error;

	LMail3Folder *To = dynamic_cast<LMail3Folder*>(NewFolder);
	if (!To)
		return Status;
	if (Items.Length() == 0)
		return Store3Success;

#if PROFILE_MOVE
LProfile prof("LMail3Store::Move");
#endif
	LDataFolderI *OldParent = NULL;
	LArray<LDataI*> Moved;
	StoreTrans Tr = StartTransaction();

	for (unsigned n=0; n<Items.Length(); n++)
	{
		LMail3Thing *Thing = 0;
		LMail3Folder *Fld = dynamic_cast<LMail3Folder*>(Items[n]);
		if (Fld)
		{
PROF_MOVE("0");
			if (Fld->ParentId == To->Id)
				Status = Store3Success;
			else
			{
				char s[256];
				sprintf_s(s, sizeof(s), "update " MAIL3_TBL_FOLDER " set ParentId=" LPrintfInt64 " where Id=" LPrintfInt64, To->Id, Fld->Id);
PROF_MOVE("1");
				LStatement Stmt(this, s);
				if (Stmt.Exec())
				{
PROF_MOVE("2");
					Status = Store3Success;
					LAssert(Fld->Parent->Sub.IndexOf(Fld) >= 0);

					Fld->Parent->Sub.Delete(Fld);
					LDataFolderI *From = Fld->Parent;
					Fld->Parent = To;
					Fld->ParentId = Fld->Parent->Id;
					To->Sub.Insert(Fld);

					if (!OldParent) OldParent = From;
					if (Callback && OldParent && Moved.Length() && OldParent != From)
					{
PROF_MOVE("3");
						Callback->OnMove(To, OldParent, Moved);
PROF_MOVE("4");
						Moved.Length(0);
						OldParent = From;
					}							
					Moved.Add(Fld);
				}
			}
		}
		else if ((Thing = dynamic_cast<LMail3Thing*>(Items[n])))
		{
PROF_MOVE("5");
			if (Thing->ParentId == To->Id)
				Status = Store3Success;
			else if (To->ItemType != MAGIC_ANY &&
						Thing->Type() != To->ItemType)
			{
				LgiTrace("%s:%i - Can't move item (type=%x) to folder containing type %x.\n", _FL, Thing->Type(), To->ItemType);
				Status = Store3Error;
			}
			else
			{
				// This needs to be before the SQL update so that duplicate Mail 
				// objects don't get created when moving to an unloaded folder.
				if (To->Items.GetState() != Store3Loaded)
					To->Children();
					
PROF_MOVE("6");
				char s[256];
				sprintf_s(s, sizeof(s), "update %s set ParentId=" LPrintfInt64 " where Id=" LPrintfInt64, Thing->GetTable(), To->Id, Thing->Id);
				LStatement Stmt(this, s);
				if (Stmt.Exec())
				{
PROF_MOVE("6");
					Status = Store3Success;
					LDataFolderI *From = Thing->Parent;
					if (Thing->Parent)
					{
						LAssert(Thing->Parent->Items.IndexOf(Thing) >= 0);
						Thing->Parent->Items.Delete(Thing);
					}

					Thing->Parent = To;
					Thing->ParentId = To->Id;
						
					LAssert(To->Items.IndexOf(Thing) < 0);
						
					#ifdef _DEBUG
					if (To->System != Store3SystemTrash)
					{
						// Check there is no duplicate ID..
						for (unsigned k=0; k<To->Items.a.Length(); k++)
						{
							if (To->Items.a[k]->Id == Thing->Id)
							{
								LAssert(!"Can't have duplicate IDs in the same folder.");
							}
						}
					}
					#endif						
						
// LgiTrace("%s:%i - Saving %p:%i to %s (%i items)\n", _FL, Thing, (int)Thing->Id, To->GetStr(FIELD_FOLDER_NAME), To->Items.Length());
					To->Items.Insert(Thing);

					if (!OldParent) OldParent = From;
					if (Callback && OldParent && Moved.Length() && OldParent != From)
					{
PROF_MOVE("7");
						Callback->OnMove(To, OldParent, Moved);
PROF_MOVE("8");
						Moved.Length(0);
						OldParent = From;
					}							
					Moved.Add(Thing);
				}
			}
		}
	}

	if (Callback && Moved.Length())
		Callback->OnMove(To, OldParent, Moved);

	return Status;
}

Store3Status LMail3Store::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	if (Items.Length() == 0)
		return Store3Error;

	LVariant FolderName;
	LMail3Folder *Trash = 0;
	if (ToTrash && Callback->GetSystemPath(FOLDER_TRASH, FolderName))
	{
		auto t = LString(FolderName.Str()).SplitDelimit("/");
		if (t.Length() == 1)
			Trash = Root->FindSub(t[0]);
	}

	if (Trash)
	{
		LArray<LDataI*> MoveItems;
		for (unsigned i=0; i<Items.Length(); i++)
		{
			LDataI *di = Items[i];
			
			LMail3Thing *t = dynamic_cast<LMail3Thing*>(di);
			if (t && t->Parent != Trash)
			{
				// Move the item to the trash instead...
				MoveItems.Add(di);
				Items.DeleteAt(i--, true);
			}
			else
			{			
				LMail3Folder *f = dynamic_cast<LMail3Folder*>(di);
				if (f && f->Parent != Trash)
				{
					// Move the folder to the trash instead...
					MoveItems.Add(di);
					Items.DeleteAt(i--, true);
				}
			}
		}
		
		if (MoveItems.Length() > 0)
		{
			Store3Status s = Move(Trash, MoveItems);
			
			if (s != Store3Success)
				return s;
		}
		
		if (Items.Length() == 0)
			return Store3Success;
	}

	Store3Status s = Store3Success;
	LDataI *Item = Items[0];
	switch ((uint32_t)Item->Type())
	{
		default:
		{
			LMail3Thing *t = dynamic_cast<LMail3Thing*>(Item);
			if (!t)
			{
				LAssert(!"What are you trying to do?");
				s = Store3Error;
			}
			else if (Callback && !Callback->OnDelete(t->Parent, Items))
			{
				s = Store3Error;
			}
			else
			{
				for (unsigned i=0; i<Items.Length(); i++)
				{
					if ((t = dynamic_cast<LMail3Thing*>(Items[i])))
					{
						if (t->DbDelete())
						{
							if (t->Parent)
								t->Parent->Items.Delete(t);
							else
								LAssert(0);
							DeleteObj(t);
						}
						else s = Store3Error;
					}
				}
			}
			break;
		}
		case MAGIC_ATTACHMENT:
		{
			for (auto i: Items)
			{
				LMail3Attachment *a = dynamic_cast<LMail3Attachment*>(i);
				if (a)
				{
					a->Delete(ToTrash);
					a->Detach();
					delete a;
				}
			}
			break;
		}
		case MAGIC_FOLDER:
		{
			LMail3Folder *f = dynamic_cast<LMail3Folder*>(Item);
			if (!f)
				s = Store3Error;
			else if (Callback && !Callback->OnDelete(f->Parent, Items))
				s = Store3Error;
			else
			{
				for (unsigned i=0; i<Items.Length(); i++)
				{
					if ((f = dynamic_cast<LMail3Folder*>(Items[i])))
					{
						if (f->DbDelete())
						{
							if (f->Parent)
								f->Parent->Sub.Delete(f);
							else
								LAssert(0);
							DeleteObj(f);
						}
						else s = Store3Error;
					}
				}
			}
			break;
		}
	}

	return s;
}

Store3Status LMail3Store::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator)
{
	if (Items.Length() == 0)
		return Store3Success;

	if (PropId != FIELD_FLAGS)
	{
		LAssert(!"Not impl.");
		return Store3NotImpl;
	}

	int32 Val = Value.CastInt32();
	if (!Val)
	{
		LAssert(!"One flag must be set");
		return Store3Error;
	}

	if (Operator != OpPlusEquals &&
		Operator != OpMinusEquals)
	{
		LAssert(!"Operator not supported.");
		return Store3Error;
	}

	StoreTrans Tr = StartTransaction();

	// Set/unset flag
	for (unsigned i=0; i<Items.Length();)
	{
		LStringPipe p;
		p.Print("update " MAIL3_TBL_MAIL " set ");

		if (Operator == OpMinusEquals)
			// Unset flag
			p.Print("Flags=Flags&%i where ", ~Val);
		else
			// Set flag
			p.Print("Flags=Flags|%i where ", Val);

		LArray<LDataI*> Chunk;
		for (unsigned n=0; n<100 && i<Items.Length(); n++, i++)
		{
			auto m = dynamic_cast<LMail3Mail*>(Items[i]);
			if (!m)
			{
				LAssert(!"Invalid object.");
				continue;
			}
			if
			(
				(Operator == OpMinusEquals && (m->Flags & Val))
				||
				(Operator == OpPlusEquals && !(m->Flags & Val))
			)
			{
				p.Print("%sId=" LPrintfInt64, Chunk.Length()?" or ":"", m->Id);
				Chunk.Add(m);
			}
		}

		if (Chunk.Length())
		{
			auto Sql = p.NewLStr();
			LStatement s(this, Sql);
			if (!s.Exec())
				return Store3Error;

			for (unsigned n=0; n<Chunk.Length(); n++)
			{
				LMail3Mail *m = dynamic_cast<LMail3Mail*>(Chunk[n]);
				if (m)
				{
					if (Operator == OpMinusEquals)
						m->Flags &= ~Val;
					else
						m->Flags |= Val;
				}
			}

			OnChange(_FL, Chunk, PropId);
		}
	}

	return Store3Success;
}

bool LMail3Store::SetFormat(LViewI *Parent, LDataPropI *Props)
{
	if (!Parent || !Props)
		return false;

	Mail3SubFormat NewFormat = (Mail3SubFormat)Props->GetInt(Store3UiNewFormat);
	if (NewFormat == Format)
		return true;
	
	bool Error = false;
	LStatement Folders(this, "select * from " MAIL3_TBL_FOLDER);
	char Sql[256];

	LStatement Count(this, "select Count(*) as c from " MAIL3_TBL_FOLDER);
	if (Count.Row())
	{
		char *c = Count.GetStr(0);
		if (c)
			Props->SetInt(Store3UiMaxPos, atoi(c));
	}

	if (NewFormat == Mail3v1)
	{
		// Change format to single table for all email
		
		// Create the mail table
		Store3Status s = CheckTable(MAIL3_TBL_MAIL, TblMail);
		if (!UpdateTable(MAIL3_TBL_MAIL, TblMail, s))
			Error = true;
		else
		{
			// Copy all the sub-folder mail into that one table...
			while (Folders.Row())
			{
				int64 Id = Folders.GetInt64(0);

				char TblName[128];
				sprintf_s(TblName, sizeof(TblName), "Mail_" LPrintfInt64, Id);

				// Set the parent ID on all mail rows in the sub-folder table (just to be sure)
				sprintf_s(Sql, sizeof(Sql), "update %s set ParentId=" LPrintfInt64, TblName, Id);
				LStatement SetId(this, Sql);
				if (!SetId.Exec())
				{
					Error = true;
					break;
				}

				// Now copy all the rows into the main Mail table
				sprintf_s(Sql, sizeof(Sql), "insert into " MAIL3_TBL_MAIL " select * from %s", TblName);
				LStatement Copy(this, Sql);
				if (!Copy.Exec())
				{
					Error = true;
					break;
				}

				// And drop the sub-folder table..				
				sprintf_s(Sql, sizeof(Sql), "drop table if exists %s", TblName);
				LStatement Del(this, Sql);
				if (!Del.Exec())
				{
					Error = true;
					break;
				}
			}
		}
	}
	else if (NewFormat == Mail3v2)
	{
		// Change format to a table per folder of email
		{
			LTransaction Trans(this);
			int Pos = 0;
			while (Folders.Row())
			{
				int64 Id = Folders.GetInt64(0);
				
				char TblName[128];
				sprintf_s(TblName, sizeof(TblName), "Mail_" LPrintfInt64, Id);
				
				sprintf_s(Sql, sizeof(Sql), "create table if not exists %s as select * from Mail where ParentId=" LPrintfInt64, TblName, Id);
				LStatement Convert(this, Sql);
				if (!Convert.Exec())
				{
					Error = true;
					Trans.RollBack();
					break;
				}

				Props->SetInt(Store3UiCurrentPos, ++Pos);
			}
		}

		sprintf_s(Sql, sizeof(Sql), "drop table if exists " MAIL3_TBL_FOLDER);
		LStatement Del(this, Sql);
		if (!Del.Exec())
			Error = true;
	}

	if (!Error)
		Format = NewFormat;
	
	return !Error;
}

void LMail3Store::Upgrade(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus)
{
	bool Status = true;

	LStringPipe p;

	for (auto it : Fields)
	{
		Store3Status s = TableStatus.Find(it.key);
		if (s == Store3UpgradeRequired)
		{
			if (!UpdateTable(it.key, it.value, s))
			{
				p.Print("Failed to upgrade %s\n", it.key);
				Status = false;
			}
		}
	}

	if (Props && !Status)
	{
		LAutoString a(p.NewStr());
		Props->SetStr(Store3UiError, a);
	}

	if (OnStatus)
		OnStatus(Status);
}

class SqliteRepairThread : public LThread
{
	LMail3Store *Store;
	LString Exe;
	LString Db;
	LString RepairSql;
	LString OldDb;
	
	LViewI *Parent = NULL;
	LDataPropI *Props = NULL;
	LAutoPtr<LSubProcess> Shell;
	ssize_t Ch = 0;
	char Line[512] = {};

public:
	bool Status = false;

	SqliteRepairThread(LMail3Store *store, LString exe, LString db, LViewI *parent, LDataPropI *props) :
		Store(store),
		LThread("SqliteRepairThread")
	{
		Exe = exe;
		Db = db;
		Parent = parent;
		Props = props;
		
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Db, "..\\Repair.sql");
		for (char *c = p; *c; c++)
		{
			if (*c == '\\')
				*c = '/';
		}
		RepairSql = p;
		
		LString s;
		LDateTime dt;
		dt.SetNow();
		char sNow[64];
		dt.Get(sNow, sizeof(sNow));
		for (char *c = sNow; *c; c++)
		{
			if (*c == ':' || *c == '/')
				*c = '-';
		}
		
		s.Printf("..\\Database %s.sqlite", sNow);
		LMakePath(p, sizeof(p), Db, s);
		OldDb = p;
		
		LgiTrace("Sqlite Repair paths:\n"
				"Exe: '%s'\n"
				"Db: '%s'\n"
				"Old: '%s'\n"
				"Sql: '%s'\n",
				Exe.Get(),
				Db.Get(),
				OldDb.Get(),
				RepairSql.Get());
		
		Run();
	}
	
	char *Read()
	{
		Ch = Shell->Read(Line, sizeof(Line)-1);
		if (Ch < 0)
			return NULL;
		Line[Ch] = 0;
		return Line;
	}
	
	bool GetPrompt()
	{
		char *l;
		while ((l = Read()))
		{
			if (stristr(l, "sqlite>"))
				return true;
		}

		if (Props)
			Props->SetStr(Store3UiError, "Failed to get prompt...");
		return false;
	}
	
	void StatusMsg(const char *msg)
	{
		if (Props)
		{
			int64 p = Props->GetInt(Store3UiCurrentPos);
			Props->SetInt(Store3UiCurrentPos, p + 1);
			Props->SetStr(Store3UiStatus, msg);
		}
	}

	int OnStatus(int s)
	{
		// Send status event back to the store...
		auto msg = new LMail3StoreMsg(LMail3StoreMsg::MsgRepairComplete);
		msg->Int = s;
		Store->PostStore(msg);

		return s;
	}
	
	int Main()
	{
		if (Props)
		{
			Props->SetInt(Store3UiMaxPos, 7);
			Props->SetStr(Store3UiStatus, "Starting sqlite...");
		}

		// Open the shell and start the export process
		LString Args;
		Args.Printf("-interactive %s", Db.Get());
		if (!Shell.Reset(new LSubProcess(Exe, Args)))
			return OnStatus(-1);
		
		if (!Shell->Start(true, true))
		{
			if (Props)
				Props->SetStr(Store3UiError, "Couldn't execute sqlite3 shell.");
			return OnStatus(-2);
		}		

		// http://froebe.net/blog/2015/05/27/error-sqlite-database-is-malformed-solved/
		//
		// Run through the commands:
		// pragma integrity_check;
		// .mode insert
		// .output mydb_export.sql
		// .dump
		// .exit
		if (!GetPrompt())
			return OnStatus(-3);
		StatusMsg("Setting mode...");
			
		LString Cmd;
		Cmd.Printf(".mode insert\r\n");
		Shell->Write(Cmd, Cmd.Length());
		
		if (!GetPrompt())
			return OnStatus(-4);
		StatusMsg("Setting output file...");
		
		Cmd.Printf(".output \"%s\"\r\n", RepairSql.Get());
		Shell->Write(Cmd, Cmd.Length());

		if (!GetPrompt())
			return OnStatus(-5);
		StatusMsg("Dumping SQL...");

		Cmd.Printf(".dump\r\n");
		Shell->Write(Cmd, Cmd.Length());

		if (!GetPrompt())
			return OnStatus(-6);

		Cmd.Printf(".exit\r\n");
		Shell->Write(Cmd, Cmd.Length());
		Shell->Wait();
		
		StatusMsg("Renaming database...");
		
		if (!FileDev->Move(Db, OldDb))
		{
			LString s;
			s.Printf("Can't move '%s' to '%s'", Db.Get(), OldDb.Get());
			Props->SetStr(Store3UiError, s);
			return -6;
		}
		
		StatusMsg("Importing SQL...");

		char *r = RepairSql;
		for (char *c = r; *c; c++)
		{
			if (*c == '/' || *c == '\\')
				*c = DIR_CHAR;
		}

		#ifdef WINDOWS
		wchar_t wd[MAX_PATH_LEN];
		_wgetcwd(wd, MAX_PATH_LEN);
		#else
		char wd[MAX_PATH_LEN];
		#ifdef __GTK_H__
		// Gtk::
		#endif
		getcwd(wd, MAX_PATH_LEN);
		#endif
		
		char base[MAX_PATH_LEN];
		LMakePath(base, sizeof(base), Db, "..");
		#ifdef WINDOWS
		LAutoWString baseW(Utf8ToWide(base));
		_wchdir(baseW);
		#else
		#ifdef __GTK_H__
		// Gtk::
		#endif
		chdir(base);
		#endif
		char *exe_leaf = strrchr(Exe, DIR_CHAR);
		
		Args.Printf("%s \"%s\" < \"%s\"", exe_leaf ? exe_leaf + 1 : Exe.Get(), Db.Get(), RepairSql.Get());
		int Result = system(Args);
		LgiTrace("Import result = %i\n", Result);
		
		#ifdef WINDOWS
		_wchdir(wd);
		#else
		#ifdef __GTK_H__
		// Gtk::
		#endif
		chdir(wd);
		#endif

		StatusMsg("Deleting temporary files...");
		FileDev->Delete(RepairSql, NULL, false);

		return OnStatus(0);
	}
};

bool CheckPathForFile(const char *File, char *Exe, int ExeSize)
{
    LString Path; 
    #ifdef WINDOWS
    char *buffer = NULL;
    size_t sz = 0;
    errno_t err = _dupenv_s(&buffer, &sz, "PATH");
    if (err)
    {
		LgiTrace("%s:%i - _dupenv_s failed with %i.\n", _FL, err);
		return false;
    }
    if (sz == 0) // PATH not found
		return false;
	Path = buffer;
	free(buffer);		
    #else
    Path = getenv("PATH");
    if (!Path)
		return false;
    #endif
    
    LString::Array p = Path.Split(LGI_PATH_SEPARATOR);
    for (unsigned i=0; i<p.Length(); i++)
    {
        char s[MAX_PATH_LEN];
        LMakePath(s, sizeof(s), p[i], File);
        if (LFileExists(s))
        {
            strcpy_s(Exe, ExeSize, s);
            return true;
        }
    }    
    
    return false;
}

void LMail3Store::Repair(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus)
{
	// Is the sqlite3 shell binary available?
	char base[MAX_PATH_LEN];
	char exe[MAX_PATH_LEN] = "";
	LMakePath(base, sizeof(base), DbFile, "..");
	
	#ifdef WINDOWS
		const char *SqliteBin = "sqlite3.exe";
		LMakePath(exe, sizeof(exe), base, SqliteBin);
	#else
		const char *SqliteBin = "sqlite3";
		CheckPathForFile(SqliteBin, exe, sizeof(exe));
	#endif		    
	if (!LFileExists(exe))
	{
		LString DownloadUrl = "https://www.sqlite.org/download.html";
		LViewI *p = dynamic_cast<LViewI*>(Props);
		LString Msg;
		Msg.Printf("Error: sqlite shell binary missing (%s). "
		            #ifdef WINDOWS
		            "Download from:\n"
					"\n"
					"    %s\n"
					#else
					"Install using your package manager:\n"
					"\n"
					"   e.g. sudo apt-get install sqlite3\n"
					#endif
					"\n"
					"Download the shell binary and extract to this folder:\n"
					"\n"
					"    %s\n"
					"\n"
					"Then try the Repair command again.\n",
					SqliteBin,
					DownloadUrl.Get(),
					base);
		auto Dlg = new LAlert(p?p:Parent,
							"LMail3Store::Repair",
							Msg,
							"Browse Download Site & Local Folder",
							"Cancel");
		Dlg->DoModal([this, Dlg, DownloadUrl, base](auto dlg, auto ctrlId)
		{
			if (ctrlId == 1)
			{
				LExecute(DownloadUrl);
				LExecute(base);
			}
		});
	
		if (OnStatus)
			OnStatus(true);
		return;
	}
	
	// Close our database...
	CloseDb();	
	
	// Do the repair in a thread
	RepairOnStatus = OnStatus;
	new SqliteRepairThread(this, exe, DbFile, Parent, Props);
}

int64 LMail3Store::GetFolderId(char *Path)
{
	auto Parts = LString(Path).SplitDelimit("/");
	LMail3Folder *Root = dynamic_cast<LMail3Folder*>(GetRoot());
	if (!Root)
		return 0;

	uint64 Id = Root->Id;
	for (unsigned i=0; i<Parts.Length(); i++)
	{
		char *p = Parts[i];
		char Sql[256];
		sprintf_s(Sql, sizeof(Sql), "select * from " MAIL3_TBL_FOLDER " where Name=\"%s\" and ParentId=" LPrintfInt64, p, Id);
		LStatement s(this, Sql);
		if (s.Row())
		{
			Id = s.GetInt64(0);
		}
		else return 0;
	}
	
	return Id;
}

bool LMail3Store::DeleteMailById(int64 Id)
{
	LDataStoreI::StoreTrans Trans = StartTransaction();
	char s[256];

	// Delete all the segments...
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_MAILSEGS " where MailId=" LPrintfInt64, Id);
	LMail3Store::LStatement Del(this, s);
	if (!Del.Exec())
		return false;

	// Delete the mail itself
	sprintf_s(s, sizeof(s), "delete from " MAIL3_TBL_MAIL " where Id=" LPrintfInt64, Id);
	LMail3Store::LStatement Del2(this, s);
	if (!Del2.Exec())
		return false;

	return true;
}

/*
	while
	(
		!Props->GetInt(Store3UiCancel)
		&&            
		(Thread.Return < 0 || !Thread.IsExited())
	)
	{
		LSleep(100);
				
		if (Thread.Lock(_FL))
		{
			if (Thread.Status)
			{
				Props->SetStr(Store3UiStatus, Thread.Status);
				Thread.Status.Empty();
			}
			if (Thread.Error)
			{
				
				Thread.Error.Empty();
			}
			if (Thread.Max > 0)
			{
				Props->SetInt(Store3UiMaxPos, Thread.Max);
				Thread.Max = -1;
			}
			if (Thread.Value >= 0)
			{
				Props->SetInt(Store3UiCurrentPos, Thread.Value);
				Thread.Value = -1;
			}
			Thread.Unlock();
		}
	}
			
	Status = Thread.Return > 0;
*/

class CompactThread : public LThread, public LCancel
{
	int64 InboxId = 0;
	LMail3Store *Store = NULL;
	LDataPropI *Props = NULL;
	int64 Value = -1;
	int64 Max = -1;

public:
	CompactThread(LMail3Store *store, int64 inboxId, LDataPropI *props) :
		LThread("CompactThread")
	{
		Store = store;
		InboxId = inboxId;
		Props = props;

		Run();
	}
	
	~CompactThread()
	{
		Cancel();
		WaitForExit();
	}

	void SetError(const char *s)
	{
		Props->SetStr(Store3UiError, s);
	}
	
	void SetStatus(const char *s)
	{
		Props->SetStr(Store3UiStatus, s);
	}

	void OnStatus(bool s)
	{
		// Send status event back to the store...
		auto msg = new LMail3StoreMsg(LMail3StoreMsg::MsgCompactComplete);
		msg->Int = s;
		Store->PostStore(msg);
	}
	
	int Main()
	{
		// Clean up orphaned mail segments.
		{
			SetStatus("Cleaning up orphaned Mail segments...");
			LMail3Store::LStatement s(Store,
				"delete from " MAIL3_TBL_MAILSEGS " where MailId not in (select Id from " MAIL3_TBL_MAIL ")");
			if (!s.Exec())
			{
				SetError("Failed to delete orphaned segments.");
				OnStatus(false);
				return -1;
			}
		}

		// Clean up orphaned mail.
		if (!IsCancelled())
		{
			SetStatus("Cleaning up orphaned Mail...");

			LMail3Store::LStatement count(Store, "select COUNT(*) from " MAIL3_TBL_MAIL " where ParentId not in (select Id from " MAIL3_TBL_FOLDER ")");
			if (count.Row())
			{
				int64 Rows = count.GetInt64(0);
				Max = Rows;
			}

			LDataStoreI::StoreTrans Trn = Store->StartTransaction();
			LMail3Store::LStatement s(Store, "select Id from " MAIL3_TBL_MAIL " where ParentId not in (select Id from " MAIL3_TBL_FOLDER ")");
			int64 RowPos = 0;
			while (!IsCancelled() && s.Row())
			{
				int64 Id = s.GetInt64(0);
				Store->DeleteMailById(Id);
				Value = ++RowPos;
			}
			
			Max = 1;
			Value = 0;
		}

		// Vacuum
		if (!IsCancelled())
		{
			SetStatus("Vacuum unused space...");

			LMail3Store::LStatement s(Store, "vacuum;");
			if (!s.Exec())
			{
				SetError("Reclaiming space failed.");
				OnStatus(false);
				return -1;
			}
		}
		
		OnStatus(true);
		return 0;
	}
};

void LMail3Store::Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus)
{
	if (!Props || !Callback)
		return;

	bool Status = false;
	LVariant InboxPath;
	if (!Callback->GetSystemPath(FOLDER_INBOX, InboxPath))
		Props->SetStr(Store3UiError, "Couldn't get path to Inbox.");
	else
	{
		int64 InboxId = GetFolderId(InboxPath.Str());
		if (!InboxId)
			Props->SetStr(Store3UiError, "Couldn't get Inbox ID.");
		{
			// Save this locally, because the thread isn't suitable for calling it.
			// Instead it will send us a message, and then the store can call it from
			// the GUI thread. This way any client implementing the callback can talk
			// to or delete UI elements.
			CompactOnStatus = OnStatus;

			new CompactThread(this, InboxId, Props);
			return; // without calling OnStatus, the CompactThread will do it.
		}
	}
	
	OnStatus(Status);
}

void LMail3Store::OnEvent(void *Param)
{
	auto msg = (LMail3StoreMsg*)Param;
	switch (msg->Msg)
	{
		case LMail3StoreMsg::MsgCompactComplete:
		{
			if (CompactOnStatus)
			{
				CompactOnStatus(msg->Int);
				CompactOnStatus = NULL;
			}
			else LgiTrace("%s:%i - No CompactOnStatus to call on MsgCompactComplete.\n", _FL);
			break;
		}
		case LMail3StoreMsg::MsgRepairComplete:
		{
			OpenDb();
	
			if (RepairOnStatus)
			{
				RepairOnStatus(msg->Int >= 0);
				RepairOnStatus = NULL;
			}
			break;
		}
		default:
		{
			LgiTrace("%s:%i - Unhandled mail3store event.\n", _FL);
			break;
		}
	}
}

bool LMail3Store::IsOk()
{
	return
		#ifndef __llvm__
		this != 0 &&
		#endif
		Db != 0;
}

bool LMail3Store::ParseTableFormat(const char *Name, TableDefn &Defs)
{
	char Sql[256];
	sprintf_s(Sql, sizeof(Sql), "SELECT * FROM sqlite_master WHERE type='table' and name='%s'", Name);

	LStatement st(this, Sql);
	if (!st.IsOk())
		return false;

	if (!st.Row())
		return false;

	char *Fmt = st.GetStr(4);
	char *s = Fmt ? strchr(Fmt, '(') + 1 : 0;
	char *e = Fmt ? strrchr(Fmt, ')') : 0;
	if (!s || !e)
		return false;

	LString f(s, e - s);
	Defs.t = f.SplitDelimit(",");

	for (unsigned i=0; i<Defs.t.Length(); i++)
	{
		char *Fld = Defs.t[i];
		while (*Fld && strchr(" \t\r\n", *Fld)) Fld++;
		char *Sp = strchr(Fld, ' ');
		if (Sp)
		{
			*Sp++ = 0;

			LMail3Def &d = Defs.New();
			d.Name = Fld;
			d.Type = Sp;
		}
		else return false;
	}

	return true;
}

Store3Status LMail3Store::CheckTable(const char *Name, LMail3Def *Flds)
{
	Store3Status Status = Store3Success;

	if (!IsOk())
		return Store3Error;

	TableDefn Defs;
	LString::Array t;
	unsigned FieldCount = 0;
	for (FieldCount=0; Flds[FieldCount].Name; FieldCount++)
		;

	if (ParseTableFormat(Name, Defs))
	{
		size_t MaxFields = MAX(Defs.Length(), FieldCount);
		for (unsigned i=0; i<MaxFields; i++)
		{
			if (i >= Defs.Length())
			{
				LString Msg;
				Msg.Printf("\nTable '%s' is missing the schema field: '%s %s'", Name, Flds[i].Type, Flds[i].Name);
				StatusMsg += Msg;
				break;
			}

			LMail3Def &Def = Defs[i];
			if (i >= FieldCount) // Schema field count is less than existing table
			{
				LString Msg;
				Msg.Printf("\nTable '%s' has an extra field: '%s %s'\n", Name, Def.Type, Def.Name);
				StatusMsg += Msg;
				break;
			}
			
			if (!Flds[i].Name ||
				!Flds[i].Type ||
				!Def.Name ||
				!Def.Type)
			{
				Status = Store3Error;
				break;
			}

			if (_stricmp(Flds[i].Name, Def.Name) != 0 ||
				_stricmp(Flds[i].Type, Def.Type) != 0)
			{
				LString Msg;
				Msg.Printf("\nTable '%s' has a field '%s %s' which is to the schema: '%s %s'\n",
							Name,
							Def.Type, Def.Name,
							Flds[i].Type, Flds[i].Name);
				StatusMsg += Msg;

				Status = Store3UpgradeRequired;
				break;
			}
		}
		if (Defs.Length() != FieldCount)
		{
			LString Msg;
			Msg.Printf("\nTable '%s' has %i fields (should have %i)\n", Name, Defs.Length(), FieldCount);
			StatusMsg += Msg;
			Status = Store3UpgradeRequired;
		}
	}
	else Status = Store3Missing;
	
	return Status;
}

bool LMail3Store::UpdateTable(const char *Name, LMail3Def *Flds, Store3Status Check)
{
	LStatement st(this);
	LAutoString TempTable;
	TableDefn Defs;

	if (Check == Store3UpgradeRequired)
	{
		// Get the old table format so we can copy over field by field later...
		if (!ParseTableFormat(Name, Defs))
		{
			return false;
		}

		char Tmp[256];
		sprintf_s(Tmp, sizeof(Tmp), "%s_tmp", Name);
		TempTable.Reset(NewStr(Tmp));

		char AlterSql[256];
		sprintf_s(AlterSql, sizeof(AlterSql), "alter table %s rename to %s", Name, Tmp);

		LStatement Alter(this, AlterSql);
		if (!Alter.Exec())
		{
			LAssert(!"Can't rename table.");
			return false;
		}
	}

	// Create table?
	LStringPipe p;
	LHashTbl<ConstStrKey<char,false>, bool> AllFlds;
	p.Print("create table %s (", Name);
	for (int i=0; Flds[i].Name; i++)
	{
		AllFlds.Add(Flds[i].Name, true);
		if (i) p.Print(", ");
		p.Print("%s %s", Flds[i].Name, Flds[i].Type);
	}
	p.Print(")");
	LAutoString s(p.NewStr());
	if (s)
	{
		if (st.Prepare(s))
			st.Exec();
		else
		{
			LgiTrace("Sql='%s'\n", s.Get());
			return false;
		}
	}

	// Copy over data from the old table
	if (TempTable)
	{
		LStringPipe p;
		p.Print("insert into %s (", Name);

		int Count = 0;
		for (unsigned i=0; i<Defs.Length(); i++) // For all common fields between the 2 tables
		{
			LMail3Def &Def = Defs[i];
			if (AllFlds.Find(Def.Name))
			{
				p.Print("%s%s", Count ? (char*)", " : "", Def.Name);
				Count++;
			}
		}

		p.Print(") select ");
		Count = 0;
		for (unsigned i=0; i<Defs.Length(); i++)
		{
			LMail3Def &Def = Defs[i];
			if (AllFlds.Find(Def.Name))
			{
				p.Print("%s%s", Count ? (char*)", " : "", Def.Name);
				Count++;
			}
		}
		p.Print(" from %s", TempTable.Get());

		// Run the copy
		LAutoString InsSql(p.NewStr());
		if (!st.Prepare(InsSql))
		{
			LAssert(!"Can't insert old data");
			return false;
		}

		if (!st.Exec())
		{
			LAssert(!"Can't copy data from old table.");
			return false;
		}

		// Delete the temporary table...
		char Sql[256];
		sprintf_s(Sql, sizeof(Sql), "drop table %s", TempTable.Get());
		if (!st.Prepare(Sql) ||
			!st.Exec())
		{
			LAssert(!"Drop table failed.");
			return false;
		}
	}

	return true;
}

LMail3Store::LInsert::LInsert(LMail3Store *store, const char *Tbl) : LStatement(store)
{
	Store = store;
	Table = Tbl;

	LMail3Def *f = Store->GetFields(Tbl);
	if (f)
	{
		LVariant v;
		LStringPipe p;
		p.Print("insert into '%s' values (", Tbl);
		for (int i=0; f[i].Name; i++)
		{
			if (i) p.Print(",");
			p.Print("?");
		}
		p.Print(")");
		v.OwnStr(p.NewStr());

		Prepare(v.Str());
	}
}

LMail3Store::LUpdate::LUpdate(LMail3Store *store, const char *Tbl, int64 rowid, char *ExcludeField) : LStatement(store)
{
	RowId = rowid;
	Store = store;
	Table = Tbl;

	LAssert(Store != 0 && Tbl != 0 && RowId > 0);

	LMail3Def *f = Store->GetFields(Tbl);
	if (f)
	{
		LVariant v;
		LStringPipe p;
		p.Print("update %s set ", Tbl);
		for (int i=1; f[i].Name; i++)
		{
			if (ExcludeField && !_stricmp(f[i].Name, ExcludeField))
				continue;

			if (i>1) p.Print(", ");
			p.Print("%s=?%i", f[i].Name, i + 1);
		}
		p.Print(" where %s=?1", f[0].Name);
		v.OwnStr(p.NewStr());

		Prepare(v.Str());
	}
}

////////////////////////////////////////////////////////////////////////
bool LMail3Obj::Check(int r, char *sql)
{
	return Store->Check(r, sql);
}

#define DEBUG_MAIL3_WRITE				0
#if DEBUG_MAIL3_WRITE
#define PROFILE(msg)					Prof.Add(msg)
#else
#define PROFILE(msg)
#endif

bool LMail3Obj::Write(const char *Table, bool Insert)
{
#if DEBUG_MAIL3_WRITE
LProfile Prof("LMail3Obj::Write");
#endif
	LAutoPtr<LMail3Store::LStatement> s;

	PROFILE("0");
	if (Insert)
		s.Reset(new LMail3Store::LInsert(Store, Table));
	else
		s.Reset(new LMail3Store::LUpdate(Store, Table, Id));

	PROFILE("1");
	if (s)
	{
		Serialize(*s, true);

		PROFILE("2");

		if (s->Exec())
		{
			PROFILE("3");
			if (Insert)
			{
				if (!SetId(s->LastInsertId()))
				{
					LAssert(!"No ID returned.");
					LgiTrace("%s:%i - No ID from statement.\n", _FL);
				}
			}
		}
		else
		{
			LgiTrace("%s:%i - Exec failed.\n", _FL);
		}
	}
	else LAssert(!"No statement");

	return Id >= 0;
}

//////////////////////////////////////////////////////////////////////////
LMail3Store::LStatement::LStatement(LMail3Store *store, const char *sql)
{
	Store = store;
	s = 0;

	#if MAIL3_TRACK_OBJS
	LMail3Store::SqliteObjs &_d = Store->All.New();
	_d.Stat = this;
	#endif

	if (sql)
		Prepare(sql);
}

LMail3Store::LStatement::~LStatement()
{
	Finalize();

	#if MAIL3_TRACK_OBJS
	Store->RemoveFromAll(this);
	#endif
}

int64 LMail3Store::LStatement::LastInsertId()
{
	return sqlite3_last_insert_rowid(Store->GetDb());
}

bool LMail3Store::LStatement::Prepare(const char *Sql)
{
	Finalize();
	
	TempSql.Reset(NewStr(Sql));
	Store->Check(sqlite3_prepare_v2(Store->GetDb(), Sql, -1, &s, 0), Sql);

	return s != 0;
}

bool LMail3Store::LStatement::Finalize()
{
	if (!s)
		return false;

	bool Status = Store->Check(sqlite3_finalize(s), 0);

	s = 0;

	return Status;
}

bool LMail3Store::LStatement::Row()
{
	if (!IsOk()) return false;
	int r = sqlite3_step(s);
	return r == SQLITE_ROW;
}

bool LMail3Store::LStatement::Exec()
{
	if (!IsOk()) return false;

	if (!Store->Check(sqlite3_step(s), TempSql))
		return false;

	bool Status = true;
	if (Post.Length())
	{
		int64 Id = GetRowId();
		LAssert(Id > 0);

		for (unsigned i=0; i<Post.Length(); i++)
		{
			PostBlob &b = Post[i];
			sqlite3_blob *Blob = 0;

			if (Store->Check(sqlite3_blob_open(	Store->GetDb(),
												0,
												Table.Str(),
												b.ColName.Str(),
												Id,
												true,
												&Blob), 0))
			{
				LArray<char> Buf;
				if (Buf.Length(32<<10))
				{
					ssize_t r, Pos = 0;
					
					b.Data->SetPos(0);

					while ((r = b.Data->Read(&Buf[0], Buf.Length())) > 0)
					{
						if (!Store->Check(sqlite3_blob_write(Blob, &Buf[0], (int)r, (int)Pos), 0))
						{
							Status = false;
							break;
						}

						Pos += r;
					}
				}
				else Status = false;

				Store->Check(sqlite3_blob_close(Blob), 0);
			}
		}

		Post.Length(0);
	}

	return Status;
}

bool LMail3Store::LStatement::Reset()
{
	if (!IsOk()) return false;
	return	Store->Check(sqlite3_reset(s), 0) &&
			Store->Check(sqlite3_clear_bindings(s), 0);
}

/* All the getter functions use 'Col' as the first column is '0' */
int LMail3Store::LStatement::GetSize(int Col)
{
	return IsOk() ? sqlite3_column_bytes(s, Col) : 0;
}

bool LMail3Store::LStatement::GetBool(int Col)
{
	if (!IsOk() || sqlite3_column_type(s, Col) == SQLITE_NULL)
		return false;
	return sqlite3_column_int(s, Col) != 0;
}

int LMail3Store::LStatement::GetInt(int Col)
{
	if (!IsOk() || sqlite3_column_type(s, Col) == SQLITE_NULL)
		return -1;
	return sqlite3_column_int(s, Col);
}

int64 LMail3Store::LStatement::GetInt64(int Col)
{
	if (!IsOk() || sqlite3_column_type(s, Col) == SQLITE_NULL)
		return -1;
	return sqlite3_column_int64(s, Col);
}

bool LMail3Store::LStatement::SetInt64(int Col, int64 n)
{
	if (!IsOk()) return false;
	if (n == -1)
		return Store->Check(sqlite3_bind_null(s, Col+1), 0);
	else
		return Store->Check(sqlite3_bind_int64(s, Col+1, n), 0);
}

char *LMail3Store::LStatement::GetStr(int Col)
{
	if (!IsOk())
		return NULL;
	auto txt = (char*)sqlite3_column_text(s, Col);
	return txt;
}

bool LMail3Store::LStatement::GetBinary(int Col, LVariant *v)
{
	if (!v)
		return false;
	
	const void *Ptr = sqlite3_column_blob(s, Col);
	if (!Ptr)
		return false;
	
	int Bytes = sqlite3_column_bytes(s, Col);	
	return v->SetBinary(Bytes, (void*)Ptr);
}

/* All the setter functions use 'Col+1' as the first column is '1' */
bool LMail3Store::LStatement::SetInt(int Col, int n)
{
	if (!IsOk()) return false;
	if (n == -1)
		return Store->Check(sqlite3_bind_null(s, Col+1), 0);
	else
		return Store->Check(sqlite3_bind_int(s, Col+1, n), 0);
}

bool LMail3Store::LStatement::SetStr(int Col, const char *Str)
{
	if (!IsOk())
		return false;
	
	if (Str)
		return Store->Check(sqlite3_bind_text(s, Col+1, Str, -1, SQLITE_STATIC), 0);
	else
		return Store->Check(sqlite3_bind_null(s, Col+1), 0);
}

bool LMail3Store::LStatement::SetDate(int Col, LDateTime &Var)
{
	if (!Var.Year())
		return Store->Check(sqlite3_bind_null(s, Col+1), 0);

	char c[64];
	sprintf_s(c, sizeof(c), "%4.4i-%2.2i-%2.2i %2.2i:%2.2i:%2.2i", Var.Year(), Var.Month(), Var.Day(), Var.Hours(), Var.Minutes(), Var.Seconds());
	return Store->Check(sqlite3_bind_text(s, Col+1, (const char*)c, -1, SQLITE_TRANSIENT), 0);
};

bool LMail3Store::LStatement::SetStream(int Col, const char *ColName, LStreamI *Data)
{
	if (!IsOk() || !Data)
		return false;

	PostBlob &p = Post.New();
	p.ColName = ColName;
	p.Data = Data;
	p.Size = Data->GetSize();

	LAssert((p.Size & 0xffffffff00000000) == 0);
	return Store->Check(sqlite3_bind_zeroblob(s, Col+1, (int)p.Size), 0);
}

bool LMail3Store::LStatement::SetBinary(int Col, const char *ColName, LVariant *v)
{
	if (!v || v->Type != GV_BINARY)
	{
		LAssert(!"Invalid binary type.");
		return false;
	}
	
	if (!Store->Check(sqlite3_bind_blob(s, Col+1, v->Value.Binary.Data, (int)v->Value.Binary.Length, SQLITE_TRANSIENT), NULL))
		return false;
		
	return true;
}

//////////////////////////////////////////////////////////////////////////
LMail3Store::LTransaction::LTransaction(LMail3Store *store)
{
	Store = store;
	
	const char *Sql = "begin;";
	Open = Store ? Store->Check(sqlite3_exec(Store->GetDb(), Sql, 0, 0, 0), Sql) : false;
}

LMail3Store::LTransaction::~LTransaction()
{
	if (Open)
	{
		const char *Sql = "end;";
		Store->Check(sqlite3_exec(Store->GetDb(), Sql, 0, 0, 0), Sql);
	}
}

bool LMail3Store::LTransaction::RollBack()
{
	bool Status = false;

	if (Open)
	{
		const char *Sql = "rollback;";
		Status = Store->Check(sqlite3_exec(Store->GetDb(), Sql, 0, 0, 0), Sql);
		Open = false;
	}

	return Status;
}

/////////////////////////////////////////////////////////////////////////
LDataStoreI *OpenMail3(const char *Mail3Folder, LDataEventsI *Callback, bool Create)
{
	return new LMail3Store(Mail3Folder, Callback, Create);
}
