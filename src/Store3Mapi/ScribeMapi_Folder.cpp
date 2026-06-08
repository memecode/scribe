#include "ScribeMapi.h"
#include "lgi/common/Com.h"

extern const GUID IID_IMessage;
DEFINE_OLEGUID(IID_IMAPIAdviseSink,	0x00020302, 0, 0);

// This is to catch new email events...
struct LMapiAdvise : public LUnknownImpl<IMAPIAdviseSink>
{
	LMapiFolder *folder = nullptr;
	ULONG_PTR connectionId = 0;
	IMsgStore *msgStore = nullptr;

	LMapiAdvise(LMapiFolder *f) :
		folder(f)
	{		
		AddInterface(IID_IMAPIAdviseSink, static_cast<IMAPIAdviseSink*>(this));

		if (f &&
			f->Store &&
			f->Store->MsgStore)
		{
			msgStore = f->Store->MsgStore;
			auto hr = msgStore->Advise(	(ULONG) f->Entry.Length(),
										(LPENTRYID) f->Entry.AddressOf(),
										fnevObjectCreated | fnevObjectDeleted | fnevObjectModified | fnevObjectMoved | fnevObjectCopied,
										this,
										&connectionId);
			if (FAILED(hr))
				LAssert(!"advise failed?");
			else
				printf("%s:%i - advise on folder '%s'\n", _FL, f->Name.Get());
		}
		else LAssert(!"missing param");
	}

	~LMapiAdvise()
	{
		if (msgStore && connectionId)
			msgStore->Unadvise(connectionId);
	}

	// IMAPIAdviseSink method
	STDMETHODIMP_(ULONG) OnNotify(ULONG cNotif, LPNOTIFICATION lpNotif)
	{
		return folder->OnNotify(cNotif, lpNotif);
	}
};

// MAPI folder impl:
LMapiFolder::LMapiFolder(LMapiStore *store)
{
	Store = store;
	if (!Parent)
		Name = Store->GetStr(FIELD_FOLDER_NAME);
}

LMapiFolder::~LMapiFolder()
{
	advise.Reset();
	if (Store->Root == this)
		Store->Root = nullptr;
	ReleaseHandle();
}

bool LMapiFolder::Set(LPMAPIFOLDER f)
{
	if (MapiFolder = f)
		advise.Reset(new LMapiAdvise(this));

	return MapiFolder != NULL;
}

ULONG LMapiFolder::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotif)
{
	if (!MapiFolder)
		return S_FALSE;

	LArray<LDataI*> newObjs, delObjs;

	for (ULONG i = 0; i < cNotif; i++)
	{
		OBJECT_NOTIFICATION &obj = lpNotif[i].info.obj;

		switch (lpNotif[i].ulEventType)
		{
			case fnevObjectCreated:
			{
				// A new object was created in the folder
				if (obj.ulObjType == MAPI_MESSAGE)
				{
					auto &children = LMapiFolder::Children();
					
					// loop over the contents table and find the new entry..
					LPMAPITABLE tbl = nullptr;
					auto hr = MapiFolder->GetContentsTable(0, &tbl);
					if (SUCCEEDED(hr) && tbl)
					{
						hr = tbl->SeekRow(BOOKMARK_BEGINNING, 0, nullptr);

						for (LMapiList contents(tbl); contents.More(); contents.Next())
						{
							auto entryProp = contents.GetField(PR_ENTRYID);
							LMapiEntry entry = entryProp;
							if (obj.cbEntryID != entry.Length())
								continue;
							if (memcmp(obj.lpEntryID, entry.AddressOf(), entry.Length()))
								continue;
							
							LAutoPtr<LDataI> t(Store->Create(MAGIC_MAIL));
							if (auto tptr = dynamic_cast<LMapiMail*>(t.Get()))
							{
								LMapiEntry entry = obj;
								tptr->Set(entryProp, this, &contents);
								Items.Insert(tptr, -1, true);
								newObjs.Add(tptr);
								t.Release();								
							}
						}
					}
				}
				// else fixme: other types like calendar and contacts?
				break;
			}
			case fnevObjectMoved:
			case fnevObjectDeleted:
			{
				if (obj.ulObjType == MAPI_MESSAGE)
				{
					// use: lpNotif[i].info.obj.lpEntryID
					int asd=0;
				}
				break;
			}
			case fnevObjectModified:
			{
				if (obj.ulObjType == MAPI_MESSAGE)
				{
					// use: lpNotif[i].info.obj.lpEntryID
					int asd=0;
				}
				break;
			}
		}
	}

	if (Store && Store->Callback)
	{
		if (newObjs.Length())
			Store->Callback->OnNew(this, newObjs, -1, true, true);

		if (delObjs.Length())
			Store->Callback->OnDelete(this, delObjs);
	}

	return S_OK;
}

bool LMapiFolder::Set(LMapiFolder *parent, LMapiList *Lst)
{
	auto p = Lst->GetField(PR_ENTRYID);
	if (!p)
		return false;

	Entry = p;

	Parent = parent;
	if (Lst)
	{	
		Name = MapiCastString(Lst->GetField(PR_DISPLAY_NAME));
		if (Name.Equals("Deleted Items") ||
			Name.Equals("Trash"))
		{
			FolderType = Store3SystemTrash;
			ItemType = MAGIC_ANY;
		}
		else if (Name.Equals("Sent"))
		{
			FolderType = Store3SystemSent;
		}
		else if (Name.Equals("Outbox"))
		{
			FolderType = Store3SystemOutbox;
		}
		
		Class = MapiCastString(Lst->GetField(PR_CONTAINER_CLASS));
		if (Class.Equals("IPF.Appointment"))
			ItemType = MAGIC_CALENDAR;
		else if (Class.Equals("IPF.Contact"))
			ItemType = MAGIC_CONTACT;

		Unread = MapiCastInt(Lst->GetField(PR_CONTENT_UNREAD));
	}

	if (Parent->Entry.Length() == 0 &&
		Store->InboxEntry.Length() > 0 &&
		Entry.Length() > 0)
	{
		// Check if we are the Inbox
		ULONG Result = 0;
		auto res = Store->Handle()->CompareEntryIDs(	(ULONG)Store->InboxEntry.Length(),
														(LPENTRYID)&Store->InboxEntry[0],
														(ULONG)Entry.Length(),
														(LPENTRYID)&Entry[0],
														0,
														&Result);
		if (SUCCEEDED(res) && Result)
			FolderType = Store3SystemInbox;
	}

	
	advise.Reset(new LMapiAdvise(this));
	
	return true;
}

LPMAPIFOLDER LMapiFolder::Handle()
{
	if (!MapiFolder)
	{
		if (Name.Equals("Server Failures"))
			return nullptr;

		if (Parent && Parent->MapiFolder)
		{
			auto startTs = LCurrentTime();

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

			auto endTs = LCurrentTime();
			if (endTs - startTs > 1000)
				LgiTrace("%s:%i - MapiFolder->OpenEntry took %ims, name=%s\n", _FL, (int)(endTs-startTs), Name.Get());

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
		MapiFolder = nullptr;
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
		case FIELD_FOLDER_PATH:
			return nullptr;
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
	switch (id)
	{
		case FIELD_PARENT:
			return Parent;
	}
	
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
			for (LMapiList Lst(Folders); Lst.More(); Lst.Next())
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
		LPMAPITABLE Tbl = nullptr;
		auto res = MapiFolder->GetContentsTable(0, &Tbl);
		if (SUCCEEDED(res))
		{
			for (LMapiList Lst(Tbl); Lst.More(); Lst.Next())
			{
				LAutoPtr<LDataI> t(Store->Create(ItemType));
				if (t)
				{
					auto tptr = dynamic_cast<LMapiThing*>(t.Get());
					if (tptr)
					{
						tptr->Set(Lst.GetField(PR_ENTRYID), this, &Lst);
						Items.Insert(tptr, -1, true);
						t.Release();
					}
					else LAssert(!"not a LMapiThing?");
				}
				else LAssert("store couldn't create object?");
			}
		}
		else Store->Error("%s:%i - GetContentsTable failed with %x\n", _FL, res);

		Items.State = Store3Loaded;
	}

	return Items;
}

LDataIterator<LDataPropI*> &LMapiFolder::Fields()
{
	Flds.State = Store3Loaded;
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

