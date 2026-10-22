#include <stdarg.h>
#include <set>
#include <map>

#include "lgi/common/Lgi.h"
#include "lgi/common/Mail.h"
#include "lgi/common/OpenSSLSocket.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"

#define SECONDS(sec)					((sec) * 1000)
#define MINUTES(min)					(SECONDS(min * 60))
#define DEBUG_OUTPUT_FETCHES			0
#define DEBUG_INPUT_FETCHES				0
#define MAX_LISTING_SIZE				50
#define IDLE_MAX						MINUTES(28)
#define ERROR_RECONNECT_TIMEOUT			SECONDS(30)

#define RUN_TEST_CMDS					0
#if RUN_TEST_CMDS
const char *TestCmds[] = {
	"COPY 3455 Trash",
	"SELECT INBOX",
	"IDLE",
	"SELECT TRASH",
	"SELECT INBOX",
};
size_t CurTestCmd = 0;
#endif

static char BodyTag[]			= "UID BODY.PEEK[]"; // If you change this, check DownloadCallback as well
static char MailListingParts[]	= "FLAGS UID RFC822.SIZE BODYSTRUCTURE BODY.PEEK[HEADER]";
static char SnapshotParts[]		= "FLAGS UID RFC822.SIZE BODY.PEEK[HEADER]";
static char OtherListingParts[]	= "FLAGS UID";

bool ListingCallback(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData);

bool FetchImapRebuild(MailIMap &Imap, const ImapFolderInfo &Folder, ImapMsg &Response, LString &Error)
{
	Response.SetType(IMAP_SYNC_FOLDER, _FL);
	Response.Mail.Empty();
	Response.Fld.Empty();
	Response.Last = false;
	Response.Diagnostic.Empty();
	Error.Empty();
	MailIMap::StrMap Before, After;
	if (!Imap.SelectFolder(Folder.Remote, &Before))
	{
		Error = "Cannot SELECT the mailbox for synchronization.";
		return false;
	}
	auto Epoch = Before.Find("UIDVALIDITY");
	auto Next = Before.Find("UIDNEXT");
	auto Count = Before.Find("Exists");
	if (!Epoch || Epoch.Int() <= 0 || Epoch.Int() > UINT32_MAX ||
		!Next || Next.Int() <= 0 || Next.Int() > UINT32_MAX || !Count || Count.Int() < 0)
	{
		Error = "SELECT did not supply valid UIDVALIDITY, UIDNEXT and EXISTS.";
		return false;
	}
	auto &Selected = Response.Fld.New();
	Selected.Remote = Folder.Remote;
	Selected.Sep = Folder.Sep;
	Selected.LastUid = 0;
	Selected.UidValidity = Epoch.Int();
	Selected.UidNext = Next.Int();
	Selected.ForceFull = Folder.ForceFull;
	Selected.Incremental = !Folder.ForceFull && Folder.Incremental && Folder.UidValidity == Epoch.Int();
	if (Selected.Incremental && Count.Int() > 0)
	{
		struct InventoryState
		{
			std::map<uint32_t, int> Flags;
			bool Invalid = false;
		} Inventory;
		auto Callback = [](MailIMap *, uint32_t, MailIMap::StrMap &Parts, void *Context)
		{
			auto &State = *static_cast<InventoryState*>(Context);
			auto Uid = Parts.Find("UID");
			if (!Uid)
				return true;
			bool HasFlags = false;
			for (auto Part: Parts)
				if (!Stricmp(Part.key, "FLAGS"))
					HasFlags = true;
			ImapMailFlags Flags;
			Flags.Set(Parts.Find("FLAGS"));
			if (!Uid.IsNumeric() || Uid.Int() <= 0 || Uid.Int() > UINT32_MAX || !HasFlags ||
				!State.Flags.emplace((uint32_t)Uid.Int(), Flags.All).second)
			{
				State.Invalid = true;
				return false;
			}
			return true;
		};
		if (!Imap.Fetch(true, "1:*", "UID FLAGS", Callback, &Inventory, NULL) ||
			Inventory.Invalid || (int64)Inventory.Flags.size() != Count.Int())
		{
			Error = "Incomplete or invalid UID/flags inventory; mailbox sync not committed.";
			return false;
		}
		std::set<uint32_t> Known;
		for (size_t Index = 0; Index < Folder.KnownUids.Length(); Index++)
			Known.insert(Folder.KnownUids.ItemAt(Index));
		LArray<uint32_t> NewUids;
		for (auto &Entry: Inventory.Flags)
			if (!Known.count(Entry.first))
				NewUids.Add(Entry.first);
		std::map<uint32_t, ImapMailInfo> NewMail;
		for (size_t Start = 0; Start < NewUids.Length(); Start += 100)
		{
			LStringPipe Range;
			for (size_t Index = Start; Index < NewUids.Length() && Index < Start + 100; Index++)
				Range.Print("%s%u", Index == Start ? "" : ",", NewUids[Index]);
			ImapMsg Headers(IMAP_SYNC_FOLDER, _FL);
			Headers.Fld.New().LastUid = 0;
			auto Requested = Range.NewLStr();
			if (!Imap.Fetch(true, Requested, SnapshotParts, ListingCallback, &Headers, NULL) ||
				Headers.Diagnostic || Headers.Mail.Length() != MIN((size_t)100, NewUids.Length() - Start))
			{
				Error = Headers.Diagnostic ? Headers.Diagnostic :
					LString("Incomplete new-message headers; mailbox sync not committed.");
				return false;
			}
			std::set<uint32_t> Batch;
			for (size_t Index = Start; Index < NewUids.Length() && Index < Start + 100; Index++)
				Batch.insert(NewUids[Index]);
			for (auto &Message: Headers.Mail)
				if (!Batch.count(Message.Uid) || !NewMail.emplace(Message.Uid, Message).second ||
					Message.Flags.All != Inventory.Flags[Message.Uid])
				{
					Error = "Unexpected UID or changed flags in new-message headers; mailbox sync not committed.";
					return false;
				}
		}
		for (auto &Entry: Inventory.Flags)
		{
			auto &Message = Response.Mail.New();
			auto New = NewMail.find(Entry.first);
			if (New != NewMail.end())
				Message = New->second;
			else
			{
				Message.Uid = Entry.first;
				Message.Flags.All = Entry.second;
			}
		}
	}
	if ((!Selected.Incremental && Count.Int() > 0 &&
			!Imap.Fetch(true, "1:*", SnapshotParts, ListingCallback, &Response, NULL)) ||
		Response.Diagnostic ||
		!Imap.SelectFolder(Folder.Remote, &After) ||
		After.Find("UIDVALIDITY") != Epoch || After.Find("UIDNEXT") != Next ||
		After.Find("Exists") != Count ||
		(int64)Response.Mail.Length() != Count.Int())
	{
		Error = Response.Diagnostic ? Response.Diagnostic :
			LString("Incomplete or changing server snapshot; mailbox sync not committed.");
		return false;
	}
	std::set<uint32_t> Uids;
	for (auto &Message: Response.Mail)
		if (!Uids.insert(Message.Uid).second)
		{
			Error = "Duplicate UID in the mailbox sync FETCH response.";
			return false;
		}
	Response.Last = true;
	return true;
}

static bool JournalMailbox(MailIMap &Imap, const LString &Name, int64 ExpectedEpoch)
{
	MailIMap::StrMap Values;
	// Always SELECT: the worker cache alone cannot establish UIDVALIDITY.
	if (!Imap.SelectFolder(Name, &Values))
		return false;
	auto Epoch = Values.Find("UIDVALIDITY");
	return Epoch.IsNumeric() && Epoch.Int() > 0 && Epoch.Int() <= UINT32_MAX && Epoch.Int() == ExpectedEpoch;
}

bool FetchImapBody(MailIMap &Imap, const LString &Folder, const ImapMailInfo &Request,
	ImapMailInfo &Response, LString &Error)
{
	Response = Request;
	Response.Body.Empty();
	Error.Empty();
	struct BodyState
	{
		uint32_t Uid;
		unsigned Count = 0;
		bool Invalid = false;
		LString Body;
	} State{Request.Uid};
	auto Callback = [](MailIMap *, uint32_t, MailIMap::StrMap &Parts, void *Context)
	{
		auto &State = *static_cast<BodyState*>(Context);
		auto Uid = Parts.Find("UID");
		if (!Uid)
			return true;
		if (!Uid.IsNumeric() || Uid.Int() <= 0 || Uid.Int() > UINT32_MAX)
		{
			State.Invalid = true;
			return false;
		}
		if (Uid.Int() != State.Uid)
			return true;
		auto Body = Parts.Find("BODY[]");
		if (++State.Count != 1 || !Body)
		{
			State.Invalid = true;
			return false;
		}
		State.Body = Body;
		return true;
	};
	auto Range = LString::Fmt("%u", Request.Uid);
	if (!Request.Uid || Request.MailId <= 0 || Request.MailboxId <= 0 || Request.Size <= 0 ||
		!JournalMailbox(Imap, Folder, Request.UidValidity) ||
		!Imap.Fetch(true, Range, BodyTag, Callback, &State, NULL, Request.Size) ||
		State.Invalid || State.Count != 1 || (int64)State.Body.Length() != Request.Size ||
		!JournalMailbox(Imap, Folder, Request.UidValidity))
	{
		Error = "The IMAP body UID, epoch, length or complete FETCH response could not be verified.";
		return false;
	}
	Response.Body = State.Body;
	return true;
}

// Answered, Deleted, Draft, Flagged, Seen; excludes server-only Recent and Expunged.
static constexpr int JournalWritableFlags = 47;

struct JournalMessage
{
	uint32_t Uid;
	unsigned Count = 0;
	ImapMailFlags Flags;
	bool Invalid = false;
};

static bool JournalMessageCallback(MailIMap *, uint32_t, MailIMap::StrMap &Parts, void *Context)
{
	auto &Message = *static_cast<JournalMessage*>(Context);
	auto Uid = Parts.Find("UID");
	auto Flags = Parts.Find("FLAGS");
	if (!Uid)
		return true; // Unsolicited sequence-only FETCH notification.
	if (!Uid.IsNumeric() || Uid.Int() <= 0 || Uid.Int() > UINT32_MAX)
	{
		Message.Invalid = true;
		return false;
	}
	if (Uid.Int() != Message.Uid)
		return true;
	bool HasFlags = false; // FLAGS () is valid even when its parsed value is empty.
	for (auto Part: Parts)
		if (!Stricmp(Part.key, "FLAGS"))
			HasFlags = true;
	if (!HasFlags || ++Message.Count != 1)
	{
		Message.Invalid = true;
		return false;
	}
	Message.Flags.Set(Flags);
	return true;
}

static bool JournalFetch(MailIMap &Imap, int64 Uid, ImapMailFlags &Flags)
{
	JournalMessage Message{(uint32_t)Uid};
	auto Range = LString::Fmt("%u", Message.Uid);
	if (!Imap.Fetch(true, Range, "UID FLAGS", JournalMessageCallback, &Message, NULL) ||
		Message.Invalid || Message.Count != 1)
		return false;
	Flags = Message.Flags;
	return true;
}

static bool JournalFetchMailbox(MailIMap &Imap, const LString &Name, int64 Epoch, int64 Uid, ImapMailFlags &Flags)
{
	return JournalMailbox(Imap, Name, Epoch) && JournalFetch(Imap, Uid, Flags) &&
		JournalMailbox(Imap, Name, Epoch);
}

ImapJournalResult ExecuteImapJournalOperation(MailIMap &Imap, ImapJournalCommand &Command)
{
	auto &Operation = Command.Operation;
	ImapJournalResult Result;
	Result.OperationId = Operation.Id;
	Result.AccountId = Operation.AccountId;
	Result.Attempt = Operation.Attempts;
	Result.State = Operation.State;
	bool Reconcile = Operation.State == LMail3ImapOperationState::Reconciling;
	bool ContinueMove = Operation.State == LMail3ImapOperationState::ContinuingMove;
	if ((Reconcile || ContinueMove) && Operation.DestinationUid != -1)
	{
		Result.DestinationUidValidity = Operation.DestinationUidValidity;
		Result.DestinationUid = Operation.DestinationUid;
	}
	auto Fail = [&](const char *Error)
	{
		Result.Error = Error;
		LgiTrace("IMAP journal %" PRIi64 ": %s\n", Operation.Id, Error);
		return Result;
	};
	bool Append = Operation.Type == LMail3ImapOperationType::Append;
	bool Move = Operation.Type == LMail3ImapOperationType::Move;
	if (Operation.Id <= 0 || Operation.AccountId < 0 ||
		(Operation.State != LMail3ImapOperationState::Executing && !Reconcile && !ContinueMove) ||
		(Operation.Type != LMail3ImapOperationType::SetFlags &&
		 Operation.Type != LMail3ImapOperationType::Delete && !Append && !Move))
		return Fail("The worker requires a claimed, supported journal operation.");
	if (ContinueMove && !Move)
		return Fail("Only a move with recorded COPYUID evidence can be continued.");
	auto ValidMailbox = [](const LString &Name, int64 Epoch)
	{
		return Name && strlen(Name.Get()) == Name.Length() && Name.Length() < MAX_PATH_LEN &&
			!strpbrk(Name, "\r\n\"\\") && Epoch > 0 && Epoch <= UINT32_MAX;
	};
	if ((!Append && (!ValidMailbox(Operation.SourceRemoteName, Operation.SourceUidValidity) ||
		 Operation.SourceUid <= 0 || Operation.SourceUid > UINT32_MAX)) ||
		((Append || Move) && !ValidMailbox(Operation.DestinationRemoteName, Operation.DestinationUidValidity)) ||
		Operation.RemoteFlags < 0 || (Operation.RemoteFlags & ~JournalWritableFlags))
		return Fail("Invalid frozen mailbox identity, UID or writable flags.");
	if (Append && !Reconcile &&
		(!Command.Upload || Command.Upload->GetSize() <= 0 || Command.Upload->SetPos(0) != 0))
		return Fail("APPEND requires an owned, seekable original RFC822 image.");
	if (Move && Operation.SourceRemoteName == Operation.DestinationRemoteName)
		return Fail("A move requires a different destination mailbox.");
	if (Reconcile || ContinueMove)
	{
		// Missing UIDPLUS evidence cannot be replaced with a guessed identity.
		if (Append || Move)
		{
			if (Operation.DestinationUid <= 0 || Operation.DestinationUid > UINT32_MAX)
				return Fail("No recorded UIDPLUS destination; APPEND/COPY must not be repeated or guessed.");
			ImapMailFlags DestinationFlags;
			if (!JournalFetchMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity,
					Operation.DestinationUid, DestinationFlags) ||
				(DestinationFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
				return Fail("The recorded destination UID, epoch or flags could not be reconciled.");
		}
		if (!Append)
		{
			ImapMailFlags SourceFlags;
			if (!JournalFetchMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity,
					Operation.SourceUid, SourceFlags))
				return Fail("The source UID or epoch could not be reconciled; no mutation was sent.");
			if (ContinueMove && !SourceFlags.ImapDeleted)
			{
				if ((SourceFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
					return Fail("The source flags changed since COPY; continuation requires an explicit recovery decision.");
				LArray<uint32_t> Uids;
				Uids.Add((uint32_t)Operation.SourceUid);
				Result.Outcome = ImapJournalOutcome::Uncertain;
				// Add only Deleted: replacing FLAGS would erase concurrent edits and server keywords.
				if (!Imap.SetFlagsByUid(Uids, "\\Deleted", true))
					return Fail("Source deletion was not confirmed; retain COPYUID evidence and reconcile before continuing.");
				if (!JournalFetchMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity,
						Operation.SourceUid, SourceFlags) || !SourceFlags.ImapDeleted)
					return Fail("The continued source deletion could not be verified; retain COPYUID evidence.");
			}
			else if (Move ? !SourceFlags.ImapDeleted :
				(SourceFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
				return Fail("The source has not reached the requested state; reconciliation will not repeat a mutation.");
		}
		if (Move)
		{
			ImapMailFlags DestinationFlags;
			if (!JournalFetchMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity,
					Operation.DestinationUid, DestinationFlags) ||
				(DestinationFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
				return Fail("The destination UID, epoch or flags changed while recovering the source.");
		}
		Result.Outcome = ImapJournalOutcome::Confirmed;
		return Result;
	}
	ImapMailFlags SourceFlags;
	if ((Append || Move) && !JournalMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity))
		return Fail("The destination SELECT failed or UIDVALIDITY changed; no mutation was sent.");
	if (!Append)
	{
		if (!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity))
			return Fail("The source SELECT failed or UIDVALIDITY changed; no mutation was sent.");
		if (!JournalFetch(Imap, Operation.SourceUid, SourceFlags))
			return Fail("The source UID could not be fetched; no mutation was sent.");
		if (Move && (SourceFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
			return Fail("The source flags changed since the move was queued; no mutation was sent.");
		if (!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity))
			return Fail("The source epoch changed during verification; no mutation was sent.");
	}
	ImapMailFlags Flags;
	Flags.All = Operation.RemoteFlags;
	// Beyond this point a failed reply may conceal a successful server mutation.
	Result.Outcome = ImapJournalOutcome::Uncertain;
	if (Append || Move)
	{
		MailIMap::UidPlusResult Mapping;
		LArray<uint32_t> SourceUids;
		if (Move)
			SourceUids.Add((uint32_t)Operation.SourceUid);
		bool Accepted = Append
			? Imap.AppendLiteral(Operation.DestinationRemoteName, Flags, *Command.Upload, Mapping)
			: Imap.CopyByUid(SourceUids, Operation.DestinationRemoteName, &Mapping);
		if (!Accepted)
			return Fail("APPEND/COPY did not return confirmed success; reconcile before retrying.");
		if (Mapping.UidValidity != Operation.DestinationUidValidity || !Mapping.DestinationUid ||
			(Move && Mapping.SourceUid != Operation.SourceUid))
			return Fail("APPEND/COPY succeeded without a valid single-message UIDPLUS mapping; do not repeat it.");
		Result.DestinationUidValidity = Mapping.UidValidity;
		Result.DestinationUid = Mapping.DestinationUid;
		ImapMailFlags DestinationFlags;
		if (!JournalMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity) ||
			!JournalFetch(Imap, Mapping.DestinationUid, DestinationFlags) ||
			!JournalMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity))
			return Fail("The APPEND/COPY destination could not be verified after success.");
		if (Move)
		{
			if ((DestinationFlags.All & JournalWritableFlags) != Operation.RemoteFlags)
				return Fail("COPY succeeded but destination flags changed; retain the COPYUID evidence.");
			// Never mark the source deleted until the COPY identity is known.
			if (!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity) ||
				!JournalFetch(Imap, Operation.SourceUid, SourceFlags) ||
				!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity))
				return Fail("COPY succeeded but the source changed before deletion; retain the COPYUID evidence.");
			Flags = SourceFlags;
			Flags.ImapRecent = Flags.ImapExpunged = false;
			Flags.ImapDeleted = true;
		}
		else
		{
			if ((DestinationFlags.All & JournalWritableFlags) != Flags.All)
				return Fail("APPEND succeeded but its destination flags do not match the requested flags.");
			Result.Outcome = ImapJournalOutcome::Confirmed;
			return Result;
		}
	}
	LArray<uint32_t> Uids;
	Uids.Add((uint32_t)Operation.SourceUid);
	if (!Imap.SetFlagsByUid(Uids, Flags.Get()))
		return Fail("UID STORE did not return confirmed success; reconcile before retrying.");
	ImapMailFlags Verified;
	if (!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity) ||
		!JournalFetch(Imap, Operation.SourceUid, Verified) ||
		!JournalMailbox(Imap, Operation.SourceRemoteName, Operation.SourceUidValidity) ||
		(Verified.All & JournalWritableFlags) != Flags.All)
		return Fail("The source UID, epoch or flags could not be verified after UID STORE.");
	if (Move && !JournalMailbox(Imap, Operation.DestinationRemoteName, Operation.DestinationUidValidity))
		return Fail("The destination epoch changed after COPY/source deletion.");
	Result.Outcome = ImapJournalOutcome::Confirmed;
	return Result;
}

int FldCmp(MailImapFolder **a, MailImapFolder **b)
{
	char *sa = (*a)->GetPath();
	char *sb = (*b)->GetPath();
	return _stricmp(sa, sb);
}

char *GetListingParts(char *Folder)
{
	if (Folder &&
		(
			!_stricmp(Folder, "/Calendar") ||
			!_stricmp(Folder, "/Contacts")
		))
		return OtherListingParts;

	return MailListingParts;
}

bool FlagsCb(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData)
{
	ImapMailInfo *Mi = (ImapMailInfo*)UserData;
	char *Flags = Parts.Find("FLAGS");
	if (!Flags)
		return false;
	
	LAssert(Mi->Uid > 0);
	Mi->Flags.Set(Flags);
	return true;
}

bool RecentCb(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData)
{
	ImapMsg *m = (ImapMsg*)UserData;
	char *Flags = Parts.Find("FLAGS");
	// char *Body = Parts.Find("BODY.PEEK[]");
	if (!Flags)
		return false;
	
	ImapMailInfo &i = m->Mail.New();
	i.Flags.Set(Flags);
	
	return true;
}

struct ImapThreadPrivate : public LMutex, public LCancel
{
	LAutoPtr<MailIMap> Imap;
	ImapStore *Store;
	ProtocolSettingStore *SettingStore;
	LString Cache;
	LArray<ImapMsg*> Msgs;
	ImapThread *Thread;
	LStream *Log;
	LString CurrentFolder;
	LCapabilityClient *Caps;
	char Sep[4];
	LString InboxPath;
	int AccountId;

	LAutoPtr<ImapMsg> Listing;
	int64 ListingTime = 0;
	int Listings = 0;
	int Exists = -1;
	int FetchSeq = 1;

	ImapThreadPrivate(	ImapThread *thread,
						ImapStore *s,
						LCapabilityClient *caps,
						LStream *log,
						ProtocolSettingStore *store,
						int accountId)
		: LMutex("ImapThreadPrivate")
	{
		Sep[0] = 0;
	    Caps = caps;
	    SettingStore = store;
	    AccountId = accountId;
		Log = log;
		Thread = thread;
		Store = s;
		Cache = s->GetCache();
		InboxPath = "/INBOX";
	}

	bool Cancel(bool b = true)
	{
		return LCancel::Cancel(b);
	}

	LStreamI *NewFetch(const char *Param, const char *Parts)
	{
		#if DEBUG_OUTPUT_FETCHES

		LFile *f = 0;

		char s[MAX_PATH_LEN], File[64], *c;
		sprintf_s(File, sizeof(File), "%04.4i %s (%s).txt", FetchSeq++, Param, Parts);
		while (c = strchr(File + 5, ':'))
			*c = '.';
		LMakePath(s, sizeof(s), Cache, File);

		if (f = new LFile)
		{
			if (!f->Open(s, O_WRITE))
			{
				LAssert(!"Can't open file...");
			}
		}

		return f;

		#else

		return new LStringPipe(4 << 10);

		#endif
	}

	bool SelectFolder(const char *f, MailIMap::StrMap *Values = NULL)
	{
		if (!f)
		{
			LAssert(!"No folder.");
			return false;
		}

		if (!CurrentFolder || Strcmp(f, CurrentFolder.Get()))
		{
			MailIMap::StrMap Local;

			Exists = -1;
			if (!Imap->SelectFolder(f, Values ? Values : &Local))
				return false;

			CurrentFolder = f;
			LString e = (Values ? *Values : Local).Find("Exists");
			if (e)
				Exists = (int) e.Int();
			
			LString s;
			s.Printf("Select(%s) Exists=%i\n", f, Exists);
			Log->Write(s.Get(), s.Length(), LSocketI::SocketMsgInfo);
		}

		return true;
	}

	void Error(const char *file, int line, const char *s, ...)
	{
		char Buf[512];

		char *Dir = strrchr((char*)file, DIR_CHAR);
		#ifndef WIN32
		#define _snprintf snprintf
		#define _vsnprintf vsnprintf
		#endif
		int c = sprintf_s(Buf, sizeof(Buf), "%s:%i - ", Dir?Dir+1:file, line);

		va_list Arg;
		va_start(Arg, s);
		int b = vsprintf_s(Buf+c, sizeof(Buf)-c, s, Arg);
		va_end(Arg);
		c = b > 0 ? c + b : -1;

		if (Log && c > 0)
		{
			if (Buf[c] != '\n')
				Buf[c++] = '\n';

			Log->Write(Buf, c, LSocketI::SocketMsgError);
		}
	}

	using TStrMap = LHashTbl<ConstStrKey<char,true>,bool>;
	
	void CollectSubFolders(TStrMap &t, const char *Path)
	{
		LDirectory Dir;
		for (int b=Dir.First(Path); b && !IsCancelled(); b=Dir.Next())
		{
			if (!Dir.IsDir())
				continue;
			t.Add(Dir.FullPath(), true);
			CollectSubFolders(t, Dir.FullPath());
		}
	}

	// Do folder sync
	void SyncFolders()
	{
		if (Store->IsLive())
		{
			LArray<MailImapFolder*> Folders;
			if (!Imap->GetFolders(Folders))
			{
				Folders.DeleteObjects();
				Error(_FL, "Cannot list the live IMAP account's mailboxes.");
				Thread->PostStore(new ImapMsg(IMAP_ERROR, _FL));
				return;
			}
			LAutoPtr<ImapMsg> Inventory(new ImapMsg(IMAP_FOLDER_LISTING, _FL));
			Inventory->Last = true;
			for (auto Folder: Folders)
			{
				if (!Sep[0])
					Sep[0] = Folder->GetSep();
				if (!Folder->NoSelect)
				{
					char Delimiter[] = {Folder->GetSep(), 0};
					auto &Info = Inventory->Fld.New();
					Info.Remote = LString("/") + LString("/").Join(LString(Folder->GetPath()).SplitDelimit(Delimiter));
					Info.Sep = Folder->GetSep();
				}
			}
			Folders.DeleteObjects();
			Thread->PostStore(Inventory.Release());
			return;
		}
		auto Root = CastFld(Store->GetRoot(false));
		if (!Root)
			return;

		// Collect all the imap folders...
		LArray<MailImapFolder*> Folders;
		if (Imap->GetFolders(Folders))
		{
			// Collect all the local folders...
			TStrMap Existing;
			CollectSubFolders(Existing, Root->Local);

			LAutoPtr<ImapMsg> NewFolders;
			
			Folders.Sort(FldCmp);

			// For all the imap folders, check they exist locally...
			for (unsigned i=0; i<Folders.Length(); i++)
			{
				auto f = Folders[i];
				if (Sep[0] == 0)
				{
					Sep[0] = f->GetSep();
					Sep[1] = 0;
				}
				
				char p[MAX_PATH_LEN];
				auto t = LString(f->Path).SplitDelimit(Sep);
				strcpy_s(p, sizeof(p), Root->Local);
				for (unsigned n=0; n<t.Length(); n++)
				{				
					LMakePath(p, sizeof(p), p, t[n]);
				}
				
				if (LDirExists(p))
				{
					// Remove from list of existing... at the end of this loop
					// existing will contain a list of folder NOT on the imap
					// server.
					if (Existing.Find(p))
						Existing.Delete(p);
					else
						LgiTrace("%s:%i - remote folder '%s' doesn't exist locally (%s).\n", _FL, f->Path, p);
				}
				else
				{
					if (!NewFolders)
						NewFolders.Reset(new ImapMsg(IMAP_ON_NEW, _FL));
					
					if (NewFolders)
					{
						if (Log)
							Log->Print("SyncFolders: creating '%s'\n", f->Path);

						ImapFolderInfo &Inf = NewFolders->Fld.New();
						Inf.Local = p;
						Inf.Remote = f->Path;
						Inf.Sep = f->GetSep();
					}
				}
			}

			if (NewFolders && NewFolders->Fld.Length() > 0)
			{
				Store->PostStore(NewFolders.Release());
			}

			// char *Deleted;
			// for (bool p=Existing.First(&Deleted); p; p=Existing.Next(&Deleted))
			for (auto it : Existing)
			{
				if (Log)
					Log->Print("SyncFolders: deleting '%s'\n", it.key);

				ImapMsg *del_folder = new ImapMsg(IMAP_ON_DEL, _FL);
				if (del_folder)
				{
					ImapFolderInfo &Inf = del_folder->Fld.New();
					Inf.Local = it.key;
					Store->PostStore(del_folder);
				}
			}

			Folders.DeleteObjects();
		}
		else LgiTrace("%s:%i - GetFolders failed.\n", _FL);
	}

	bool DownloadIds(char *Parts, bool Uid, LString::Array &Ids)
	{
		auto Lst = LString(",").Join(Ids);
		LAutoPtr<LStreamI> Raw(NewFetch("All", Parts));
		LAutoPtr<ImapMsg> m(new ImapMsg(IMAP_ON_NEW, _FL));
		if (!m)
			return false;

		if (!Imap->Fetch(Uid, Lst, Parts, ListingCallback, m, Raw))
			return false;

		m->New = true;
		m->Parent = CurrentFolder.Get();
		Thread->PostStore(m.Release());

		return true;
	}
};

ImapThread::ImapThread(ImapStore *s, LCapabilityClient *caps, LStream *log, ProtocolSettingStore *store, int AccountId) :
	LThread("ImapThread"),
	LMutex("ImapThread.Mutex")
{
	if ((d = new ImapThreadPrivate(this, s, caps, log, store, AccountId)))
	{
		Run();
	}
}

ImapThread::~ImapThread()
{
	d->Cancel(true);

	auto Start = LCurrentTime();
	while (!IsExited())
	{
		LSleep(10);
		if (LCurrentTime() - Start > 2000)
		{
			LAssert(!"Why is it taking so long?");
		}
	}

	DeleteObj(d);
}

void ImapThread::PostThread(ImapMsg *m, bool UiPriority)
{
	if (Lock(_FL))
	{
		if (d->Store->ItemProgress)
		{
			if (d->Store->ItemProgress->Range == 0)
				d->Store->ItemProgress->Start = LCurrentTime();
			if (d->Store->DataProgress)
				d->Store->DataProgress->Start = LCurrentTime();

			d->Store->ItemProgress->Range++;
		}

		if (UiPriority)
			d->Msgs.AddAt(0, m);
		else
			d->Msgs.Add(m);
		Unlock();
	}
}

void ImapThread::PostStore(ImapMsg *m)
{
	if (m)
		d->Store->PostStore(m);
}

/*
bool IdleCallback(MailIMap *Imap, char *Msg, GHashTbl<const char*, char*> &Parts, void *UserData)
{
	ImapMsg *m = (ImapMsg*) UserData;
	if (!m)
		return false;
	
	char *Flags = Parts.Find("FLAGS");
	char *Uid = Parts.Find("UID");
	if (Flags && Uid)
	{
		ImapMailInfo &u = m->Mail.New();
		u.Uid.Reset(NewStr(Uid));
		u.Flags.Set(Flags);		
	}
	else return false;
	
	return true;
}
*/

bool ListingCallback(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData)
{
	ImapMsg *m = (ImapMsg*) UserData;
	if (!m)
	{
		LAssert(!"No user data.");
		return false;
	}

	auto Flags			= Parts.Find("FLAGS");
	auto Uid			= Parts.Find("UID");
	auto Rfc822Size		= Parts.Find("RFC822.SIZE");
	auto BodyStructure	= Parts.Find("BODYSTRUCTURE");
	auto Header			= Parts.Find("BODY[HEADER]");
	if (!Uid)
		return true; // Unsolicited sequence-only FETCH notification.
	bool HasFlags = false, HasHeader = false;
	for (auto Part: Parts)
	{
		if (!Stricmp(Part.key, "FLAGS"))
			HasFlags = true;
		if (!Stricmp(Part.key, "BODY[HEADER]"))
			HasHeader = true;
	}
	if (m->Type == IMAP_SYNC_FOLDER &&
		(!HasFlags || !Rfc822Size.IsNumeric() || Rfc822Size.Int() < 0 || !Header))
	{
		m->Diagnostic = LString::Fmt("Incomplete server rebuild FETCH metadata (sequence %u, UID %" PRIi64 "): "
			"FLAGS present=%i, numeric RFC822.SIZE=%i, header field present=%i, header bytes=%" PRIi64 ".",
			Msg, Uid.Int(), (int)HasFlags, (int)(Rfc822Size.IsNumeric() && Rfc822Size.Int() >= 0),
			(int)HasHeader, (int64)Header.Length());
		LgiTrace("%s\n", m->Diagnostic.Get());
		return false;
	}

	#if DEBUG_INPUT_FETCHES
	const char *k;
	for (char *v = Parts.First(&k); v; v = Parts.Next(&k))
	{
		LgiTrace("%s\n", k);
	}
	#endif

	auto Number = Uid.Int();
	if (!Uid.IsNumeric() || Number <= 0 || Number > UINT32_MAX)
	{
		if (m->Type == IMAP_SYNC_FOLDER)
			m->Diagnostic = "Invalid UID in the server rebuild FETCH response.";
		LgiTrace("Invalid UID in the server FETCH response: '%s'.\n", Uid.Get());
		return false;
	}
	uint32_t uid = (uint32_t)Number;
	#ifdef _DEBUG
	if (m->Type != IMAP_SYNC_FOLDER)
		for (auto &Message: m->Mail)
		{
			if (Message.Uid == uid)
			{
				LAssert(!"Duplicate UID.");
				return false;
			}
		}
	#endif

	if (m->Fld.Length() > 0 &&
		uid <= m->Fld[0].LastUid)
	{
		// WTF server? (dovecot brokenness)
	}
	else
	{
		auto &i = m->Mail.New();

		i.Seq = Msg;
		i.Flags.Set(Flags);
		i.Uid = uid;
		i.Structure = BodyStructure;

		if (Header)
		{
			i.Headers = Header;
			
			if (auto Date = LGetHeaderField(Header, "Date"))
				i.Date = Date;
			#if DEBUG_INPUT_FETCHES
			else LgiTrace("%s:%i - No date field? (Uid=%i)\n", _FL, i.Uid);
			#endif
		}
		#if DEBUG_INPUT_FETCHES
		else LgiTrace("%s:%i - No header?\n", _FL);
		#endif

		if (Rfc822Size)
			i.Size = Rfc822Size.Int();
	}

	return true;
}

void ImapThread::Log(LSocketI::SocketMsgType Type, const char *Fmt, ...)
{
	LString s;
	va_list arg;
	va_start(arg, Fmt);
	s.Printf(arg, Fmt);
	va_end(arg);
	d->Log->Write(s.Get(), s.Length(), Type);
}

ImapMsg *ImapThread::GetListing()
{
	if (!d->Listing)
	{
		d->Listing.Reset(new ImapMsg(IMAP_FOLDER_LISTING, _FL));
		d->ListingTime = LCurrentTime();
	}

	return d->Listing;
}

void ImapThread::FlushListing(bool Force)
{
	if (d->Listing)
	{
		int64 Now = LCurrentTime();
		if (Force ||
			!d->ListingTime ||
			Now - d->ListingTime > TIMEOUT_LISTING_CHUNK ||
			d->Listing->Mail.Length() >= MAX_LISTING_SIZE)
		{
			d->Listings += (int)d->Listing->Mail.Length();
			// LgiTrace("Flushing %i listings (%i)\n", d->Listing->Ids.Length(), d->Listings);
			d->Listing->Last = Force;
			PostStore(d->Listing.Release());
		}
	}
}

bool HeadersCallback(MailIMap *Imap, char *Msg, LHashTbl<ConstStrKey<char,false>, char*> &Parts, void *UserData)
{
	ImapMsg *m = (ImapMsg*) UserData;
	if (m)
	{
		char *Uid = Parts.Find("UID");
		char *Headers = Parts.Find("BODY[HEADER]");
		if (Headers && Uid)
		{
			ImapMailInfo &i = m->Mail.New();

			i.Uid = atoi(Uid);
			i.Headers = Headers;

			return true;
		}

		LgiTrace("%s:%i - Missing fields: %p,%p?", _FL, Headers, Uid);
	}

	return false;
}

struct DownloadInfo
{
	uint64_t Uid;
	LString Parent;
	LString Local;
	ImapThread *Thread;
};

bool DownloadCallback(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData)
{
	auto Inf = (DownloadInfo*) UserData;
	auto sUid = Parts.Find("UID");
	auto Body = Parts.Find("BODY[]");
	if (sUid && Body)
	{
		auto Uid = atoi(sUid);

		if (Inf->Local.Find(sUid) < 0)
		{
			// Ugh no, the UID and file need to match.
			LAssert(!"UID not in file name?");
		}

		// Do a UID check on the email...
		if (!Inf->Uid || Uid != Inf->Uid)
		{
			// This is probably caused by a FETCH result parsing error.
			// LAssert(!"Uid mismatch.");
			LgiTrace("%s:%i - Uid mismatch: %i %i\n", _FL, Uid, Inf->Uid);
			return false;
		}
		else
		{
			// Write it out (loop is because windows is shit)
			LFile f;
			for (int i=0; !f.Open(Inf->Local, O_WRITE) && i<100; i++)
				LSleep(20);

			if (f)
			{
				size_t BodyLen = strlen(Body);
				if (BodyLen < 8)
				{
					// Yargh, there be trouble brewin...
					LAssert(!"No body? Really?");
				}
				else
				{
					f.SetSize(0);
					f.Write(Body, BodyLen);
					f.Close();

					auto Msg = new ImapMsg(IMAP_DOWNLOAD, _FL);
					if (Msg)
					{
						ImapMailInfo &i = Msg->Mail.New();
						i.Uid = Uid;
						i.Local = Inf->Local;
						Msg->Parent = Inf->Parent;

						// auto exists = LFileExists(i.Local);
						// LgiTrace("LocalImapFile: uid=%i file=%s exists=%i\n", Uid, Inf->Local.Get(), exists);
						Inf->Thread->PostStore(Msg);
					}
					else LAssert(!"Alloc err");
				}
			}
			else
			{
				LAssert(!"Can't open IMAP cache file?");
				LgiTrace("%s:%i - Failed to open '%s' for writing.\n", _FL, Inf->Local.Get());
				return false;
			}
		}
	}
	else LgiTrace("%s:%i - Missing parts: %p %p\n", _FL, sUid.Get(), Body.Get());

	return true;
}

template <class T>
class ImapLogSocket : public T
{
	LAutoPtr<LFile> File;
	MailProtocolProgress *Prog = nullptr;

public:
	ImapLogSocket(char *file, LCapabilityClient *caps, LStreamI *logger, MailProtocolProgress *prog) :
		T(dynamic_cast<LStream*>(logger), caps)
	{
		Prog = prog;
		
		if (file &&
			File.Reset(new LFile))
		{
			if (File->Open(file, O_WRITE))
				File->SetPos(File->GetSize());
			else
				File.Reset();
		}
	}

	~ImapLogSocket()
	{
		if (File)
			File->ChangeThread();
	}

	void OnDisconnect() override
	{
		if (auto log = dynamic_cast<LStream*>(T::GetLog()))
			log->Print("Disconnected.\n");
	}

	ssize_t Read(void *ptr, ssize_t size, int flags) override
	{
        memset(ptr, 0, size);
		ssize_t r = T::Read(ptr, size, flags);
		if (r > 0)
		{
			if (Prog)
				Prog->Value += r;
			if (File)
				File->Write(ptr, r);
		}
		return r;
	}

	ssize_t Write(const void *ptr, ssize_t size, int flags) override
	{
		if (Prog)
			Prog->Value += size;

		if (File)
			File->Write(ptr, size);
		
		return T::Write(ptr, size, flags);
	}
};


bool UidCb(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData)
{
	ImapMsg *m = (ImapMsg*)UserData;
	auto sUid = Parts.Find("UID");
	if (sUid && Msg)
	{
		bool Found = false;
		for (auto &i: m->Mail)
		{
			if (i.Seq == Msg)
			{
				i.Uid = atoi(sUid);
				LgiTrace("%s:%i - Msg=%i has Uid=%i\n", _FL, Msg, i.Uid);
				Found = true;
				break;
			}
		}

		if (!Found)
			LgiTrace("%s:%i - No Msg='%s'\n", _FL, Msg);
	}
	else LgiTrace("%s:%i - Needs Uid and Msg\n", _FL);

	return true;
}

int ImapThread::Main()
{
	bool Online = false, InIdle = false;
	int IdleCount = 0;
	uint64 IdleStart = 0;
	uint64 ErrorStart = 0;
	unsigned Connection = 0;
	ImapMsgType LastCommand = IMAP_NULL;
	LString LastMailbox;

	while (!d->IsCancelled())
	{
		if (d->Imap &&
			!d->Imap->IsOnline())
		{
			// Lost connection... delete and start again
			LgiTrace("IMAP account %i connection %u lost after %s '%s'; idle=%i.\n",
				d->AccountId, Connection, ImapMsgTypeNames[LastCommand],
				LastMailbox.Get() ? LastMailbox.Get() : "", (int)InIdle);
			if (d->Lock(_FL))
			{
				d->Imap.Reset();
				d->Unlock();
			}
		}

		if
		(
			!d->Imap
			&&
			(
				!ErrorStart
				||
				(LCurrentTime() - ErrorStart > ERROR_RECONNECT_TIMEOUT)
			)
		)
		{
			// Setup a new imap protocol object
			if (d->Lock(_FL))
			{
				d->Imap.Reset(new MailIMap);
				d->Unlock();
			}

			if (d->Imap)
			{
				LUri u(d->Store->Host);
				if (u.Port <= 0 &&
					d->Store->Port > 0)
				{
					u.Port = d->Store->Port;
				}
				
				d->Imap->Logger = d->Store->GetLogger();
				d->Imap->SetCancel(d);

				char LogFileName[MAX_PATH_LEN] = "", Part[32];
				LVariant LogFmt;
				if (d->SettingStore->GetOptions()->GetValue(OPT_LogFormat, LogFmt) &&
					LogFmt.CastInt32() > 0)
				{
					sprintf_s(Part, sizeof(Part), "../%i_log.txt", d->Store->AccountId);
					LAssert(d->Cache != NULL);
					LMakePath(LogFileName, sizeof(LogFileName), d->Cache, Part);
				}

				// Connect to the server
				LSocketI *Sock = nullptr;
				bool SslDirect = (d->Store->ConnectFlags & MAIL_SSL) != 0;
				bool StartTls = (d->Store->ConnectFlags & MAIL_USE_STARTTLS) != 0;
				if (SslDirect || StartTls)
				{
					auto s = new ImapLogSocket<SslSocket>(	LogFileName[0] ? LogFileName : NULL,
																				d->Caps,
																				d->Store->GetLogger(),
																				d->Store->DataProgress);
					Sock = s;
					if (s)
					{
						// Set up cert callback stuff:
						s->UserRef.Printf("accountId=%i", d->AccountId);
						s->SetSslOnConnect(SslDirect);
						s->SetCertCallback([this](auto host, auto *data)
							{
								if (d->Store->Callback)
									return d->Store->Callback->AllowSslCert(host, data);
								
								LgiTrace("%s:%i - no callback to handler certs.\n", _FL);
								return false;
							});
					}
				}
				else
				{
					Sock = new ImapLogSocket<LSocket>(	LogFileName[0] ? LogFileName : NULL,
														d->Caps,
														d->Store->GetLogger(),
														d->Store->DataProgress);
				}
				
				bool reqsOk = true;
				auto p = GetOAuth2Params(u.sHost, MAGIC_MAIL);
				if (p.Provider != LOAuth2::Params::None)
				{
					// Check any requirements:
					for (auto req: p.RequiredCapabilities)
					{
						if (!p.CheckRequirement(req))
						{
							reqsOk = false;
							if (d->Caps)
								d->Caps->NeedsCapability(req);
							else
								LAssert(!"no capabilty object?");
						}
					}

					d->Imap->SetOAuthParams(p);
					d->Imap->SetParentWindow(dynamic_cast<LViewI*>(d->Store->Callback));
				}

				Sock->SetCancel(d);
					
				if (reqsOk &&
					d->Imap->Open(	Sock,
									u.sHost,
									u.Port,
									d->Store->User,
									d->Store->Pass,
									d->SettingStore,
									d->Store->ConnectFlags))
				{
					ErrorStart = 0;
					Connection++;
					d->CurrentFolder.Empty();
					LgiTrace("IMAP account %i connection %u established; requesting folder inventory.\n",
						d->AccountId, Connection);
					if (d->Log)
						d->Log->Print("Connected to %s:%i\n", u.sHost.Get(), u.Port);

					// Sync our folders
					d->SyncFolders();
				}
				else
				{
					// Failed to connect... start again
					if (d->Lock(_FL))
					{
						d->Imap.Reset();
						d->Unlock();
					}

					if (u.Port == 0)
					{
						if (d->Store->ConnectFlags & MAIL_SSL) u.Port = IMAP_SSL_PORT;
						else u.Port = IMAP_PORT;
					}
					
					d->Error(_FL, "IMAP connection to %s:%i failed.", u.sHost.Get(), u.Port);
					if (ErrorStart == 0)
						PostStore(new ImapMsg(IMAP_ERROR, _FL));
					ErrorStart = LCurrentTime();
				}
			}
		}

		bool CurrentlyOnline = d->Imap ? d->Imap->IsOnline() : false;
		if (CurrentlyOnline ^ Online)
		{
			PostStore(new ImapMsg(CurrentlyOnline ? IMAP_ONLINE : IMAP_OFFLINE, _FL));
			Online = CurrentlyOnline;
		}

		if (!d->Imap)
		{
			LSleep(50);
			continue;
		}

		LAutoPtr<ImapMsg> m;
		if (Lock(_FL))
		{
			if (d->Msgs.Length())
			{
				m.Reset(d->Msgs[0]);
				d->Msgs.DeleteAt(0, true);
					
				uint64 Now = LCurrentTime();
				if (d->Store->DataProgress &&
					(Now - d->Store->DataProgress->Start) > 10000)
				{
					d->Store->DataProgress->Start = Now;
					d->Store->DataProgress->Value = 0;
				}
			}
			else
			{
				if (d->Store->ItemProgress)
					d->Store->ItemProgress->Empty();
				if (d->Store->DataProgress)
					d->Store->DataProgress->Empty();
			}
				
			Unlock();
		}
			
		if (m)
		{
			LastCommand = m->Type;
			LastMailbox = m->Parent ? m->Parent :
				m->Fld.Length() ? m->Fld[0].Remote : LString();
			// This block skips the idle stop code if the folder select is the current folder,
			// which in turn is a noop anyway.
			if (m->Type == IMAP_SELECT_FOLDER &&
				m->Fld.Length() == 1 &&
				m->Fld[0].Remote &&
				d->CurrentFolder &&
				!strcmp(m->Fld[0].Remote, d->CurrentFolder))
			{
				continue;
			}
				
			// If we're currently in idle state, we need to stop that before we issue another command
			if (InIdle)
			{
				IdleCount = 0;
				d->Imap->FinishIdle();
				InIdle = false;
				IdleStart = 0;
			}
				
			// Process the command from the GUI thread.
			switch (m->Type)
			{
				default: break;
				case IMAP_JOURNAL_OPERATION:
				{
					if (!m->Journal)
					{
						d->Error(_FL, "Missing journal command payload.");
						PostStore(new ImapMsg(IMAP_ERROR, _FL));
						break;
					}
					// The dispatcher forces its own SELECTs; invalidate the legacy shortcut.
					d->CurrentFolder.Empty();
					m->JournalResult = ExecuteImapJournalOperation(*d->Imap, *m->Journal);
					m->Journal.Reset();
					if (m->JournalResult.Outcome != ImapJournalOutcome::Confirmed && d->Lock(_FL))
					{
						LgiTrace("IMAP account %i closing connection %u: unconfirmed journal operation %" PRIi64 ": %s\n",
							d->AccountId, Connection, m->JournalResult.OperationId, m->JournalResult.Error.Get());
						// Even a failed preflight may leave an unconsumed transport reply.
						d->Imap.Reset();
						d->Unlock();
					}
					PostStore(m.Release());
					break;
				}
				case IMAP_SYNC_FOLDER:
				{
					for (auto &Folder: m->Fld)
					{
						LAutoPtr<ImapMsg> Response(new ImapMsg(IMAP_SYNC_FOLDER, _FL));
						Response->Generation = m->Generation;
						LString Error;
						d->CurrentFolder.Empty();
						if (!FetchImapRebuild(*d->Imap, Folder, *Response, Error))
						{
							LgiTrace("IMAP account %i sync '%s' rejected on connection %u; transport online=%i.\n",
								d->AccountId, Folder.Remote.Get(), Connection, (int)d->Imap->IsOnline());
							d->Error(_FL, "Mailbox sync '%s': %s", Folder.Remote.Get(), Error.Get());
							LAutoPtr<ImapMsg> Failure(new ImapMsg(IMAP_SYNC_FOLDER, _FL));
							Failure->Generation = m->Generation;
							Failure->Fld.New() = Folder;
							Failure->Diagnostic = LString::Fmt("IMAP account %i snapshot '%s' rejected: %s",
								d->AccountId, Folder.Remote.Get(), Error.Get());
							PostStore(Failure.Release());
							continue;
						}
						d->CurrentFolder = Folder.Remote;
						d->Exists = (int)Response->Mail.Length();
						PostStore(Response.Release());
					}
					break;
				}
				case IMAP_SELECT_FOLDER:
				{
					if (d->Store->IsLive())
					{
						m->Type = IMAP_SYNC_FOLDER;
						PostThread(m.Release(), true);
						break;
					}
					if (m->Fld.Length() == 1)
					{
						ImapFolderInfo &Inf = m->Fld[0];
						MailIMap::StrMap Values;
						if (d->SelectFolder(Inf.Remote, &Values))
						{
							LString Recent = Values.Find("Recent");
							if (Recent && Recent.Int() > 0)
							{
								LString::Array Uids;
								if (d->Imap->Search(true, Uids, "recent"))
								{
									d->DownloadIds(GetListingParts(Inf.Remote), true, Uids);
								}
							}
						}
					}
					break;
				}
				case IMAP_FOLDER_LISTING:
				{
					// This gets us the UID/Flags and sequence numbers.
					for (unsigned i=0; i<m->Fld.Length(); i++)
					{
						ImapFolderInfo &Inf = m->Fld[i];

						LAssert(Inf.Remote);

						if (d->SelectFolder(Inf.Remote))
						{
							char UidRange[32];
							char *ListingParts = GetListingParts(Inf.Remote);

							LAutoPtr<LStreamI> Raw(d->NewFetch("All", ListingParts));
							LAutoPtr<ImapMsg> Resp(new ImapMsg(IMAP_FOLDER_LISTING, _FL));
							ImapFolderInfo &r = Resp->Fld.New();
							r.LastUid = Inf.LastUid;

                            sprintf_s(UidRange, sizeof(UidRange), "%i:*", Inf.LastUid > 0 ? Inf.LastUid + 1 : 1);
								
							#if !RUN_TEST_CMDS
							LError fetchErr;
							if (d->Imap->Fetch(	true,
												UidRange,
												ListingParts,
												ListingCallback,
												Resp,
												Raw,
												-1,
												&fetchErr))
							{
								r.Local = Inf.Local;
								r.Remote = Inf.Remote;
								Resp->Last = 1;
								PostStore(Resp.Release());
							}
							else
								d->Error(_FL, LString::Fmt("Fetch failed: %s", fetchErr.ToString().Get()));
							#endif
						}
					}
					break;
				}
				case IMAP_DOWNLOAD:
				{
					if (d->Store->IsLive())
					{
						d->CurrentFolder.Empty();
						for (auto &Request: m->Mail)
						{
							LAutoPtr<ImapMsg> Response(new ImapMsg(IMAP_DOWNLOAD, _FL));
							Response->Parent = m->Parent;
							auto &Body = Response->Mail.New();
							if (!FetchImapBody(*d->Imap, m->Parent, Request, Body, Response->Diagnostic))
							{
								LgiTrace("IMAP account %i closing connection %u: body verification failed for '%s', UID %u: %s\n",
									d->AccountId, Connection, m->Parent.Get(), Request.Uid, Response->Diagnostic.Get());
								d->Error(_FL, "%s", Response->Diagnostic.Get());
								if (d->Lock(_FL))
								{
									d->Imap.Reset();
									d->Unlock();
								}
								PostStore(Response.Release());
								break;
							}
							PostStore(Response.Release());
						}
						break;
					}
					LAssert(m->Parent);
					if (!d->SelectFolder(m->Parent))
						break;

					// Optionally collect the failures and report them back to the store thread
					LAutoPtr<ImapMsg> failed;

					for (auto &mi: m->Mail)
					{
						LAutoPtr<DownloadInfo> Inf(new DownloadInfo);
						if (Inf)
						{
							Inf->Uid    = mi.Uid;
							Inf->Local  = mi.Local;
							Inf->Parent = m->Parent;
							Inf->Thread = this;

							MailProtocolProgress *p = m->Mail.Length() == 1 ? d->Store->DataProgress : NULL;
							if (p && mi.Size > 0)
							{
								if (p)
									p->StartTransfer((int)mi.Size);
										
								if (mi.Size > (100 << 10) && d->Log)
									d->Log->Print("Fetching %s...\n", LFormatSize(mi.Size).Get());
							}
								
							LString s;
							s.Printf("%i", mi.Uid);
							LError Err;
							auto Status = d->Imap->Fetch(true, s, BodyTag, DownloadCallback, Inf, NULL, mi.Size, &Err);
							if (!Status)
							{
								// Ok the fetch failed because the object doesn't exist anymore?
								// We should delete it from the store right? Hmmmm...
								if (!failed)
								{
									if (failed.Reset(new ImapMsg(IMAP_ON_DEL, _FL)))
									{
										failed->Parent = Inf->Parent;
										failed->Error = Err;
									}
								}
								if (failed)
								{									
									auto &i = failed->Mail.New();
									i.Uid = mi.Uid;
								}
							}

							if (p)
								p->StartTransfer(0);
						}
					}
					
					if (failed)
						PostStore(failed.Release());
					break;
				}
				case IMAP_CREATE_FOLDER:
				{
					ImapMsg *Failed = NULL;
						
					for (unsigned i=0; i<m->Fld.Length(); i++)
					{
						ImapFolderInfo &Inf = m->Fld[i];
						if (Inf.Remote && Inf.Local)
						{
							MailImapFolder t;
							t.SetPath(Inf.Remote);
							if (!d->Imap->CreateFolder(&t))
							{
								// Create a failed response for the store...
								if (!Failed)
									Failed = new ImapMsg(IMAP_ON_DEL, _FL);
								if (Failed)
								{
									ImapFolderInfo &k = Failed->Fld.New();
									k.Local = Inf.Local;
									k.Remote = Inf.Remote;
								}
									
								// Remove the failed entry from the success msg
								m->Fld.DeleteAt(i--);
							}
						}
					}
						
					if (m->Fld.Length() > 0)
					{
						m->SetType(m->Type, _FL);
						PostStore(m.Release());
					}

					if (Failed)
						PostStore(Failed);
					break;
				}
				case IMAP_DELETE_FOLDER:
				{
					for (unsigned i=0; i<m->Fld.Length(); i++)
					{
						ImapFolderInfo &Inf = m->Fld[i];
						if (Inf.Remote && Inf.Local)
						{
							if (d->Imap->DeleteFolder(Inf.Remote))
							{
								ImapMsg *Fail = new ImapMsg(IMAP_ON_DEL, _FL);
								if (Fail)
								{
									ImapFolderInfo &k = Fail->Fld.New();
									k.Local = Inf.Local;
									k.Remote = Inf.Remote;
									d->Store->PostStore(Fail);
								}
							}
						}
					}
					break;
				}
				case IMAP_MOVE_EMAIL:
				{
					if (!m->NewRemote || !m->Parent)
					{
						d->Error(_FL, "Argument missing.");
						break;
					}

					// The store should've broken the deletes up into
					// blocks for us.
					LAssert(m->Mail.Length() <= IMAP_BLOCK_SIZE);

					if (!d->SelectFolder(m->Parent))
					{
						d->Error(_FL, "Imap select failed.");
						break;
					}

					LArray<uint32_t> Lst;
					for (unsigned i=0; i<m->Mail.Length(); i++)
						Lst.Add(m->Mail[i].Uid);

					if (!d->Imap->CopyByUid(Lst, m->NewRemote))
					{
						d->Error(_FL, "Imap copy failed.");
						break;
					}

					// Delete the source...
					if (!d->Imap->SetFlagsByUid(Lst, "\\Deleted"))
					{
						d->Error(_FL, "Imap DeleteByUid() failed.");
						break;
					}

					m->SetType(IMAP_ON_DEL, _FL);
					LString Remote = m->NewRemote;
					d->Store->PostStore(m.Release());

					MailIMap::StrMap NewFolderValues;
					auto OldFolder = d->CurrentFolder;
					if (d->SelectFolder(Remote, &NewFolderValues))
					{
						// Not sure of the UIDs so search them
						LString::Array Uids;
						if (d->Imap->Search(true, Uids, "RECENT"))
							d->DownloadIds(GetListingParts(d->CurrentFolder), true, Uids);
					}
					break;
				}
				case IMAP_DELETE:
				{
					if (d->SelectFolder(m->Parent))
					{
						LArray<uint32_t> Ids;

						// The store should've broken the deletes up into
						// blocks for us.
						LAssert(m->Mail.Length() <= IMAP_BLOCK_SIZE);

						for (unsigned i=0; i<m->Mail.Length(); i++)
							Ids.Add(m->Mail[i].Uid);

						LgiTrace("IMAP_DELETE....\n");
						if (d->Imap->SetFlagsByUid(Ids, "\\Deleted"))
						{
							LgiTrace("Sending IMAP_ON_DEL....\n");
							m->SetType(IMAP_ON_DEL, _FL);
							PostStore(m.Release());
						}
						else
						{
							LgiTrace("SetFlagsByUid failed....\n");
						}
					}
					break;
				}
				case IMAP_UNDELETE:
				{
					if (d->SelectFolder(m->Parent))
					{
						LArray<uint32_t> Ids;
						for (unsigned i=0; i<m->Mail.Length(); i++)
						{
							Ids.Add(m->Mail[i].Uid);
						}							

						if (d->Imap->SetFlagsByUid(Ids, "\\seen"))
						{
							// m->SetType(IMAP_ON_DEL, _FL);
							// PostStore(m.Release());
						}
					}
					break;
				}
				case IMAP_SET_FLAGS:
				{
					// Set the flags on message(s)
					if (m->Parent)
						d->SelectFolder(m->Parent);

					LHashTbl<IntKey<int>, LArray<uint32_t>*> Map(0, NULL);

					// Group all the messages into similar flags groups
					for (auto &mail: m->Mail)
					{
						auto map = Map.Find(mail.Flags.All);
						if (!map) Map.Add(mail.Flags.All, map = new LArray<uint32_t>);
						map->Add(mail.Uid);
					}

					for (auto &p: Map)
					{
						auto flags = p.key;
						auto arr = p.value;
							
						ImapMailFlags f;
						f.All = flags;
						f.ImapRecent = 0;
						auto Flags = f.Get();

						d->Imap->SetFlagsByUid(*arr, Flags);

						LAutoPtr<ImapMsg> resp(new ImapMsg(IMAP_SET_FLAGS, _FL));
						if (resp)
						{
							resp->Parent = m->Parent.Get();
							for (auto i: *arr)
							{
								auto &n = resp->Mail.New();
								n.Uid = i;
								n.Flags = f;
							}
							d->Store->PostStore(resp.Release());
						}
					}

					Map.DeleteObjects();
					break;
				}
				case IMAP_APPEND:
				{
					for (auto &inf: m->Mail)
					{
						auto Rfc822 = LReadFile(inf.Local);
						LString NewUid;
						if (d->Imap->Append(m->Parent, &inf.Flags, Rfc822, NewUid))
						{
							if (NewUid)
								inf.Uid = (uint32_t)NewUid.Int();
							else
							{
								// Do a specific search to find the UID....
								// (Although this is not tested and doesn't work on at least exchange...)
								char Key[256];
								LArray<LString> Uids;
								auto MessageId = LGetHeaderField(Rfc822, "Message-Id").Strip("<>");
								
								sprintf_s(Key, sizeof(Key), "HEADER Message-Id \"%s\"", MessageId.Get());
								if (d->Imap->Search(true, Uids, Key))
								{
								    if (Uids.Length() > 0)
    								    inf.Uid = (uint32_t)Uids[0].Int();
    								else
    								    LAssert(!"No uid?");
								}
								// else LAssert(!"No search results?");
							}
						}
					}

					m->SetType(m->Type, _FL);
					PostStore(m.Release());
					break;
				}
				case IMAP_EXPUNGE_FOLDER:
				{
					for (unsigned i=0; i<m->Fld.Length(); i++)
					{
						if (d->SelectFolder(m->Fld[i].Remote))
						{
							if (!d->Imap->ExpungeFolder())
							{
								m->Fld.DeleteAt(i);
								i--;
							}
						}
					}

					m->SetType(m->Type, _FL);
					PostStore(m.Release());
					break;
				}
				case IMAP_RENAME_FOLDER:
				{
					char *Old = m->Parent;
					if (!Old) break;
					if (*Old == d->Sep[0])
						Old++;
							
					char *New = m->NewRemote;
					if (!New) break;
					if (*New == d->Sep[0])
						New++;
							
					bool r = d->Imap->RenameFolder(Old, New);
					if (r)
					{
						m->SetType(m->Type, _FL);
						PostStore(m.Release());
					}
					break;
				}
			}
		}
		else
		{
			if (InIdle)
			{
				LString::Array Resp;
				if (d->Imap->OnIdle(200, Resp))
				{
					LAutoPtr<ImapMsg> FlagsUpdate;
					LAutoPtr<ImapMsg> ExpungeUpdate;

					for (unsigned i=0; i<Resp.Length(); i++)
					{
						auto &u = Resp[i];

						LArray<MailIMap::StrRange> Ranges;
						d->Imap->ParseImapResponse(u, u.Length(), Ranges, 2);
						if (Ranges.Length() < 2)
							continue;

						#define RngStr(idx, name) LString name; \
							{ auto &r = Ranges[idx]; name.Set(u.Get() + r.Start, r.Len()); }
						RngStr(0, Id);
						RngStr(1, Cmd);
						if (Cmd.Equals("RECENT"))
						{
							d->Imap->FinishIdle();

							LArray<LString> Numbers;
							if (d->Imap->Search(true, Numbers, "UNDELETED UNSEEN"))
							{
								LStringPipe n(64);
								for (unsigned i=0; i<Numbers.Length(); i++)
								{
									if (i) n.Print(",");
									n.Print("%s", Numbers[i].Get());
								}
								auto Range = n.NewLStr();
								LgiTrace("Recent.Range=%s\n", Range.Get());

								LAutoPtr<ImapMsg> New(new ImapMsg(IMAP_ON_NEW, _FL));
								if (d->Imap->Fetch(true, Range, GetListingParts(d->CurrentFolder), ListingCallback, New))
								{
									New->Parent = d->CurrentFolder.Get();
									New->New = true;
									PostStore(New.Release());
								}
    							else d->Error(_FL, "IDLE FETCH failed\n");
							}

							if ((InIdle = d->Imap->StartIdle()))
								IdleStart = LCurrentTime();
						}
						else if (Cmd.Equals("EXISTS"))
						{
							int NewExists = (int)Id.Int();
							Log(LSocketI::SocketMsgInfo, "Got EXISTS: %i->%i\n", d->Exists, NewExists);
									
							if (d->Exists >= 0 &&
								NewExists > d->Exists)
							{
								d->Imap->FinishIdle();

								char Range[32];
								if (NewExists == d->Exists + 1)
									sprintf_s(Range, sizeof(Range), "%i", NewExists);
								else
									sprintf_s(Range, sizeof(Range), "%i:%i", d->Exists + 1, NewExists);
										
								LAutoPtr<ImapMsg> Update(new ImapMsg(IMAP_SET_FLAGS, _FL));
								auto ListingParts = GetListingParts(d->CurrentFolder);
								int b = d->Imap->Fetch(false, Range, ListingParts, ListingCallback, Update);
								if (b)
								{
									Update->Parent = d->CurrentFolder.Get();
									PostStore(Update.Release());										
								}
								else d->Error(_FL, "IDLE FETCH failed\n");

								if ((InIdle = d->Imap->StartIdle()))
									IdleStart = LCurrentTime();
							}
							else Log(LSocketI::SocketMsgInfo, "Exists no action: %i, %i\n", d->Exists, NewExists);

							d->Exists = NewExists;
						}
						else if (Cmd.Equals("FETCH"))
						{
							uint32_t Uid = 0;
							for (int i = 2; i < Ranges.Length() - 1; i += 2)
							{
								RngStr(i, Fld);
								RngStr(i+1, Val);
									
								if (Fld.Equals("UID"))
								{
									Uid = (uint32_t)Val.Int();
								}
								else if (Fld.Equals("FLAGS"))
								{
									if (FlagsUpdate || FlagsUpdate.Reset(new ImapMsg(IMAP_SET_FLAGS, _FL)))
									{
										if (!FlagsUpdate->Parent)
											FlagsUpdate->Parent = d->CurrentFolder.Get();

										auto Seq = (int)Id.Int();
										auto &m = FlagsUpdate->Mail.New();
										m.Seq = Seq;
										m.Uid = Uid;
										m.Flags.Set(Val);

										if (d->Exists >= 0 && d->Exists < Seq)
										{
											// Update the exists...
											LgiTrace("%s:%i - Updating exists %i->%i\n", _FL, d->Exists, Seq);
											d->Exists = Seq;
										}
									}
								}
								else
								{
									LgiTrace("%s:%i - Unknown FETCH field: %s\n", _FL, Fld.Get());
								}
							}
						}
 						else if (Cmd.Equals("EXPUNGE"))
						{
							if (ExpungeUpdate || ExpungeUpdate.Reset(new ImapMsg(IMAP_EXPUNGE_FOLDER, _FL)))
							{
								if (!ExpungeUpdate->Parent)
									ExpungeUpdate->Parent = d->CurrentFolder.Get();

								ImapMailInfo &Mi = ExpungeUpdate->Mail.New();
								Mi.Seq = (uint32_t)Id.Int();
							}
						}
						else if (Id.Equals("OK"))
						{
							// Fluff...
						}
						else
						{
							d->Error(_FL, "Unexpected IDLE message: %s", Cmd.Get());
						}
					}

					if (FlagsUpdate)
					{
						LString::Array Seqs;
						Seqs.SetFixedLength(false);
						for (auto &m: FlagsUpdate->Mail)
						{
							if (!m.Uid)
								Seqs.New().Printf("%i", m.Seq);
						}
						if (Seqs.Length())
						{
							LString Val = LString(",").Join(Seqs);

							// At this point we have some message sequence numbers. But we need the UIDs.
							d->Imap->FinishIdle();
							d->Imap->Fetch(false, Val, "UID", UidCb, FlagsUpdate.Get());
							if ((InIdle = d->Imap->StartIdle()))
								IdleStart = LCurrentTime();
						}

						// Now send the message
						PostStore(FlagsUpdate.Release());
					}

					if (ExpungeUpdate)
					{
						PostStore(ExpungeUpdate.Release());
					}
				}

				if (LCurrentTime() > IdleStart + IDLE_MAX)
				{
					d->Imap->FinishIdle();
					if ((InIdle = d->Imap->StartIdle()))
						IdleStart = LCurrentTime();
				}
			}
			else if (IdleCount > 5000 / 50)
			{
				#if RUN_TEST_CMDS

					if (CurTestCmd < CountOf(TestCmds))
					{
						auto parts = LString(TestCmds[CurTestCmd++]).SplitDelimit();
						if (parts[0] == "COPY")
						{
							LAuto	
							
							Mv->Parent = d->CurrentFolder;
							auto &m = Mv->Mail.New();
							m.Uid = (uint32_t) parts[1].Int();
							Mv->NewRemote = parts[2];

							PostThread(Mv.Release(), false);
						}
						else if (parts[0] == "SELECT")
						{
							LAutoPtr<ImapMsg> Sel(new ImapMsg(IMAP_SELECT_FOLDER, _FL));
							Sel->Fld[0].Remote = parts[1];
							PostThread(Sel.Release(), false);
						}
						else if (parts[0] == "IDLE")
						{
							InIdle = d->Imap->StartIdle();
							CurTestCmd++;
						}
						else LAssert(!"Unknown cmd");
					}

				#else

					if (!d->CurrentFolder.Equals(d->InboxPath))
						d->SelectFolder(d->InboxPath);

					if ((InIdle = d->Imap->StartIdle()))
					{
						IdleStart = LCurrentTime();
					}
					else
					{
						// This can happen when the connection dies... so closing
						// it will allow proper cleanup and re-connection.
						LgiTrace("IMAP account %i closing connection %u: cannot start IDLE for '%s'.\n",
							d->AccountId, Connection, d->CurrentFolder.Get());
						if (d->Lock(_FL))
						{
							d->Imap->Close();
							d->Unlock();
						}
					}

				#endif
			}
			else if (IdleCount >= 0)
			{
				IdleCount++;
				LSleep(50);
			}
		}
			
		if (d->Store->ItemProgress)
			d->Store->ItemProgress->Value++;
	}

	if (d->Store->ItemProgress)
		d->Store->ItemProgress->Empty();
	if (d->Store->DataProgress)
		d->Store->DataProgress->Empty();

	return 0;
}
