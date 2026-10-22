#include "lgi/common/Lgi.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"
#include "../Store3Mail3/Mail3.h"
#include <map>

#if IMAP_PROTOBUF
	#ifdef _DEBUG
		#pragma comment(lib, "libprotobufd.lib")
	#else
		#pragma comment(lib, "libprotobufd.lib")
	#endif
#endif

const char *ImapMsgTypeNames[IMAP_MSG_MAX] =
{
	"IMAP_NULL",

	// Connection
	"IMAP_ONLINE",
	"IMAP_OFFLINE",
	"IMAP_ERROR",

	// Lifespan events
	"IMAP_ON_NEW",
	"IMAP_ON_DEL",
	"IMAP_ON_MOVE",

	// Folder
	"IMAP_SELECT_FOLDER",
	"IMAP_CREATE_FOLDER",
	"IMAP_DELETE_FOLDER",
	"IMAP_EXPUNGE_FOLDER",
	"IMAP_RENAME_FOLDER",
	"IMAP_LOAD_FOLDER",

	// Mail
	"IMAP_FOLDER_SELECTED",
	"IMAP_FOLDER_LISTING",
	"IMAP_DOWNLOAD",
	"IMAP_APPEND",
	"IMAP_MOVE_EMAIL",

	// General
	"IMAP_DELETE",
	"IMAP_UNDELETE",
	"IMAP_SET_FLAGS",
	"IMAP_REBUILD_MAILBOX",
	"IMAP_JOURNAL_OPERATION",
};

//////////////////////////////////////////////////////////////////////////////////
char *TrimWhite(char *c)
{
	if (!c)
		return 0;

	char *s = c;
	while (*s && strchr(LWhiteSpace, *s))
		s++;
	char *e = s + strlen(s);
	while (e > s && strchr(LWhiteSpace, e[-1]))
		e--;
	*e = 0;
	if (s > c)
		memmove(c, s, e-s+1);
	return c;
}

//////////////////////////////////////////////////////////////////////////////////
ImapStore::ImapStore(LMail3Store &Storage, int AccountId) :
	ImapStore(LString(), 0, LString(), LString(), 0, NULL, NULL, NULL, NULL,
		AccountId, LAutoPtr<ProtocolSettingStore>(), &Storage)
{
}

ImapStore::ImapStore(LString host,
					int port,
					LString user,
					LString pass,
					int flags,
					LDataEventsI *callback,
					LCapabilityClient *caps,
					MailProtocolProgress *prog[2],
					LStream *log,
					int accountid,
					LAutoPtr<ProtocolSettingStore> store,
					LMail3Store *storage)
{
	AccountId = accountid;
	Host = host;
	Port = port;
	User = user;
	Pass = pass;
	ConnectFlags = flags;
	Log = log;
	Callback = callback;
	ItemProgress = prog ? prog[0] : NULL;
	DataProgress = prog ? prog[1] : NULL;
	SettingStore = store;
	Storage = storage;
	if (IsLive() && !Host)
	{
		GetRoot(false);
		return;
	}

	LVariant v;
	if (SettingStore && SettingStore->GetValue(OPT_ReceiveFilterIncoming, v))
	{
		FilterIncoming = v.CastInt32() != 0;
	}

	if (IsLive())
	{
		auto CachePath = GetCache();
		if (!CachePath || !Storage->ImportImapSettings(AccountId, CachePath) ||
			!Storage->RecoverImapLocalMail(AccountId, CachePath) || !Storage->RecoverImapOperations(AccountId))
		{
			ErrorMsg.Reset(NewStr(CachePath ? Storage->GetStr(FIELD_ERROR) : "Cannot locate the IMAP cache."));
			if (Log)
				Log->Write(ErrorMsg.Get(), strlen(ErrorMsg.Get()), LSocketI::SocketMsgError);
			return;
		}
	}

	if (IsLive())
		GetRoot(false);
	Thread = new ImapThread(this, caps, log, SettingStore, AccountId);
}

ImapStore::~ImapStore()
{
	FolderLoader.Reset();
	DeleteObj(Thread);
	InterruptPendingJournal("The IMAP worker stopped before its journal result was committed.");
	DeleteObj(Root);
	DeleteArray(Cache);
}

bool ImapStore::ReportError(const char *Error)
{
	ErrorMsg.Reset(NewStr(ValidStr(Error) ? Error : "An IMAP SQLite operation failed."));
	LgiTrace("%s\n", ErrorMsg.Get());
	if (Log)
		Log->Write(ErrorMsg.Get(), strlen(ErrorMsg.Get()), LSocketI::SocketMsgError);
	if (Callback)
		Callback->OnPropChange(this, FIELD_ERROR, GV_STRING);
	return false;
}

bool ImapStore::RefreshMailbox(const char *Remote, char Delimiter)
{
	if (!IsLive() || !Remote || !Strcmp(Remote, "/"))
		return false;
	LArray<LMail3ImapOperation> Pending;
	if (!Storage->LoadImapOperations(AccountId, Pending))
		return ReportError(Storage->GetStr(FIELD_ERROR));
	if (Pending.Length())
		return true; // Completion refreshes the account after the frozen operations have drained.
	LAutoPtr<ImapMsg> Message(new ImapMsg(IMAP_REBUILD_MAILBOX, _FL));
	Message->Generation = LiveGeneration;
	auto &Folder = Message->Fld.New();
	Folder.Remote = Remote;
	Folder.Sep = Delimiter;
	LArray<LMail3ImapMailbox> Mailboxes;
	if (!Storage->LoadImapMailboxes(AccountId, Mailboxes))
		return ReportError(Storage->GetStr(FIELD_ERROR));
	for (auto &Mailbox: Mailboxes)
		if (!Delimiter && Mailbox.RemoteName == Remote && Mailbox.Delimiter)
			Folder.Sep = Mailbox.Delimiter[0];
	return PostThread(Message.Release(), false);
}

bool ImapStore::SyncLiveFolders()
{
	if (!Root)
		return false;
	LArray<LMail3ImapMailbox> Mailboxes;
	if (!Storage->LoadImapMailboxes(AccountId, Mailboxes))
		return ReportError(Storage->GetStr(FIELD_ERROR));
	std::map<std::string, ImapFolder*> Known;
	std::function<void(ImapFolder*)> Collect = [&](ImapFolder *Folder)
	{
		Known[Folder->Remote.Get()] = Folder;
		for (auto Child: Folder->Sub.a)
			Collect(Child);
	};
	Collect(Root);
	Root->Sub.State = Store3Loaded;
	Mailboxes.Sort([](auto a, auto b) { return a->RemoteName.Length() < b->RemoteName.Length() ? -1 :
		a->RemoteName.Length() > b->RemoteName.Length() ? 1 : Strcmp(a->RemoteName.Get(), b->RemoteName.Get()); });
	for (auto &Mailbox: Mailboxes)
	{
		auto Existing = Known.find(Mailbox.RemoteName.Get());
		if (Existing != Known.end())
		{
			if (!Existing->second->ReloadCanonical(Mailbox.Id))
				return false;
			Existing->second->Sub.State = Store3Loaded;
			continue;
		}
		auto Name = Mailbox.RemoteName;
		auto Slash = Name.RFind("/");
		LString ParentName = Slash > 0 ? LString(Name.Get(), Slash) : LString("/");
		auto Parent = Known.find(ParentName.Get());
		if (Parent == Known.end())
			return ReportError("A canonical IMAP folder has no account parent.");
		LAutoPtr<ImapFolder> New(new ImapFolder(this, Mailbox.RemoteName));
		if (!New->ReloadCanonical(Mailbox.Id))
			return false;
		New->Sub.State = Store3Loaded;
		New->SetParent(Parent->second);
		auto Folder = New.Release();
		Known[Mailbox.RemoteName.Get()] = Folder;
		LArray<LDataI*> Added;
		Added.Add(Folder);
		OnNew(_FL, Parent->second, Added, -1, false);
	}
	return true;
}

bool ImapStore::PumpJournal()
{
	if (!IsLive() || Online != STATUS_ONLINE || PendingJournalId > 0)
		return true;
	LArray<LMail3ImapOperation> Operations;
	if (!Storage->LoadImapOperations(AccountId, Operations))
		return ReportError(Storage->GetStr(FIELD_ERROR));
	if (!Operations.Length())
		return true;
	if (Operations[0].State == LMail3ImapOperationState::NeedsReconciliation)
	{
		if (!ErrorMsg || Strcmp(ErrorMsg.Get(), Operations[0].LastError.Get()))
			return ReportError(Operations[0].LastError);
		return false;
	}
	return DispatchJournalOperation(Operations[0].Id);
}

void ImapStore::JournalQueued()
{
	++LiveGeneration;
	PumpJournal();
}

Store3Status ImapStore::QueueMutation(ImapMail &Mail, LMail3ImapOperationType Type,
	int Flags, ImapFolder *Destination)
{
	if (!Mail.Canonical || Mail.Store != this || (Destination && (Destination->Store != this || !Destination->Canonical)))
	{
		ReportError("IMAP mutation requires canonical objects on the same account.");
		return Store3Error;
	}
	LMail3ImapOperation Operation;
	Operation.AccountId = AccountId;
	Operation.MailId = Mail.Canonical->Id;
	Operation.Type = Type;
	Operation.RemoteFlags = Flags;
	if (Destination)
		Operation.DestinationMailboxId = Destination->Canonical->Id;
	if (!Storage->QueueImapOperation(Operation))
	{
		ReportError(Storage->GetStr(FIELD_ERROR));
		return Store3Error;
	}
	Mail.SetState(Type == LMail3ImapOperationType::Move ? ImapMail::ImapMailMoving :
		Type == LMail3ImapOperationType::Delete ? ImapMail::ImapMailDeleting : ImapMail::ImapMailIdle, _FL);
	JournalQueued();
	return Store3Delayed;
}

bool ImapStore::OnLiveEvent(ImapMsg &Message)
{
	if (Message.Type == IMAP_FOLDER_LISTING && Message.Last && !Message.Parent)
	{
		LiveFolders.Swap(Message.Fld);
		for (auto &Folder: LiveFolders)
			RefreshMailbox(Folder.Remote, Folder.Sep);
		return true;
	}
	if (Message.Type == IMAP_REBUILD_MAILBOX)
	{
		if (Message.Generation != LiveGeneration)
			return true;
		LArray<LMail3ImapOperation> Pending;
		if (!Storage->LoadImapOperations(AccountId, Pending))
		{
			ReportError(Storage->GetStr(FIELD_ERROR));
			return true;
		}
		if (Pending.Length())
			return true;
		if (Message.Fld.Length() != 1 || !Message.Last || Message.Fld[0].LastUid != 0)
		{
			ReportError("A live IMAP refresh requires a complete verified server snapshot.");
			return true;
		}
		auto &Info = Message.Fld[0];
		LMail3ImapMailbox Mailbox;
		Mailbox.AccountId = AccountId;
		Mailbox.RemoteName = Info.Remote;
		Mailbox.Delimiter = Info.Sep ? LString::Fmt("%c", Info.Sep) : LString();
		Mailbox.UidValidity = Info.UidValidity;
		Mailbox.UidNext = Info.UidNext;
		LArray<LMail3ImapServerMessage> Messages;
		for (auto &Info: Message.Mail)
		{
			auto &Mail = Messages.New();
			Mail.Uid = Info.Uid;
			Mail.RemoteFlags = Info.Flags.All;
			Mail.Size = Info.Size;
			Mail.Headers = Info.Headers;
		}
		if (!Storage->RebuildImapMailbox(Mailbox, Messages) || !SyncLiveFolders())
			ReportError(Storage->GetStr(FIELD_ERROR));
		else if (auto Folder = Root->Find(NULL, Info.Remote))
			Folder->SyncLiveMail(true);
		return true;
	}
	if (Message.Type == IMAP_DOWNLOAD)
	{
		for (auto &Info: Message.Mail)
		{
			auto Folder = Root->Find(NULL, Message.Parent);
			auto Mail = Folder ? Folder->GetMail(Info.Uid) : NULL;
			if (!Mail || !Mail->Canonical || Mail->Canonical->Id != Info.MailId)
				continue;
			Mail->SetState(ImapMail::ImapMailIdle, _FL);
			Mail->Loaded = Store3Headers;
			LMail3ImapMessage Identity;
			Identity.MailId = Info.MailId;
			Identity.MailboxId = Info.MailboxId;
			Identity.UidValidity = Info.UidValidity;
			Identity.Uid = Info.Uid;
			LMemStream Body(Info.Body.Get(), Info.Body.Length());
			if (Message.Diagnostic || !Storage->StoreImapBody(AccountId, Identity, Body))
				ReportError(Message.Diagnostic ? Message.Diagnostic.Get() : Storage->GetStr(FIELD_ERROR));
			else if (Mail->ReloadCanonical(Info.MailId))
				Mail->ReadMime(NULL);
			LArray<LDataI*> Changed;
			Changed.Add(Mail);
			OnChange(_FL, Changed, FIELD_LOADED);
		}
		return true;
	}
	switch (Message.Type)
	{
		case IMAP_ON_NEW:
		case IMAP_ON_DEL:
		case IMAP_SET_FLAGS:
		case IMAP_EXPUNGE_FOLDER:
		case IMAP_FOLDER_LISTING:
			if (Message.Parent)
				RefreshMailbox(Message.Parent);
			return true;
		default:
			return false;
	}
}

bool ImapStore::InterruptPendingJournal(const char *Reason)
{
	if (PendingJournalId <= 0)
		return true;
	if (!Storage || !Storage->InterruptImapOperation(PendingJournalId, Reason))
	{
		ErrorMsg.Reset(NewStr(Storage ? Storage->GetStr(FIELD_ERROR) : "No shared store for the pending IMAP command."));
		LgiTrace("%s\n", ErrorMsg.Get());
		if (Log)
			Log->Write(ErrorMsg.Get(), strlen(ErrorMsg.Get()), LSocketI::SocketMsgError);
		return false;
	}
	PendingJournalId = -1;
	PendingJournalAttempt = 0;
	return true;
}

bool PrepareImapJournalOperation(LMail3Store &Storage, int64 AccountId, int64 OperationId,
	ImapJournalCommand &Command, LMail3ImapOperationState ClaimedState)
{
	bool Execute = ClaimedState == LMail3ImapOperationState::Executing;
	LArray<LMail3ImapOperation> Operations;
	if (!Storage.LoadImapOperations(AccountId, Operations))
		return false;
	if (!Operations.Length() || Operations[0].Id != OperationId ||
		Operations[0].State != (Execute ? LMail3ImapOperationState::Queued : LMail3ImapOperationState::NeedsReconciliation))
	{
		Storage.SetStr(FIELD_ERROR, "IMAP dispatch requires the account's first journal entry in the requested queued/recovery state.");
		LgiTrace("%s\n", Storage.GetStr(FIELD_ERROR));
		return false;
	}
	LAutoStreamI Upload;
	if (Execute && Operations[0].Type == LMail3ImapOperationType::Append)
	{
		Upload = Storage.OpenImapUpload(AccountId, OperationId);
		if (!Upload)
			return false;
	}
	LMail3ImapOperation Claimed;
	if (!Storage.BeginImapOperation(OperationId, Claimed, ClaimedState))
		return false;
	Command.Operation = Claimed;
	Command.Upload = Upload;
	return true;
}

bool ApplyImapJournalResult(LMail3Store &Storage, int64 AccountId, const ImapJournalResult &Result)
{
	auto Fail = [&](const char *Error)
	{
		Storage.SetStr(FIELD_ERROR, Error);
		LgiTrace("%s\n", Error);
		return false;
	};
	if (Result.AccountId != AccountId || Result.OperationId <= 0)
		return Fail("IMAP journal result belongs to another account or has no operation ID.");
	if (Result.State != LMail3ImapOperationState::Executing &&
		Result.State != LMail3ImapOperationState::Reconciling &&
		Result.State != LMail3ImapOperationState::ContinuingMove)
		return Fail("IMAP journal result has no valid active claim state.");
	LArray<LMail3ImapOperation> Operations;
	if (!Storage.LoadImapOperations(AccountId, Operations))
		return false;
	if (!Operations.Length() || Operations[0].Id != Result.OperationId ||
		Operations[0].State != Result.State ||
		Result.Attempt != Operations[0].Attempts)
		return Fail("IMAP journal result does not match this account's executing command.");
	if (Result.State != LMail3ImapOperationState::Executing &&
		(Operations[0].Type == LMail3ImapOperationType::Move || Operations[0].Type == LMail3ImapOperationType::Append) &&
		(Result.DestinationUid != Operations[0].DestinationUid ||
		 Result.DestinationUidValidity != (Operations[0].DestinationUid == -1 ? -1 : Operations[0].DestinationUidValidity)))
		return Fail("IMAP recovery result changed the recorded destination evidence.");
	if (Result.Outcome == ImapJournalOutcome::Confirmed)
	{
		if (Result.Error)
			return Fail("A confirmed IMAP journal result cannot contain an error.");
		if (Storage.CompleteImapOperation(Result.OperationId, Result.DestinationUidValidity, Result.DestinationUid))
			return true;
		LString Error = Storage.GetStr(FIELD_ERROR);
		LString Reason = LString::Fmt("Server confirmed the command, but the local commit failed: %s",
			Error ? Error.Get() : "unknown storage error");
		// Keep evidence if the local commit failed after the server already acted.
		if (!Storage.InterruptImapOperation(Result.OperationId, Reason,
			Result.DestinationUidValidity, Result.DestinationUid))
			return false;
		Storage.SetStr(FIELD_ERROR, Reason);
		return false;
	}
	if ((Result.Outcome != ImapJournalOutcome::NotExecuted && Result.Outcome != ImapJournalOutcome::Uncertain) ||
		!Result.Error)
		return Fail("An unsuccessful IMAP journal result requires an explicit diagnostic.");
	auto Reason = LString::Fmt("%s: %s",
		Result.State == LMail3ImapOperationState::Reconciling ? "Reconciliation incomplete" :
		Result.Outcome == ImapJournalOutcome::NotExecuted ? "Not executed" : "Server outcome uncertain",
		Result.Error.Get());
	return Storage.InterruptImapOperation(Result.OperationId, Reason,
		Result.DestinationUidValidity, Result.DestinationUid);
}

bool ImapStore::DispatchJournalOperation(int64 OperationId, LMail3ImapOperationState ClaimedState)
{
	auto Fail = [&](const char *Error)
	{
		ErrorMsg.Reset(NewStr(Error));
		LgiTrace("%s\n", ErrorMsg.Get());
		if (Log)
			Log->Write(ErrorMsg.Get(), strlen(ErrorMsg.Get()), LSocketI::SocketMsgError);
		return false;
	};
	if (!Storage || !Storage->InThread() || !Thread || !Callback || Online != STATUS_ONLINE ||
		PendingJournalId > 0 || OperationId <= 0)
		return Fail("IMAP journal dispatch requires an online owning-thread store with no command in flight.");
	LAutoPtr<ImapMsg> Message(new ImapMsg(IMAP_JOURNAL_OPERATION, _FL));
	Message->Journal.Reset(new ImapJournalCommand);
	if (!PrepareImapJournalOperation(*Storage, AccountId, OperationId, *Message->Journal, ClaimedState))
		return Fail(Storage->GetStr(FIELD_ERROR));
	PendingJournalId = OperationId;
	PendingJournalAttempt = Message->Journal->Operation.Attempts;
	if (!PostThread(Message.Release(), false))
	{
		PendingJournalId = -1;
		PendingJournalAttempt = 0;
		const char *Reason = "The journal command could not be handed to the worker; reconcile before retrying.";
		if (!Storage->InterruptImapOperation(OperationId, Reason))
			return Fail(Storage->GetStr(FIELD_ERROR));
		return Fail(Reason);
	}
	return true;
}

bool ImapStore::OnIdle()
{
	if (IsLive())
		PumpJournal();
	if (Listing.Length() > 0)
	{
		LAutoPtr<ImapMsg> m(Listing[0]);
		Listing.DeleteAt(0, true);
		return Listing.Length() > 0;
	}

	int64 Now = LCurrentTime();
	if (!LastPulse || (Now - LastPulse) > TIMEOUT_STORE_ONPULSE)
	{
		LastPulse = Now;
		if (Root)
			Root->OnPulse();
	}

	return false;
}

const char *ImapStore::GetStr(int id)
{
	switch (id)
	{
		case FIELD_ERROR:
			return ErrorMsg;
		case FIELD_STORE_TYPE:
			return "ImapStore";
	}
	
	return NULL;
}

int64 ImapStore::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STATUS:
			return Thread || (IsLive() && !ErrorMsg) ? Store3Success : Store3Error;
		case FIELD_STORE_TYPE:
			return Store3Imap;
		case FIELD_VERSION:
			return 10;
		case FIELD_ACCOUNT_ID:
			return AccountId;
		case FIELD_IS_ONLINE:
			return Online == STATUS_ONLINE;
		case FIELD_PROFILE_IMAP_LISTING:
			return ListingTime;
		case FIELD_PROFILE_IMAP_SELECT:
			return SelectTime;
		case FIELD_STORE_STATUS:
			return Online;
	}

	return -1;
}

Store3Status ImapStore::SetInt(int id, int64 val)
{
	if (id == FIELD_IS_ONLINE && !val)
	{
		DeleteObj(Thread);
		Online = STATUS_OFFLINE;
		return InterruptPendingJournal("The account was taken offline before the journal result was committed.")
			? Store3Success : Store3Error;
	}

	return Store3Error;
}

ImapFolder *ImapStore::GetSystemFolder(int Type)
{
	if (!Root)
		return NULL;
	
	Store3SystemFolder t = Store3SystemNone;
	switch (Type)
	{
		case FOLDER_INBOX:
			t = Store3SystemInbox;
			break;
		case FOLDER_OUTBOX:
			t = Store3SystemOutbox;
			break;
		case FOLDER_SENT:
			t = Store3SystemSent;
			break;
		case FOLDER_TRASH:
			t = Store3SystemTrash;
			break;
	}
	if (!t)
		return NULL;
	for (auto f: Root->Sub.a)
	{
		if (f->System == t)
			return f;
	}

	return NULL;
}

LDataPropI *ImapStore::GetObj(int id)
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

void ImapStore::OnNew(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
{
	if (!Callback)
		return;
	Callback->SetContext(File, Line);
	Callback->OnNew(parent, new_items, pos, is_new, FilterIncoming);
}

bool ImapStore::OnDelete(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &del)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnDelete(parent, del);
}

bool ImapStore::OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint)
{
	if (!Callback)
		return false;
	Callback->SetContext(File, Line);
	return Callback->OnChange(items, FieldHint);
}

static bool CheckCreateFolder(char *s, LStream *Log)
{
	if (LDirExists(s))
		return true;

	if (FileDev->CreateFolder(s))
		return true;

	char m[256];
	Log->Write(m, sprintf_s(m, sizeof(m), "Can't create '%s'", s), LSocketI::SocketMsgError);
	return false;
}

char *ImapStore::GetCache()
{
	if (!Cache)
	{
		char s[MAX_PATH_LEN], a[32];

		LVariant Portable;
		LOptionsFile *Opts = Callback->GetOptions();
		if (!Opts || !Opts->GetValue(OPT_IsPortableInstall, Portable))
			Portable = true;
		
		sprintf_s(a, sizeof(a), "%i", AccountId);

		if (Portable.CastInt32())
			LGetSystemPath(LSP_APP_INSTALL, s, sizeof(s));
		else
			LGetSystemPath(LSP_APP_CACHE, s, sizeof(s));
		
		if (!CheckCreateFolder(s, Log))
		{
		    LgiTrace("%s:%i - CheckCreateFolder(%s) failed.\n", _FL, s);
			return NULL;
		}
		
		LMakePath(s, sizeof(s), s, "ImapCache");

		if (!CheckCreateFolder(s, Log))
		{
		    LgiTrace("%s:%i - CheckCreateFolder(%s) failed.\n", _FL, s);
			return NULL;
		}

		LMakePath(s, sizeof(s), s, a);

		if (!CheckCreateFolder(s, Log))
		{
		    LgiTrace("%s:%i - CheckCreateFolder(%s) failed.\n", _FL, s);
			return NULL;
		}

		Cache = NewStr(s);
	}

	return Cache;
}

uint64 ImapStore::Size()
{
	return -1;
}

LDataI *ImapStore::Create(int Type)
{
	switch ((uint32_t)Type)
	{
		case MAGIC_FOLDER:
		{
			return new ImapFolder(this, 0);
		}
		case MAGIC_MAIL:
		{
			return new ImapMail(this, _FL, 0);
		}
	}

	return 0;
}

class FolderLoaderThread : public LThread
{
	bool Loop;
	ImapStore *Store;

public:
	uint64_t StartTs;
	LAutoPtr<ImapFolder> Root;

	FolderLoaderThread(ImapStore *store, const char *base) : LThread("FolderLoaderThread")
	{
		Loop = true;
		Store = store;
		StartTs = LCurrentTime();
		Root.Reset(new ImapFolder(Store, "/"));

		Run();
	}

	~FolderLoaderThread()
	{
		Loop = false;
		while (!IsExited())
			LSleep(1);
	}

	int Main()
	{
		Root->LoadSub(true);
		Store->PostStore(new ImapMsg(IMAP_ONLINE, _FL));
		return 0;
	}
};

LDataFolderI *ImapStore::GetRoot(bool create)
{
    if (!IsLive() && !GetCache())
        return NULL;
       
	if (!Root)
	{
		Root = new ImapFolder(this, "/");
		if (IsLive())
			SyncLiveFolders();

		#if 0
		// Start a thread to load the folder tree..
		FolderLoader.Reset(new FolderLoaderThread(this, GetCache()));
		#endif
	}

	return Root;
}

ImapFolder *ImapStore::GetTrash()
{
	ImapFolder *Trash = Root->Find(0, "/Trash");
	if (!Trash)
		Trash = Root->Find(0, "/Deleted Items");
	// Should we create a trash folder if it doesn't exist?
	return Trash;	
}

Store3Status ImapStore::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	if (IsLive())
	{
		auto Trash = ToTrash ? GetTrash() : NULL;
		for (auto Item: Items)
		{
			auto Mail = dynamic_cast<ImapMail*>(Item);
			if (!Mail || QueueMutation(*Mail, Trash && Mail->Parent != Trash ?
				LMail3ImapOperationType::Move : LMail3ImapOperationType::Delete, 0,
				Trash && Mail->Parent != Trash ? Trash : NULL) == Store3Error)
				return Store3Error;
		}
		return Items.Length() ? Store3Delayed : Store3Error;
	}
	if (Items.Length() == 0)
		return Store3Error;

	Store3Status Status = Store3Error;
	LArray<LDataI*> Mv, Dl;
	ImapFolder *Trash = GetTrash();
	LAutoPtr<ImapMsg> DelMsg(new ImapMsg(IMAP_DELETE, _FL));

	for (unsigned n=0; n<Items.Length(); n++)
	{
		ImapFolder *f = dynamic_cast<ImapFolder*>(Items[n]);
		if (f)
		{
			Status = f->Delete();
		}
		else
		{
			ImapMail *m = dynamic_cast<ImapMail*>(Items[n]);
			if (m)
			{
				if (ToTrash && Root)
				{
					if (Trash && m->Parent != Trash)
					{
						Mv.Add(m);
						m = NULL;
					}
				}
				
				if (m)
				{
					if (!DelMsg->Parent)
						DelMsg->Parent = m->Parent->Remote.Get();
					
					// This breaks messages up into blocks of the same parent folder
					// but no more than IMAP_BLOCK items.
					if (_stricmp(DelMsg->Parent, m->Parent->Remote) ||
						DelMsg->Mail.Length() >= IMAP_BLOCK_SIZE)
					{
						PostThread(DelMsg.Release(), false);
						DelMsg.Reset(new ImapMsg(IMAP_DELETE, _FL));
						DelMsg->Parent = m->Parent->Remote.Get();
						Status = Store3Delayed;
					}

					Dl.Add(m);
					m->SetState(ImapMail::ImapMailDeleting, _FL);
					DelMsg->Mail.New().Uid = m->Uid;
				}
			}
		}
	}

	if (Trash && Mv.Length())
	{
		Status = Move(Trash, Mv);
	}
	if (DelMsg->Mail.Length())
	{
		if (PostThread(DelMsg.Release(), false))
		{
			Status = Store3Delayed;
			OnChange(_FL, Dl, 0);
		}
	}

	return Status;
}

Store3Status ImapStore::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	Store3Status Status = Store3Error;

	if (NewFolder)
	{
		ImapFolder *To = CastFld(NewFolder);
		if (To)
		{
			Status = To->Move(Items);
		}
		else LAssert(!"Not an imap folder.");
	}

	return Status;
}

Store3Status ImapStore::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator)
{
	if (IsLive())
	{
		if (PropId != FIELD_FLAGS || (Operator != OpPlusEquals && Operator != OpMinusEquals))
		{
			ReportError("Unsupported IMAP property change operation.");
			return Store3Error;
		}
		for (auto Item: Items)
		{
			auto Mail = dynamic_cast<ImapMail*>(Item);
			int Flags = Mail ? (int)Mail->GetInt(FIELD_FLAGS) : 0;
			int Mask = Value.CastInt32();
			if (!Mail || Mail->SetInt(FIELD_FLAGS, Operator == OpPlusEquals ? Flags | Mask : Flags & ~Mask) == Store3Error)
				return Store3Error;
		}
		return Store3Delayed;
	}
	switch (PropId)
	{
		case FIELD_FLAGS:
		{
			uint32_t Val = Value.CastInt32();
			LAutoPtr<ImapMsg> Msg;

			if (Operator != OpPlusEquals &&
				Operator != OpMinusEquals)
			{
				LAssert(!"Operator not supported.");
				return Store3Error;
			}

			for (auto i: Items)
			{
				if (!Msg && !Msg.Reset(new ImapMsg(IMAP_SET_FLAGS, _FL)))
					return Store3Error;

				ImapMail *m = dynamic_cast<ImapMail*>(i);
				if (!m || !m->Parent)
					continue;

				auto Fld = m->Parent->Remote;
				if (Msg->Parent && (Fld != Msg->Parent || Msg->Mail.Length() >= IMAP_BLOCK_SIZE))
				{
					LAssert(Msg->Parent);
					PostThread(Msg.Release(), false);
					if (!Msg.Reset(new ImapMsg(IMAP_SET_FLAGS, _FL)))
						return Store3Error;
				}
				if (!Msg->Parent)
					Msg->Parent = Fld;
				
				auto PrevFlags = m->RemoteFlags.All;
				if (Val & MAIL_READ)
					m->RemoteFlags.ImapSeen = Operator == OpPlusEquals;
				else if (Val & MAIL_REPLIED)
					m->RemoteFlags.ImapAnswered = Operator == OpPlusEquals;
				else
					LAssert(!"Unsupported flag.");

				if (Operator == OpPlusEquals)
					m->LocalFlags |= Val;
				else
					m->LocalFlags &= ~Val;

				auto t = m->GetMeta();
				if (t)
				{
					#if IMAP_PROTOBUF
					t->set_remoteflags(m->RemoteFlags.All);
					t->set_localflags(m->LocalFlags);
					#else
					t->SetAttr(ATTR_FLAGS, m->RemoteFlags.Get());
					t->SetAttr(ATTR_LOCAL, m->LocalFlags);
					#endif
					
					m->Parent->SetDirty();
				}
				else LAssert(!"No tag.");

				if (m->RemoteFlags.All != PrevFlags)
				{
					ImapMailInfo &Info = Msg->Mail.New();
					Info.Uid = m->Uid;
					Info.Flags = m->RemoteFlags;
				}
			}

			if (Msg && Msg->Mail.Length() > 0)
			{
				LAssert(Msg->Parent);
				if (!PostThread(Msg.Release(), false))
					return Store3Error;
			}

			return Store3Delayed;
			break;
		}
	}

	return Store3Error;
}

void ImapStore::Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus)
{
	if (OnStatus)
		OnStatus(true);
}

bool ImapStore::PostThread(ImapMsg *m, bool UiPriority)
{
	if (Thread)
	{
		Thread->PostThread(m, UiPriority);
		return true;
	}

	// This happens when the imap store is in offline mode.
	DeleteObj(m);
	return false;
}

ImapFolder *ImapStore::GetParent(char *Local, char *Remote)
{
	if (!Root)
		return 0;

	LAutoString l(NewStr(Local)), r(NewStr(Remote));
	char *s;
	if (l && (s = strrchr(l, DIR_CHAR)))
		*s = 0;
	if (r && (s = strrchr(r, '/')))
		*s = 0;
	return Root->Find(l, r);
}

void MailInfToObject(LAutoPtr<ImapMsg> &DownloadMsg, LArray<LDataI*> *OnNew, ImapFolder *Parent, ImapMail *o, ImapMailInfo &Inf)
{
	o->Path = Parent->MailPath(Inf.Uid, false);

	if (Inf.Date)
	{
		o->DateSent.Decode(Inf.Date);
		o->DateSent.ToUtc();
		ValidateImapDate(o->DateSent);
	}
	// LgiTrace("%s:%i - %p.DateSent = %s\n", _FL, o, Inf.Date.Get());

	o->SetRemoteFlags(Inf.Flags);
	// LgiTrace("MailInfToObject %p -> %s / %s\n", o, Inf.Flags.Get(), EmailFlagsToStr(o->LocalFlags).Get());
	o->Parent = Parent;

	if (o->RemoteFlags.ImapRecent || !LFileExists(o->Path))
	{
		if (!DownloadMsg)
		{
			if (DownloadMsg.Reset(new ImapMsg(IMAP_DOWNLOAD, _FL)))
				DownloadMsg->Parent = Parent->Remote.Get();
		}
		if (DownloadMsg)
		{
			ImapMailInfo &Info = DownloadMsg->Mail.New();
			Info.Uid = o->Uid;
			Info.Local = o->Path.Get();

			o->SetState(ImapMail::ImapMailGettingBody, _FL);
		}
	}

	Parent->WhenLoaded([Parent, o, OnNew](auto Status)
	{
		Parent->AddMail(o, Status > Store3Error ? OnNew : NULL);
	});
}

static bool _in_download = false;

void ImapStore::OnEvent(void *Param)
{
	if (!Root && static_cast<ImapMsg*>(Param)->Type != IMAP_JOURNAL_OPERATION)
		return;

	Msgs.Add((ImapMsg*)Param);

	while (Msgs.Length())
	{
		int64 Start = LCurrentTime();
		ImapMsg *m = Msgs[0];
		Msgs.DeleteAt(0, true);
		if (IsLive() && OnLiveEvent(*m))
		{
			delete m;
			continue;
		}

		switch (m->Type)
		{
			default:
				break;
			case IMAP_JOURNAL_OPERATION:
			{
				bool Matched = Storage && m->JournalResult.OperationId == PendingJournalId &&
					m->JournalResult.AccountId == AccountId && m->JournalResult.Attempt == PendingJournalAttempt;
				bool Applied = Matched && ApplyImapJournalResult(*Storage, AccountId, m->JournalResult);
				if (Matched)
				{
					PendingJournalId = -1;
					PendingJournalAttempt = 0;
				}
				if (IsLive() && Applied && m->JournalResult.Outcome == ImapJournalOutcome::Confirmed)
				{
					std::function<void(ImapFolder*)> Reload = [&](ImapFolder *Folder)
					{
						if (Folder->Mail.State == Store3Loaded)
							Folder->SyncLiveMail(true);
						for (auto Child: Folder->Sub.a)
							Reload(Child);
					};
					Reload(Root);
					PumpJournal();
					if (PendingJournalId <= 0)
					{
						if (LiveFolders.Length())
						{
							for (auto &Folder: LiveFolders)
								RefreshMailbox(Folder.Remote, Folder.Sep);
						}
						else
						{
							LArray<LMail3ImapMailbox> Mailboxes;
							if (Storage->LoadImapMailboxes(AccountId, Mailboxes))
								for (auto &Mailbox: Mailboxes)
									RefreshMailbox(Mailbox.RemoteName);
						}
					}
				}
				if (!Applied || m->JournalResult.Outcome != ImapJournalOutcome::Confirmed)
				{
					const char *Error = !Matched ? "Unexpected IMAP journal worker response." :
						!Applied ? Storage->GetStr(FIELD_ERROR) : m->JournalResult.Error.Get();
					ErrorMsg.Reset(NewStr(Error));
					LgiTrace("%s\n", ErrorMsg.Get());
					if (Log)
						Log->Write(ErrorMsg.Get(), strlen(ErrorMsg.Get()), LSocketI::SocketMsgError);
					Online = STATUS_ERROR;
				}
				// Canonical UI notifications are wired with the live adapters, not the legacy cache.
				break;
			}
			case IMAP_ONLINE:
			{
				Online = STATUS_ONLINE;

				// LProfile p("IMAP_ONLINE");
				#if 1
				if (Root)
					Root->LoadSub();
				#else
				if (Root && Root->Sub.GetState() < Store3Loaded)
				{
					if (!FolderLoader)
					{
						// p.Add("CreateThread");
						FolderLoader.Reset(new FolderLoaderThread(this, GetCache()));
					}
					else
					{
						FolderLoaderThread *t = dynamic_cast<FolderLoaderThread*>(FolderLoader.Get());
						if (t && t->Root && t->Root->Sub.GetState() == Store3Loaded)
						{
							// p.Add("RootSwap");
							Root->Swap(*t->Root);

							// LgiTrace("FolderLoadTime=" LPrintfInt64 "\n", LCurrentTime() - t->StartTs);
						}
					}
				}
				#endif

				// Select the inbox
				if (Root && Root->Sub.GetState() == Store3Loaded)
				{					
					// p.Add("Inbox");
					// Select the INBOX and start loading it's mail
					ImapFolder *Inbox = Root->Find(NULL, "/INBOX");
					if (Inbox)
					{
						// p.Add("Inbox.Select");
						Inbox->OnSelect(true);
						// p.Add("Inbox.Load");
						Inbox->LoadMail();
					}

					// p.Add("Callback");
					if (Callback)
						Callback->OnPropChange(this, FIELD_IS_ONLINE, GV_BOOL);
				}
				break;
			}
			case IMAP_OFFLINE:
			{
				Online = STATUS_OFFLINE;
				if (Callback)
					Callback->OnPropChange(this, FIELD_IS_ONLINE, GV_BOOL);
				break;
			}
			case IMAP_ERROR:
			{
				Online = STATUS_ERROR;
				break;
			}
			case IMAP_ON_NEW:
			{
				if (!Root)
					break;

				// New folders?
				LArray<ImapFolder*> NewEvent;
				for (unsigned i=0; i<m->Fld.Length(); i++)
				{
					ImapFolderInfo &Inf = m->Fld[i];
					
					ImapFolder *Parent = GetParent(Inf.Local, 0);
					if (!Parent)
					{
						// This happens when there is "Inbox/abc/def" but no
						// "Inbox/abc". Apparently this is legal for some servers.
						char sep[] = {Inf.Sep, 0};
						LString Sep(sep);
						LString Unix("/");
						LAssert(Inf.Sep != 0);
						LString::Array a = LString(Inf.Remote).Split(Sep);
						
						Parent = Root;
						
						for (unsigned i=0; Parent && i<a.Length()-1; i++)
						{
							LString::Array p;
							for (unsigned n=0; n<=i; n++)
								p[n] = a[n];
							LString Rem = Unix + Unix.Join(p);
							ImapFolder *f = Parent->Find(NULL, Rem);
							if (!f)
							{
								// Start creating entries...
								f = new ImapFolder(this);
								if (!f)
									break;
								f->Remote = Rem.Get();
								
								char s[MAX_PATH_LEN];
								LMakePath(s, sizeof(s), GetCache(), Rem);
								f->Local = s;

								if (!LDirExists(f->Local) &&
									FileDev->CreateFolder(f->Local))
								{
									// Probably should get the server to recreate the folder too?
									ImapMsg *Create = new ImapMsg(IMAP_CREATE_FOLDER, _FL);
									if (Create)
									{
										ImapFolderInfo &Inf = Create->Fld.New();
										Inf.Local = f->Local.Get();
										Inf.Remote = f->Remote.Get();
										PostThread(Create, false);
									}
								}

								f->SetParent(Parent);
								NewEvent.Add(f);
								f->Field.State = Store3Loaded;
							}

							if (!f)
								break;
							Parent = f;
						}
					}
					
					if (Parent)
					{
						if (!LDirExists(Inf.Local))
						{
							// Create local folder...
							if (FileDev->CreateFolder(Inf.Local))
							{
								if (Parent->Sub.GetState() != Store3Loaded)
								{
									// This will cause the folder to be loaded as if it was pre-existing
									Parent->LoadSub();
									
									ImapFolder *f = Parent->Find(Inf.Local, NULL);
									// Get the new folder ptr and pass it to the UI level
									if (f && Callback)
										NewEvent.Add(f);
								}
								else
								{
									LString RemotePath = Parent->MakeImapPath(Inf.Local);
									ImapFolder *f = new ImapFolder(this, RemotePath);
									if (f)
									{
										f->SetParent(Parent);
										
										// LgiTrace("IMAP Store '%s', parent state=%i\n", f->Remote.Get(), Parent->Sub.State);
										if (Callback)
											NewEvent.Add(f);
									}
									else if (Log)
										Log->Print("IMAP_ON_NEW: mem alloc failed\n");
								}
							}
							else
							{
								LAssert(!"Failed to create folder.");
								if (Log)
									Log->Print("IMAP_ON_NEW: Failed to create folder\n");
							}
						}
					}
				}
				for (unsigned i=0; i<NewEvent.Length(); i++)
				{
					LArray<LDataI*> Items;
					Items.Add(NewEvent[i]);

					ImapFolder *Parent = NewEvent[i]->GetParent();
					if (Parent->Sub.State != Store3Loaded)
						Parent->LoadSub();
						
					Callback->OnNew(Parent, Items, -1, false, FilterIncoming);
				}

				// New email?
				ImapFolder *Parent = m->Parent ? Root->Find(0, m->Parent) : 0;
				if (Parent)
				{
					LArray<LDataI*> Old, Recent;
					LAutoPtr<ImapMsg> DownloadMsg;

					ImapMail *o;
					for (unsigned i=0; i<m->Mail.Length(); i++)
					{
						ImapMailInfo &Inf = m->Mail[i];
						if (!Inf.Uid)
						{
							LAssert(!"No server id.");
							continue;
						}

						bool ParentHasUid = Parent->GetMail(Inf.Uid) != NULL;

						if (!Inf.Flags.ImapDeleted &&
							!ParentHasUid &&
							(o = new ImapMail(this, _FL, Inf.Uid)))
						{
							LArray<LDataI*> *a = Inf.Flags.ImapRecent
												&&
												!Inf.Flags.ImapSeen
												&&
												Parent->System == Store3SystemInbox
												?
												&Recent
												:
												&Old;
							MailInfToObject(DownloadMsg, a, Parent, o, Inf);
						}
					}

					if (DownloadMsg)
					{
						PostThread(DownloadMsg.Release(), false);
					}

					if (Callback)
					{
						if (Recent.Length())
							Callback->OnNew(Parent, Recent, -1, true, FilterIncoming);
						if (Old.Length())
							Callback->OnNew(Parent, Old, -1, false, FilterIncoming);
					}
				}
				break;
			}
			case IMAP_ON_DEL:
			{
				if (!Root)
				{
					LAssert(!"No root?");
					break;
				}

				LgiTrace("IMAP_ON_DEL %i,%i\n", (int)m->Fld.Length(), (int)m->Mail.Length());

				// Folders
				for (unsigned i=0; i<m->Fld.Length(); i++)
				{
					ImapFolderInfo &Inf = m->Fld[i];
					ImapFolder *f = Root->Find(Inf.Local, Inf.Remote);
					if (f)
					{
						LArray<LDataI*> Lst;
						Lst.Add(f);
						if (Callback->OnDelete(f->GetParent(), Lst))
						{
							f->OnDeleteComplete();
							DeleteObj(f);
						}
						else LAssert(!"OnDelete failed.");
					}
				}

				// Email
				ImapFolder *Parent = m->Parent ? Root->Find(0, m->Parent) : 0;
				if (Parent)
				{
					LArray<LDataI*> Lst;

                    if (m->Mail.Length() > 0)
                    {
					    for (unsigned i=0; i<m->Mail.Length(); i++)
					    {
						    ImapMailInfo &Inf = m->Mail[i];
						    ImapMail *e = Parent->GetMail(Inf.Uid);
						    if (!e)
						    {
							    LgiTrace("\t%s:%i - Can't find uid '%i' in folder '%s' (msg from %s:%i)\n",
								    _FL,
								    Inf.Uid,
								    m->Parent.Get(),
								    m->File, m->Line);
								// LAssert(!"No email to delete.");
						    }
						    else if (!Lst.HasItem(e))
						    {
								// LgiTrace("%s:%i - Adding %p %s to del lst.\n", _FL, e, e->Uid.Get());
							    Lst.Add(e);
						    }
					    }
					}
					else
					{
					    for (unsigned i=0; i<Parent->Children().Length(); i++)
					    {
					        LDataI *di = Parent->Children()[i];
					        if (di)
					            Lst.Add(di);
					    }
					}

					// LgiTrace("\tCallback->OnDelete %i\n", (int)Lst.Length());
					if (!Callback || Callback->OnDelete(Parent, Lst))
					{
						for (unsigned i=0; i<Lst.Length(); i++)
						{
							ImapMail *e = dynamic_cast<ImapMail*>(Lst[i]);
							if (e)
							{
								#if 0
								LgiTrace("\t%s:%i - OnDelete '%s' from folder '%s' (msg from %s:%i)\n",
									_FL, e->Uid.Get(), Parent->Remote.Get(),
									m->File, m->Line);
								#endif
								LAssert(!_in_download);
								e->OnDelete();
								DeleteObj(e);
							}
						}
					}
				}

				// LgiTrace("Ending IMAP_ON_DEL\n");
				break;
			}
			case IMAP_REBUILD_MAILBOX:
			{
				ReportError("A server snapshot requires the shared SQLite IMAP backend.");
				break;
			}
			case IMAP_FOLDER_LISTING:
			{
				if (m->Fld.Length() != 1)
				{
					LAssert(!"Not the right folder length.");
					break;
				}
				if (auto f = Root->Find(0, m->Fld[0].Remote))
					f->OnListing(m);
				else
					LAssert(!"No folder");
				break;
			}
			case IMAP_DOWNLOAD:
			{
				auto f = Root->Find(0, m->Parent);
				if (!f)
				{
					LAssert(!"No folder");
					LgiTrace("%s:%i - No folder in IMAP_DOWNLOAD: '%s'\n", _FL, m->Parent.Get());
					break;
				}

				_in_download = true;

				LArray<LDataI*> Changed;
				for (unsigned i=0; i<m->Mail.Length(); i++)
				{
					auto &Inf = m->Mail[i];
					auto e = f->GetMail(Inf.Uid);
					if (e)
					{
						e->SetState(ImapMail::ImapMailIdle, _FL);

						if (Inf.Local)
						{
							if (!LFileExists(Inf.Local))
								LAssert(!"Shouldn't the thread download create this file?");
							//else
							//	LgiTrace("%s:%i - inf.local for %i exists.. yay\n", _FL, Inf.Uid);
						}
						else
						{
							LgiTrace("%s:%i - No inf.local for %i?\n", _FL, Inf.Uid);
						}

						auto t = e->GetMeta();
						if (t)
						{
							e->ReadMime(t);
							Changed.Add(e);
						}
						else
						{
							f->Save();
							LgiTrace("Error: Can't find meta data for email %s\n", strrchr(e->Path, DIR_CHAR));
							LAssert(!"No meta tag for this email?");
						}
					}
					else
					{
						// That can happen if you delete something before the IMAP_DOWNLOAD msg
						// gets back from the worker thread.
					}
				}

				_in_download = false;

				f->SetDirty();

				if (Changed.Length())
					OnChange(_FL, Changed, 0);

				break;
			}
			case IMAP_CREATE_FOLDER:
			{
				if (Callback && Root)
				{
					for (unsigned i=0; i<m->Fld.Length(); i++)
					{
						auto &Inf = m->Fld[i];
						auto f = Root->Find(Inf.Local, Inf.Remote);
						if (f)
						{
							LArray<LDataI*> New;
							New.Add(f);
							Callback->OnNew(f->GetParent(), New, -1, false, false);
						}
					}
				}
				break;
			}
			case IMAP_APPEND:
			{
				LDataFolderI *NewItemParent = NULL;
				LArray<LDataI*> NewItems;
				
				auto f = Root->Find(0, m->Parent);
				if (!f)
				{
					LAssert(!"Folder missing.");
					LgiTrace("%s:%i - Folder '%s' missing.\n", _FL, m->Parent.Get());
				}
				else
				{
					for (unsigned i=0; i<m->Mail.Length(); i++)
					{
						auto &Inf = m->Mail[i];
						auto m = f->FindMail(Inf.Local, Inf.Uid);
						if (!m)
						{
							LAssert(!"There should be an email by this point.");
						}
						else
						{
							if (Inf.Uid)
							{
								// Ok rename the mail object, setup it's new UID
								m->Uid = Inf.Uid;
								LString Path = f->MailPath(m->Uid);
								if (FileDev->Move(m->Path, Path))
								{
									m->Path = Path;
								}

								// Create the meta data tag and hook it up
								auto t = f->GetMeta(m->Uid, true);
								if (t)
								{
									LAssert(m->GetMeta() != NULL);
									m->Serialize(t, true);
									
									if (NewItemParent != NULL && NewItemParent != m->Parent)
									{
										// If items from different parents come through we should OnNew them
										// only when the parent is the same, so flush through ones that have
										// the old parent folder before we start collecting ones from the new
										// folder..
										if (Callback && NewItems.Length() > 0)
											Callback->OnNew(NewItemParent, NewItems, -1, false, FilterIncoming);
										NewItems.Length(0);										
									}
									
									NewItemParent = m->Parent;
									NewItems.Add(m);

									if (!m->Seg)
									{
										auto Meta = m->GetMeta();
										m->Loaded = Store3Unloaded;
										m->ReadMime(Meta);
									}
								}
							}
							else
							{
								// No uid, just destroy it, we'll get it eventually through the
								// \recent mail search
								FileDev->Delete(m->Path, NULL, false);
								m->Path.Empty();
							}
						}
					}
				}

				if (Callback && NewItems.Length())
				{
					// Notify the client the object has arrived.
					Callback->OnNew(NewItemParent, NewItems, -1, false, FilterIncoming);
				}
				break;
			}
			case IMAP_EXPUNGE_FOLDER:
			{
				ImapFolder *f = Root->Find(0, m->Parent);
				if (f)
					f->OnExpunge();
				break;
			}
			case IMAP_RENAME_FOLDER:
			{
				LArray<LDataI*> Renamed;
				auto f = Root->Find(0, m->Parent);
				if (f && f->OnRename(m->NewRemote))
					Renamed.Add(f);
				OnChange(_FL, Renamed, FIELD_FOLDER_NAME);
				break;
			}
			case IMAP_SET_FLAGS:
			{
				LArray<LDataI*> Change, Delete, OnNew;
				
				ImapFolder *f = Root->Find(0, m->Parent);
				if (!f)
				{
					LgiTrace("%s:%i - Missing folder '%s'\n", _FL, m->Parent.Get());
					break;
				}

				LAutoPtr<ImapMsg> DownloadMsg;

				#define USE_BINARY_SEARCH	1

				// LProfile prof("IMAP_SET_FLAGS");

				#if USE_BINARY_SEARCH
				// Sort the list for BinarySearch
				//prof.Add("sort");
				f->Mail.a.Sort([](auto a, auto b)
				{
					return (int32_t)(*a)->Uid - (int32_t)(*b)->Uid;
				});
				#endif

				// prof.Add("process");
				for (auto &e: m->Mail)
				{
					if (e.Uid)
					{
						#if USE_BINARY_SEARCH
							ImapMail *Email = f->Mail.a.BinarySearch([Uid=e.Uid](auto a)
							{
								return (int32_t)a->Uid - (int32_t)Uid;
							},	NULL);
						#else
							// On large folders this is sooooo slow...
							ImapMail *Email = f->GetMail(e.Uid);
						#endif
						
						// This is causing deleted email to reappear??
						if (!Email && !e.Flags.ImapDeleted)
						{
							// Existing email that we don't know about?
							// Ok, well add new entry to track it.
							ImapMail *NewMail = new ImapMail(this, _FL, e.Uid);
							if (NewMail)
								MailInfToObject(DownloadMsg, &OnNew, f, NewMail, e);
						}
	
						if (Email)
						{
							if (Email->RemoteFlags.ImapDeleted ^ e.Flags.ImapDeleted)
							{
								if (e.Flags.ImapDeleted)
									Delete.Add(Email);

								// LAssert(!"What now?");
							}
							else
							{
								Change.Add(Email);
							}

							Email->SetRemoteFlags(e.Flags);
						}
					}
					else
					{
						LgiTrace("%s:%i - No UID: seq=%i\n", _FL, e.Seq);
					}
				}

				if (OnNew.Length())
				{
					f->SetDirty();
					Callback->OnNew(f, OnNew, -1, true, FilterIncoming);
				}
				if (Change.Length())
				{
					f->SetDirty();
					OnChange(_FL, Change, FIELD_FLAGS);
				}
				else if (Delete.Length())
				{
					f->SetDirty();
					Callback->OnDelete(f, Delete);
				}
				if (DownloadMsg)
				{
					PostThread(DownloadMsg.Release(), false);
				}
				break;
			}
			case IMAP_LOAD_FOLDER:
			{
				// This event comes from the delayed ImapFolder::LoadMail thread (ImapFolderLoadThread)...
				// NOT the imap worker thread.
				if (!m->Fld.Length())
				{
					LAssert(!"Must have 1 entry.");
					break;
				}

				auto f = Root->Find(m->Fld[0].Local, nullptr);
				if (!f)
				{
					LAssert(!"Not an IMAP folder.");
					break;
				}

				f->OnLoadMail(true);
				break;
			}
		}

		auto Time = LCurrentTime() - Start;
		if (Time > 1000)
		{
			LgiTrace("%s:%i - ImapStore::OnEvent took " LPrintfUInt64 "ms processing msg %s (len=%i)\n",
				_FL,
				Time,
				m && m->Type < IMAP_MSG_MAX
					?
					ImapMsgTypeNames[m->Type]
					:
					"<error>",
				m ? m->Mail.Length() : 0);
		}
		DeleteObj(m);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
bool ValidateImapDate(LDateTime &dt)
{
	if (dt.Year() == 2000 &&
		dt.Hours() == 0)
	{
		LAssert(!"Invalid IMAP date.");
		dt.Empty();
		return false;
	}

	if (dt.GetTimeZone() != 0)
	{
		LgiTrace("%s:%i - Should be a UTC date.\n", _FL);
		return false;
	}
	
	return true;
}

LDataStoreI *OpenImap(	LString Host,
						int Port,
						LString User,
						LString Pass,
						int ConnectFlags,
						LDataEventsI *Callback,
						LCapabilityClient *Caps,
						MailProtocolProgress *prog[2],
						LStream *Log,
						int AccoundId,
						LAutoPtr<ProtocolSettingStore> store)
{
	return new ImapStore(Host, Port, User, Pass, ConnectFlags, Callback, Caps, prog, Log, AccoundId, store);
}

LDataStoreI *OpenImapWithStorage(	LString Host,
									int Port,
									LString User,
									LString Pass,
									int ConnectFlags,
									LDataEventsI *Callback,
									LCapabilityClient *Caps,
									MailProtocolProgress *prog[2],
									LStream *Log,
									int AccountId,
									LAutoPtr<ProtocolSettingStore> store,
									LMail3Store *Storage)
{
	if (LAppInst->GetOption("imap-import-cache"))
	{
		const char *Error = "Legacy IMAP cache import is retired; persistent accounts now refresh SQLite from the server automatically.";
		LgiTrace("%s\n", Error);
		if (Log)
			Log->Write(Error, strlen(Error), LSocketI::SocketMsgError);
		return NULL;
	}
	if (!Storage)
	{
		const char *Error = "Persistent IMAP accounts require the already-loaded default Mail3 store.";
		LgiTrace("%s\n", Error);
		if (Log)
			Log->Write(Error, strlen(Error), LSocketI::SocketMsgError);
		return NULL;
	}
	auto Result = new ImapStore(Host, Port, User, Pass, ConnectFlags, Callback, Caps, prog, Log, AccountId, store, Storage);
	if (Result->GetInt(FIELD_STATUS) != Store3Success)
	{
		delete Result;
		return NULL;
	}
	return Result;
}
