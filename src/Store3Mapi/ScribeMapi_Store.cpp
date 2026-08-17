#define INITGUID
#define USES_IID_IMAPIAdviseSink
#include <initguid.h>
#if _MSC_VER < _MSC_VER_VS2013
#include <MAPIguid.h>
#endif
#include <tchar.h>
#include "ScribeMapi.h"
#include "lgi/common/Com.h"

/////////////////////////////////////////////////////////////////////////////
/*
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
*/

/////////////////////////////////////////////////////////////////////////////
LMapiStore::LMapiStore(	const char *profile,
						const char *username,
						const char *password,
						uint64 accountId,
						LDataEventsI *callback,
						LStream *log) :
	Profile(profile),
	Username(username),
	Password(password),
	AccountId(accountId),
	Callback(callback),
	Log(log)
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
		ERR("%s:%i - Failed to load \"mapi32.dll\".\n", _FL);
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

	if (!Login())
		LOG("%s:%i - Login failed.\n", _FL);
	else
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
			ULONG ulStoreFlags = MDB_WRITE | MAPI_BEST_ACCESS;
			auto res = Session->OpenMsgStore(Ui,
											(ULONG)toOpen->Entry.Length(),	// entry bytes
											(LPENTRYID)&toOpen->Entry[0],	// ptr to entry
											NULL,						// default interface: IMsgStore
											ulStoreFlags,
											&MsgStore);
			
			if (SUCCEEDED(res))
			{
				Stores.Delete(toOpen);
				EntryRef.Reset(toOpen);
			}
			else
			{
				LOG("%s:%i - OpenMsgStore failed with 0x%x.\n", _FL, res);
				Stores.Delete(toOpen);
			}
		}
		else LOG("%s:%i - No profile found for '%s'.\n", _FL, Username.Get());

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

				#if false
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
			else ERR("%s:%i - GetReceiveFolder failed (0x%x).\n", _FL, res);
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

void LMapiStore::LOG(const char *Fmt, ...)
{
	if (!Log)
		return;

	va_list arg;
	va_start(arg, Fmt);
	LStreamPrintf(Log, LogMsg, Fmt, arg);
	va_end(arg);
}

bool LMapiStore::ERR(const char *Fmt, ...)
{
	va_list arg;
	va_start(arg, Fmt);
	if (Log)
		LStreamPrintf(Log, LogErr, Fmt, arg);
	else
		LgiTrace("%s", Fmt);
	va_end(arg);
	return false;
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
				if (!nm)
					continue;

				SPropValue e = {};
				e.ulPropTag = PROP_TAG(PR_ENTRYID, PT_BINARY);
				e.Value.bin.lpb = (LPBYTE)n->info.newmail.lpEntryID;
				e.Value.bin.cb = n->info.newmail.cbEntryID;
				nm->Set(&e, Inbox, NULL);
				
				NewItems.Add(nm);				
				Inbox->Items.Insert(nm.Release());

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
		ERR("%s:%i - Missing address of MAPI functions.\n", _FL);
		return false;
	}
	
	MAPIINIT_0 mapiInit = { MAPI_INIT_VERSION, MAPI_MULTITHREAD_NOTIFICATIONS };
	auto res = MAPIInitialize(&mapiInit);
	if (FAILED(res))
	{
		ERR("%s:%i - MAPIInitialize failed with 0x%x.\n", _FL, res);
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
						MAPI_NEW_SESSION |  // Force a completely isolated session
						MAPI_NO_MAIL |
						(wProfile ? MAPI_EXPLICIT_PROFILE : 0),
						&Session);
	if (FAILED(res) || !Session)
	{
		if (MAPIUninitialize)
			MAPIUninitialize();

		ERR("%s:%i - MAPILogonEx failed (0x%x).\n", _FL, res);
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
	if (!Stricmp(MethodName, Method_init))
	{
		if (!MAPIInitialize)
		{
			ERR("%s:%i - no MAPIInitialize fn\n", _FL);
			return false;
		}
		
		auto hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (FAILED(hr))
		{
			ERR("%s:%i - CoInitializeEx failed with: 0x%x\n", _FL, hr);
			return false;
		}

		MAPIINIT_0 mapiInit = { MAPIINIT_0_VERSION, MAPI_MULTITHREAD_NOTIFICATIONS };
		hr = MAPIInitialize(&mapiInit);
		if (FAILED(hr))
		{
			ERR("%s:%i - MAPIInitialize failed with: 0x%x\n", _FL, hr);
			return false;
		}

		return true;
	}
	else if (!Stricmp(MethodName, Method_sendMessage))
	{
		LString mailFrom = Args.StringAt(SendMsg_MailFrom);
		auto rcptTo = LString(Args.StringAt(SendMsg_RcptTo)).SplitDelimit(",");
		LString data = Args.StringAt(SendMsg_Data);
		if (mailFrom.IsEmpty() ||
			rcptTo.Length() == 0 ||
			data.IsEmpty())
		{
			ERR("%s:%i - sendMessage missing required args\n", _FL);
			return false;
		}

		if (!Session || !MsgStore)
		{
			ERR("%s:%i - sendMessage failed: not logged in\n", _FL);
			return false;
		}

		if (auto support = mapi.MapiGetProp(MsgStore, PR_STORE_SUPPORT_MASK))
		{
			auto mask = (ULONG)mapi.MapiCastInt(support);
			LOG("%s:%i - sendMessage store support mask=0x%x\n", _FL, mask);
			#ifdef STORE_SUBMIT_OK
			if ((mask & STORE_SUBMIT_OK) == 0)
				LOG("%s:%i - sendMessage warning: STORE_SUBMIT_OK not set\n", _FL);
			#endif
		}

		LComPtr<IMAPIFolder> inboxFolder;
		ULONG cbInboxEID = 0;
		LPENTRYID lpInboxEID = nullptr;
		auto hr = MsgStore->GetReceiveFolder(_T("IPM.Note"), 0, &cbInboxEID, &lpInboxEID, nullptr);
		if (SUCCEEDED(hr) && lpInboxEID)
		{
			ULONG objType = 0;
			auto hr = MsgStore->OpenEntry(	cbInboxEID,
											lpInboxEID,
											NULL,
											MAPI_BEST_ACCESS,
											&objType,
											inboxFolder.Unknown());
			if (FAILED(hr) || !inboxFolder)
			{
				ERR("%s:%i - sendMessage failed: OpenEntry(inbox) hr=0x%x\n", _FL, hr);
				return false;
			}
		}
		else
		{
			ERR("%s:%i - sendMessage failed: no GetReceiveFolder\n", _FL);
			return false;
		}

		LComPtr<IMAPIFolder> draftsFolder;
		if (auto entry = mapi.MapiGetProp(inboxFolder, PR_IPM_DRAFTS_ENTRYID))
		{
			ULONG objType = 0;
			auto hr = MsgStore->OpenEntry(	entry->Value.bin.cb,
											(LPENTRYID)entry->Value.bin.lpb,
											NULL,
											MAPI_BEST_ACCESS,
											&objType,
											draftsFolder.Unknown());
			if (FAILED(hr) || !draftsFolder)
			{
				ERR("%s:%i - sendMessage failed: OpenEntry(drafts) hr=0x%x\n", _FL, hr);
				return false;
			}
		}
		else
		{
			ERR("%s:%i - sendMessage failed: no PR_IPM_DRAFTS_ENTRYID\n", _FL);
			return false;
		}

		LComPtr<IMAPIFolder> outboxFolder;
		if (auto entry = mapi.MapiGetProp(MsgStore, PR_IPM_OUTBOX_ENTRYID))
		{
			ULONG objType = 0;
			auto hr = MsgStore->OpenEntry(	entry->Value.bin.cb,
											(LPENTRYID)entry->Value.bin.lpb,
											NULL,
											MAPI_BEST_ACCESS,
											&objType,
											outboxFolder.Unknown());
			if (FAILED(hr) || !outboxFolder)
			{
				ERR("%s:%i - sendMessage failed: OpenEntry(outbox) hr=0x%x\n", _FL, hr);
				return false;
			}
		}
		else
		{
			ERR("%s:%i - sendMessage failed: no PR_IPM_OUTBOX_ENTRYID\n", _FL);
			return false;
		}

		LComPtr<IMessage> msg;
		auto createHr = draftsFolder->CreateMessage(nullptr, 0, msg.Set());
		if (FAILED(createHr) || !msg)
		{
			ERR("%s:%i - sendMessage failed: CreateMessage hr=0x%x\n", _FL, createHr);
			return false;
		}

		if (!mapi.MapiSetPropStr(msg, PR_MESSAGE_CLASS_A, "IPM.Note"))
			LOG("%s:%i - sendMessage warning: failed to set PR_MESSAGE_CLASS_A\n", _FL);
		// Let the active MAPI profile stamp sender identity and transport flags.
		// Forcing sender/representing props can cause server-side rejection.
		mapi.MapiSetPropBool(msg, PR_DELETE_AFTER_SUBMIT, false);

		// Ensure a sent copy lands in Sent Items for visibility and troubleshooting.
		if (auto sentEntry = mapi.MapiGetProp(MsgStore, PR_IPM_SENTMAIL_ENTRYID))
		{
			SPropValue sent = {};
			sent.ulPropTag = PR_SENTMAIL_ENTRYID;
			sent.Value.bin.cb = sentEntry->Value.bin.cb;
			sent.Value.bin.lpb = sentEntry->Value.bin.lpb;
			auto sentHr = msg->SetProps(1, &sent, nullptr);
			if (FAILED(sentHr))
				LOG("%s:%i - sendMessage warning: failed to set PR_SENTMAIL_ENTRYID hr=0x%x\n", _FL, sentHr);
		}
		else LAssert(!"Failed to get PR_SENTMAIL_ENTRYID");

		LString::Array recipients;
		for (auto &r: rcptTo)
		{
			auto to = r.Strip();
			if (to)
				recipients.Add(to);
		}

		if (recipients.Length() == 0)
		{
			ERR("%s:%i - sendMessage failed: no valid recipients\n", _FL);
			return false;
		}

		ADRLIST *adr = nullptr;
		auto allocHr = MAPIAllocateBuffer(CbNewADRLIST((ULONG)recipients.Length()), (void**)&adr);
		if (allocHr != S_OK || !adr)
		{
			ERR("%s:%i - sendMessage failed: ADRLIST alloc hr=0x%x\n", _FL, allocHr);
			return false;
		}

		adr->cEntries = (ULONG)recipients.Length();
		for (unsigned i = 0; i < adr->cEntries; i++)
		{
			auto &entry = adr->aEntries[i];
			entry.cValues = 5;
			entry.ulReserved1 = 0;
			auto oneHr = MAPIAllocateBuffer(sizeof(SPropValue) * entry.cValues, (void**)&entry.rgPropVals);
			if (oneHr != S_OK || !entry.rgPropVals)
			{
				for (unsigned j = 0; j < i; j++)
					MAPIFreeBuffer(adr->aEntries[j].rgPropVals);
				MAPIFreeBuffer(adr);
				ERR("%s:%i - sendMessage failed: recipient props alloc hr=0x%x\n", _FL, oneHr);
				return false;
			}

			// Keep recipient backing storage alive until ModifyRecipients returns.
			const LString &to = recipients[i];
			SPropValue *p = entry.rgPropVals;

			p[0].ulPropTag = PR_RECIPIENT_TYPE;
			p[0].Value.l = MAPI_TO;
			p[0].dwAlignPad = 0;

			p[1].ulPropTag = PR_ADDRTYPE_A;
			p[1].Value.lpszA = (LPSTR)"SMTP";
			p[1].dwAlignPad = 0;

			p[2].ulPropTag = PR_DISPLAY_NAME_A;
			p[2].Value.lpszA = (LPSTR)to.Get();
			p[2].dwAlignPad = 0;

			p[3].ulPropTag = PR_EMAIL_ADDRESS_A;
			p[3].Value.lpszA = (LPSTR)to.Get();
			p[3].dwAlignPad = 0;

			p[4].ulPropTag = PR_SEND_RICH_INFO;
			p[4].Value.b = false;
			p[4].dwAlignPad = 0;
		}

		LPADRBOOK addrBook = nullptr;
		auto abHr = Session->OpenAddressBook(0, nullptr, AB_NO_DIALOG, &addrBook);
		if (SUCCEEDED(abHr) && addrBook)
		{
			auto resolveHr = addrBook->ResolveName(Ui, 0, nullptr, adr);
			if (FAILED(resolveHr))
				LOG("%s:%i - sendMessage warning: ResolveName hr=0x%x (continuing with SMTP props)\n", _FL, resolveHr);
			addrBook->Release();
		}
		else
		{
			LOG("%s:%i - sendMessage warning: OpenAddressBook hr=0x%x\n", _FL, abHr);
		}

		auto modHr = msg->ModifyRecipients(MODRECIP_ADD, adr);
		for (unsigned i = 0; i < adr->cEntries; i++)
			MAPIFreeBuffer(adr->aEntries[i].rgPropVals);
		MAPIFreeBuffer(adr);
		if (FAILED(modHr))
		{
			ERR("%s:%i - sendMessage failed: ModifyRecipients hr=0x%x\n", _FL, modHr);
			return false;
		}

		LString headers, body;
		auto splitPos = data.Find("\r\n\r\n");
		auto sepLen = 4;
		if (splitPos < 0)
		{
			splitPos = data.Find("\n\n");
			sepLen = 2;
		}
		if (splitPos >= 0)
		{
			headers = data(0, splitPos);
			body = data(splitPos + sepLen, -1);
		}
		else
		{
			ERR("%s:%i - sendMessage failed: no boundary between headers and data\n", _FL);
			return false;
		}

		if (headers)
		{
			mapi.MapiSetPropStr(msg, PR_TRANSPORT_MESSAGE_HEADERS, headers);
			if (auto subj = LGetHeaderField(headers, "Subject"))
			{
				if (!mapi.MapiSetPropStr(msg, PR_SUBJECT_A, subj))
					LOG("%s:%i - sendMessage warning: failed to set PR_SUBJECT_A\n", _FL);
			}
		}

		if (body)
		{
			if (!mapi.MapiSetPropStr(msg, PR_BODY, body))
				LOG("%s:%i - sendMessage warning: failed to set PR_BODY\n", _FL);
		}

		LDateTime submitNow;
		submitNow.SetNow();
		mapi.MapiSetPropDate(msg, PR_CLIENT_SUBMIT_TIME, submitNow);
		mapi.MapiSetPropLong(msg, PR_MESSAGE_FLAGS, MSGFLAG_UNSENT);

		auto saveHr = msg->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
		if (FAILED(saveHr))
		{
			ERR("%s:%i - sendMessage failed: SaveChanges hr=0x%x\n", _FL, saveHr);
			return false;
		}

		LMapiEntry createdMsgEntry;
		if (auto msgEntry = mapi.MapiGetProp(msg, PR_ENTRYID))
		{
			createdMsgEntry = msgEntry;
			LOG("%s:%i - sendMessage message entry created, cb=%u\n", _FL, (unsigned)msgEntry->Value.bin.cb);

			LMapiEntry searchKey = mapi.MapiGetProp(msg, PR_SEARCH_KEY);

			ENTRYLIST eList = { 1, &msgEntry->Value.bin };
			auto hr = draftsFolder->CopyMessages(&eList, NULL, outboxFolder, NULL, NULL, MESSAGE_MOVE);
			if (FAILED(hr))
				ERR("%s:%i - sendMessage failed: CopyMessages hr=0x%x\n", _FL, hr);

			createdMsgEntry.Empty(); // no longer valid, the 'CopyMessages' deleted it...

			// Find the newly created message via the search key...
			LPMAPITABLE tbl = nullptr;
			hr = outboxFolder->GetContentsTable(0, &tbl);
			if (SUCCEEDED(hr) && tbl)
			{
				for (LMapiList contents(tbl); contents.More(); contents.Next())
				{
					LMapiEntry search = contents.GetField(PR_SEARCH_KEY);
					if (search == searchKey)
					{
						createdMsgEntry = contents.GetField(PR_ENTRYID);
						break;
					}
				}
			}

			if (!createdMsgEntry.Length())
			{
				ERR("%s:%i - sendMessage failed: failed to find message in outbox after CopyMessages.\n", _FL);
				return false;
			}
		}
		else
		{
			ERR("%s:%i - sendMessage failed: no msg entry\n", _FL);
			return false;
		}

		// Find the new message pointer...
		msg.Release();
		{
			ULONG objType = 0;
			hr = MsgStore->OpenEntry(	(ULONG)createdMsgEntry.Length(),
										createdMsgEntry,
										NULL,
										MAPI_BEST_ACCESS,
										&objType,
										msg.Unknown());
			if (FAILED(hr) || !msg)
			{
				ERR("%s:%i - sendMessage failed: failed to open message hr=0x%x\n", _FL, hr);
				return false;
			}
		}

		// Use normal submission path; FORCE_SUBMIT can bypass normal client flow.
		ULONG submitFlags = 0;
		hr = msg->SubmitMessage(submitFlags);
		if (FAILED(hr))
		{
			ERR("%s:%i - sendMessage failed: SubmitMessage hr=0x%x flags=0x%x\n", _FL, hr, submitFlags);
			return false;
		}

		LOG("%s:%i - sendMessage succeeded: from='%s' to='%s'\n", _FL, mailFrom.Get(), LString(",").Join(rcptTo).Get());
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
						ERR("%s:%i - Session->Logoff failed with %x\n", _FL, res);
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
			ERR("%s:%i - Unsupported create type 0x%x\n", _FL, Type);
			break;
	}
	
	return NULL;
}

LDataFolderI *LMapiStore::GetRoot(bool create)
{
	if (!Session)
		ERR("%s:%i - Session is NULL.\n", _FL);
	else if (!Root)
	{
		if (!EntryRef)
			ERR("%s:%i - EntryRef is NULL.\n", _FL);
		else
		{
			IMAPIFolder *r = NULL;
			if (!EntryRef->OpenRoot(Session, Ui, &MsgStore, &r))
				ERR("%s:%i - OpenRoot failed.\n", _FL);
			else if (!r)
				ERR("%s:%i - OpenRoot returned NULL.\n", _FL);
			else
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
		ERR("%s:%i - CopyMessages failed with 0x%x\n", _FL, res);
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

	auto Trash = ToTrash ? FindSystemFolder(Store3SystemTrash) : NULL;
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
						ERR("%s:%i - DeleteMessages failed with 0x%x\n", _FL, Status);
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
						ERR("%s:%i - Failed to delete folder '%s', err=0x%x\n", _FL, f->Name.Get(), res);
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

	if (!Session)
		Store->ERR("%s:%i - Session is NULL.\n", _FL);
	else if (!MsgStore)
		Store->ERR("%s:%i - MsgStore is NULL.\n", _FL);
	else if (!RootFolder)
		Store->ERR("%s:%i - RootFolder is NULL.\n", _FL);
	else
	{
		if (!*MsgStore)
		{
			ULONG ulStoreFlags = MDB_WRITE | MDB_ONLINE | MAPI_BEST_ACCESS;
			HRESULT res = Session->OpenMsgStore(UiHnd,
												(ULONG)Entry.Length(),			// entry bytes
												(LPENTRYID)&Entry[0],	// ptr to entry
												NULL,					// default interface: IMsgStore
												ulStoreFlags,
												MsgStore);
			if (res == MAPI_E_UNKNOWN_FLAGS && (ulStoreFlags & MDB_ONLINE))
			{
				ulStoreFlags &= ~MDB_ONLINE; // Strip the flag and try again
				res = Session->OpenMsgStore(UiHnd, (ULONG)Entry.Length(), (LPENTRYID)&Entry[0], NULL, ulStoreFlags, MsgStore);
			}

			if (FAILED(res))
			{
				Store->ERR("%s:%i - OpenMsgStore failed with 0x%x\n", _FL, res);
			}
		}
		
		if (*MsgStore)
		{
			auto SubTree = MapiGetProp(*MsgStore, PR_IPM_SUBTREE_ENTRYID);
			if (!SubTree)
				Store->ERR("%s:%i - Failed to get PR_IPM_SUBTREE_ENTRYID.\n", _FL);
			else
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
					*MsgStore = nullptr;
					
					return Store->ERR("%s:%i - OpenEntry failed with 0x%x\n", _FL, res);
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
							LDataEventsI *Callback,
							LStream *Log)
{
	return new LMapiStore(Profile, Username, Password, AccountId, Callback, Log);
}
