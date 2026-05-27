#define INITGUID
#define USES_IID_IMAPIAdviseSink
#include <initguid.h>
#if _MSC_VER < _MSC_VER_VS2013
#include <MAPIguid.h>
#endif
#include "ScribeMapi.h"

/////////////////////////////////////////////////////////////////////////////
class LMapiAdviseSink : public IMAPIAdviseSink
{
	LMapiStore *Store;
	volatile LONG Refs;

public:
	#if _MSC_VER < _MSC_VER_VS2013
	ULONG Connection;
	#else
	ULONG_PTR Connection;
	#endif

	LMapiAdviseSink(LMapiStore *store)
	{
		Store = store;
		Refs = 1;
		Connection = 0;
	}
	
	~LMapiAdviseSink()
	{
		LAssert(Refs == 0);
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID FAR * ppvObj)
	{
		if (!ppvObj)
			return E_INVALIDARG;
		
		*ppvObj = NULL;
		
		if (IsEqualGUID(IID_IMAPIAdviseSink, riid))
		{
			*ppvObj = this;
			AddRef();
			return NOERROR;
		}
		
		return E_NOINTERFACE;
	}
	
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&Refs);
	}
	
	ULONG STDMETHODCALLTYPE Release()
	{
		if (Refs == 0)
			return 0;

		LONG r = InterlockedDecrement(&Refs);
		if (r == 0)
			delete this;
		return r;
	}

	ULONG STDMETHODCALLTYPE OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications)
	{
		if (!lpNotifications)
			return E_POINTER;
		return Store->OnNotify(cNotif, lpNotifications);
	}
};

/////////////////////////////////////////////////////////////////////////////
LMapiStore::LMapiStore(const char *profile, const char *username, const char *password, uint64 accountId, LDataEventsI *callback)
{
	Profile = profile;
	Username = username;
	Password = password;
	Callback = callback;
	AccountId = accountId;
	
	auto v = dynamic_cast<LViewI*>(Callback);
	Ui = 
		#ifndef __GTK_H__
		v ? (UI_TYPE)v->Handle() :
		#endif
		NULL;
	
	if (!Load("mapi32.dll"))
	{
		Error("%s:%i - Failed to load \"mapi32.dll\".\n", _FL);
	}
	
	MAPIInitialize				= (MAPIINITIALIZE*)				GetAddress("MAPIInitialize");
	MAPIUninitialize			= (MAPIUNINITIALIZE*)			GetAddress("MAPIUninitialize");
	MAPILogonEx					= (MAPILOGONEX*)				GetAddress("MAPILogonEx");
	MAPIAllocateBuffer			= (MAPIALLOCATEBUFFER*)			GetAddress("MAPIAllocateBuffer");
	MAPIFreeBuffer				= (MAPIFREEBUFFER*)				GetAddress("MAPIFreeBuffer");

	WrapCompressedRTFStream		= (pWrapCompressedRTFStream)	GetAddress("WrapCompressedRTFStream");
	HrCreateNewToMapiConverter	= (pHrCreateNewToMapiConverter)	GetAddress("HrCreateNewToMapiConverter");
	if (!HrCreateNewToMapiConverter)
	{
		// FFS microsoft... really?
		HrCreateNewToMapiConverter = (pHrCreateNewToMapiConverter) GetProcAddress(
			LLibrary::Handle(), 
			MAKEINTRESOURCEA(237) // Pass ordinal 237 instead of the string name
		);
	}

	if (Login())
	{
		ScribeMsgStores Stores(this, Session);

		MapiEntryRef *toOpen = nullptr;
		for (auto e: Stores)
		{
			availableProfiles.Add(e->DisplayName);
			if (e->DisplayName.Equals(Username))
				toOpen = e;
		}

		if (Callback)
			Callback->OnPropChange(this, FIELD_MAPI_PROFILES, GV_LIST);

		if (toOpen)
		{
			auto res = Session->OpenMsgStore(Ui,
											(ULONG)toOpen->Entry.Length(),	// entry bytes
											(LPENTRYID)&toOpen->Entry[0],	// ptr to entry
											NULL,						// default interface: IMsgStore
											MAPI_BEST_ACCESS,
											&MsgStore);
			if (SUCCEEDED(res))
			{
				Stores.Delete(toOpen);
				EntryRef.Reset(toOpen);
			}
		}

		if (MsgStore)
		{
			ULONG size = 0;
			LPENTRYID entry = NULL;
			HRESULT res = MsgStore->GetReceiveFolder
			(
				NULL,			// Get default receive folder
				MAPI_UNICODE,	// Flags
				&size,			// Size and ...
				&entry,			// Value of the EntryID to be returned
				NULL			// You don't care to see the class returned
			);
			if (SUCCEEDED(res))
			{
				InboxEntry.Add((uint8_t*)entry, size);
				MAPIFreeBuffer(entry);

				#if 1
				// Setup notification
				Notify = new LMapiAdviseSink(this);
				if (Notify)
				{					
					res = MsgStore->Advise(	(ULONG)InboxEntry.Length(),
											(LPENTRYID)&InboxEntry[0],
											fnevNewMail |
												fnevObjectCreated |
												fnevObjectDeleted |
												fnevObjectModified |
												fnevObjectMoved,
											Notify,
											&Notify->Connection);
				}
				#endif
			}
			else Error("%s:%i - GetReceiveFolder failed (0x%x).\n", _FL, res);
		}
	}
}

LMapiStore::~LMapiStore()
{
	// Make sure we have cleaned up
	SetInt(FIELD_IS_ONLINE, false);
	
	if (MapiInitialized && MAPIUninitialize)
		MAPIUninitialize();
}

ULONG LMapiStore::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications)
{
	LMapiFolder *Inbox = FindSystemFolder(Store3SystemInbox);
	if (!Inbox)
		return S_OK;

	LArray<LDataI*> NewItems, DelItems;
	
	for (ULONG i=0; i<cNotif; i++)
	{
		LPNOTIFICATION n = lpNotifications + i;
		switch (n->ulEventType)
		{
			case fnevNewMail:
			{
				LAutoPtr<LMapiMail> nm(new LMapiMail(this));
				if (nm)
				{
					SPropValue e;
					ZeroObj(e);
					e.Value.bin.lpb = (LPBYTE)n->info.newmail.lpEntryID;
					e.Value.bin.cb = n->info.newmail.cbEntryID;
					nm->Set(&e, Inbox, NULL);
					NewItems.Add(nm);
					Inbox->Items.Insert(nm.Release());
				}
				break;
			}
			case fnevObjectDeleted:
			{
				OBJECT_NOTIFICATION *o = &n->info.obj;
				int asd=0;
				break;
			}
			case fnevObjectCreated:
			{
				OBJECT_NOTIFICATION *o = &n->info.obj;
				int asd=0;
				break;
			}
			case fnevObjectModified:
			{
				OBJECT_NOTIFICATION *o = &n->info.obj;
				int asd=0;
				break;
			}
			case fnevObjectMoved:
			{
				OBJECT_NOTIFICATION *o = &n->info.obj;
				int asd=0;
				break;
			}
		}
	}

	if (NewItems.Length() && Callback)
		Callback->OnNew(Inbox, NewItems, -1, true, false);
	if (DelItems.Length() && Callback)
		Callback->OnDelete(Inbox, DelItems);
	
	return S_OK;
}

bool LMapiStore::Error(const char *Fmt, ...)
{
	va_list arg;
	va_start(arg, Fmt);
	char buffer[256];
	int ch = vsprintf_s(buffer, sizeof(buffer), Fmt, arg);
	va_end(arg);
	if (ch > 0)
		LgiTrace("%s", buffer);
	return false;
}

bool LMapiStore::Login()
{
    char16 Cur[MAX_PATH_LEN];
    GetCurrentDirectory(CountOf(Cur), Cur);

	if (!IsLoaded() ||
		!MAPIInitialize ||
		!MAPILogonEx ||
		!MAPIAllocateBuffer ||
		!MAPIFreeBuffer)
	{
		Error("%s:%i - Missing address of MAPI functions.\n", _FL);
		return false;
	}
	
	auto res = MAPIInitialize(NULL);
	if (FAILED(res))
	{
		Error("%s:%i - MAPIInitialize failed with 0x%x.\n", _FL, res);
		return false;
	}
	
	LAutoWString wProfile(Utf8ToWide(Profile));
	LAutoWString wPassword(Utf8ToWide(Password));
	
	MapiInitialized = true;	
	res = MAPILogonEx(	Ui,
						wProfile,
						wPassword,
						MAPI_LOGON_UI |
						MAPI_UNICODE |
							MAPI_EXTENDED |
							MAPI_NEW_SESSION |
							(wProfile ? MAPI_EXPLICIT_PROFILE : MAPI_USE_DEFAULT),
						&Session);
	if (FAILED(res) || !Session)
	{
		if (MAPIUninitialize)
			MAPIUninitialize();

		Error("%s:%i - MAPILogonEx failed (0x%x).\n", _FL, res);
		return false;
	}

    SetCurrentDirectory(Cur);
	return true;
}

IConverterSession *LMapiStore::CreateConverterSession()
{
	IConverterSession *cs = nullptr;

	auto hr = CoCreateInstance(	CLSID_IConverterSession, 
								nullptr, 
								CLSCTX_INPROC_SERVER, 
								IID_IConverterSession, 
								reinterpret_cast<void**>(&cs));
	if (SUCCEEDED(hr) && cs)
		return cs;

	// oh we can't have nice things :(
	if (!HrCreateNewToMapiConverter)
		return nullptr;

	hr = HrCreateNewToMapiConverter(&cs);
	if (SUCCEEDED(hr))
		return cs;

	// argh, still can't have nice things...
	return nullptr;
}

LMapiFolder *LMapiStore::FindSystemFolder(Store3SystemFolder Type)
{
	auto r = GetRoot();
	if (!r)
		return nullptr;
	
	auto &it = r->SubFolders();
	for (LDataFolderI *f=it.First(); f; f=it.Next())
	{
		if (f->GetInt(FIELD_SYSTEM_FOLDER) == Type)
		{
			LMapiFolder *Trash = dynamic_cast<LMapiFolder*>(f);
			LAssert(Trash != nullptr);
			return Trash;
		}
	}
	
	return nullptr;
}

bool LMapiStore::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	if (!Stricmp(MethodName, "init"))
	{
		if (!MAPIInitialize)
		{
			LgiTrace("%s:%i - no MAPIInitialize fn\n", _FL);
			return false;
		}
		
		auto hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (FAILED(hr))
		{
			LgiTrace("%s:%i - CoInitializeEx failed with: 0x%x\n", _FL, hr);
			return false;
		}

		MAPIINIT_0 mapiInit = { MAPIINIT_0_VERSION, MAPI_MULTITHREAD_NOTIFICATIONS };
		hr = MAPIInitialize(&mapiInit);
		if (FAILED(hr))
		{
			LgiTrace("%s:%i - MAPIInitialize failed with: 0x%x\n", _FL, hr);
			return false;
		}

		return true;
	}
	else LAssert(!"not impl");

	return false;
}

Store3Status LMapiStore::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_IS_ONLINE:
		{
			if (!i)
			{
				if (Root)
					Root->ReleaseHandle();
				
				EntryRef.Reset();
				if (Notify)
				{
					ULONG r = Notify->Release();
					Notify = NULL;
				}
				if (MsgStore)
				{
					ULONG r = MsgStore->Release();
					MsgStore = NULL;
				}

				if (Session)
				{
					HRESULT res = Session->Logoff(Ui, 0, 0);
					if (FAILED(res))
					{
						Error("%s:%i - Session->Logoff failed with %x\n", _FL, res);
					}
					
					ULONG r = Session->Release();
					LAssert(r == 0);
					Session = NULL; 
				}
			}
			break;
		}
		default:
			LAssert(0);
			return Store3Error;
	}
	
	return Store3Success;
}

int64 LMapiStore::GetInt(int id)
{
	switch (id)
	{
		case FIELD_IS_ONLINE:
			return Session != nullptr;
			break;
		case FIELD_ACCOUNT_ID:
			return AccountId;
		default:
			LAssert(0);
			break;
	}
	
	return -1;
}

const char *LMapiStore::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			RootName = Username + "@" + Profile;
			return RootName;
		case FIELD_STORE_TYPE:
			return "LMapiStore";
		case FIELD_MAPI_PROFILES:
			profileCache = LString(",").Join(availableProfiles);
			return profileCache;
		case FIELD_NAME:
			return Username;
	}
	
	return NULL;
}

bool LMapiStore::OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnChange(items, FieldHint);
}

LDataI *LMapiStore::Create(int Type)
{
	switch (Type)
	{
		case MAGIC_MAIL:
			return new LMapiMail(this);
		case MAGIC_FOLDER:
			return new LMapiFolder(this);
		case MAGIC_CALENDAR:
			return new LMapiCalendar(this);
		case MAGIC_CONTACT:
			return new LMapiContact(this);
		default:
			LAssert(!"Unsupport item type.");
			Error("%s:%i - Unsupported create type 0x%x\n", _FL, Type);
			break;
	}
	
	return NULL;
}

LDataFolderI *LMapiStore::GetRoot(bool create)
{
	if (Session && !Root)
	{
		if (EntryRef)
		{
			IMAPIFolder *r = NULL;
			if (EntryRef->OpenRoot(Session, Ui, &MsgStore, &r))
			{
				if ((Root = new LMapiFolder(this)))
				{
					Root->SetStr(FIELD_FOLDER_NAME, EntryRef->DisplayName);
					Root->Set(r);
				}
			}
		}
	}
	
	return Root;
}

Store3Status LMapiStore::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	LMapiFolder *Dest = dynamic_cast<LMapiFolder*>(NewFolder);
	if (!Dest ||
		Items.Length() == 0)
	{
		return Store3Error;
	}

	LArray<LDataI*> Moved;
	LMapiFolder *Source = NULL;
	LArray<SBinary> Entries;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		LMapiThing *t = dynamic_cast<LMapiThing*>(Items[i]);
		if (t && t->Parent)
		{
			if (!Source)
				Source = t->Parent;
			else if (Source != t->Parent)
			{
				Items.DeleteAt(i--);
				continue;
			}
			
			SBinary &bin = Entries.New();
			bin.lpb = &t->Entry[0];
			bin.cb = (ULONG)t->Entry.Length();
			if (t->UserData)
				Moved.Add(t);
		}
		else Items.DeleteAt(i--);
	}

	ENTRYLIST Msgs;
	Msgs.cValues = (ULONG)Entries.Length();
	Msgs.lpbin = &Entries[0];

	if (!Items.Length())
	{
		LAssert(!"No valid items to move.");
		return Store3Error;
	}

	if (!Source->Handle() || !Dest->Handle())
	{
		LAssert(!"Can't get folder handle.");
		return Store3Error;
	}
	
	HRESULT res = Source->Handle()->CopyMessages
	(
		&Msgs,
		NULL,
		Dest->Handle(),
		Ui,
		NULL, // Progress
		MESSAGE_MOVE // Flags
	);
	if (FAILED(res))
	{
		LAssert(!"CopyMessages failed.");
		Error("%s:%i - CopyMessages failed with 0x%x\n", _FL, res);
		return Store3Error;
	}
	
	for (unsigned i=0; i<Moved.Length(); i++)
	{
		LMapiThing *t = dynamic_cast<LMapiThing*>(Moved[i]);
		if (t)
		{
			Source->Items.a.Delete(t);
			Dest->Items.a.Add(t);
			t->Parent = Dest;
		}
	}
	
	if (Moved.Length() > 0 &&
		Callback &&
		!Callback->OnMove(NewFolder, Source, Moved))
	{
		return Store3Error;
	}

	return Store3Success;
}

Store3Status LMapiStore::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	if (Items.Length() == 0)
		return Store3Error;

	LMapiFolder *Trash = ToTrash ? FindSystemFolder(Store3SystemTrash) : NULL;
	if (Trash)
	{
	    LArray<LDataI*> MoveItems;
	    for (unsigned i=0; i<Items.Length(); i++)
	    {
	        LDataI *di = Items[i];
	        LMapiThing *t = dynamic_cast<LMapiThing*>(di);
	        if (t && t->Parent != Trash)
	        {
	            // Move the item to the trash instead...
	            MoveItems.Add(di);
	            Items.DeleteAt(i--, true);
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
	switch (Item->Type())
	{
		default:
		{
			LMapiThing *t = dynamic_cast<LMapiThing*>(Item);
			if (!t)
				s = Store3Error;
			else if (Callback && !Callback->OnDelete(t->Parent, Items))
				s = Store3Error;
			else
			{
				LArray<LMapiThing*> Del;
				LMapiFolder *Parent = NULL;

				for (unsigned i=0; i<Items.Length(); i++)
				{
					t = dynamic_cast<LMapiThing*>(Items[i]);
					if (!t)
						continue;

					if (!Parent)
						Parent = t->Parent;
					if (t->Parent != NULL &&
						t->Parent == Parent)
					{
						Del.Add(t);
					}
				}

				if (Del.Length() > 0)
				{
					ENTRYLIST Msgs;
					LArray<SBinary> Entries;
					
					Msgs.cValues = (ULONG)Del.Length();
					Entries.Length(Del.Length());
					
					for (unsigned i=0; i<Del.Length(); i++)
					{
						t = Del[i];
						SBinary &e = Entries[i];
						e.cb = (ULONG)t->Entry.Length();
						e.lpb = &t->Entry[0];
					}
					
					Msgs.lpbin = &Entries[0];
					
					HRESULT Status = Parent->Handle()->DeleteMessages(	&Msgs,
																		NULL/*UI*/,
																		NULL/*Prog*/,
																		0/*Flags*/);

					if (SUCCEEDED(Status))
					{
						for (unsigned i=0; i<Del.Length(); i++)
						{
							t = Del[i];
							t->Parent->Items.Delete(t);
							t->Parent = NULL;
							DeleteObj(t);
						}
					}
					else
					{
						Error("%s:%i - DeleteMessages failed with 0x%x\n", _FL, Status);
						s = Store3Error;
					}
				}
			}

			break;
		}
		case MAGIC_FOLDER:
		{
			for (unsigned i=0; i<Items.Length(); i++)
			{
				LMapiFolder *f = dynamic_cast<LMapiFolder*>(Items[i]);
				if (!f)
				{
					s = Store3Error;
					break;
				}

				LArray<LDataI*> Del;
				Del.Add(f);
				if (Callback && !Callback->OnDelete(f->Parent, Del))
				{
					s = Store3Error;
					break;
				}
				
				if (f->Parent &&
					f->Parent->MapiFolder)
				{
					HRESULT res = f->Parent->MapiFolder->DeleteFolder
					(
						(ULONG)f->Entry.Length(),
						(LPENTRYID) &f->Entry[0],
						Ui,
						NULL, // Progress
						0 // Flags
					);
					if (FAILED(res))
					{
						Error("%s:%i - Failed to delete folder '%s', err=0x%x\n", _FL, f->Name.Get(), res);
						s = Store3Error;
						break;
					}
				}
				
				if (f->Parent)
					f->Parent->Sub.Delete(f);
				else
					LAssert(0);
				DeleteObj(f);
			}
			break;
		}
	}

	return s;
}

Store3Status LMapiStore::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator)
{
	switch (PropId)
	{
		case FIELD_FLAGS:
		{
			uint64 v = Value.CastInt64();
			if (v == MAIL_READ)
			{
				LArray<LDataI*> Changed;
				
				for (unsigned i=0; i<Items.Length(); i++)
				{
					LDataI *d = Items[i];
					int64 Old = d->GetInt(FIELD_FLAGS);
					if ((Old & MAIL_READ) == 0)
					{
						d->SetInt(FIELD_FLAGS, Old | MAIL_READ);
						Changed.Add(d);
					}
				}
				
				if (Changed.Length())
					OnChange(_FL, Changed, PropId);
			}
			break;
		}
		default:
		{
			LAssert(!"Unsupport prop ID.");
			return Store3Error;
		}
	}
	
	return Store3Success;
}

void LMapiStore::Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus)
{
	if (OnStatus)
		OnStatus(true);
}

void LMapiStore::OnEvent(void *Param)
{
}

bool LMapiStore::OnIdle()
{
	if (Dirty.Length() > 0)
	{
		for (unsigned i=0; i<Dirty.Length(); i++)
		{
			LMapiThing *t = Dirty[i];
			if (t && t->MapiMsg)
			{
				HRESULT res = t->MapiMsg->SaveChanges(KEEP_OPEN_READWRITE);
				if (FAILED(res))
				{
					LgiTrace("%s:%i - SaveChanges failed with 0x%x\n", _FL, res);
				}
				
				t->IsDirty = false;
			}
		}
		Dirty.Length(0);
	}
	
	return false;
}

LDataEventsI *LMapiStore::GetEvents()
{
	return NULL;
}

//////////////////////////////////////////////////////
LDataStoreI *LMapiThing::GetStore()
{
	return Store;
}

bool MapiEntryRef::OpenRoot(LPMAPISESSION Session, UI_TYPE UiHnd, IMsgStore **MsgStore, IMAPIFolder **RootFolder)
{
	bool Status = false;

	if (Session && MsgStore && RootFolder)
	{
		if (!*MsgStore)
		{
			HRESULT res = Session->OpenMsgStore(UiHnd,
												(ULONG)Entry.Length(),			// entry bytes
												(LPENTRYID)&Entry[0],	// ptr to entry
												NULL,					// default interface: IMsgStore
												MAPI_BEST_ACCESS,
												MsgStore);
			if (FAILED(res))
			{
				Store->Error("%s:%i - OpenMsgStore failed with 0x%x\n", _FL, res);
			}
		}
		
		if (*MsgStore)
		{
			SPropValue *SubTree = MapiGetProp(*MsgStore, PR_IPM_SUBTREE_ENTRYID);
			if (SubTree)
			{
				ULONG ObjType;
				HRESULT res = (*MsgStore)->OpenEntry(SubTree->Value.bin.cb,
													(LPENTRYID)SubTree->Value.bin.lpb,
													NULL,
													MAPI_MODIFY,
													&ObjType,
													(IUnknown**) RootFolder);
				if (SUCCEEDED(res) && *RootFolder)
				{
					Status = true;
				}
				else
				{
					(*MsgStore)->Release();
					*MsgStore = 0;
					
					return Store->Error("%s:%i - OpenEntry failed with 0x%x\n", _FL, res);
				}
			}
		}
	}

	return Status;
}

//////////////////////////////////////////////////////
LDataStoreI *OpenMapiStore(	const char *Profile,
							const char *Username,
							const char *Password,
							uint64 AccountId,
							LDataEventsI *Callback)
{
	return new LMapiStore(Profile, Username, Password, AccountId, Callback);
}
