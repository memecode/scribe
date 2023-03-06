#include "ScribeMapi.h"

extern const GUID IID_IMessage;

LMapiFolder::LMapiFolder(LMapiStore *store)
{
	Store = store;
	Parent = NULL;
	MapiFolder = NULL;
	FolderType = Store3SystemNone;

	Unread = 0;
	IsOpen = false;
	SortIndex = -6;
	ItemType = MAGIC_MAIL;

	if (!Parent)
		Name = Store->GetStr(FIELD_FOLDER_NAME);
}

LMapiFolder::~LMapiFolder()
{
	if (Store->Root == this)
		Store->Root = NULL;
	ReleaseHandle();
}

bool LMapiFolder::Set(LPMAPIFOLDER f)
{
	MapiFolder = f;
	return MapiFolder != NULL;
}

bool LMapiFolder::Set(LMapiFolder *parent, ScribeMapiList *Lst)
{
	SPropValue *p = Lst->GetField(PR_ENTRYID);
	if (!p)
		return false;

	Entry.Add(p->Value.bin.lpb, p->Value.bin.cb);

	Parent = parent;
	if (Lst)
	{	
		Name = MapiCastString(Lst->GetField(PR_DISPLAY_NAME));
		if (Name)
		{
			if (!_stricmp(Name, "Deleted Items") ||
				!_stricmp(Name, "Trash"))
			{
				FolderType = Store3SystemTrash;
				ItemType = MAGIC_ANY;
			}
			else if (!_stricmp(Name, "Sent"))
			{
				FolderType = Store3SystemSent;
			}
			else if (!_stricmp(Name, "Outbox"))
			{
				FolderType = Store3SystemOutbox;
			}
		}
		
		Class = MapiCastString(Lst->GetField(PR_CONTAINER_CLASS));
		if (Class)
		{
			if (!_stricmp(Class, "IPF.Appointment"))
				ItemType = MAGIC_CALENDAR;
			else if (!_stricmp(Class, "IPF.Contact"))
				ItemType = MAGIC_CONTACT;
		}

		Unread = MapiCastInt(Lst->GetField(PR_CONTENT_UNREAD));
	}

	if (Parent->Entry.Length() == 0 &&
		Store->InboxEntry.Length() > 0 &&
		Entry.Length() > 0)
	{
		// Check if we are the Inbox
		ULONG Result = 0;
		HRESULT res = Store->Handle()->CompareEntryIDs(	(ULONG)Store->InboxEntry.Length(),
														(LPENTRYID)&Store->InboxEntry[0],
														(ULONG)Entry.Length(),
														(LPENTRYID)&Entry[0],
														0,
														&Result);
		if (SUCCEEDED(res) && Result)
			FolderType = Store3SystemInbox;
	}
	
	return true;
}

LPMAPIFOLDER LMapiFolder::Handle()
{
	if (!MapiFolder)
	{
		if (Parent && Parent->MapiFolder)
		{
			ULONG Type;
			HRESULT res = Parent->MapiFolder->OpenEntry
			(
				(ULONG)Entry.Length(),
				(LPENTRYID)&Entry[0],
				NULL,
				MAPI_BEST_ACCESS,
				&Type,
				(IUnknown**)&MapiFolder
			);
			if (FAILED(res) || !MapiFolder)
			{
				Store->Error("%s:%i - OpenEntry failed with 0x%x\n", _FL, res);
			}
		}
		else LAssert(!"No parent MAPI folders.");
	}
	
	return MapiFolder;
}

void LMapiFolder::ReleaseHandle()
{
	if (MapiFolder)
	{
		MapiFolder->Release();
		MapiFolder = NULL;
	}
	for (unsigned i=0; i<Items.Length(); i++)
	{
		Items.a[i]->ReleaseHandle();
	}
	for (unsigned i=0; i<Sub.Length(); i++)
	{
		Sub.a[i]->ReleaseHandle();
	}
}

Store3CopyImpl(LMapiFolder)
{
	LAssert(0);
	return false;
}

const char *LMapiFolder::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			return Name;
		default:
			LAssert(0);
			break;
	}

	return NULL;
}

Store3Status LMapiFolder::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			Name = str;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 LMapiFolder::GetInt(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_TYPE:
			return ItemType;
		case FIELD_IS_ONLINE:
			return true;
		case FIELD_UNREAD:
			return Unread;
		case FIELD_FOLDER_OPEN:
			return IsOpen;
		case FIELD_FOLDER_PERM_READ:
		case FIELD_FOLDER_PERM_WRITE:
			return PermRequireUser;
		case FIELD_FOLDER_THREAD:
			return false;
		case FIELD_SORT:
			return SortIndex;
		case FIELD_FOLDER_INDEX:
			return -1;
		case FIELD_LOADED:
			return Items.State == Store3Loaded;
		case FIELD_SYSTEM_FOLDER:
			return FolderType;
		case FIELD_STORE_TYPE:
			return Store3Mapi;
		default:
			LAssert(0);
			break;
	}

	return -1;
}

Store3Status LMapiFolder::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_FOLDER_OPEN:
		{
			IsOpen = i != 0;
			break;
		}
		case FIELD_SORT:
		{
			SortIndex = (int)i;
			break;
		}
		case FIELD_UNREAD:
		{
			Unread = i;
			break;
		}
		default:
		{
			LAssert(0);
			return Store3Error;
		}
	}

	return Store3Success;
}

const LDateTime *LMapiFolder::GetDate(int id)
{
	LAssert(0);
	return NULL;
}

Store3Status LMapiFolder::SetDate(int id, const LDateTime *i)
{
	LAssert(0);
	return Store3Success;
}

LDataPropI *LMapiFolder::GetObj(int id)
{
	LAssert(0);
	return NULL;
}

LDataIt LMapiFolder::GetList(int id)
{
	LAssert(0);
	return NULL;
}

Store3Status LMapiFolder::SetRfc822(LStreamI *m)
{
	LAssert(0);
	return Store3Error;
}

uint32_t LMapiFolder::Type()
{
	return MAGIC_FOLDER;
}

bool LMapiFolder::IsOnDisk()
{
	return true;
}

bool LMapiFolder::IsOrphan()
{
	return false;
}

uint64 LMapiFolder::Size()
{
	return 0;
}

Store3Status LMapiFolder::Save(LDataI *Parent)
{
	return Store3Success;
}

Store3Status LMapiFolder::Delete(bool ToTrash)
{
	return Store3Error;
}

LDataStoreI *LMapiFolder::GetStore()
{
	return Store;
}

LAutoStreamI LMapiFolder::GetStream(const char *file, int line)
{
	LAutoStreamI s;
	return s;
}

LDataIterator<LDataFolderI*> &LMapiFolder::SubFolders()
{
	#if 1
	if (Sub.State == Store3Unloaded &&
		Handle())
	{
		LPMAPITABLE Folders = 0; // Released by ScribeMapiList
		HRESULT res = MapiFolder->GetHierarchyTable(MAPI_UNICODE , &Folders);
		if (SUCCEEDED(res))
		{
			for (ScribeMapiList Lst(Folders); Lst.More(); Lst.Next())
			{
				LMapiFolder *SubFolder = new LMapiFolder(Store);
				if (SubFolder)
				{
					SubFolder->Set(this, &Lst);
					Sub.Insert(SubFolder, -1, true);
				}
			}			
		}
		else Store->Error("%s:%i - GetHierarchyTable failed with %x\n", _FL, res);

		Sub.State = Store3Loaded;
	}
	#endif

	return Sub;
}

LDataIterator<LDataI*> &LMapiFolder::Children()
{
	if (Items.State == Store3Unloaded &&
		Handle())
	{
		LPMAPITABLE Tbl = 0;
		HRESULT res = MapiFolder->GetContentsTable(0, &Tbl);
		if (SUCCEEDED(res))
		{
			for (ScribeMapiList Lst(Tbl); Lst.More(); Lst.Next())
			{
				LAutoPtr<LDataI> t(Store->Create(ItemType));
				if (t)
				{
					LMapiThing *tptr = dynamic_cast<LMapiThing*>(t.Get());
					if (tptr)
					{
						tptr->Set(Lst.GetField(PR_ENTRYID), this, &Lst);
						Items.Insert(tptr, -1, true);
						t.Release();
					}
				}
			}
		}
		else Store->Error("%s:%i - GetContentsTable failed with %x\n", _FL, res);

		Items.State = Store3Loaded;
	}

	return Items;
}

LDataIterator<LDataPropI*> &LMapiFolder::Fields()
{
	return Flds;
}

Store3Status LMapiFolder::DeleteAllChildren()
{
	return Store3Error;
}

Store3Status LMapiFolder::FreeChildren()
{
	return Store3Error;
}

void LMapiFolder::OnSelect(bool s)
{
}

void LMapiFolder::OnCommand(const char *Name)
{
}

//////////////////////////////////////////////////////////////////////////
LMapiFolderField::LMapiFolderField(LMapiStore *store)
{
	Store = store;
	Id = -1;
	Width = 100;
}

LMapiFolderField::~LMapiFolderField()
{
}

LDataPropI &LMapiFolderField::operator =(LDataPropI &p)
{
	return *this;
}

const char *LMapiFolderField::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
			return Name;
		default:
			LAssert(0);
			break;
	}

	return NULL;
}

Store3Status LMapiFolderField::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_NAME:
			Name = str;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 LMapiFolderField::GetInt(int id)
{
	switch (id)
	{
		case FIELD_ID:
			return Id;
		case FIELD_WIDTH:
			return Width;
		default:
			LAssert(0);
			break;
	}

	return -1;
}

Store3Status LMapiFolderField::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_ID:
			Id = (int)i;
			break;
		case FIELD_WIDTH:
			Width = (int)i;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

const LDateTime *LMapiFolderField::GetDate(int id)
{
	return NULL;
}

Store3Status LMapiFolderField::SetDate(int id, const LDateTime *i)
{
	return Store3Error;
}

LDataPropI *LMapiFolderField::GetObj(int id)
{
	return NULL;
}

LDataIt LMapiFolderField::GetList(int id)
{
	return NULL;
}

Store3Status LMapiFolderField::SetRfc822(LStreamI *m)
{
	return Store3Error;
}

