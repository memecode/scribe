#include "Mail3.h"
#include "resdefs.h"
#include "lgi/common/SubProcess.h"
#include <wchar.h>
#include <cerrno>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "lgi/common/LgiRes.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Store3MimeTree.h"

#define SanityCheck() if (!IsOk()) return NULL;

LMail3Def TblImapOperation[] =
{
	{"Id",						"INTEGER PRIMARY KEY AUTOINCREMENT"},
	{"AccountId",				"INTEGER NOT NULL"},
	{"OperationType",			"INTEGER NOT NULL"},
	{"State",					"INTEGER NOT NULL"},
	{"MailId",					"INTEGER"},
	{"SourceMailboxId",			"INTEGER"},
	{"DestinationMailboxId",	"INTEGER"},
	{"SourceUidValidity",		"INTEGER"},
	{"SourceUid",				"INTEGER"},
	{"DestinationUidValidity",	"INTEGER"},
	{"DestinationUid",			"INTEGER"},
	{"Created",					"TEXT"},
	{"Attempts",				"INTEGER"},
	{"LastError",				"TEXT"},
	{"SourceRemoteName",			"TEXT"},
	{"DestinationRemoteName",	"TEXT"},
	{"RemoteFlags",				"INTEGER"},
	{0, 0}
};

LMail3Def TblImapLocalSettings[] =
{
	{"AccountId",		"INTEGER NOT NULL"},
	{"RemoteName",		"TEXT NOT NULL"},
	{"Settings",		"TEXT NOT NULL"},
	{0, 0}
};

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
			if (Parent->Items.IndexOf(this, true) < 0)
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
	{"ImapOperationPendingIdx", MAIL3_TBL_IMAP_OPERATION, "AccountId, State, Id"},
};

LMail3Store::LMail3Store(const char *Mail3Folder, LDataEventsI *callback, bool Create) :
	TableStatus(0, Store3Error)
{
	threadId = LCurrentThreadId();
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
	Fields.Add(MAIL3_TBL_IMAP_OPERATION, TblImapOperation);
	Fields.Add(MAIL3_TBL_IMAP_SETTINGS, TblImapLocalSettings);

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

bool LMail3Store::InThread() const
{
	auto cur = LCurrentThreadId();
	return cur == threadId;
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
			else if (s == Store3Missing)
			{
				OpenStatus = Store3Error;
				ErrorMsg.Printf("Failed to create table '%s'.", it.key);
				return false;
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

		if (!CreateIndexes())
		{
			OpenStatus = Store3Error;
			return false;
		}
	}

	return true;
}

bool LMail3Store::CreateIndexes()
{
	LDataStoreI::StoreTrans Trn = StartTransaction();
	for (auto &Index: Mail3Indexes)
	{
		LString Sql = LString::Fmt("create index if not exists %s on %s(%s)",
			Index.IdxName, Index.Table, Index.Column);
		LStatement s(this, Sql);
		if (!s.Exec())
		{
			ErrorMsg.Printf("Failed to create index '%s'.", Index.IdxName);
			return false;
		}
	}
	const struct
	{
		const char *Table;
		const char *Sql;
	} ImapUniqueIndexes[] =
	{
		{MAIL3_TBL_FOLDER, "create unique index if not exists FolderImapAccountRemoteIdx "
			"on " MAIL3_TBL_FOLDER "(ImapAccountId, ImapRemoteName)"},
		{MAIL3_TBL_MAIL, "create unique index if not exists MailImapFolderUidIdx "
			"on " MAIL3_TBL_MAIL "(ParentId, ImapUidValidity, ImapUid)"},
		{MAIL3_TBL_IMAP_SETTINGS, "create unique index if not exists ImapLocalSettingsAccountRemoteIdx "
			"on " MAIL3_TBL_IMAP_SETTINGS "(AccountId, RemoteName)"},
		{MAIL3_TBL_IMAP_OPERATION, "create unique index if not exists ImapOperationMailIdx "
			"on " MAIL3_TBL_IMAP_OPERATION "(MailId)"}
	};
	for (auto &Index: ImapUniqueIndexes)
	{
		if (TableStatus.Find(Index.Table) != Store3Success)
			continue;
		LStatement s(this, Index.Sql);
		if (!s.Exec())
		{
			ErrorMsg.Printf("Failed to create IMAP storage index: %s", Index.Sql);
			return false;
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
	if (!Transaction && sqlite3_get_autocommit(Db))
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
	Callback->OnNew(parent, new_items, pos, is_new, true);
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
			else
				TableStatus.Add(it.key, Store3Success);
		}
	}
	if (Status && !CreateIndexes())
	{
		p.Print("%s\n", ErrorMsg.Get());
		Status = false;
	}
	OpenStatus = Status ? Store3Success : Store3Error;

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
		if (!getcwd(wd, MAX_PATH_LEN))
		{
			printf("%s:%i - getcwd failed.\n", _FL);
			return -1;
		}
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
		if (chdir(base))
		{
			printf("%s:%i - chdir(%s) failed.\n", _FL, base);
			return -1;
		}
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
		if (chdir(wd))
		{
			printf("%s:%i - chdir(%s) failed.\n", _FL, wd);
			return -1;
		}
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

bool LMail3Store::LoadImapMailboxes(int64 AccountId, LArray<LMail3ImapMailbox> &Mailboxes)
{
	if (!IsOk() || !InThread() || AccountId < 0)
		return false;

	LStatement s(this,
		"select Id, ImapAccountId, ImapRemoteName, ImapDelimiter, "
		"ImapUidValidity, ImapUidNext, ImapHighestModSeq "
		"from " MAIL3_TBL_FOLDER " where ImapAccountId=? and ImapRemoteName is not null order by Id");
	if (!s.IsOk() || !s.SetInt64(0, AccountId))
		return false;

	Mailboxes.Length(0);
	int Result;
	while ((Result = sqlite3_step(s)) == SQLITE_ROW)
	{
		LMail3ImapMailbox &Mailbox = Mailboxes.New();
		Mailbox.Id = s.GetInt64(0);
		Mailbox.AccountId = s.GetInt64(1);
		Mailbox.RemoteName = s.GetStr(2);
		Mailbox.Delimiter = s.GetStr(3);
		Mailbox.UidValidity = s.GetInt64(4);
		Mailbox.UidNext = s.GetInt64(5);
		Mailbox.HighestModSeq = s.GetStr(6);
	}

	return Check(Result, NULL);
}

bool LMail3Store::SaveImapMailbox(LMail3ImapMailbox &Mailbox)
{
	if (!IsOk() ||
		!InThread() ||
		Mailbox.AccountId < 0 ||
		Mailbox.Id <= 0 ||
		!ValidStr(Mailbox.RemoteName))
	{
		ErrorMsg = "Saving IMAP mailbox state requires an existing canonical folder.";
		LgiTrace("%s\n", ErrorMsg.Get());
		return false;
	}

	LStatement Update(this,
		"update " MAIL3_TBL_FOLDER " set "
		"ImapAccountId=?, ImapRemoteName=?, ImapDelimiter=?, ImapUidValidity=?, "
		"ImapUidNext=?, ImapHighestModSeq=? where Id=? and (ImapAccountId is null or ImapAccountId=?)");
	if (!Update.IsOk() ||
		!Update.SetInt64(0, Mailbox.AccountId) ||
		!Update.SetStr(1, Mailbox.RemoteName) ||
		!Update.SetStr(2, Mailbox.Delimiter) ||
		!Update.SetInt64(3, Mailbox.UidValidity) ||
		!Update.SetInt64(4, Mailbox.UidNext) ||
		!Update.SetStr(5, Mailbox.HighestModSeq) ||
		!Update.SetInt64(6, Mailbox.Id) ||
		!Update.SetInt64(7, Mailbox.AccountId) ||
		!Update.Exec())
		return false;
	if (sqlite3_changes(Db) == 1)
		return true;
	ErrorMsg = "The IMAP folder was removed or belongs to another account.";
	LgiTrace("%s\n", ErrorMsg.Get());
	return false;
}

bool LMail3Store::LoadImapMessage(int64 MailId, LMail3ImapMessage &Message)
{
	if (!IsOk() || !InThread() || MailId <= 0)
		return false;

	LStatement s(this,
		"select Id, ParentId, ImapUidValidity, ImapUid, ImapRemoteFlags, "
		"ImapLocalFlags, ImapModSeq, ImapDownloaded from " MAIL3_TBL_MAIL " "
		"where Id=? and ImapUid is not null");
	if (!s.IsOk() || !s.SetInt64(0, MailId) || !s.Row())
		return false;

	Message.MailId = s.GetInt64(0);
	Message.MailboxId = s.GetInt64(1);
	Message.UidValidity = s.GetInt64(2);
	Message.Uid = s.GetInt64(3);
	Message.RemoteFlags = s.GetInt(4);
	Message.LocalFlags = s.GetInt(5);
	Message.ModSeq = s.GetStr(6);
	Message.Downloaded = s.GetBool(7);
	return true;
}

bool LMail3Store::FindImapMessage(int64 MailboxId, int64 UidValidity, int64 Uid, LMail3ImapMessage &Message)
{
	if (!IsOk() ||
		!InThread() ||
		MailboxId <= 0 ||
		UidValidity <= 0 ||
		Uid <= 0)
		return false;

	LStatement s(this,
		"select Id, ParentId, ImapUidValidity, ImapUid, ImapRemoteFlags, "
		"ImapLocalFlags, ImapModSeq, ImapDownloaded from " MAIL3_TBL_MAIL " "
		"where ParentId=? and ImapUidValidity=? and ImapUid=?");
	if (!s.IsOk() ||
		!s.SetInt64(0, MailboxId) ||
		!s.SetInt64(1, UidValidity) ||
		!s.SetInt64(2, Uid) ||
		!s.Row())
		return false;

	Message.MailId = s.GetInt64(0);
	Message.MailboxId = s.GetInt64(1);
	Message.UidValidity = s.GetInt64(2);
	Message.Uid = s.GetInt64(3);
	Message.RemoteFlags = s.GetInt(4);
	Message.LocalFlags = s.GetInt(5);
	Message.ModSeq = s.GetStr(6);
	Message.Downloaded = s.GetBool(7);
	return true;
}

bool LMail3Store::SaveImapMessage(const LMail3ImapMessage &Message)
{
	if (!IsOk() ||
		!InThread() ||
		Message.MailId <= 0 ||
		Message.MailboxId <= 0 ||
		Message.UidValidity <= 0 ||
		Message.Uid <= 0)
		return false;

	LStatement Write(this,
		"update " MAIL3_TBL_MAIL " set "
		"ImapUidValidity=?, ImapUid=?, ImapRemoteFlags=?, ImapLocalFlags=?, "
		"ImapModSeq=?, ImapDownloaded=? where Id=? and ParentId=? and exists"
		" (select 1 from " MAIL3_TBL_FOLDER " F where F.Id=Mail.ParentId and F.ImapAccountId=Mail.AccountId"
		" and F.ImapRemoteName is not null)");
	if (!Write.IsOk() ||
		!Write.SetInt64(0, Message.UidValidity) ||
		!Write.SetInt64(1, Message.Uid) ||
		!Write.SetInt(2, Message.RemoteFlags) ||
		!Write.SetInt(3, Message.LocalFlags) ||
		!Write.SetStr(4, Message.ModSeq) ||
		!Write.SetInt(5, Message.Downloaded ? 1 : 0) ||
		!Write.SetInt64(6, Message.MailId) ||
		!Write.SetInt64(7, Message.MailboxId) ||
		!Write.Exec())
		return false;

	if (sqlite3_changes(Db) == 1)
		return true;
	ErrorMsg = "The IMAP message has no matching canonical mail row, parent folder or account.";
	LgiTrace("%s\n", ErrorMsg.Get());
	return false;
}

bool LMail3Store::DeleteImapMessage(int64 MailId)
{
	if (!IsOk() || !InThread() || MailId <= 0)
		return false;

	LStatement s(this, "update " MAIL3_TBL_MAIL " set ImapUidValidity=null, ImapUid=null,"
		" ImapRemoteFlags=null, ImapLocalFlags=null, ImapModSeq=null, ImapDownloaded=null where Id=?");
	return s.IsOk() &&
		s.SetInt64(0, MailId) &&
		s.Exec();
}

class ImapCacheIndex : public LXmlTree
{
	class RootTag : public LXmlTag
	{
	public:
		RootTag() : LXmlTag("Folder") {}
		LXmlAlloc *GetAllocator() { return Allocator; }
	};

public:
	RootTag Root;

	// LXmlTree::Read accepts unclosed elements; an import must not accept a partial index.
	bool ReadIndex(LStreamI &Input)
	{
		auto Text = Input.Read();
		if (!Text || Text.Length() != Input.GetSize() || strlen(Text.Get()) != Text.Length())
			return false;

		char *Cursor = Text.Get();
		LXmlTag *Parent = NULL;
		bool HasRoot = false;
		while (Cursor && *Cursor)
		{
			bool NoChildren = true;
			auto Tag = Parse(Root.GetAllocator(), Cursor, NoChildren, false);
			if (!Tag)
				break;
			auto Name = Tag->GetTag();
			if (!ValidStr(Name))
				return false;
			if (*Name == '/')
			{
				if (!Parent || Strcmp(Parent->GetTag(), Name + 1))
					return false;
				Parent = Parent->Parent;
			}
			else if (!HasRoot)
			{
				if (Strcmp(Name, "Folder"))
					return false;
				Root.Swap(*Tag);
				HasRoot = true;
				Parent = NoChildren ? NULL : &Root;
			}
			else
			{
				if (!Parent)
					return false;
				auto Child = Tag.Release();
				Parent->InsertTag(Child);
				if (!NoChildren)
					Parent = Child;
			}
		}
		return HasRoot && !Parent && Cursor && !*Cursor;
	}
};

class ImapStorageTransaction
{
	LMail3Store *Store;
	bool Active;

public:
	ImapStorageTransaction(LMail3Store *store) : Store(store)
	{
		Active = Store->Check(sqlite3_exec(Store->GetDb(),
			"savepoint ImapStorage", NULL, NULL, NULL), NULL);
	}
	~ImapStorageTransaction()
	{
		if (Active)
		{
			Store->Check(sqlite3_exec(Store->GetDb(), "rollback to ImapStorage", NULL, NULL, NULL), NULL);
			Store->Check(sqlite3_exec(Store->GetDb(), "release ImapStorage", NULL, NULL, NULL), NULL);
		}
	}
	bool IsOk() const { return Active; }
	bool Commit()
	{
		if (!Active || !Store->Check(sqlite3_exec(Store->GetDb(),
			"release ImapStorage", NULL, NULL, NULL), NULL))
			return false;
		Active = false;
		return true;
	}
};

bool LMail3Store::ImapFailure(const char *Action, const char *Reason)
{
	ErrorMsg.Printf("IMAP %s: %s", Action, Reason);
	LgiTrace("%s\n", ErrorMsg.Get());
	return false;
}

static bool ImapStreamsEqual(LStreamI &First, LStreamI &Second)
{
	if (First.GetSize() < 0 || First.GetSize() != Second.GetSize() ||
		First.SetPos(0) != 0 || Second.SetPos(0) != 0)
		return false;
	char A[8192], B[8192];
	for (int64 Remaining = First.GetSize(); Remaining > 0; )
	{
		auto Size = (ssize_t)MIN(Remaining, (int64)sizeof(A));
		for (auto Stream: {&First, &Second})
		{
			auto Buffer = Stream == &First ? A : B;
			ssize_t Offset = 0;
			while (Offset < Size)
			{
				auto Read = Stream->Read(Buffer + Offset, Size - Offset);
				if (Read <= 0 || Read > Size - Offset)
					return false;
				Offset += Read;
			}
		}
		if (memcmp(A, B, Size))
			return false;
		Remaining -= Size;
	}
	return true;
}

bool LMail3Store::LoadImapBodyMail(int64 AccountId, const LMail3ImapMessage &Identity, LMail3Mail &Mail)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		AccountId < 0 || AccountId > INT_MAX || Identity.MailId <= 0 || Identity.MailboxId <= 0 ||
		Identity.UidValidity <= 0 || Identity.UidValidity > UINT32_MAX ||
		Identity.Uid <= 0 || Identity.Uid > UINT32_MAX)
		return ImapFailure("body access", "A verified canonical identity on the owning thread is required.");
	LStatement Query(this, "select M.* from " MAIL3_TBL_MAIL " M join " MAIL3_TBL_FOLDER " F"
		" on F.Id=M.ParentId where M.Id=? and M.AccountId=? and M.ParentId=?"
		" and M.ImapUidValidity=? and M.ImapUid=? and F.ImapAccountId=M.AccountId"
		" and F.ImapUidValidity=M.ImapUidValidity and F.ImapRemoteName is not null");
	if (!Query.SetInt64(0, Identity.MailId) || !Query.SetInt64(1, AccountId) ||
		!Query.SetInt64(2, Identity.MailboxId) || !Query.SetInt64(3, Identity.UidValidity) ||
		!Query.SetInt64(4, Identity.Uid) || !Query.Row() || !Mail.Serialize(Query, false))
		return ImapFailure("body access", "The message, parent folder, account or UIDVALIDITY has changed.");
	return true;
}

bool LMail3Store::StoreImapBody(int64 AccountId, const LMail3ImapMessage &Identity, LStreamI &Body)
{
	if (!InThread() || !IsOk() || !sqlite3_get_autocommit(Db))
		return ImapFailure("body download", "An idle store on its owning thread is required.");
	ImapStorageTransaction Transaction(this);
	LMail3Mail Mail(this);
	if (!Transaction.IsOk() || !LoadImapBodyMail(AccountId, Identity, Mail))
		return false;
	if (Body.GetSize() < 0 || (Mail.MailSize > 0 && Body.GetSize() != Mail.MailSize) ||
		Body.SetPos(0) != 0)
		return ImapFailure("body download", "The RFC822 size does not match the server metadata.");
	LString Headers = HeadersFromStream(&Body);
	if (!Headers || LGetHeaderField(Headers, "Message-ID").Strip() != LString(Mail.MessageID.Str()).Strip() ||
		Body.SetPos(0) != 0)
		return ImapFailure("body download", "The downloaded Message-ID does not match the canonical message.");
	if (Mail.SetRfc822(&Body, true) != Store3Success)
		return ImapFailure("body download", "Cannot store the original RFC822 image.");
	{
		auto Stored = Mail.GetStream(_FL);
		if (!Stored || !ImapStreamsEqual(Body, *Stored))
			return ImapFailure("body download", "The stored RFC822 image failed byte-for-byte verification.");
	}
	Mail.MailSize = Body.GetSize();
	Mail.ImapDownloaded = 1;
	if (!Mail.Write(MAIL3_TBL_MAIL, false))
		return ImapFailure("body download", "Cannot mark the canonical message downloaded.");
	return Transaction.Commit();
}

LAutoStreamI LMail3Store::OpenImapBody(int64 AccountId, const LMail3ImapMessage &Identity)
{
	LAutoStreamI Result;
	LMail3Mail Mail(this);
	if (!LoadImapBodyMail(AccountId, Identity, Mail))
		return Result;
	if (Mail.ImapDownloaded != 1)
	{
		ImapFailure("body access", "The message body has not been downloaded.");
		return Result;
	}
	if (!(Mail.Flags & MAIL_STORED_FLAT))
	{
		ImapFailure("body access", "The canonical IMAP body is not stored as an original RFC822 image.");
		return Result;
	}
	Result = Mail.GetStream(_FL);
	if (!Result)
		ImapFailure("body access", "The downloaded message has no readable MIME content.");
	else if (Result->SetPos(0) != 0)
	{
		ImapFailure("body access", "Cannot seek the downloaded message.");
		Result.Reset();
	}
	return Result;
}

bool LMail3Store::CheckImapOperation(LMail3ImapOperation &Operation, bool Snapshot)
{
	switch (Operation.Type)
	{
		case LMail3ImapOperationType::SetFlags:
		case LMail3ImapOperationType::Delete:
		case LMail3ImapOperationType::Move:
		case LMail3ImapOperationType::Append:
			break;
		default:
			return ImapFailure("operation", "Unknown operation type.");
	}
	if (Operation.AccountId < 0 || Operation.AccountId > INT_MAX || Operation.MailId <= 0)
		return ImapFailure("operation", "A canonical mail row and account are required.");
	bool Append = Operation.Type == LMail3ImapOperationType::Append;
	bool Destination = Append || Operation.Type == LMail3ImapOperationType::Move;
	ImapMailFlags Flags;
	Flags.All = Operation.RemoteFlags;
	if (Operation.RemoteFlags < 0 || Operation.RemoteFlags > UINT16_MAX ||
		Flags.ImapRecent || Flags.ImapExpunged || (Operation.RemoteFlags & ~63))
		return ImapFailure("operation", "Only writable IMAP system flags may be queued.");
	LStatement Mail(this, "select ParentId, ImapUidValidity, ImapUid, ImapRemoteFlags, ImapDownloaded, Flags"
		" from " MAIL3_TBL_MAIL " where Id=? and AccountId=?");
	if (!Mail.SetInt64(0, Operation.MailId) || !Mail.SetInt64(1, Operation.AccountId) || !Mail.Row())
		return ImapFailure("operation", "The canonical mail row does not belong to this account.");
	auto ParentId = Mail.GetInt64(0);
	if (Append)
	{
		if (Snapshot)
		{
			Operation.SourceMailboxId = -1;
			Operation.SourceUidValidity = Operation.SourceUid = -1;
			Operation.SourceRemoteName.Empty();
		}
		if (Mail.GetInt64(1) != -1 || Mail.GetInt64(2) != -1 ||
			Mail.GetInt(4) != 1 || !(Mail.GetInt(5) & MAIL_STORED_FLAT) ||
			ParentId != Operation.DestinationMailboxId)
			return ImapFailure("operation", "APPEND requires an unassigned staged message with a stored body.");
		LStatement Content(this, "select 1 from " MAIL3_TBL_MAILSEGS " where MailId=?");
		if (!Content.SetInt64(0, Operation.MailId) || !Content.Row())
			return ImapFailure("operation", "The staged APPEND body is missing.");
	}
	else
	{
		if (Snapshot)
		{
			Operation.SourceMailboxId = ParentId;
			Operation.SourceUidValidity = Mail.GetInt64(1);
			Operation.SourceUid = Mail.GetInt64(2);
			if (Operation.Type == LMail3ImapOperationType::Move)
			{
				Flags.All = Mail.GetInt(3);
				Flags.ImapRecent = Flags.ImapExpunged = false;
				Operation.RemoteFlags = Flags.All;
			}
			if (Operation.Type == LMail3ImapOperationType::Delete)
			{
				Flags.All = Mail.GetInt(3);
				Flags.ImapDeleted = true;
				Flags.ImapRecent = false;
				Flags.ImapExpunged = false;
				Operation.RemoteFlags = Flags.All;
			}
		}
		if (ParentId != Operation.SourceMailboxId || Mail.GetInt64(1) != Operation.SourceUidValidity ||
			Mail.GetInt64(2) != Operation.SourceUid || Operation.SourceUidValidity <= 0 ||
			Operation.SourceUidValidity > UINT32_MAX || Operation.SourceUid <= 0 || Operation.SourceUid > UINT32_MAX)
			return ImapFailure("operation", "The source message identity has changed or is unverified.");
	}
	auto CheckFolder = [&](int64 Id, LString &Name, int64 &Epoch)
	{
		LStatement Folder(this, "select ImapRemoteName, ImapUidValidity from " MAIL3_TBL_FOLDER
			" where Id=? and ImapAccountId=?");
		if (!Folder.SetInt64(0, Id) || !Folder.SetInt64(1, Operation.AccountId) || !Folder.Row())
			return ImapFailure("operation", "A mailbox is missing or belongs to another account.");
		LString Remote = Folder.GetStr(0);
		auto UidValidity = Folder.GetInt64(1);
		if (!Remote || Remote.Find("\r") >= 0 || Remote.Find("\n") >= 0 ||
			UidValidity <= 0 || UidValidity > UINT32_MAX)
			return ImapFailure("operation", "A mailbox has no verified server identity.");
		if (Snapshot)
			Name = Remote;
		if (Snapshot && Destination && Id == Operation.DestinationMailboxId)
			Epoch = UidValidity;
		if (Name != Remote || Epoch != UidValidity)
			return ImapFailure("operation", "A mailbox name or UIDVALIDITY has changed since enqueueing.");
		return true;
	};
	if (!Append && !CheckFolder(Operation.SourceMailboxId, Operation.SourceRemoteName, Operation.SourceUidValidity))
		return false;
	if (Destination)
	{
		if (!Append && Operation.SourceMailboxId == Operation.DestinationMailboxId)
			return ImapFailure("operation", "A move requires a different destination mailbox.");
		if (!CheckFolder(Operation.DestinationMailboxId, Operation.DestinationRemoteName, Operation.DestinationUidValidity))
			return false;
	}
	else if (Operation.DestinationMailboxId != -1)
		return ImapFailure("operation", "This operation has no destination mailbox.");
	return true;
}

static void ReadImapOperation(LMail3Store::LStatement &Query, LMail3ImapOperation &Operation)
{
	Operation.Id = Query.GetInt64(0);
	Operation.AccountId = Query.GetInt64(1);
	Operation.Type = (LMail3ImapOperationType)Query.GetInt(2);
	Operation.State = (LMail3ImapOperationState)Query.GetInt(3);
	Operation.MailId = Query.GetInt64(4);
	Operation.SourceMailboxId = Query.GetInt64(5);
	Operation.DestinationMailboxId = Query.GetInt64(6);
	Operation.SourceUidValidity = Query.GetInt64(7);
	Operation.SourceUid = Query.GetInt64(8);
	Operation.DestinationUidValidity = Query.GetInt64(9);
	Operation.DestinationUid = Query.GetInt64(10);
	Operation.Attempts = Query.GetInt(12);
	Operation.LastError = Query.GetStr(13);
	Operation.SourceRemoteName = Query.GetStr(14);
	Operation.DestinationRemoteName = Query.GetStr(15);
	Operation.RemoteFlags = Query.GetInt(16);
}

bool LMail3Store::InsertImapOperation(LMail3ImapOperation &Operation)
{
	if (!CheckImapOperation(Operation, true))
		return false;
	LStatement Insert(this, "insert into " MAIL3_TBL_IMAP_OPERATION
		" (AccountId,OperationType,State,MailId,SourceMailboxId,DestinationMailboxId,SourceUidValidity,"
		"SourceUid,DestinationUidValidity,Created,Attempts,SourceRemoteName,DestinationRemoteName,RemoteFlags)"
		" values (?,?,0,?,?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'),0,?,?,?)");
	if (!Insert.SetInt64(0, Operation.AccountId) || !Insert.SetInt(1, (int)Operation.Type) ||
		!Insert.SetInt64(2, Operation.MailId) || !Insert.SetInt64(3, Operation.SourceMailboxId) ||
		!Insert.SetInt64(4, Operation.DestinationMailboxId) || !Insert.SetInt64(5, Operation.SourceUidValidity) ||
		!Insert.SetInt64(6, Operation.SourceUid) || !Insert.SetInt64(7, Operation.DestinationUidValidity) ||
		!Insert.SetStr(8, Operation.SourceRemoteName) || !Insert.SetStr(9, Operation.DestinationRemoteName) ||
		!Insert.SetInt(10, Operation.RemoteFlags) || !Insert.Exec())
		return ImapFailure("enqueue", "Cannot save the operation; another operation may already own this message.");
	Operation.Id = Insert.LastInsertId();
	Operation.Attempts = 0;
	Operation.LastError.Empty();
	Operation.DestinationUid = -1;
	return true;
}

bool LMail3Store::QueueImapOperation(LMail3ImapOperation &Result)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success || !sqlite3_get_autocommit(Db))
		return ImapFailure("enqueue", "An idle store on its owning thread is required.");
	auto Operation = Result;
	if (Operation.Id != -1 || Operation.State != LMail3ImapOperationState::Queued)
		return ImapFailure("enqueue", "Only a new queued operation can be inserted.");
	ImapStorageTransaction Transaction(this);
	if (!Transaction.IsOk() || !InsertImapOperation(Operation))
		return false;
	if (!Transaction.Commit())
		return false;
	Result = Operation;
	return true;
}

bool LMail3Store::StageImapAppend(int64 AccountId, int64 MailboxId, int RemoteFlags, LStreamI &Body,
	LMail3ImapOperation &Result)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success || !sqlite3_get_autocommit(Db) ||
		AccountId < 0 || AccountId > INT_MAX || Body.SetPos(0) != 0)
		return ImapFailure("stage APPEND", "An idle owning-thread store and seekable RFC822 image are required.");
	ImapStorageTransaction Transaction(this);
	if (!Transaction.IsOk())
		return false;
	LMail3Mail Mail(this);
	Mail.ParentId = MailboxId;
	Mail.AccountId = (int)AccountId;
	if (Mail.SetRfc822(&Body, true) != Store3Success || !Mail.ParseHeaders())
		return ImapFailure("stage APPEND", "Cannot store and parse the original RFC822 image.");
	{
		auto Stored = Mail.GetStream(_FL);
		if (!Stored || !ImapStreamsEqual(Body, *Stored))
			return ImapFailure("stage APPEND", "The stored RFC822 image failed byte-for-byte verification.");
	}
	Mail.MailSize = Body.GetSize();
	Mail.ImapDownloaded = 1;
	if (!Mail.Write(MAIL3_TBL_MAIL, false))
		return ImapFailure("stage APPEND", "Cannot save the staged canonical message.");
	LMail3ImapOperation Operation;
	Operation.AccountId = AccountId;
	Operation.Type = LMail3ImapOperationType::Append;
	Operation.MailId = Mail.Id;
	Operation.DestinationMailboxId = MailboxId;
	Operation.RemoteFlags = RemoteFlags;
	if (!InsertImapOperation(Operation))
		return ImapFailure("stage APPEND", "Cannot durably queue the staged APPEND.");
	if (!Transaction.Commit())
		return false;
	Result = Operation;
	return true;
}

bool LMail3Store::LoadImapOperations(int64 AccountId, LArray<LMail3ImapOperation> &Operations)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success || AccountId < 0)
		return ImapFailure("load journal", "An account and owning-thread store are required.");
	LArray<LMail3ImapOperation> Loaded;
	LStatement Query(this, "select * from " MAIL3_TBL_IMAP_OPERATION " where AccountId=? order by Id");
	if (!Query.SetInt64(0, AccountId))
		return false;
	int Result;
	while ((Result = sqlite3_step(Query)) == SQLITE_ROW)
		ReadImapOperation(Query, Loaded.New());
	if (!Check(Result, NULL))
		return false;
	Operations.Swap(Loaded);
	return true;
}

bool LMail3Store::BeginImapOperation(int64 Id, LMail3ImapOperation &Result, LMail3ImapOperationState ClaimedState)
{
	bool Execute = ClaimedState == LMail3ImapOperationState::Executing;
	bool ContinueMove = ClaimedState == LMail3ImapOperationState::ContinuingMove;
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || Id <= 0 ||
		(!Execute && !ContinueMove && ClaimedState != LMail3ImapOperationState::Reconciling))
		return ImapFailure("claim operation", "An idle owning-thread store, operation ID and valid active state are required.");
	ImapStorageTransaction Transaction(this);
	LMail3ImapOperation Operation;
	{
		LStatement Query(this, "select * from " MAIL3_TBL_IMAP_OPERATION " where Id=?");
		if (!Transaction.IsOk() || !Query.SetInt64(0, Id) || !Query.Row())
			return ImapFailure("claim operation", "The journal entry is missing.");
		ReadImapOperation(Query, Operation);
	}
	auto Expected = Execute ? LMail3ImapOperationState::Queued : LMail3ImapOperationState::NeedsReconciliation;
	if (Operation.State != Expected || !CheckImapOperation(Operation, false))
		return ImapFailure("claim operation", "The operation is changed or is not in the required queued/recovery state.");
	if (ContinueMove && (Operation.Type != LMail3ImapOperationType::Move ||
		Operation.DestinationUid <= 0 || Operation.DestinationUid > UINT32_MAX))
		return ImapFailure("claim operation", "Move continuation requires previously recorded COPYUID evidence.");
	LStatement Claim(this, "update " MAIL3_TBL_IMAP_OPERATION " set State=?,Attempts=Attempts+1,LastError=null"
		" where Id=? and State=? and not exists (select 1 from " MAIL3_TBL_IMAP_OPERATION
		" Earlier where Earlier.AccountId=? and Earlier.Id<?)");
	if (!Claim.SetInt(0, (int)ClaimedState) || !Claim.SetInt64(1, Id) || !Claim.SetInt(2, (int)Expected) ||
		!Claim.SetInt64(3, Operation.AccountId) || !Claim.SetInt64(4, Id) ||
		!Claim.Exec() || sqlite3_changes(Db) != 1)
		return ImapFailure("claim operation", "An earlier operation must be completed or reconciled first.");
	if (!Transaction.Commit())
		return false;
	Operation.State = ClaimedState;
	Operation.Attempts++;
	Operation.LastError.Empty();
	Result = Operation;
	return true;
}

LAutoStreamI LMail3Store::OpenImapUpload(int64 AccountId, int64 OperationId)
{
	LAutoStreamI Result;
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || AccountId < 0 || OperationId <= 0)
	{
		ImapFailure("upload access", "An account, operation ID and idle owning-thread store are required.");
		return Result;
	}
	LMail3ImapOperation Operation;
	{
		LStatement Query(this, "select * from " MAIL3_TBL_IMAP_OPERATION " where Id=? and AccountId=?");
		if (!Query.SetInt64(0, OperationId) || !Query.SetInt64(1, AccountId) || !Query.Row())
		{
			ImapFailure("upload access", "The APPEND journal entry is missing.");
			return Result;
		}
		ReadImapOperation(Query, Operation);
	}
	if (Operation.Type != LMail3ImapOperationType::Append ||
		(Operation.State != LMail3ImapOperationState::Queued && Operation.State != LMail3ImapOperationState::Executing) ||
		!CheckImapOperation(Operation, false))
	{
		ImapFailure("upload access", "Only an unchanged queued or executing APPEND can supply upload bytes.");
		return Result;
	}
	LMail3Mail Mail(this);
	{
		LStatement Query(this, "select * from " MAIL3_TBL_MAIL " where Id=?");
		if (!Query.SetInt64(0, Operation.MailId) || !Query.Row() || !Mail.Serialize(Query, false))
		{
			ImapFailure("upload access", "Cannot load the staged canonical message.");
			return Result;
		}
	}
	auto Stored = Mail.GetStream(_FL);
	// Never hand a SQLite BLOB handle to the network worker.
	Result.Reset(new LTempStream(GetStr(FIELD_TEMP_PATH)));
	LCopyStreamer Copy;
	if (!Stored || Stored->GetSize() != Mail.MailSize || Stored->SetPos(0) != 0 ||
		!Result || !Copy.Copy(Stored, Result) || !ImapStreamsEqual(*Stored, *Result) ||
		Result->SetPos(0) != 0)
	{
		ImapFailure("upload access", "Cannot detach and verify the original APPEND image.");
		Result.Reset();
	}
	return Result;
}

bool LMail3Store::InterruptImapOperation(int64 Id, const char *Reason,
	int64 DestinationUidValidity, int64 DestinationUid)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || Id <= 0 || !ValidStr(Reason))
		return ImapFailure("interrupt operation", "An idle owning-thread store, operation ID and reason are required.");
	bool HasDestination = DestinationUidValidity != -1 || DestinationUid != -1;
	if (HasDestination && (DestinationUidValidity <= 0 || DestinationUidValidity > UINT32_MAX ||
		DestinationUid <= 0 || DestinationUid > UINT32_MAX))
		return ImapFailure("interrupt operation", "Invalid destination identity evidence.");
	LStatement Update(this, "update " MAIL3_TBL_IMAP_OPERATION
		" set State=2,LastError=?,DestinationUid=coalesce(?,DestinationUid) where Id=? and State in (1,3,4)"
		" and (? is null or (OperationType in (3,4) and DestinationUidValidity=?))"
		" and (DestinationUid is null or ? is null or DestinationUid=?)");
	if (!Update.SetStr(0, Reason) || !Update.SetInt64(1, DestinationUid) || !Update.SetInt64(2, Id) ||
		!Update.SetInt64(3, DestinationUidValidity) || !Update.SetInt64(4, DestinationUidValidity) ||
		!Update.SetInt64(5, DestinationUid) || !Update.SetInt64(6, DestinationUid) ||
		!Update.Exec() || sqlite3_changes(Db) != 1)
		return ImapFailure("interrupt operation", "The operation is not active or its recorded destination evidence conflicts.");
	return true;
}

bool LMail3Store::RecoverImapOperations(int64 AccountId)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || AccountId < 0)
		return ImapFailure("recover journal", "An account and idle owning-thread store are required.");
	LStatement Update(this, "update " MAIL3_TBL_IMAP_OPERATION
		" set State=2,LastError='Interrupted command: reconcile server state before retrying.'"
		" where AccountId=? and State in (1,3,4)");
	if (!Update.SetInt64(0, AccountId) || !Update.Exec())
		return ImapFailure("recover journal", "Cannot quarantine interrupted server commands.");
	return true;
}

bool LMail3Store::CompleteImapOperation(int64 Id, int64 DestinationUidValidity, int64 DestinationUid)
{
	if (!InThread() || !IsOk() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || Id <= 0)
		return ImapFailure("complete operation", "An idle owning-thread store and operation ID are required.");
	ImapStorageTransaction Transaction(this);
	LMail3ImapOperation Operation;
	{
		LStatement Query(this, "select * from " MAIL3_TBL_IMAP_OPERATION " where Id=?");
		if (!Transaction.IsOk() || !Query.SetInt64(0, Id) || !Query.Row())
			return ImapFailure("complete operation", "The journal entry is missing.");
		ReadImapOperation(Query, Operation);
	}
	if ((Operation.State != LMail3ImapOperationState::Executing &&
		Operation.State != LMail3ImapOperationState::Reconciling &&
		Operation.State != LMail3ImapOperationState::ContinuingMove) || !CheckImapOperation(Operation, false))
		return ImapFailure("complete operation", "The executing command no longer matches the canonical identity.");
	if (Operation.State == LMail3ImapOperationState::ContinuingMove && Operation.Type != LMail3ImapOperationType::Move)
		return ImapFailure("complete operation", "Only a move may complete a continuation claim.");
	bool Destination = Operation.Type == LMail3ImapOperationType::Move || Operation.Type == LMail3ImapOperationType::Append;
	if (Destination && (DestinationUidValidity != Operation.DestinationUidValidity ||
		DestinationUid <= 0 || DestinationUid > UINT32_MAX))
		return ImapFailure("complete operation", "COPY/APPEND requires a verified destination epoch and UID.");
	if (Destination && Operation.State != LMail3ImapOperationState::Executing &&
		DestinationUid != Operation.DestinationUid)
		return ImapFailure("complete operation", "Recovery requires the previously recorded UIDPLUS evidence.");
	if (!Destination && (DestinationUidValidity != -1 || DestinationUid != -1))
		return ImapFailure("complete operation", "This command has no destination identity.");
	LMail3Mail Mail(this);
	{
		LStatement Query(this, "select * from " MAIL3_TBL_MAIL " where Id=?");
		if (!Query.SetInt64(0, Operation.MailId) || !Query.Row() || !Mail.Serialize(Query, false))
			return ImapFailure("complete operation", "Cannot load the canonical message.");
	}
	if (Destination)
	{
		Mail.ParentId = Operation.DestinationMailboxId;
		Mail.ImapUidValidity = DestinationUidValidity;
		Mail.ImapUid = DestinationUid;
	}
	if (Operation.Type != LMail3ImapOperationType::Move)
	{
		Mail.ImapRemoteFlags = Operation.RemoteFlags;
		ImapMailFlags Flags;
		Flags.All = Operation.RemoteFlags;
		Mail.Flags = (Mail.Flags & ~(MAIL_READ | MAIL_REPLIED)) |
			(Flags.ImapSeen ? MAIL_READ : 0) | (Flags.ImapAnswered ? MAIL_REPLIED : 0);
		Mail.ImapLocalFlags = Mail.Flags & ~MAIL_STORED_FLAT;
	}
	if (!Mail.Write(MAIL3_TBL_MAIL, false))
		return ImapFailure("complete operation", "Cannot commit the server-confirmed message state.");
	LStatement Delete(this, "delete from " MAIL3_TBL_IMAP_OPERATION " where Id=? and State in (1,3,4)");
	if (!Delete.SetInt64(0, Id) || !Delete.Exec() || sqlite3_changes(Db) != 1)
		return ImapFailure("complete operation", "Cannot retire the confirmed journal entry.");
	return Transaction.Commit();
}

bool LMail3Store::ImportImapSettings(int64 AccountId, const char *CachePath)
{
	auto Fail = [&](const char *Reason)
	{
		ErrorMsg.Printf("IMAP settings recovery failed for account %" PRIi64 ": %s (%s)",
			AccountId, Reason, CachePath ? CachePath : "no cache path");
		LgiTrace("%s\n", ErrorMsg.Get());
		return false;
	};
	if (!IsOk() || !InThread() || AccountId < 0 || AccountId > INT_MAX || !ValidStr(CachePath) ||
		GetInt(FIELD_STATUS) != Store3Success || !sqlite3_get_autocommit(Db))
		return Fail("An open, idle Mail3 store on its owning thread is required.");
	ImapStorageTransaction Transaction(this);
	if (!Transaction.IsOk())
		return Fail("Cannot start the settings transaction.");

	std::function<bool(std::filesystem::path, LString, unsigned)> Visit;
	Visit = [&](std::filesystem::path Path, LString Remote, unsigned Depth)
	{
		if (Depth > 64)
			return Fail("The cache folder tree is too deep.");
		std::error_code Error;
		auto Status = std::filesystem::symlink_status(Path, Error);
		if (Depth == 0 && Error == std::errc::no_such_file_or_directory)
			return true;
		if (Error || !std::filesystem::is_directory(Status))
			return Fail("Cannot inspect a cache directory, or it is a symbolic link.");
		auto XmlPath = Path / "Folder.xml";
		Status = std::filesystem::symlink_status(XmlPath, Error);
		if (Error && Error != std::errc::no_such_file_or_directory)
			return Fail("Cannot inspect the folder index.");
		if (std::filesystem::exists(Status))
		{
			if (!std::filesystem::is_regular_file(Status))
				return Fail("The folder index must be a regular file.");
			LFile Input;
			ImapCacheIndex Index;
			if (!Input.Open(XmlPath.u8string().c_str(), O_READ) || !Index.ReadIndex(Input))
				return Fail("Cannot read the folder index.");

			LXmlTag Settings("Folder");
			for (auto Name: {"Sort", "Expanded", "Thread"})
				if (auto Value = Index.Root.GetAttr(Name))
					Settings.SetAttr(Name, Value);
			if (auto Fields = Index.Root.GetChildTag("Fields"))
			{
				auto Recovered = Settings.GetChildTag("Fields", true);
				for (auto Field: Fields->Children)
				{
					if (!Field->IsTag("Field"))
						return Fail("Invalid display-field element in the folder index.");
					auto Copy = new LXmlTag("Field");
					Recovered->InsertTag(Copy);
					for (auto Name: {"Id", "Width"})
					{
						auto Text = Field->GetAttr(Name);
						char *End = NULL;
						errno = 0;
						auto Value = Text ? strtoll(Text, &End, 10) : 0;
						if (!Text || errno || End == Text || *End || Value < INT_MIN || Value > INT_MAX)
							return Fail("Invalid display-field settings in the folder index.");
						Copy->SetAttr(Name, Text);
					}
				}
			}
			auto Emails = Settings.GetChildTag("Emails", true);
			if (auto Source = Index.Root.GetChildTag("Emails"))
				for (auto Mail: Source->Children)
				{
					if (!Mail->IsTag("Mail"))
						return Fail("Invalid message element in the folder index.");
					auto Copy = new LXmlTag("Mail");
					for (auto Name: {"MsgId", "Label", "Colour"})
						if (auto Value = Mail->GetAttr(Name))
							Copy->SetAttr(Name, Value);
					Emails->InsertTag(Copy);
				}
			LStringPipe Output;
			LXmlTree Writer;
			if (!Writer.Write(&Settings, &Output))
				return Fail("Cannot serialize recovered settings.");
			auto Text = Output.NewLStr();
			LStatement Save(this, "insert into " MAIL3_TBL_IMAP_SETTINGS
				" (AccountId, RemoteName, Settings) values (?,?,?)"
				" on conflict(AccountId, RemoteName) do nothing");
			if (!Save.SetInt64(0, AccountId) || !Save.SetStr(1, Remote) ||
				!Save.SetStr(2, Text) || !Save.Exec())
				return Fail("Cannot save recovered settings.");
		}
		else
		{
			auto Proto = std::filesystem::symlink_status(Path / "Folder.proto", Error);
			if ((Error && Error != std::errc::no_such_file_or_directory) ||
				std::filesystem::exists(Proto))
				return Fail("Cannot recover settings from a Protobuf cache.");
		}
		std::filesystem::directory_iterator It(Path, Error), End;
		for (; !Error && It != End; It.increment(Error))
		{
			auto Entry = It->symlink_status(Error);
			if (Error || std::filesystem::is_symlink(Entry))
				return Fail("Cannot inspect a cache entry, or it is a symbolic link.");
			if (std::filesystem::is_directory(Entry))
			{
				LString Name = It->path().filename().u8string().c_str();
				if (!Visit(It->path(), Remote == "/" ? Remote + Name : Remote + "/" + Name, Depth + 1))
					return false;
			}
		}
		return !Error || Fail("Cannot enumerate the cache directory.");
	};
	if (!Visit(std::filesystem::u8path(CachePath), "/", 0))
		return false;
	return Transaction.Commit() || Fail("Cannot commit recovered settings.");
}

bool LMail3Store::RebuildImapMailbox(LMail3ImapMailbox &ResultMailbox, LArray<LMail3ImapServerMessage> &Messages)
{
	auto Mailbox = ResultMailbox;
	auto Fail = [&](const char *Reason)
	{
		ErrorMsg.Printf("IMAP server rebuild failed for '%s': %s",
			Mailbox.RemoteName ? Mailbox.RemoteName.Get() : "<missing mailbox>", Reason);
		LgiTrace("%s\n", ErrorMsg.Get());
		return false;
	};
	if (!IsOk() || !InThread() || GetInt(FIELD_STATUS) != Store3Success ||
		!sqlite3_get_autocommit(Db) || Mailbox.AccountId < 0 || Mailbox.AccountId > INT_MAX ||
		!Mailbox.RemoteName || Mailbox.RemoteName.Get()[0] != '/' ||
		Mailbox.RemoteName == "/" || Mailbox.UidValidity <= 0 || Mailbox.UidValidity > UINT32_MAX ||
		(Mailbox.UidNext != -1 && (Mailbox.UidNext <= 0 || Mailbox.UidNext > UINT32_MAX)))
		return Fail("A complete server listing with a valid SELECT epoch is required.");

	std::set<int64> Uids;
	std::map<std::string, unsigned> ServerIds;
	for (auto &Message: Messages)
	{
		if (Message.Uid <= 0 || Message.Uid > UINT32_MAX || !Message.Headers || Message.Size < 0 ||
			!Uids.insert(Message.Uid).second)
			return Fail("The server listing contains missing headers or invalid/duplicate UIDs.");
		auto Id = LGetHeaderField(Message.Headers, "Message-ID").Strip();
		if (Id)
			ServerIds[Id.Get()]++;
	}
	ImapStorageTransaction Transaction(this);
	if (!Transaction.IsOk())
		return Fail("Cannot start the rebuild transaction.");
	{
		LStatement Pending(this, "select 1 from " MAIL3_TBL_IMAP_OPERATION " where AccountId=? limit 1");
		if (!Pending.SetInt64(0, Mailbox.AccountId))
			return Fail("Cannot inspect pending server operations.");
		auto Result = sqlite3_step(Pending);
		if (Result != SQLITE_DONE)
			return Fail("Resolve the account's operation journal before rebuilding.");
	}

	LArray<LMail3ImapMailbox> Mailboxes;
	if (!LoadImapMailboxes(Mailbox.AccountId, Mailboxes))
		return Fail("Cannot load the account's mailbox mappings.");
	int64 ParentId;
	{
		LStatement Root(this, "select Id from " MAIL3_TBL_FOLDER " where ParentId is null");
		if (!Root.Row())
			return Fail("The destination store has no root folder.");
		ParentId = Root.GetInt64(0);
	}
	auto Components = Mailbox.RemoteName.SplitDelimit("/");
	LString Remote = "/";
	ImapCacheIndex LocalSettings;
	for (size_t Depth = 0; Depth <= Components.Length(); Depth++)
	{
		if (Depth)
			Remote = Depth == 1 ? Remote + Components[Depth-1] : Remote + "/" + Components[Depth-1];
		LMail3ImapMailbox Current;
		for (auto &Existing: Mailboxes)
			if (Existing.RemoteName == Remote)
				Current = Existing;
		ImapCacheIndex Settings;
		{
			LStatement Query(this, "select Settings from " MAIL3_TBL_IMAP_SETTINGS " where AccountId=? and RemoteName=?");
			if (!Query.SetInt64(0, Mailbox.AccountId) || !Query.SetStr(1, Remote))
				return Fail("Cannot look up recovered folder settings.");
			auto Result = sqlite3_step(Query);
			if (Result == SQLITE_ROW)
			{
				LString Text = Query.GetStr(0);
				LMemStream Input(Text.Get(), Text.Length());
				if (!Settings.ReadIndex(Input))
					return Fail("Cannot read recovered folder settings.");
			}
			else if (Result != SQLITE_DONE)
				return Fail("Cannot read recovered folder settings.");
		}
		if (Current.Id < 0)
		{
			LMail3Folder Folder(this);
			Folder.ParentId = ParentId;
			Folder.Name = Depth ? Components[Depth-1] : LString::Fmt("IMAP (%" PRIi64 ")", Mailbox.AccountId);
			for (auto Name: {"Sort", "Expanded", "Thread"})
				if (auto Text = Settings.Root.GetAttr(Name))
				{
					char *End;
					errno = 0;
					auto Value = strtol(Text, &End, 10);
					if (errno || End == Text || *End || Value < INT_MIN || Value > INT_MAX)
						return Fail("Invalid recovered folder settings.");
					if (!Strcmp(Name, "Sort")) Folder.Sort = (int)Value;
					else if (!Strcmp(Name, "Expanded")) Folder.Open = (int)Value;
					else Folder.Threaded = (int)Value;
				}
			if (!Folder.Write(MAIL3_TBL_FOLDER, true))
				return Fail("Cannot create a server folder.");
			if (auto Fields = Settings.Root.GetChildTag("Fields"))
				for (auto Field: Fields->Children)
					Folder.Flds.Insert(new Store3Field(this, Field->GetAsInt("Id"), Field->GetAsInt("Width")), -1, true);
			if (!Folder.Write(MAIL3_TBL_FOLDER, false))
				return Fail("Cannot save server folder display settings.");
			Current.AccountId = Mailbox.AccountId;
			Current.Id = Folder.Id;
			Current.RemoteName = Remote;
			Current.Delimiter = Mailbox.Delimiter;
			if (!SaveImapMailbox(Current))
				return Fail("Cannot save a server folder mapping.");
		}
		else
		{
			LStatement Folder(this, "select 1 from " MAIL3_TBL_FOLDER " where Id=? and ParentId=?");
			if (!Folder.SetInt64(0, Current.Id) || !Folder.SetInt64(1, ParentId) || !Folder.Row())
				return Fail("The mapped server folder was removed or reparented.");
		}
		ParentId = Current.Id;
		if (Depth == Components.Length())
		{
			Mailbox.Id = Current.Id;
			LocalSettings.Root.Swap(Settings.Root);
		}
	}

	struct Customization
	{
		unsigned Count = 0;
		LString Label;
		uint32_t Colour = 0;
	};
	std::map<std::string, Customization> Recovered, ExistingSettings;
	if (auto Emails = LocalSettings.Root.GetChildTag("Emails"))
		for (auto Email: Emails->Children)
		{
			LString Id = Email->GetAttr("MsgId");
			Id = Id.Strip();
			if (!Id)
				continue;
			auto &Settings = Recovered[Id.Get()];
			Settings.Count++;
			Settings.Label = Email->GetAttr("Label");
			if (auto Text = Email->GetAttr("Colour"))
			{
				char *End;
				errno = 0;
				auto Colour = strtoull(Text, &End, 10);
				if (errno || End == Text || *End || Colour > UINT32_MAX)
					return Fail("Invalid recovered message colour.");
				Settings.Colour = (uint32_t)Colour;
			}
		}
	std::map<int64, int64> ExistingIds;
	LXmlTag ExistingRecords("Emails");
	{
		LStatement Query(this, "select Id, MessageID, Label, MarkColour, ImapUid, ImapUidValidity from "
			MAIL3_TBL_MAIL " where ParentId=? and ImapUid is not null");
		if (!Query.SetInt64(0, Mailbox.Id))
			return Fail("Cannot inspect existing mail.");
		int Result;
		while ((Result = sqlite3_step(Query)) == SQLITE_ROW)
		{
			LString Id = Query.GetStr(1);
			Id = Id.Strip();
			if (Id)
			{
				auto &Settings = ExistingSettings[Id.Get()];
				Settings.Count++;
				Settings.Label = Query.GetStr(2);
				Settings.Colour = (uint32_t)Query.GetInt64(3);
				auto Record = new LXmlTag("Mail");
				Record->SetAttr("MsgId", Id);
				Record->SetAttr("Label", Settings.Label);
				Record->SetAttr("Colour", (int64)Settings.Colour);
				ExistingRecords.InsertTag(Record);
			}
			if (Query.GetInt64(5) == Mailbox.UidValidity)
				ExistingIds[Query.GetInt64(4)] = Query.GetInt64(0);
		}
		if (Result != SQLITE_DONE)
			return Fail("Cannot read existing mail.");
	}
	for (auto &Entry: ExistingSettings)
		Recovered[Entry.first] = Entry.second;
	if (!ExistingSettings.empty())
	{
		auto Emails = LocalSettings.Root.GetChildTag("Emails", true);
		for (size_t Index = 0; Index < Emails->Children.Length(); )
		{
			auto Record = Emails->Children[Index];
			LString Id = Record->GetAttr("MsgId");
			Id = Id.Strip();
			if (Id && ExistingSettings.count(Id.Get()))
				delete Record;
			else
				Index++;
		}
		for (auto Record: ExistingRecords.Children)
			Emails->InsertTag(new LXmlTag(*Record));
		LStringPipe Output;
		LXmlTree Writer;
		if (!Writer.Write(&LocalSettings.Root, &Output))
			return Fail("Cannot retain current local customizations.");
		auto Text = Output.NewLStr();
		LStatement Save(this, "insert into " MAIL3_TBL_IMAP_SETTINGS
			" (AccountId, RemoteName, Settings) values (?,?,?)"
			" on conflict(AccountId, RemoteName) do update set Settings=excluded.Settings");
		if (!Save.SetInt64(0, Mailbox.AccountId) || !Save.SetStr(1, Mailbox.RemoteName) ||
			!Save.SetStr(2, Text) || !Save.Exec())
			return Fail("Cannot persist current local customizations.");
	}

	std::set<int64> Keep;
	unsigned Restored = 0;
	// Parse server metadata without creating dummy MIME content for undownloaded mail.
	struct ServerMail : public LMail3Mail
	{
		LString Headers;
		ServerMail(LMail3Store *Store, LString headers) : LMail3Mail(Store), Headers(headers) {}
		const char *GetStr(int Field) override
		{
			return Field == FIELD_INTERNET_HEADER ? Headers.Get() : LMail3Mail::GetStr(Field);
		}
	};
	for (auto &Message: Messages)
	{
		ServerMail Mail(this, Message.Headers);
		auto Existing = ExistingIds.find(Message.Uid);
		bool NewIdentity = Existing == ExistingIds.end();
		if (!NewIdentity)
		{
			LStatement Query(this, "select * from " MAIL3_TBL_MAIL " where Id=?");
			if (!Query.SetInt64(0, Existing->second) || !Query.Row() || !Mail.Serialize(Query, false))
				return Fail("Cannot load an existing server message.");
			LString PreviousId = Mail.MessageID.Str();
			if (PreviousId.Strip() != LGetHeaderField(Message.Headers, "Message-ID").Strip())
				return Fail("A server UID changed Message-ID without changing UIDVALIDITY.");
		}
		Mail.ParentId = Mailbox.Id;
		Mail.AccountId = (int)Mailbox.AccountId;
		ImapMailFlags Flags;
		Flags.All = Message.RemoteFlags;
		Mail.Flags = (Mail.Flags & ~(MAIL_READ | MAIL_REPLIED)) |
			(Flags.ImapSeen ? MAIL_READ : 0) | (Flags.ImapAnswered ? MAIL_REPLIED : 0);
		Mail.MailSize = Message.Size;
		if (!Mail.ParseHeaders())
			return Fail("Cannot parse server message headers.");
		Mail.MessageID = LGetHeaderField(Message.Headers, "Message-ID").Strip();
		if (NewIdentity && ValidStr(Mail.MessageID.Str()))
		{
			auto Key = std::string(Mail.MessageID.Str());
			auto Settings = Recovered.find(Key);
			if (ServerIds[Key] == 1 && Settings != Recovered.end() && Settings->second.Count == 1)
			{
				Mail.Label = Settings->second.Label;
				Mail.MarkColour = Settings->second.Colour;
				Restored++;
			}
		}
		if (!Mail.Write(MAIL3_TBL_MAIL, NewIdentity))
			return Fail("Cannot save refreshed server metadata.");
		Keep.insert(Mail.Id);
		LMail3ImapMessage Mapping;
		if (!NewIdentity && !LoadImapMessage(Mail.Id, Mapping))
			return Fail("Cannot load the verified UID mapping.");
		Mapping.MailId = Mail.Id;
		Mapping.MailboxId = Mailbox.Id;
		Mapping.UidValidity = Mailbox.UidValidity;
		Mapping.Uid = Message.Uid;
		Mapping.RemoteFlags = Message.RemoteFlags;
		Mapping.LocalFlags = Mail.Flags & ~MAIL_STORED_FLAT;
		if (!SaveImapMessage(Mapping))
			return Fail("Cannot save the verified UID mapping.");
	}
	std::vector<int64> Removed;
	{
		LStatement Query(this, "select Id from " MAIL3_TBL_MAIL " where ParentId=? and ImapUid is not null");
		if (!Query.SetInt64(0, Mailbox.Id))
			return Fail("Cannot find stale messages.");
		int Result;
		while ((Result = sqlite3_step(Query)) == SQLITE_ROW)
			if (!Keep.count(Query.GetInt64(0)))
				Removed.push_back(Query.GetInt64(0));
		if (Result != SQLITE_DONE)
			return Fail("Cannot enumerate stale messages.");
	}
	for (auto Id: Removed)
	{
		for (auto Sql: {
			"delete from " MAIL3_TBL_MAILSEGS " where MailId=?",
			"delete from " MAIL3_TBL_MAIL " where Id=?" })
		{
			LStatement Delete(this, Sql);
			if (!Delete.SetInt64(0, Id) || !Delete.Exec())
				return Fail("Cannot remove stale server state.");
		}
	}
	if (!SaveImapMailbox(Mailbox))
		return Fail("Cannot save SELECT state.");
	if (!Transaction.Commit())
		return Fail("Cannot commit the server rebuild.");
	ResultMailbox = Mailbox;
	LgiTrace("IMAP server rebuild '%s': %zu messages; %u uniquely matched customizations restored. "
		"Unmatched customizations remain in the recovery settings and legacy cache.\n",
		Mailbox.RemoteName.Get(), (size_t)Messages.Length(), Restored);
	return true;
}

bool LMail3Store::ImportImapCache(int64 AccountId, const char *CachePath)
{
	LString Context = CachePath ? CachePath : "<missing cache path>";
	bool ValidAttributes = true;
	auto AttributeInt = [&](LXmlTag &Tag, const char *Name, int64 Default = 0) -> int64
	{
		auto Text = Tag.GetAttr(Name);
		if (!Text)
			return Default;
		char *End = NULL;
		errno = 0;
		auto Value = strtoll(Text, &End, 10);
		if (errno || End == Text || *End)
			ValidAttributes = false;
		return Value;
	};
	auto Fail = [&](const char *Reason)
	{
		ErrorMsg.Printf("IMAP cache import failed at '%s': %s", Context.Get(), Reason);
		LgiTrace("%s\n", ErrorMsg.Get());
		return false;
	};
	auto SourceFile = [&](const char *Path, bool &Exists)
	{
		std::error_code Error;
		auto Status = std::filesystem::symlink_status(std::filesystem::u8path(Path), Error);
		if (Error && Error != std::errc::no_such_file_or_directory)
			return Fail("Cannot inspect the source file.");
		Exists = std::filesystem::exists(Status);
		if (Exists && !std::filesystem::is_regular_file(Status))
			return Fail("Cache files must be regular files, not links or special files.");
		return true;
	};
	if (!IsOk() || !InThread() || AccountId < 0 || AccountId > INT_MAX ||
		!CachePath || !LDirExists(CachePath))
		return Fail("An open Mail3 store, its owning thread and an existing cache directory are required.");
	if (GetInt(FIELD_STATUS) != Store3Success || !sqlite3_get_autocommit(Db))
		return Fail("Finish database upgrades and transactions before importing a cache.");

	int64 RootId = -1;
	{
		LStatement RootQuery(this, "select Id from " MAIL3_TBL_FOLDER " where ParentId is null");
		if (!RootQuery.IsOk() || !RootQuery.Row())
			return Fail("The destination store has no root folder.");
		RootId = RootQuery.GetInt64(0);
	}
	if (!Check(sqlite3_exec(Db, "savepoint ImapCacheImport", NULL, NULL, NULL), NULL))
		return Fail("Cannot start the import transaction.");

	std::function<bool(LString, LString, int64, unsigned)> ImportFolder;
	ImportFolder = [&](LString Path, LString Remote, int64 ParentId, unsigned Depth)
	{
		Context = Path;
		if (Depth > 64)
			return Fail("The cache folder tree is too deep.");
		std::error_code DirectoryError;
		auto NativePath = std::filesystem::u8path(Path.Get());
		auto DirectoryStatus = std::filesystem::symlink_status(NativePath, DirectoryError);
		if (DirectoryError || !std::filesystem::is_directory(DirectoryStatus))
			return Fail("Cannot read the cache directory, or it is a symbolic link.");

		ImapCacheIndex Xml;
		auto &Meta = Xml.Root;
		LFile::Path MetadataPath(Path);
		MetadataPath += "Folder.xml";
		bool HasMetadata;
		if (!SourceFile(MetadataPath, HasMetadata))
			return false;
		if (HasMetadata)
		{
			LFile Input;
			if (!Input.Open(MetadataPath, O_READ) || !Xml.ReadIndex(Input))
				return Fail("Cannot read the folder XML.");
		}
		LFile::Path ProtoPath(Path);
		ProtoPath += "Folder.proto";
		bool HasProto;
		if (!SourceFile(ProtoPath, HasProto))
			return false;
		if (HasProto)
			return Fail("Protobuf caches must be converted to XML before importing.");

		int64 FolderId = -1;
		{
			LStatement Find(this, "select Id, ImapUidValidity from " MAIL3_TBL_FOLDER
				" where ImapAccountId=? and ImapRemoteName=?");
			if (!Find.IsOk() || !Find.SetInt64(0, AccountId) || !Find.SetStr(1, Remote))
				return Fail("Cannot look up the mailbox.");
			auto Result = sqlite3_step(Find);
			if (Result == SQLITE_ROW)
			{
				FolderId = Find.GetInt64(0);
				if (Find.GetInt64(1) > 0)
					return Fail("Cannot merge an unverified cache into a synchronized mailbox.");
			}
			else if (Result != SQLITE_DONE)
				return Fail("Cannot read the mailbox mapping.");
		}
		if (FolderId < 0)
		{
			LMail3Folder Folder(this);
			Folder.ParentId = ParentId;
			Folder.Name = Depth == 0
				? LString::Fmt("IMAP cache (%" PRIi64 ")", AccountId)
				: LString(LGetLeaf(Path));
			Folder.Sort = (int)AttributeInt(Meta, "Sort");
			Folder.Open = (int)AttributeInt(Meta, "Expanded");
			Folder.Unread = (int)AttributeInt(Meta, "Unread");
			Folder.Threaded = (int)AttributeInt(Meta, "Thread");
			Folder.ReadPerm = (int16_t)AttributeInt(Meta, "ReadPerm");
			Folder.WritePerm = (int16_t)AttributeInt(Meta, "WritePerm");
			if (!ValidAttributes)
				return Fail("Invalid integer folder metadata.");
			if (!Folder.Write(MAIL3_TBL_FOLDER, true))
				return Fail("Cannot create the canonical folder.");
			FolderId = Folder.Id;

			if (auto Fields = Meta.GetChildTag("Fields"))
			{
				for (auto Field: Fields->Children)
				{
					if (!Field->IsTag("Field"))
						return Fail("Invalid display-field element in the folder XML.");
					Folder.Flds.Insert(new Store3Field(this,
						(int)AttributeInt(*Field, "Id"), (int)AttributeInt(*Field, "Width")), -1, true);
				}
			}
			if (!ValidAttributes)
				return Fail("Invalid integer display-field metadata.");
			if (!Folder.Write(MAIL3_TBL_FOLDER, false))
				return Fail("Cannot save the folder display settings.");

			LMail3ImapMailbox Mailbox;
			Mailbox.AccountId = AccountId;
			Mailbox.Id = FolderId;
			Mailbox.RemoteName = Remote;
			if (!SaveImapMailbox(Mailbox))
				return Fail("Cannot create the mailbox mapping.");
		}
		else
		{
			LStatement CheckFolder(this, "select Id from " MAIL3_TBL_FOLDER " where Id=? and ParentId=?");
			if (!CheckFolder.IsOk() || !CheckFolder.SetInt64(0, FolderId) ||
				!CheckFolder.SetInt64(1, ParentId) || !CheckFolder.Row())
				return Fail("The imported folder was removed or reparented.");
		}

		LHashTbl<StrKey<char>, bool> IndexedFiles;
		LHashTbl<IntKey<int64>, bool> Uids;
		if (auto Emails = Meta.GetChildTag("Emails"))
		{
			for (auto Email: Emails->Children)
			{
				Context = Path;
				if (!Email->IsTag("Mail"))
					return Fail("Invalid message element in the folder XML.");
				int64 Uid = 0;
				auto UidText = Email->GetAttr("Uid");
				if (!ValidStr(UidText))
					return Fail("The message UID is missing from the folder XML.");
				for (auto Digit = UidText; *Digit; Digit++)
				{
					if (*Digit < '0' || *Digit > '9')
						return Fail("The message UID is not an unsigned decimal number.");
					Uid = Uid * 10 + (*Digit - '0');
					if (Uid > UINT32_MAX)
						return Fail("The message UID exceeds the IMAP UID range.");
				}
				if (!Uid || Uids.Find(Uid))
					return Fail("Invalid or duplicate message UID in the folder XML.");
				Uids.Add(Uid, true);
				LString Leaf = Email->GetContent();
				if (!Leaf)
					Leaf = LString::Fmt("%" PRIi64 ".eml", Uid);
				Leaf = Leaf.Strip();
				if (Leaf.Find("/") >= 0 || Leaf.Find("\\") >= 0 || Leaf == "." || Leaf == "..")
					return Fail("Invalid cache message filename.");
				IndexedFiles.Add(Leaf, true);
				LFile::Path MessagePath(Path);
				MessagePath += Leaf;
				Context = MessagePath.GetFull();

				LMail3Mail Mail(this);
				bool Downloaded = false;
				{
					// Epoch zero is deliberately not a server UIDVALIDITY.
					LStatement Find(this, "select Id, ImapDownloaded from " MAIL3_TBL_MAIL
						" where ParentId=? and ImapUidValidity=0 and ImapUid=?");
					if (!Find.IsOk() || !Find.SetInt64(0, FolderId) || !Find.SetInt64(1, Uid))
						return Fail("Cannot look up the cached message.");
					auto Result = sqlite3_step(Find);
					if (Result == SQLITE_ROW)
					{
						Mail.Id = Find.GetInt64(0);
						Downloaded = Find.GetBool(1);
					}
					else if (Result != SQLITE_DONE)
						return Fail("Cannot read the message mapping.");
				}
				if (Mail.Id > 0)
				{
					LStatement Existing(this, "select * from " MAIL3_TBL_MAIL " where Id=?");
					if (!Existing.IsOk() || !Existing.SetInt64(0, Mail.Id) ||
						!Existing.Row() || !Mail.Serialize(Existing, false) || Mail.ParentId != FolderId)
						return Fail("The imported mail row was removed or reparented.");
					if (Downloaded)
						continue;
				}
				Mail.ParentId = FolderId;
				Mail.AccountId = (int)AccountId;
				ImapMailFlags Flags(Email->GetAttr("Flags"));
				Mail.Flags = (int)AttributeInt(*Email, "Local",
					(Flags.ImapSeen ? MAIL_READ : 0) | (Flags.ImapAnswered ? MAIL_REPLIED : 0));
				Mail.Subject = Email->GetAttr("Subject");
				Mail.Label = Email->GetAttr("Label");
				Mail.MessageID = Email->GetAttr("MsgId");
				Mail.MarkColour = (uint32_t)AttributeInt(*Email, "Colour");
				Mail.MailSize = AttributeInt(*Email, "Size");
				if (!ValidAttributes)
					return Fail("Invalid integer message metadata.");
				LVariant From = Email->GetAttr("From"), Reply = Email->GetAttr("ReplyTo");
				Mail.From.SetValue("Text", From);
				Mail.Reply.SetValue("Text", Reply);
				Mail.DateSent.Set(Email->GetAttr("Date"));
				Mail.DateReceived = Mail.DateSent;

				bool HasContent;
				if (!SourceFile(MessagePath, HasContent))
					return false;
				if (HasContent)
				{
					LFile Input;
					if (!Input.Open(MessagePath, O_READ) || Mail.SetRfc822(&Input, true) != Store3Success)
						return Fail("Cannot import the RFC822 image.");
					if (!Mail.ParseHeaders())
						return Fail("Cannot parse the cached message headers.");
					Mail.MailSize = Input.GetSize();
					auto Stored = Mail.GetStream(_FL);
					if (!Stored || !ImapStreamsEqual(Input, *Stored))
						return Fail("The stored RFC822 image failed byte-for-byte verification.");
					Downloaded = true;
				}
				Mail.ImapUidValidity = 0;
				Mail.ImapUid = Uid;
				Mail.ImapRemoteFlags = Flags.All;
				Mail.ImapLocalFlags = Mail.Flags & ~MAIL_STORED_FLAT;
				Mail.ImapDownloaded = Downloaded ? 1 : 0;
				if (!Mail.Write(MAIL3_TBL_MAIL, Mail.Id < 0))
					return Fail("Cannot save the unverified UID mapping.");
			}
		}

		std::filesystem::directory_iterator Directory(NativePath, DirectoryError), End;
		for (; !DirectoryError && Directory != End; Directory.increment(DirectoryError))
		{
			LString Name = Directory->path().filename().u8string().c_str();
			auto Status = Directory->symlink_status(DirectoryError);
			if (DirectoryError)
				return Fail("Cannot inspect a cache directory entry.");
			if (std::filesystem::is_symlink(Status))
			{
				Context = Path + "/" + Name;
				return Fail("Symbolic links are not supported in a cache import.");
			}
			if (std::filesystem::is_directory(Status))
			{
				LFile::Path Child(Path);
				Child += Name;
				LString ChildRemote = Remote == "/" ? Remote + Name : Remote + "/" + Name;
				if (!ImportFolder(Child.GetFull(), ChildRemote, FolderId, Depth + 1))
					return false;
			}
			else if (auto Extension = LGetExtension(Name))
			{
				if (!Stricmp(Extension, "eml") && !IndexedFiles.Find(Name))
				{
					Context = Path + "/" + Name;
					return Fail("Unindexed or pending-APPEND mail must be recovered before importing.");
				}
			}
		}
		if (DirectoryError)
		{
			Context = Path;
			return Fail("Cannot enumerate the cache directory.");
		}
		return true;
	};

	bool Imported = ImportFolder(CachePath, "/", RootId, 0);
	if (!Imported)
	{
		Check(sqlite3_exec(Db, "rollback to ImapCacheImport", NULL, NULL, NULL), NULL);
		Check(sqlite3_exec(Db, "release ImapCacheImport", NULL, NULL, NULL), NULL);
		return false;
	}
	if (!Check(sqlite3_exec(Db, "release ImapCacheImport", NULL, NULL, NULL), NULL))
	{
		Check(sqlite3_exec(Db, "rollback to ImapCacheImport", NULL, NULL, NULL), NULL);
		Check(sqlite3_exec(Db, "release ImapCacheImport", NULL, NULL, NULL), NULL);
		return Fail("Cannot commit the imported cache.");
	}
	return true;
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
	LString TempTable;
	TableDefn Defs;

	if (Check == Store3UpgradeRequired)
	{
		// Get the old table format so we can copy over field by field later...
		if (!ParseTableFormat(Name, Defs))
		{
			return false;
		}

		TempTable = LString::Fmt("%s_tmp", Name);

		auto AlterSql = LString::Fmt("alter table %s rename to %s", Name, TempTable.Get());
		
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
		if (!Serialize(*s, true))
			return false;

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
					return false;
				}
			}
		}
		else
		{
			LgiTrace("%s:%i - Exec failed.\n", _FL);
			return false;
		}
	}
	else
	{
		LAssert(!"No statement");
		return false;
	}

	return Id >= 0;
}

//////////////////////////////////////////////////////////////////////////
LMail3Store::LStatement::LStatement(LMail3Store *store, const char *sql)
{
	Store = store;
	s = nullptr;
	LAssert(Store->InThread());

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
