#define INITGUID
#define USES_IID_IMAPIAdviseSink
#include <initguid.h>
#if _MSC_VER < _MSC_VER_VS2013
#include <MAPIguid.h>
#endif
#include "ScribeMapi.h"

/////////////////////////////////////////////////////////////////////////////
class GMapiAdviseSink : public IMAPIAdviseSink
{
	GMapiStore *Store;
	volatile LONG Refs;

public:
	#if _MSC_VER < _MSC_VER_VS2013
	ULONG Connection;
	#else
	ULONG_PTR Connection;
	#endif

	GMapiAdviseSink(GMapiStore *store)
	{
		Store = store;
		Refs = 1;
		Connection = 0;
	}
	
	~GMapiAdviseSink()
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
GMapiStore::GMapiStore(const char *profile, const char *username, const char *password, uint64 accountId, LDataEventsI *callback)
{
	Session = NULL;
	MsgStore = NULL;
	Profile = profile;
	Username = username;
	Password = password;
	Callback = callback;
	AccountId = accountId;
	Notify = NULL;
	MapiInitialized = false;
	Root = NULL;
	
	LViewI *v = dynamic_cast<LViewI*>(Callback);
	Ui = 
		#ifndef __GTK_H__
		v ? (UI_TYPE)v->Handle() :
		#endif
		NULL;
	
	if (!Load("mapi32.dll"))
	{
		Error("%s:%i - Failed to load \"mapi32.dll\".\n", _FL);
	}
	
	MAPIInitialize			= (MAPIINITIALIZE*)				GetAddress("MAPIInitialize");
	MAPILogonEx				= (MAPILOGONEX*)				GetAddress("MAPILogonEx");
	MAPIAllocateBuffer		= (MAPIALLOCATEBUFFER*)			GetAddress("MAPIAllocateBuffer");
	MAPIFreeBuffer			= (MAPIFREEBUFFER*)				GetAddress("MAPIFreeBuffer");
	WrapCompressedRTFStream = (pWrapCompressedRTFStream)	GetAddress("WrapCompressedRTFStream");
	MAPIUninitialize		= (MAPIUNINITIALIZE*)			GetAddress("MAPIUninitialize");

	if (Login())
	{
		ScribeMsgStores Stores(this, Session);

		for (unsigned i=0; i<Stores.Length(); i++)
		{
			MapiEntryRef *e = Stores[i];
			if (e->DisplayName && stristr(e->DisplayName, Username))
			{
				HRESULT res = Session->OpenMsgStore(Ui,
													e->Entry.Length(),		// entry bytes
													(LPENTRYID)&e->Entry[0],// ptr to entry
													NULL,					// default interface: IMsgStore
													MAPI_BEST_ACCESS,
													&MsgStore);
				if (SUCCEEDED(res))
				{
					Stores.Delete(e);
					EntryRef.Reset(e);
					break;
				}
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
				Notify = new GMapiAdviseSink(this);
				if (Notify)
				{					
					res = MsgStore->Advise(	InboxEntry.Length(),
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

GMapiStore::~GMapiStore()
{
	MAPIUninitialize = NULL;
	MAPIInitialize = NULL;
	MAPILogonEx = NULL;
	MAPIAllocateBuffer = NULL;
	MAPIFreeBuffer = NULL;
	WrapCompressedRTFStream = NULL;

	// Make sure we have cleaned up
	SetInt(FIELD_IS_ONLINE, false);
	
	if (MapiInitialized && MAPIUninitialize)
		MAPIUninitialize();
}

ULONG GMapiStore::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications)
{
	GMapiFolder *Inbox = FindSystemFolder(Store3SystemInbox);
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
				LAutoPtr<GMapiMail> nm(new GMapiMail(this));
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
		Callback->OnNew(Inbox, NewItems, -1, true);
	if (DelItems.Length() && Callback)
		Callback->OnDelete(Inbox, DelItems);
	
	return S_OK;
}

bool GMapiStore::Error(const char *Fmt, ...)
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

bool GMapiStore::Login()
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
	
	HRESULT res = MAPIInitialize(NULL);
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
						MAPI_LOGON_UI | MAPI_EXTENDED,
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

GMapiFolder *GMapiStore::FindSystemFolder(Store3SystemFolder Type)
{
	LDataFolderI *r = GetRoot();
	if (!r)
		return NULL;
	
	LDataIterator<LDataFolderI*> &it = r->SubFolders();
	for (LDataFolderI *f=it.First(); f; f=it.Next())
	{
		if (f->GetInt(FIELD_SYSTEM_FOLDER) == Type)
		{
			GMapiFolder *Trash = dynamic_cast<GMapiFolder*>(f);
			LAssert(Trash != NULL);
			return Trash;
		}
	}
	
	return NULL;
}


Store3Status GMapiStore::SetInt(int id, int64 i)
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

int64 GMapiStore::GetInt(int id)
{
	switch (id)
	{
		case FIELD_IS_ONLINE:
			return Session != NULL;
			break;
		case FIELD_ACCOUNT_ID:
			return AccountId;
		default:
			LAssert(0);
			break;
	}
	
	return -1;
}

const char *GMapiStore::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			RootName = Username + "@" + Profile;
			return RootName;
	}
	
	return NULL;
}

bool GMapiStore::OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnChange(items, FieldHint);
}

LDataI *GMapiStore::Create(int Type)
{
	switch (Type)
	{
		case MAGIC_MAIL:
			return new GMapiMail(this);
		case MAGIC_FOLDER:
			return new GMapiFolder(this);
		case MAGIC_CALENDAR:
			return new GMapiCalendar(this);
		case MAGIC_CONTACT:
			return new GMapiContact(this);
		default:
			LAssert(!"Unsupport item type.");
			Error("%s:%i - Unsupported create type 0x%x\n", _FL, Type);
			break;
	}
	
	return NULL;
}

LDataFolderI *GMapiStore::GetRoot(bool create)
{
	if (Session && !Root)
	{
		if (EntryRef)
		{
			IMAPIFolder *r = NULL;
			if (EntryRef->OpenRoot(Session, Ui, &MsgStore, &r))
			{
				if ((Root = new GMapiFolder(this)))
				{
					Root->SetStr(FIELD_FOLDER_NAME, EntryRef->DisplayName);
					Root->Set(r);
				}
			}
		}
		// else LAssert(0);
	}
	
	return Root;
}

Store3Status GMapiStore::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	GMapiFolder *Dest = dynamic_cast<GMapiFolder*>(NewFolder);
	if (!Dest ||
		Items.Length() == 0)
	{
		return Store3Error;
	}

	LArray<LDataI*> Moved;
	GMapiFolder *Source = NULL;
	LArray<SBinary> Entries;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		GMapiThing *t = dynamic_cast<GMapiThing*>(Items[i]);
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
			bin.cb = t->Entry.Length();
			if (t->UserData)
				Moved.Add(t);
		}
		else Items.DeleteAt(i--);
	}

	ENTRYLIST Msgs;
	Msgs.cValues = Entries.Length();
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
		GMapiThing *t = dynamic_cast<GMapiThing*>(Moved[i]);
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

Store3Status GMapiStore::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	if (Items.Length() == 0)
		return Store3Error;

	GMapiFolder *Trash = ToTrash ? FindSystemFolder(Store3SystemTrash) : NULL;
	if (Trash)
	{
	    LArray<LDataI*> MoveItems;
	    for (unsigned i=0; i<Items.Length(); i++)
	    {
	        LDataI *di = Items[i];
	        GMapiThing *t = dynamic_cast<GMapiThing*>(di);
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
			GMapiThing *t = dynamic_cast<GMapiThing*>(Item);
			if (!t)
				s = Store3Error;
			else if (Callback && !Callback->OnDelete(t->Parent, Items))
				s = Store3Error;
			else
			{
				LArray<GMapiThing*> Del;
				GMapiFolder *Parent = NULL;

				for (unsigned i=0; i<Items.Length(); i++)
				{
					t = dynamic_cast<GMapiThing*>(Items[i]);
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
					
					Msgs.cValues = Del.Length();
					Entries.Length(Del.Length());
					
					for (unsigned i=0; i<Del.Length(); i++)
					{
						t = Del[i];
						SBinary &e = Entries[i];
						e.cb = t->Entry.Length();
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
				GMapiFolder *f = dynamic_cast<GMapiFolder*>(Items[i]);
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
						f->Entry.Length(),
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

Store3Status GMapiStore::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator)
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

bool GMapiStore::Compact(LViewI *Parent, LDataPropI *Props)
{
	LAssert(0);
	return false;
}

void GMapiStore::OnEvent(void *Param)
{
}

bool GMapiStore::OnIdle()
{
	if (Dirty.Length() > 0)
	{
		for (unsigned i=0; i<Dirty.Length(); i++)
		{
			GMapiThing *t = Dirty[i];
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

LDataEventsI *GMapiStore::GetEvents()
{
	return NULL;
}

//////////////////////////////////////////////////////
LDataStoreI *GMapiThing::GetStore()
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
												Entry.Length(),			// entry bytes
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
	return new GMapiStore(Profile, Username, Password, AccountId, Callback);
}
