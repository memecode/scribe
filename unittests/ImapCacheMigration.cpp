#include "Store3Mail3/Mail3.h"
#include "lgi/common/Mail.h"
#include "Store3Imap/ScribeImap.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern void InitStrToDom();
extern void FreeStrToDom();

namespace
{
	void Require(bool Condition, const char *Message)
	{
		if (!Condition)
			throw std::runtime_error(Message ? Message : "Fixture check failed");
	}

	struct Fixture
	{
		std::filesystem::path Root;

		Fixture()
		{
			char Path[] = "/tmp/scribe-imap-import-XXXXXX";
			auto Created = mkdtemp(Path);
			Require(Created != NULL, "Cannot create the temporary fixture");
			Root = Created;
		}

		~Fixture()
		{
			std::filesystem::remove_all(Root);
		}

		void Write(const std::filesystem::path &Path, const std::string &Content)
		{
			std::filesystem::create_directories(Path.parent_path());
			std::ofstream Output(Path, std::ios::binary);
			Output.write(Content.data(), Content.size());
			Require(Output.good(), "Cannot write fixture");
		}
	};

	int64 Scalar(LMail3Store &Store, const char *Sql)
	{
		LMail3Store::LStatement Query(&Store, Sql);
		Require(Query.IsOk() && Query.Row(), "Scalar query failed");
		return Query.GetInt64(0);
	}

	std::string Export(LMail3Store &Store, int64 MailId)
	{
		LMail3Mail Mail(&Store);
		{
			LMail3Store::LStatement Query(&Store, "select * from Mail where Id=?");
			Require(Query.SetInt64(0, MailId) && Query.Row() && Mail.Serialize(Query, false),
				"Cannot load imported mail");
		}
		auto Stream = Mail.GetStream(_FL);
		Require(Stream.Get() != NULL, "Cannot export imported mail");
		std::string Result((size_t)Stream->GetSize(), '\0');
		Stream->SetPos(0);
		for (size_t Offset = 0; Offset < Result.size(); )
		{
			auto Read = Stream->Read(Result.data() + Offset, Result.size() - Offset);
			Require(Read > 0, "Cannot read imported RFC822");
			Offset += Read;
		}
		return Result;
	}

	void TestCutoverPersistence()
	{
		Fixture Files;
		auto Database = Files.Root / "cutover.mail3";
		std::string Raw = "From: sender@example.test\r\nTo: receiver@example.test\r\n"
			"Subject: Cutover fixture\r\nMessage-ID: <cutover@example.test>\r\n"
			"Content-Type: application/octet-stream\r\n\r\noriginal";
		Raw.append("\0binary\xff", 8);
		LMail3ImapMailbox Inbox, Archive;
		Inbox.AccountId = Archive.AccountId = 42;
		Inbox.RemoteName = "/INBOX";
		Archive.RemoteName = "/Archive";
		Inbox.UidValidity = 100;
		Archive.UidValidity = UINT32_MAX;
		Inbox.UidNext = UINT32_MAX;
		Archive.UidNext = 100;
		LMail3ImapMessage Identity;
		int64 InterruptedId = -1, QueuedId = -1, StagedId = -1;
		{
			LMail3Store Store(Database.c_str(), NULL, true);
			Require(Store.GetRoot() != NULL, "Cannot create the cutover fixture root");
			LArray<LMail3ImapServerMessage> Messages, Empty;
			auto &Server = Messages.New();
			Server.Uid = UINT32_MAX - 1;
			Server.Headers = Raw.substr(0, Raw.find("\r\n\r\n")).c_str();
			Server.Size = Raw.size();
			Require(Store.RebuildImapMailbox(Inbox, Messages) && Store.RebuildImapMailbox(Archive, Empty),
				"Cannot create verified cutover mailboxes");
			Require(Store.FindImapMessage(Inbox.Id, 100, Server.Uid, Identity),
				"Cannot find the canonical cutover identity");
			Require(!Store.OpenImapBody(42, Identity), "Undownloaded mail pretended to contain a body");
			LMemStream Body(Raw.data(), Raw.size());
			Require(Store.StoreImapBody(42, Identity, Body), "Verified SQLite body download failed");
			Require(Export(Store, Identity.MailId) == Raw &&
				Scalar(Store, "select count(*) from Mail where ImapDownloaded=1") == 1,
				"SQLite body storage changed RFC822 bytes or lost download state");
			{
				auto Stored = Store.OpenImapBody(42, Identity);
				Require(Stored && Stored->GetSize() == (int64)Raw.size(),
					"Canonical body stream did not survive its detached mail object's lifetime");
				char First[4];
				Require(Stored->Read(First, sizeof(First)) == sizeof(First) && !memcmp(First, "From", 4),
					"Canonical body stream is not readable");
			}
			auto Stale = Identity;
			Stale.UidValidity++;
			Require(!Store.StoreImapBody(42, Stale, Body) && !Store.OpenImapBody(43, Identity),
				"Body access accepted a stale epoch or another account");
			auto Wrong = Raw;
			Wrong.replace(Wrong.find("<cutover@"), 8, "<changed");
			LMemStream WrongBody(Wrong.data(), Wrong.size());
			Require(!Store.StoreImapBody(42, Identity, WrongBody) && Export(Store, Identity.MailId) == Raw,
				"A mismatched downloaded Message-ID replaced existing content");
			LMemStream ShortBody(Raw.data(), Raw.size() - 1);
			Require(!Store.StoreImapBody(42, Identity, ShortBody), "A truncated RFC822 download was accepted");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailDownloaded before update of ImapDownloaded on Mail when new.ImapDownloaded=1 "
				"begin select raise(abort,'fixture download completion failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install the body completion rollback trigger");
			auto Replacement = Raw;
			Replacement.back() = 'x';
			LMemStream ReplacementBody(Replacement.data(), Replacement.size());
			Require(!Store.StoreImapBody(42, Identity, ReplacementBody) && Export(Store, Identity.MailId) == Raw,
				"A late download failure damaged the prior body");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailDownloaded", NULL, NULL, NULL), NULL),
				"Cannot remove the body completion rollback trigger");

			LMail3ImapOperation Flags;
			Flags.AccountId = 42;
			Flags.MailId = Identity.MailId;
			ImapMailFlags Desired;
			Desired.ImapSeen = true;
			Desired.ImapAnswered = true;
			Flags.RemoteFlags = Desired.All;
			auto WrongAccount = Flags;
			WrongAccount.AccountId = 43;
			Require(!Store.QueueImapOperation(WrongAccount) && WrongAccount.Id == -1,
				"A queued command acquired another account's mail");
			Require(Store.QueueImapOperation(Flags) && Flags.Id > 0 &&
				Flags.SourceMailboxId == Inbox.Id && Flags.SourceUidValidity == 100 &&
				Flags.SourceUid == UINT32_MAX - 1 && Flags.SourceRemoteName == "/INBOX",
				"Enqueue did not freeze the canonical source identity");
			Require(Scalar(Store, "select count(*) from Mail where ImapRemoteFlags=0") == 1,
				"Enqueue changed canonical flags before server confirmation");
			auto Duplicate = Flags;
			Duplicate.Id = -1;
			Require(!Store.QueueImapOperation(Duplicate) && Duplicate.Id == -1,
				"Two commands acquired the same canonical message");
			LMail3ImapOperation Claimed;
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"update Folder set ImapUidValidity=101 where ImapRemoteName='/INBOX'",
				NULL, NULL, NULL), NULL), "Cannot change the fixture mailbox epoch");
			Require(!Store.BeginImapOperation(Flags.Id, Claimed) &&
				Scalar(Store, "select count(*) from ImapOperation where State=0 and Attempts=0") == 1,
				"A queued UID was dispatched after its mailbox epoch changed");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"update Folder set ImapUidValidity=100 where ImapRemoteName='/INBOX'",
				NULL, NULL, NULL), NULL), "Cannot restore the fixture mailbox epoch");
			Require(Store.BeginImapOperation(Flags.Id, Claimed) && Claimed.Attempts == 1 &&
				Claimed.State == LMail3ImapOperationState::Executing, "Cannot durably claim a queued command");
			Require(!Store.BeginImapOperation(Flags.Id, Claimed), "An executing command was dispatched twice");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailJournalRetire before delete on ImapOperation "
				"begin select raise(abort,'fixture journal retirement failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install the journal retirement rollback trigger");
			Require(!Store.CompleteImapOperation(Flags.Id) &&
				Scalar(Store, "select count(*) from Mail where ImapRemoteFlags=0") == 1 &&
				Scalar(Store, "select count(*) from ImapOperation where State=1") == 1,
				"A late journal failure partially committed canonical flags");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailJournalRetire", NULL, NULL, NULL), NULL),
				"Cannot remove the journal retirement rollback trigger");
			Require(Store.CompleteImapOperation(Flags.Id) && !Store.CompleteImapOperation(Flags.Id),
				"Confirmed flag update failed or acknowledged twice");
			Require(Scalar(Store, LString::Fmt("select count(*) from Mail where (Flags & %i)=%i and ImapRemoteFlags<>0",
				MAIL_READ | MAIL_REPLIED, MAIL_READ | MAIL_REPLIED)) == 1,
				"Server-confirmed flags were not applied to canonical metadata");

			LMail3ImapOperation Move;
			Move.AccountId = 42;
			Move.Type = LMail3ImapOperationType::Move;
			Move.MailId = Identity.MailId;
			Move.DestinationMailboxId = Archive.Id;
			Require(Store.QueueImapOperation(Move) && Store.BeginImapOperation(Move.Id, Claimed),
				"Cannot queue and claim a move");
			Require(!Store.CompleteImapOperation(Move.Id) &&
				!Store.CompleteImapOperation(Move.Id, 99, 77) &&
				Store.LoadImapMessage(Identity.MailId, Identity) && Identity.MailboxId == Inbox.Id,
				"A move committed without a verified destination identity");
			LMail3Mail Collision(&Store);
			Collision.ParentId = Archive.Id;
			Collision.AccountId = 42;
			Collision.ImapUidValidity = UINT32_MAX;
			Collision.ImapUid = 77;
			Require(Collision.Write(MAIL3_TBL_MAIL, true), "Cannot create a destination UID collision fixture");
			Require(!Store.CompleteImapOperation(Move.Id, UINT32_MAX, 77) &&
				Store.LoadImapMessage(Identity.MailId, Identity) && Identity.MailboxId == Inbox.Id &&
				Scalar(Store, "select count(*) from ImapOperation where State=1") == 1,
				"A conflicting destination UID partially committed or retired a move");
			LMail3Store::LStatement RemoveCollision(&Store, "delete from Mail where Id=?");
			Require(RemoveCollision.SetInt64(0, Collision.Id) && RemoveCollision.Exec(),
				"Cannot remove the destination UID collision fixture");
			Require(Store.CompleteImapOperation(Move.Id, UINT32_MAX, 77) &&
				Store.LoadImapMessage(Identity.MailId, Identity) && Identity.MailboxId == Archive.Id &&
				Identity.UidValidity == UINT32_MAX && Identity.Uid == 77 && Export(Store, Identity.MailId) == Raw,
				"Confirmed move lost the canonical ID, body or destination identity");
			LMail3ImapOperation Delete;
			Delete.AccountId = 42;
			Delete.MailId = Identity.MailId;
			Delete.Type = LMail3ImapOperationType::Delete;
			Require(Store.QueueImapOperation(Delete) && Store.BeginImapOperation(Delete.Id, Claimed) &&
				Store.CompleteImapOperation(Delete.Id), "Confirmed delete failed");
			Require(Store.LoadImapMessage(Identity.MailId, Identity), "Delete discarded the unexpunged server identity");
			ImapMailFlags Deleted;
			Deleted.All = Identity.RemoteFlags;
			Require(Deleted.ImapDeleted && Export(Store, Identity.MailId) == Raw,
				"Delete must retain content until confirmed expunge/reconciliation");

			LMail3ImapOperation Append;
			auto MailCount = Scalar(Store, "select count(*) from Mail");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailAppendJournal before insert on ImapOperation when new.OperationType=4 "
				"begin select raise(abort,'fixture APPEND staging failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install the APPEND staging rollback trigger");
			Require(!Store.StageImapAppend(42, Archive.Id, 0, Body, Append) && Append.Id == -1 &&
				Scalar(Store, "select count(*) from Mail") == MailCount &&
				Scalar(Store, "select count(*) from MailSegs") == 1,
				"Failed APPEND staging left an orphan body or journal entry");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailAppendJournal", NULL, NULL, NULL), NULL),
				"Cannot remove the APPEND staging rollback trigger");
			Require(!Store.StageImapAppend(43, Archive.Id, 0, Body, Append) &&
				Scalar(Store, "select count(*) from Mail") == MailCount,
				"APPEND staging crossed accounts or failed to roll back its body");
			Require(Store.StageImapAppend(42, Archive.Id, 0, Body, Append) &&
				Export(Store, Append.MailId) == Raw &&
				Scalar(Store, "select count(*) from Mail where ImapUid is null and ImapDownloaded=1") == 1,
				"APPEND was not durably staged without a speculative server UID");
			Require(Store.BeginImapOperation(Append.Id, Claimed) &&
				Store.CompleteImapOperation(Append.Id, UINT32_MAX, 78) &&
				Export(Store, Append.MailId) == Raw, "Confirmed APPEND did not preserve the staged body");

			LMail3ImapOperation First;
			First.AccountId = 42;
			First.MailId = Append.MailId;
			Require(Store.QueueImapOperation(First), "Cannot queue the interrupted-operation fixture");
			InterruptedId = First.Id;
			Require(Store.StageImapAppend(42, Archive.Id, 0, Body, Append), "Cannot stage the restart APPEND fixture");
			QueuedId = Append.Id;
			StagedId = Append.MailId;
			Require(!Store.BeginImapOperation(QueuedId, Claimed), "Account command ordering was not enforced");
			Require(Store.BeginImapOperation(InterruptedId, Claimed), "Cannot dispatch the first account command");
			LMail3ImapMailbox Other;
			Other.AccountId = 43;
			Other.RemoteName = "/INBOX";
			Other.UidValidity = 123;
			Other.UidNext = 10;
			Require(Store.RebuildImapMailbox(Other, Empty) &&
				Store.StageImapAppend(43, Other.Id, 0, Body, Append) &&
				Store.BeginImapOperation(Append.Id, Claimed), "Independent account work was unnecessarily blocked");
			Require(Store.InterruptImapOperation(Append.Id, "Connection lost after APPEND") &&
				!Store.InterruptImapOperation(Append.Id, "A second interruption"),
				"Interrupted APPEND did not enter a stable reconciliation state");
			LArray<LMail3ImapOperation> OtherOperations;
			Require(Store.LoadImapOperations(43, OtherOperations) && OtherOperations.Length() == 1 &&
				OtherOperations[0].State == LMail3ImapOperationState::NeedsReconciliation &&
				OtherOperations[0].LastError == "Connection lost after APPEND" &&
				Export(Store, Append.MailId) == Raw,
				"Interrupted APPEND lost the diagnostic or recoverable upload body");
			auto Transaction = Store.StartTransaction();
			Require(!Store.RecoverImapOperations(42), "Journal recovery accepted an uncommitted outer transaction");
		}
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success, "Cannot reopen cutover persistence fixture");
			LArray<LMail3ImapOperation> Operations;
			Require(Store.LoadImapOperations(42, Operations) && Operations.Length() == 2 &&
				Operations[0].State == LMail3ImapOperationState::Executing &&
				Operations[1].State == LMail3ImapOperationState::Queued,
				"Journal claims and queued APPENDs were not durable across restart");
			Require(Store.RecoverImapOperations(43) && Store.LoadImapOperations(42, Operations) &&
				Operations[0].State == LMail3ImapOperationState::Executing,
				"Journal recovery changed another account");
			Require(Store.RecoverImapOperations(42) && Store.RecoverImapOperations(42) &&
				Store.LoadImapOperations(42, Operations) &&
				Operations[0].State == LMail3ImapOperationState::NeedsReconciliation &&
				Operations[0].LastError && Operations[0].Attempts == 1 &&
				Operations[1].State == LMail3ImapOperationState::Queued && Operations[1].Attempts == 0,
				"Restart must retain queued work and quarantine uncertain server outcomes");
			LMail3ImapOperation Claimed;
			Require(!Store.BeginImapOperation(InterruptedId, Claimed) &&
				!Store.BeginImapOperation(QueuedId, Claimed) &&
				!Store.CompleteImapOperation(InterruptedId),
				"An uncertain command was retried or bypassed without reconciliation");
			Require(Export(Store, StagedId) == Raw, "A queued APPEND lost its original bytes on restart");
			LArray<LMail3ImapServerMessage> Empty;
			Require(!Store.RebuildImapMailbox(Archive, Empty),
				"A snapshot bypassed outstanding journal work");
		}
	}

	enum class JournalScript
	{
		Normal,
		WrongSourceEpoch,
		WrongDestinationEpoch,
		MissingSource,
		MissingCopyUid,
		WrongCopySource,
		WrongCopyEpoch,
		LostCopyReply,
		LostStoreReply,
		StoreRejected,
		SourceDeleteFailed,
		MissingAppendUid,
		LostAppendReply,
		AppendRejected,
		AppendBadUid,
		CopyUidSet,
		WrongFlags,
		EpochChangedAfterStore,
		SourceFlagsChanged,
		CopyFlagsChanged,
		MissingDestination,
		LostFetchReply,
		DuplicateFetch,
		FetchWithoutFlags,
		EpochChangedDuringReconciliation
	};

	class JournalSocket : public LSocket
	{
		std::string Response, Selected, AppendTag, Literal, Terminator;
		size_t Offset = 0, LiteralSize = 0;
		bool InLiteral = false, Open = true;
		unsigned SourceSelects = 0, DestinationSelects = 0;
		ImapMailFlags SourceFlags, DestinationFlags;
		JournalScript Mode;

		void Reply(std::string Text)
		{
			Response = std::move(Text);
			Offset = 0;
		}

	public:
		unsigned Copies = 0, Stores = 0, Appends = 0, Selects = 0;
		std::string Appended;
		std::vector<std::string> Commands;
		JournalSocket(JournalScript mode) : Mode(mode)
		{
			SourceFlags.ImapFlagged = mode == JournalScript::SourceFlagsChanged;
		}
		void SeedFlags(int Source, int Destination)
		{
			SourceFlags.All = Source;
			DestinationFlags.All = Destination;
		}
		bool IsOpen() override { return Open; }
		int Close() override { Open = false; return 1; }
		ssize_t Write(const void *Data, ssize_t Size, int Flags = 0) override
		{
			if (!Open)
				return -1;
			std::string Bytes((const char*)Data, Size);
			if (InLiteral)
			{
				// Force short writes in the literal and its command terminator.
				auto Count = std::max<size_t>(1, Bytes.size() / 2);
				for (size_t Index = 0; Index < Count; Index++)
				{
					if (Literal.size() < LiteralSize)
						Literal += Bytes[Index];
					else
						Terminator += Bytes[Index];
				}
				if (Terminator.size() == 2)
				{
					Require(Terminator == "\r\n", "APPEND did not terminate its literal correctly");
					InLiteral = false;
					Appended = Literal;
					std::string Code = Mode == JournalScript::MissingAppendUid ? "" :
						Mode == JournalScript::AppendBadUid ? "[APPENDUID 4294967295 4294967296] " :
						"[APPENDUID 4294967295 4294967295] ";
					auto Unsolicited = Mode == JournalScript::MissingAppendUid ?
						"* OK [APPENDUID 4294967295 4294967295] unrelated\r\n" : "";
					Reply(Mode == JournalScript::LostAppendReply ? "" :
						Unsolicited + AppendTag + " OK " + Code + "done\r\n");
				}
				return Count;
			}
			Commands.push_back(Bytes);
			auto Tag = Bytes.substr(0, Bytes.find(' '));
			if (Bytes.find(" SELECT ") != std::string::npos)
			{
				Selects++;
				Selected = Bytes.find("INBOX") != std::string::npos ? "INBOX" : "Archive";
				auto MailboxSelects = Selected == "INBOX" ? ++SourceSelects : ++DestinationSelects;
				bool EpochChanged = Mode == JournalScript::EpochChangedDuringReconciliation && MailboxSelects > 1;
				auto Epoch = Selected == "INBOX" ?
					(Mode == JournalScript::WrongSourceEpoch || EpochChanged ||
					 (Mode == JournalScript::EpochChangedAfterStore && Stores) ? "101" : "100") :
					(Mode == JournalScript::WrongDestinationEpoch || EpochChanged ? "200" : "4294967295");
				Reply("* 1 EXISTS\r\n* 0 RECENT\r\n* OK [UIDVALIDITY " + std::string(Epoch) +
					"] epoch\r\n* OK [UIDNEXT 4294967295] next\r\n" + Tag + " OK [READ-WRITE] selected\r\n");
			}
			else if (Bytes.find(" UID FETCH ") != std::string::npos)
			{
				auto Uid = Selected == "INBOX" ? "4294967294" : "4294967295";
				auto Current = Selected == "INBOX" ? SourceFlags : DestinationFlags;
				auto FlagsText = Current.Get();
				auto Listing = (Selected == "INBOX" && Mode == JournalScript::MissingSource) ||
					(Selected == "Archive" && Mode == JournalScript::MissingDestination) ? "" :
					"* 1 FETCH (UID " + std::string(Uid) +
						(Mode == JournalScript::FetchWithoutFlags ? ")\r\n" : " FLAGS (" +
						std::string(FlagsText ? FlagsText.Get() : "") + "))\r\n");
				if (Mode == JournalScript::DuplicateFetch)
					Listing += Listing;
				Reply(Mode == JournalScript::LostFetchReply ? "" : Listing + Tag + " OK fetched\r\n");
			}
			else if (Bytes.find(" UID COPY ") != std::string::npos)
			{
				Require(Bytes.find("UID COPY 4294967294 ") != std::string::npos,
					"COPY truncated an unsigned source UID");
				Copies++;
				DestinationFlags.ImapFlagged = Mode == JournalScript::CopyFlagsChanged;
				std::string Code = Mode == JournalScript::MissingCopyUid ? "" :
					Mode == JournalScript::WrongCopySource ? "[COPYUID 4294967295 20 4294967295] " :
					Mode == JournalScript::WrongCopyEpoch ? "[COPYUID 200 4294967294 4294967295] " :
					Mode == JournalScript::CopyUidSet ? "[COPYUID 4294967295 4294967294:4294967295 1:2] " :
					"[COPYUID 4294967295 4294967294 4294967295] ";
				auto Unsolicited = Mode == JournalScript::MissingCopyUid ?
					"* OK [COPYUID 4294967295 4294967294 4294967295] unrelated\r\n" : "";
				Reply(Mode == JournalScript::LostCopyReply ? "" : Unsolicited + Tag + " OK " + Code + "copied\r\n");
			}
			else if (Bytes.find(" UID STORE ") != std::string::npos)
			{
				Require(Bytes.find("UID STORE 4294967294 ") != std::string::npos,
					"STORE truncated an unsigned source UID");
				Stores++;
				if (Mode != JournalScript::WrongFlags)
				{
					auto Start = Bytes.find('('), End = Bytes.find(')', Start);
					Require(Start != std::string::npos && End != std::string::npos, "Malformed STORE command");
					SourceFlags.Set(Bytes.substr(Start + 1, End - Start - 1).c_str());
				}
				Reply(Mode == JournalScript::LostStoreReply ? "" :
					Tag + ((Mode == JournalScript::StoreRejected || Mode == JournalScript::SourceDeleteFailed) ?
						" NO cannot store\r\n" : " OK stored\r\n"));
			}
			else if (Bytes.find(" APPEND ") != std::string::npos)
			{
				Appends++;
				if (Mode == JournalScript::AppendRejected)
					Reply(Tag + " NO cannot append\r\n");
				else
				{
					auto Start = Bytes.find('{'), End = Bytes.find('}', Start);
					Require(Start != std::string::npos && End != std::string::npos, "Malformed APPEND literal");
					LiteralSize = std::stoull(Bytes.substr(Start + 1, End - Start - 1));
					DestinationFlags.ImapSeen = Bytes.find("\\seen") != std::string::npos;
					AppendTag = Tag;
					InLiteral = true;
					Reply("+ send literal\r\n");
				}
			}
			else
				throw std::runtime_error("Unexpected command in the scripted journal dispatcher");
			return Size;
		}
		ssize_t Read(void *Data, ssize_t Size, int Flags = 0) override
		{
			auto Count = std::min<size_t>(Size, Response.size() - Offset);
			if (!Count && Mode == JournalScript::LostFetchReply)
			{
				Open = false;
				return -1;
			}
			memcpy(Data, Response.data() + Offset, Count);
			Offset += Count;
			return Count;
		}
	};

	class JournalImap : public MailIMap
	{
	public:
		JournalSocket *Script;
		JournalImap(JournalScript Mode)
		{
			Script = new JournalSocket(Mode);
			Socket.Reset(Script);
		}
	};

	void TestJournalProtocol()
	{
		std::string Raw = "From: sender@example.test\r\nMessage-ID: <journal>\r\n\r\nraw\nbody\rbytes";
		Raw.append("\0binary\xff", 8);
		for (auto Type: {LMail3ImapOperationType::SetFlags, LMail3ImapOperationType::Delete,
			LMail3ImapOperationType::Move, LMail3ImapOperationType::Append})
		{
			for (auto Mode: {JournalScript::Normal, JournalScript::WrongSourceEpoch,
				JournalScript::WrongDestinationEpoch, JournalScript::MissingSource, JournalScript::MissingCopyUid,
				JournalScript::WrongCopySource, JournalScript::WrongCopyEpoch, JournalScript::LostCopyReply,
				JournalScript::LostStoreReply, JournalScript::StoreRejected, JournalScript::SourceDeleteFailed,
				JournalScript::MissingAppendUid, JournalScript::LostAppendReply, JournalScript::AppendRejected,
				JournalScript::AppendBadUid, JournalScript::CopyUidSet, JournalScript::WrongFlags,
				JournalScript::EpochChangedAfterStore, JournalScript::SourceFlagsChanged,
				JournalScript::CopyFlagsChanged})
			{
				ImapJournalCommand Command;
				auto &Operation = Command.Operation;
				Operation.Id = 10;
				Operation.AccountId = 42;
				Operation.State = LMail3ImapOperationState::Executing;
				Operation.Type = Type;
				Operation.SourceRemoteName = "/INBOX";
				Operation.SourceUidValidity = 100;
				Operation.SourceUid = UINT32_MAX - 1;
				Operation.DestinationRemoteName = "/Archive";
				Operation.DestinationUidValidity = UINT32_MAX;
				Operation.RemoteFlags = Type == LMail3ImapOperationType::Move ? 0 : 32;
				if (Type == LMail3ImapOperationType::Delete)
					Operation.RemoteFlags |= 2;
				Command.Upload.Reset(new LMemStream(Raw.data(), Raw.size()));
				JournalImap Imap(Mode);
				auto Result = ExecuteImapJournalOperation(Imap, Command);
				bool Append = Type == LMail3ImapOperationType::Append;
				bool Move = Type == LMail3ImapOperationType::Move;
				bool BeforeFailure = (!Append && (Mode == JournalScript::WrongSourceEpoch || Mode == JournalScript::MissingSource)) ||
					((Append || Move) && Mode == JournalScript::WrongDestinationEpoch) ||
					(Move && Mode == JournalScript::SourceFlagsChanged);
				bool CopyFailure = Move && (Mode == JournalScript::MissingCopyUid ||
					Mode == JournalScript::WrongCopySource || Mode == JournalScript::WrongCopyEpoch ||
					Mode == JournalScript::LostCopyReply || Mode == JournalScript::CopyUidSet);
				bool StoreFailure = !Append && (Mode == JournalScript::LostStoreReply ||
					Mode == JournalScript::StoreRejected || Mode == JournalScript::SourceDeleteFailed ||
					Mode == JournalScript::WrongFlags || Mode == JournalScript::EpochChangedAfterStore);
				bool AppendFailure = Append && (Mode == JournalScript::MissingAppendUid ||
					Mode == JournalScript::LostAppendReply || Mode == JournalScript::AppendRejected ||
					Mode == JournalScript::AppendBadUid);
				bool CopyFlagsFailure = Move && Mode == JournalScript::CopyFlagsChanged;
				auto Expected = BeforeFailure ? ImapJournalOutcome::NotExecuted :
					CopyFailure || StoreFailure || AppendFailure || CopyFlagsFailure ? ImapJournalOutcome::Uncertain :
					ImapJournalOutcome::Confirmed;
				Require(Result.Outcome == Expected, "Journal dispatcher misclassified a server outcome");
				Require(Result.OperationId == 10 && Result.AccountId == 42, "Dispatcher lost journal correlation");
				Require((Expected == ImapJournalOutcome::Confirmed) != (bool)Result.Error,
					"Journal result contains a missing/spurious diagnostic");
				if (BeforeFailure)
					Require(!Imap.Script->Copies && !Imap.Script->Stores && !Imap.Script->Appends,
						"A preflight identity failure sent a mutation");
				if (CopyFailure)
					Require(Imap.Script->Copies == 1 && Imap.Script->Stores == 0 &&
						Result.DestinationUid == -1, "Unverified COPY reply deleted the source or fabricated evidence");
				if (Move && StoreFailure)
					Require(Result.DestinationUidValidity == UINT32_MAX && Result.DestinationUid == UINT32_MAX,
						"A partial move discarded confirmed COPYUID evidence");
				if (CopyFlagsFailure)
					Require(Imap.Script->Stores == 0 && Result.DestinationUid == UINT32_MAX,
						"Unexpected COPY flags caused premature source deletion or lost COPYUID evidence");
				if (Append && !BeforeFailure && Mode != JournalScript::AppendRejected)
					Require(Imap.Script->Appended == Raw, "APPEND changed RFC822 bytes or lost partial literal writes");
			}
		}
		ImapJournalCommand Invalid;
		Invalid.Operation.Id = 1;
		Invalid.Operation.AccountId = 42;
		Invalid.Operation.State = LMail3ImapOperationState::Executing;
		Invalid.Operation.SourceUid = 1;
		Invalid.Operation.SourceUidValidity = 100;
		Invalid.Operation.SourceRemoteName = "/INBOX\r\nA1234 EXPUNGE";
		JournalImap Imap(JournalScript::Normal);
		auto Result = ExecuteImapJournalOperation(Imap, Invalid);
		Require(Result.Outcome == ImapJournalOutcome::NotExecuted && Imap.Script->Commands.empty(),
			"Invalid journal command fields reached the transport");
		for (bool FailRead: {false, true})
		{
			class PartialUpload : public LMemStream
			{
				bool Fail;
			public:
				PartialUpload(const std::string &Raw, bool fail) : LMemStream(Raw.data(), Raw.size()), Fail(fail) {}
				ssize_t Read(void *Data, ssize_t Size, int Flags = 0) override
				{
					if (Fail && GetPos() >= 6)
						return 0;
					return LMemStream::Read(Data, MIN(Size, (ssize_t)3), Flags);
				}
			};
			ImapJournalCommand Command;
			Command.Operation.Id = 11;
			Command.Operation.AccountId = 42;
			Command.Operation.State = LMail3ImapOperationState::Executing;
			Command.Operation.Type = LMail3ImapOperationType::Append;
			Command.Operation.DestinationRemoteName = "/Archive";
			Command.Operation.DestinationUidValidity = UINT32_MAX;
			Command.Upload.Reset(new PartialUpload(Raw, FailRead));
			JournalImap Server(JournalScript::Normal);
			auto Result = ExecuteImapJournalOperation(Server, Command);
			Require(Result.Outcome == (FailRead ? ImapJournalOutcome::Uncertain : ImapJournalOutcome::Confirmed),
				"APPEND did not correctly handle partial or failed input stream reads");
			if (FailRead)
				Require(!Server.Script->IsOpen() && Result.DestinationUid == -1 && Result.Error,
					"A partial literal left a reusable protocol connection or invented a destination UID");
			else
				Require(Server.Script->Appended == Raw, "Partial upload reads changed the literal bytes");
		}
	}

	void TestJournalReconciliationProtocol()
	{
		for (auto Type: {LMail3ImapOperationType::SetFlags, LMail3ImapOperationType::Delete,
			LMail3ImapOperationType::Move, LMail3ImapOperationType::Append})
		{
			bool Append = Type == LMail3ImapOperationType::Append;
			bool Move = Type == LMail3ImapOperationType::Move;
			for (auto Mode: {JournalScript::Normal, JournalScript::WrongSourceEpoch,
				JournalScript::WrongDestinationEpoch, JournalScript::MissingSource,
				JournalScript::MissingDestination, JournalScript::LostFetchReply,
				JournalScript::DuplicateFetch, JournalScript::FetchWithoutFlags,
				JournalScript::EpochChangedDuringReconciliation})
			{
				ImapJournalCommand Command;
				auto &Operation = Command.Operation;
				Operation.Id = 10;
				Operation.AccountId = 42;
				Operation.Attempts = 2;
				Operation.State = LMail3ImapOperationState::Reconciling;
				Operation.Type = Type;
				Operation.SourceRemoteName = "/INBOX";
				Operation.SourceUidValidity = 100;
				Operation.SourceUid = UINT32_MAX - 1;
				Operation.DestinationRemoteName = "/Archive";
				Operation.DestinationUidValidity = (Move || Append) ? UINT32_MAX : -1;
				Operation.DestinationUid = (Move || Append) ? UINT32_MAX : -1;
				Operation.RemoteFlags = Move ? 0 : Type == LMail3ImapOperationType::Delete ? 34 : 32;
				JournalImap Imap(Mode);
				Imap.Script->SeedFlags(Move ? 2 : Operation.RemoteFlags, Operation.RemoteFlags);
				auto Result = ExecuteImapJournalOperation(Imap, Command);
				bool Failure = (!Append && (Mode == JournalScript::WrongSourceEpoch || Mode == JournalScript::MissingSource)) ||
					((Append || Move) && (Mode == JournalScript::WrongDestinationEpoch || Mode == JournalScript::MissingDestination)) ||
					Mode == JournalScript::LostFetchReply || Mode == JournalScript::DuplicateFetch ||
					Mode == JournalScript::FetchWithoutFlags || Mode == JournalScript::EpochChangedDuringReconciliation;
				Require((Result.Outcome == ImapJournalOutcome::Confirmed) == !Failure &&
					(bool)Result.Error == Failure && Result.Reconciliation && Result.Attempt == 2,
					"Read-only reconciliation accepted incomplete evidence or lost attempt correlation");
				Require(!Imap.Script->Copies && !Imap.Script->Stores && !Imap.Script->Appends,
					"Reconciliation sent a server mutation");
				if (Move || Append)
				{
					Operation.DestinationUid = -1;
					JournalImap Missing(JournalScript::Normal);
					auto Unknown = ExecuteImapJournalOperation(Missing, Command);
					Require(Unknown.Outcome != ImapJournalOutcome::Confirmed && Unknown.Error &&
						Missing.Script->Commands.empty(), "Missing UIDPLUS evidence was guessed or searched");
				}
			}
		}
	}

	void TestJournalReconciliationBridge()
	{
		for (auto Type: {LMail3ImapOperationType::SetFlags, LMail3ImapOperationType::Delete,
			LMail3ImapOperationType::Move, LMail3ImapOperationType::Append})
		{
			for (int Scenario = 0; Scenario < 6; Scenario++)
			{
				bool Append = Type == LMail3ImapOperationType::Append;
				bool Move = Type == LMail3ImapOperationType::Move;
				Fixture Files;
				auto Database = Files.Root / "reconciliation.mail3";
				std::string Raw = "From: sender@example.test\r\nMessage-ID: <reconciliation>\r\n\r\noriginal";
				LMail3ImapOperation Operation;
				LMail3ImapMailbox Source, Destination;
				ImapJournalResult Original;
				{
					LMail3Store Store(Database.c_str(), NULL, true);
					Require(Store.GetRoot() != NULL, "Cannot create the reconciliation store");
					Source.AccountId = Destination.AccountId = 42;
					Source.RemoteName = "/INBOX";
					Destination.RemoteName = "/Archive";
					Source.UidValidity = 100;
					Destination.UidValidity = UINT32_MAX;
					Source.UidNext = Destination.UidNext = UINT32_MAX;
					LArray<LMail3ImapServerMessage> Messages, Empty;
					auto &Message = Messages.New();
					Message.Uid = UINT32_MAX - 1;
					Message.Headers = Raw.substr(0, Raw.find("\r\n\r\n")).c_str();
					Message.Size = Raw.size();
					Require(Store.RebuildImapMailbox(Source, Messages) && Store.RebuildImapMailbox(Destination, Empty),
						"Cannot establish reconciliation mailboxes");
					LMemStream Body(Raw.data(), Raw.size());
					if (Append)
						Require(Store.StageImapAppend(42, Destination.Id, 32, Body, Operation),
							"Cannot stage reconciliation APPEND");
					else
					{
						LMail3ImapMessage Identity;
						Require(Store.FindImapMessage(Source.Id, 100, UINT32_MAX - 1, Identity) &&
							Store.StoreImapBody(42, Identity, Body), "Cannot store the reconciliation source");
						Operation.AccountId = 42;
						Operation.MailId = Identity.MailId;
						Operation.Type = Type;
						Operation.RemoteFlags = 32;
						if (Move)
							Operation.DestinationMailboxId = Destination.Id;
						Require(Store.QueueImapOperation(Operation), "Cannot queue reconciliation fixture");
					}
					Require(Store.Check(sqlite3_exec(Store.GetDb(),
						"update Mail set Label='recovery label', MarkColour=1234", NULL, NULL, NULL), NULL),
						"Cannot customize the reconciliation fixture");
					ImapJournalCommand Command;
					Require(!PrepareImapJournalOperation(Store, 42, Operation.Id, Command, true),
						"A queued command was claimed for recovery");
					Require(PrepareImapJournalOperation(Store, 42, Operation.Id, Command),
						"Cannot claim the original command");
					JournalImap Imap(JournalScript::Normal);
					Original = ExecuteImapJournalOperation(Imap, Command);
					Require(Original.Outcome == ImapJournalOutcome::Confirmed,
						"The original reconciliation fixture failed");
					Require(Store.InterruptImapOperation(Operation.Id, "Fixture interrupted before local commit",
						Scenario == 1 ? -1 : Original.DestinationUidValidity,
						Scenario == 1 ? -1 : Original.DestinationUid), "Cannot quarantine the interrupted command");
				}
				ImapJournalCommand Reconcile;
				{
					LMail3Store Store(Database.c_str(), NULL, false);
					Require(!PrepareImapJournalOperation(Store, 43, Operation.Id, Reconcile, true) &&
						!PrepareImapJournalOperation(Store, 42, Operation.Id, Reconcile),
						"An uncertain command was claimed by another account or redispatched");
					Require(Store.Check(sqlite3_exec(Store.GetDb(), "begin", NULL, NULL, NULL), NULL),
						"Cannot begin the reconciliation outer transaction");
					Require(!PrepareImapJournalOperation(Store, 42, Operation.Id, Reconcile, true),
						"Reconciliation was claimed without a durable commit");
					Require(Store.Check(sqlite3_exec(Store.GetDb(), "rollback", NULL, NULL, NULL), NULL),
						"Cannot roll back the reconciliation outer transaction");
					Require(PrepareImapJournalOperation(Store, 42, Operation.Id, Reconcile, true) &&
						!Reconcile.Upload && Reconcile.Operation.Attempts == 2 &&
						Scalar(Store, "select count(*) from ImapOperation where State=3") == 1,
						"Reconciliation claim was not durable or unnecessarily detached an upload");
				}
				JournalImap Imap(Scenario == 3 ?
					(Append ? JournalScript::WrongDestinationEpoch : JournalScript::WrongSourceEpoch) : JournalScript::Normal);
				int SourceFlags = Move ? 2 : Reconcile.Operation.RemoteFlags;
				int DestinationFlags = Reconcile.Operation.RemoteFlags;
				if (Scenario == 2)
				{
					if (Append)
						DestinationFlags ^= 32;
					else if (Move)
						SourceFlags = 0;
					else
						SourceFlags ^= 32;
				}
				Imap.Script->SeedFlags(SourceFlags, DestinationFlags);
				auto Result = ExecuteImapJournalOperation(Imap, Reconcile);
				Require(!Imap.Script->Copies && !Imap.Script->Stores && !Imap.Script->Appends,
					"Restarted recovery repeated a mutation");
				LMail3Store Store(Database.c_str(), NULL, false);
				if (Scenario == 5)
				{
					Require(Store.RecoverImapOperations(42) &&
						PrepareImapJournalOperation(Store, 42, Operation.Id, Reconcile, true),
						"Interrupted reconciliation could not be recovered");
					Require(!ApplyImapJournalResult(Store, 42, Result) &&
						Scalar(Store, "select Attempts from ImapOperation") == 3 &&
						Scalar(Store, "select State from ImapOperation") == 3,
						"A stale reconciliation result retired a newer attempt");
					JournalImap Retry(JournalScript::Normal);
					Retry.Script->SeedFlags(SourceFlags, DestinationFlags);
					Result = ExecuteImapJournalOperation(Retry, Reconcile);
				}
				Require(!ApplyImapJournalResult(Store, 43, Result) && !ApplyImapJournalResult(Store, 42, Original),
					"Recovery accepted another account or the original late mutation result");
				auto WrongMode = Result;
				WrongMode.Reconciliation = false;
				Require(!ApplyImapJournalResult(Store, 42, WrongMode),
					"A mutation result completed a read-only reconciliation claim");
				if ((Move || Append) && Scenario != 1)
				{
					auto WrongEvidence = Result;
					WrongEvidence.DestinationUid--;
					Require(!ApplyImapJournalResult(Store, 42, WrongEvidence) &&
						!Store.CompleteImapOperation(Operation.Id, UINT32_MAX, UINT32_MAX - 1) &&
						!Store.InterruptImapOperation(Operation.Id, "Conflicting destination", UINT32_MAX, UINT32_MAX - 1),
						"Recovery substituted an identity not present in the recorded UIDPLUS evidence");
				}
				if (Scenario == 4)
					Require(Store.Check(sqlite3_exec(Store.GetDb(),
						"create trigger FailReconciliation before update on Mail "
						"begin select raise(abort,'fixture reconciliation commit failure'); end",
						NULL, NULL, NULL), NULL), "Cannot inject the recovery commit failure");
				bool Confirmed = Scenario != 2 && Scenario != 3 && !(Scenario == 1 && (Move || Append));
				Require((Result.Outcome == ImapJournalOutcome::Confirmed) == Confirmed,
					"Recovery misclassified changed flags, missing evidence or an epoch reset");
				Require(ApplyImapJournalResult(Store, 42, Result) == (Scenario != 4),
					"Recovery misreported a local commit failure");
				Require(!ApplyImapJournalResult(Store, 42, Result), "Recovery applied a duplicate result");
				LArray<LMail3ImapOperation> Remaining;
				Require(Store.LoadImapOperations(42, Remaining) && Export(Store, Operation.MailId) == Raw &&
					Scalar(Store, "select count(*) from Mail where Label='recovery label' and MarkColour=1234") == (Append ? 2 : 1),
					"Recovery lost the original message body, label or colour");
				LMail3Mail Identity(&Store);
				{
					LMail3Store::LStatement Query(&Store, "select * from Mail where Id=?");
					Require(Query.SetInt64(0, Operation.MailId) && Query.Row() && Identity.Serialize(Query, false),
						"Recovery lost the canonical message");
				}
				if (Confirmed && Scenario != 4)
				{
					Require(!Remaining.Length() && Identity.ParentId == ((Move || Append) ? Destination.Id : Source.Id) &&
						Identity.ImapUid == ((Move || Append) ? (int64)UINT32_MAX : (int64)UINT32_MAX - 1),
						"Recovery failed to atomically complete canonical membership and retire the journal");
				}
				else
				{
					Require(Remaining.Length() == 1 &&
						Remaining[0].State == LMail3ImapOperationState::NeedsReconciliation && Remaining[0].LastError &&
						Identity.ParentId == (Append ? Destination.Id : Source.Id) &&
						Identity.ImapUid == (Append ? -1 : (int64)UINT32_MAX - 1),
						"Unresolved recovery changed canonical membership or lost its journal diagnostic");
					Require(Remaining[0].DestinationUid == ((Move || Append) && Scenario != 1 ? (int64)UINT32_MAX : -1),
						"Recovery discarded or fabricated UIDPLUS evidence");
				}
				if (Scenario == 4)
				{
					Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailReconciliation",
						NULL, NULL, NULL), NULL) && PrepareImapJournalOperation(Store, 42, Operation.Id, Reconcile, true),
						"Cannot retry read-only verification after a failed local commit");
					JournalImap Retry(JournalScript::Normal);
					Retry.Script->SeedFlags(SourceFlags, DestinationFlags);
					auto Retried = ExecuteImapJournalOperation(Retry, Reconcile);
					Require(ApplyImapJournalResult(Store, 42, Retried) &&
						Scalar(Store, "select count(*) from ImapOperation") == 0 &&
						Export(Store, Operation.MailId) == Raw, "Recovery could not finish after a local commit failure");
				}
			}
		}
	}

	void TestCopyByUid()
	{
		class CopySocket : public LSocket
		{
			std::string Response;
			size_t Offset = 0;
		public:
			std::string Command;
			bool IsOpen() override { return true; }
			ssize_t Write(const void *Data, ssize_t Size, int Flags = 0) override
			{
				Command.assign((const char*)Data, Size);
				auto Tag = Command.substr(0, Command.find(' '));
				Response = Tag + " OK [COPYUID 100 1,4294967295 2,3] copied\r\n";
				Offset = 0;
				return Size;
			}
			ssize_t Read(void *Data, ssize_t Size, int Flags = 0) override
			{
				auto Count = std::min<size_t>(Size, Response.size() - Offset);
				memcpy(Data, Response.data() + Offset, Count);
				Offset += Count;
				return Count;
			}
		};
		class CopyImap : public MailIMap
		{
		public:
			CopySocket *Script;
			CopyImap()
			{
				Script = new CopySocket;
				Socket.Reset(Script);
			}
		} Imap;
		LArray<uint32_t> Uids;
		MailIMap::UidPlusResult Mapping;
		Mapping.DestinationUid = 123;
		Require(!Imap.CopyByUid(Uids, "/Archive", &Mapping) &&
			Imap.Script->Command.empty() && !Mapping.DestinationUid,
			"Empty COPY input reached the server or retained stale UIDPLUS output");
		Uids.Add(0);
		Require(!Imap.CopyByUid(Uids, "/Archive") && Imap.Script->Command.empty(),
			"Zero COPY UID reached the server");
		Uids[0] = 1;
		Uids.Add(UINT32_MAX);
		Require(Imap.CopyByUid(Uids, "/Archive") &&
			Imap.Script->Command.find("UID COPY 1,4294967295 \"Archive\"\r\n") != std::string::npos,
			"Existing batch COPY callers failed or truncated unsigned UIDs");
		Mapping.UidValidity = Mapping.SourceUid = Mapping.DestinationUid = 123;
		Require(Imap.CopyByUid(Uids, "/Archive", &Mapping) &&
			!Mapping.UidValidity && !Mapping.SourceUid && !Mapping.DestinationUid,
			"Batch COPY returned a misleading single-message UIDPLUS identity");
	}

	void TestJournalMoveBridge()
	{
		for (int Scenario = 0; Scenario < 4; Scenario++)
		{
			auto Mode = Scenario < 2 ? JournalScript::Normal :
				Scenario == 2 ? JournalScript::SourceDeleteFailed : JournalScript::LostCopyReply;
			Fixture Files;
			auto Database = Files.Root / "move-journal.mail3";
			std::string Raw = "From: sender@example.test\r\nMessage-ID: <move-journal>\r\n\r\nbody";
			LMail3ImapOperation Move;
			int64 SourceId, MailId;
			{
				LMail3Store Store(Database.c_str(), NULL, true);
				Require(Store.GetRoot() != NULL, "Cannot create the move journal root");
				LMail3ImapMailbox Source, Destination;
				Source.AccountId = Destination.AccountId = 42;
				Source.RemoteName = "/INBOX";
				Destination.RemoteName = "/Archive";
				Source.UidValidity = 100;
				Destination.UidValidity = UINT32_MAX;
				Source.UidNext = Destination.UidNext = UINT32_MAX;
				LArray<LMail3ImapServerMessage> Messages, Empty;
				auto &Message = Messages.New();
				Message.Uid = UINT32_MAX - 1;
				Message.Headers = Raw.substr(0, Raw.find("\r\n\r\n")).c_str();
				Message.Size = Raw.size();
				Require(Store.RebuildImapMailbox(Source, Messages) && Store.RebuildImapMailbox(Destination, Empty),
					"Cannot create the move journal mailboxes");
				LMail3ImapMessage Identity;
				Require(Store.FindImapMessage(Source.Id, 100, UINT32_MAX - 1, Identity),
					"Cannot find the move journal source");
				LMemStream Body(Raw.data(), Raw.size());
				Require(Store.StoreImapBody(42, Identity, Body), "Cannot store the move journal body");
				SourceId = Source.Id;
				MailId = Identity.MailId;
				Move.AccountId = 42;
				Move.Type = LMail3ImapOperationType::Move;
				Move.MailId = MailId;
				Move.DestinationMailboxId = Destination.Id;
				Require(Store.QueueImapOperation(Move), "Cannot queue the worker move");
				ImapJournalCommand Command;
				Require(PrepareImapJournalOperation(Store, 42, Move.Id, Command), "Cannot claim the worker move");
				JournalImap Imap(Mode);
				auto Result = ExecuteImapJournalOperation(Imap, Command);
				if (Scenario == 1)
					Require(Store.Check(sqlite3_exec(Store.GetDb(),
						"create trigger FailWorkerCommit before update of ParentId on Mail "
						"when new.ImapUid=4294967295 "
						"begin select raise(abort,'fixture confirmed worker commit failure'); end",
						NULL, NULL, NULL), NULL), "Cannot inject the confirmed worker commit failure");
				Require(ApplyImapJournalResult(Store, 42, Result) == (Scenario != 1),
					"The owning-thread bridge misreported a result commit");
				Require(Store.LoadImapMessage(MailId, Identity) && Identity.MailboxId ==
					(Scenario == 0 ? Destination.Id : Source.Id) && Export(Store, MailId) == Raw,
					"The worker move changed canonical membership prematurely or lost its body");
			}
			LMail3Store Store(Database.c_str(), NULL, false);
			LArray<LMail3ImapOperation> Operations;
			Require(Store.LoadImapOperations(42, Operations), "Cannot reload the worker move journal");
			if (Scenario == 0)
				Require(!Operations.Length(), "A confirmed move did not retire its journal entry");
			else
			{
				Require(Operations.Length() == 1 && Operations[0].State == LMail3ImapOperationState::NeedsReconciliation &&
					Operations[0].SourceMailboxId == SourceId && Operations[0].LastError &&
					Operations[0].DestinationUid == (Scenario == 3 ? -1 : (int64)UINT32_MAX) &&
					Operations[0].DestinationUidValidity == UINT32_MAX,
					"An incomplete move lost its frozen source, confirmed COPYUID evidence or diagnostic on restart");
				ImapJournalCommand Retry;
				Require(!PrepareImapJournalOperation(Store, 42, Move.Id, Retry),
					"An uncertain COPY was redispatched and could create duplicate mail");
			}
		}
	}

	void TestJournalBridge()
	{
		for (auto Mode: {JournalScript::Normal, JournalScript::MissingAppendUid,
			JournalScript::LostAppendReply, JournalScript::WrongDestinationEpoch})
		{
			Fixture Files;
			auto Database = Files.Root / "journal.mail3";
			std::string Raw = "From: sender@example.test\r\nMessage-ID: <journal>\r\n\r\nbody\nraw";
			if (Mode == JournalScript::Normal)
			{
				// Exercise LTempStream's disk spill before transfer to the worker.
				Raw.append((1 << 20) + 4096, 'x');
				Raw.append("\0binary\xff", 8);
			}
			LMail3ImapOperation Operation;
			ImapJournalCommand Command;
			{
				LMail3Store Store(Database.c_str(), NULL, true);
				Require(Store.GetRoot() != NULL, "Cannot create journal bridge root");
				LMail3ImapMailbox Mailbox;
				Mailbox.AccountId = 42;
				Mailbox.RemoteName = "/Archive";
				Mailbox.UidValidity = UINT32_MAX;
				Mailbox.UidNext = 10;
				LArray<LMail3ImapServerMessage> Empty;
				Require(Store.RebuildImapMailbox(Mailbox, Empty), "Cannot establish the bridge mailbox identity");
				LMemStream Body(Raw.data(), Raw.size());
				Require(Store.StageImapAppend(42, Mailbox.Id, 32, Body, Operation), "Cannot stage the bridge APPEND");
				Require(!PrepareImapJournalOperation(Store, 43, Operation.Id, Command) &&
					Scalar(Store, "select count(*) from ImapOperation where State=0") == 1,
					"Preparing another account's command acquired the journal entry");
				Require(PrepareImapJournalOperation(Store, 42, Operation.Id, Command) && Command.Upload,
					"Cannot prepare and durably claim the journal payload");
				Require(Scalar(Store, "select count(*) from ImapOperation where State=1 and Attempts=1") == 1,
					"Preparing a command did not commit its executing state");
			}
			// Neither the upload nor the worker may depend on the store connection or owner thread.
			ImapJournalResult Result;
			std::exception_ptr WorkerError;
			std::thread Worker([&]()
			{
				try
				{
					JournalImap Imap(Mode);
					Result = ExecuteImapJournalOperation(Imap, Command);
				}
				catch (...)
				{
					WorkerError = std::current_exception();
				}
			});
			Worker.join();
			if (WorkerError)
				std::rethrow_exception(WorkerError);
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(!ApplyImapJournalResult(Store, 43, Result), "Another account applied a journal worker result");
			Require(ApplyImapJournalResult(Store, 42, Result), "Cannot persist the worker's journal result");
			Require(!ApplyImapJournalResult(Store, 42, Result), "A duplicate worker result was applied twice");
			if (Mode == JournalScript::Normal)
			{
				LMail3ImapMessage Mapping;
				Require(Store.LoadImapMessage(Operation.MailId, Mapping) && Mapping.Uid == UINT32_MAX &&
					Mapping.UidValidity == UINT32_MAX && Mapping.Downloaded &&
					Scalar(Store, "select count(*) from ImapOperation") == 0,
					"Confirmed worker APPEND did not atomically bind its destination UID");
			}
			else
			{
				LArray<LMail3ImapOperation> Remaining;
				Require(Store.LoadImapOperations(42, Remaining) && Remaining.Length() == 1 &&
					Remaining[0].State == LMail3ImapOperationState::NeedsReconciliation &&
					Remaining[0].LastError &&
					Scalar(Store, "select count(*) from Mail where ImapUid is null") == 1,
					"Uncertain or unexecuted APPEND was acknowledged, lost or left dispatchable");
			}
			Require(Export(Store, Operation.MailId) == Raw, "Worker journal handling damaged the recoverable upload");
		}
	}

	void TestSelectEpoch()
	{
		class ScriptedSocket : public LSocket
		{
			std::string Response;
			size_t Offset = 0;
			int Mode;
			unsigned Selects = 0;

		public:
			unsigned Fetches = 0;
			ScriptedSocket(int mode) : Mode(mode) {}
			bool IsOpen() override { return true; }
			ssize_t Write(const void *Data, ssize_t Size, int Flags = 0) override
			{
				std::string Command((const char*)Data, Size);
				auto End = Command.find(' ');
				auto Tag = Command.substr(0, End);
				Response.clear();
				if (Command.find("SELECT") != std::string::npos)
				{
					Selects++;
					Response = Mode == 1 ? "* 0 EXISTS\r\n" :
						(Mode == 3 && Selects > 1 ? "* 3 EXISTS\r\n" : "* 2 EXISTS\r\n");
					Response += "* 0 RECENT\r\n";
					if (Mode != 5)
						Response += Mode == 2 && Selects > 1 ?
							"* OK [UIDVALIDITY 200] UIDs valid\r\n" :
							"* OK [UIDVALIDITY 4294967295] UIDs valid\r\n";
					if (Mode != 7)
						Response += Mode == 6 && Selects > 1 ?
							"* OK [UIDNEXT 3000000001] next UID\r\n" :
							"* OK [UIDNEXT 3000000000] next UID\r\n";
					Response += Tag + " OK [READ-WRITE] SELECT done\r\n";
				}
				else if (Command.find("UID FETCH 1:*") != std::string::npos)
				{
					Fetches++;
					for (int n = 1; n <= (Mode == 4 ? 1 : 2); n++)
					{
						std::string Header = "From: server@example.com\r\nSubject: Server\r\nMessage-ID: <" +
							std::to_string(n) + ">\r\n\r\n";
						Response += "* " + std::to_string(n) + " FETCH (UID " +
							std::to_string(n == 1 ? 2147483649LL : 2LL) +
							" FLAGS (\\Seen) RFC822.SIZE 100 BODY[HEADER] {" +
							std::to_string(Header.size()) + "}\r\n" + Header + ")\r\n";
					}
					Response += Tag + " OK FETCH done\r\n";
				}
				else
					throw std::runtime_error("Unexpected command in the scripted rebuild");
				Offset = 0;
				return Size;
			}
			ssize_t Read(void *Data, ssize_t Size, int Flags = 0) override
			{
				auto Count = std::min((size_t)Size, Response.size() - Offset);
				memcpy(Data, Response.data() + Offset, Count);
				Offset += Count;
				return Count;
			}
		};
		class ScriptedImap : public MailIMap
		{
		public:
			ScriptedSocket *Script;
			ScriptedImap(int Mode = 0)
			{
				Script = new ScriptedSocket(Mode);
				Socket.Reset(Script);
			}
		} Imap;
		MailIMap::StrMap Values;
		Require(Imap.SelectFolder("/INBOX", &Values), "Scripted SELECT failed");
		Require(Values.Find("UIDVALIDITY").Int() == UINT32_MAX, "SELECT lost unsigned UIDVALIDITY");
		Require(Values.Find("UIDNEXT").Int() == 3000000000LL, "SELECT lost UIDNEXT");
		Require(Values.Find("Exists").Int() == 2, "SELECT changed existing EXISTS parsing");
		for (int Mode = 0; Mode <= 7; Mode++)
		{
			ScriptedImap Server(Mode);
			ImapFolderInfo Folder;
			Folder.Remote = "/INBOX";
			Folder.Sep = '/';
			Folder.LastUid = UINT32_MAX;
			ImapMsg Result(IMAP_REBUILD_MAILBOX, _FL);
			Result.Last = true;
			LString Error;
			bool Ok = FetchImapRebuild(Server, Folder, Result, Error);
			Require(Ok == (Mode <= 1), "Snapshot verification accepted a partial/changing server listing");
			if (Ok)
			{
				Require(Result.Last && Result.Fld.Length() == 1 && Result.Fld[0].LastUid == 0 &&
					Result.Fld[0].UidValidity == UINT32_MAX, "Snapshot did not establish fresh server identity");
				if (Mode == 0)
					Require(Result.Mail.Length() == 2 && Result.Mail[0].Uid == 2147483649U,
						"Snapshot truncated server UIDs or respected a cached last UID");
				else
					Require(Result.Mail.Length() == 0 && Server.Script->Fetches == 0,
						"Empty mailbox rebuild issued an invalid FETCH");
			}
			else
				Require(Error.Get() && !Result.Last, "Snapshot verification failed without an explicit error");
		}
	}

	void TestCanonicalSchemaUpgrade()
	{
		Fixture Files;
		auto Database = Files.Root / "old.mail3";
		std::filesystem::create_directories(Database);
		sqlite3 *Db = NULL;
		Require(sqlite3_open((Database / MAIL3_DB_FILE).c_str(), &Db) == SQLITE_OK,
			"Cannot create old Mail3 schema fixture");
		auto Result = sqlite3_exec(Db,
			"create table Folder (Id INTEGER PRIMARY KEY AUTOINCREMENT, ParentId INTEGER, Name TEXT, "
			"Unread INTEGER, Open INTEGER, ItemType INTEGER, Sort INTEGER, Threaded INTEGER, "
			"SortIndex INTEGER, AccessPerm INTEGER);"
			"create table Mail (Id INTEGER PRIMARY KEY AUTOINCREMENT, ParentId INTEGER, "
			"Priority INTEGER, Flags INTEGER, AccountId INTEGER, MarkColour INTEGER, Subject TEXT, "
			"ToAddr TEXT, FromAddr TEXT, Reply TEXT, Label TEXT, InternetHeader TEXT, MessageID TEXT, "
			"Ref TEXT, FwdMsgId TEXT, BounceMsgId TEXT, ServerUid TEXT, DateReceived TEXT, DateSent TEXT, Size INTEGER);"
			"create table MailSegs (Id INTEGER PRIMARY KEY AUTOINCREMENT, MailId INTEGER, "
			"ParentId INTEGER, Headers TEXT, Data BLOB);"
			"create table ImapOperation (Id INTEGER PRIMARY KEY AUTOINCREMENT, AccountId INTEGER NOT NULL, "
			"OperationType INTEGER NOT NULL, State INTEGER NOT NULL, MailId INTEGER, SourceMailboxId INTEGER, "
			"DestinationMailboxId INTEGER, SourceUidValidity INTEGER, SourceUid INTEGER, "
			"DestinationUidValidity INTEGER, DestinationUid INTEGER, Created TEXT, Attempts INTEGER, LastError TEXT);"
			"insert into Folder (Id, Name) values (1,'Old root');"
			"insert into Folder (Id, ParentId, Name, Sort, Threaded) values (2,1,'Old folder',123,1);"
			"insert into Mail (Id, ParentId, Subject, Label, MarkColour) values (3,2,'Old mail','keep me',456);"
			"insert into MailSegs (Id, MailId, Data) values (4,3,X'000102ff');",
			NULL, NULL, NULL);
		auto Closed = sqlite3_close(Db);
		Require(Result == SQLITE_OK && Closed == SQLITE_OK, "Cannot populate old schema fixture");
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(Store.GetInt(FIELD_STATUS) == Store3UpgradeRequired,
				"Old canonical schema failed to reach the normal upgrade path");
			bool Upgraded = false;
			Store.Upgrade(NULL, NULL, [&](bool Status) { Upgraded = Status; });
			Require(Upgraded && Store.GetInt(FIELD_STATUS) == Store3Success,
				"Cannot upgrade old canonical schema");
			Require(Scalar(Store, "select count(*) from sqlite_master where type='index' and name in "
				"('FolderImapAccountRemoteIdx','MailImapFolderUidIdx','MailFolderIdx')") == 3,
				"Upgrade must restore indexes before using the store, without requiring a restart");
			Require(Scalar(Store, "select count(*) from pragma_table_info('ImapOperation') "
				"where name in ('SourceRemoteName','DestinationRemoteName','RemoteFlags')") == 3 &&
				Scalar(Store, "select count(*) from sqlite_master where type='index' and name='ImapOperationMailIdx'") == 1,
				"Upgrade did not add the command snapshots and unique message-operation index");
		}
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success, "Upgraded canonical schema failed to reopen");
			Require(Scalar(Store, "select count(*) from Folder where Id=2 and ParentId=1 and Name='Old folder' "
				"and Sort=123 and Threaded=1 and ImapAccountId is null and ImapRemoteName is null") == 1,
				"Upgrade changed an existing local folder");
			Require(Scalar(Store, "select count(*) from Mail where Id=3 and ParentId=2 and Subject='Old mail' "
				"and Label='keep me' and MarkColour=456 and ImapUid is null and ImapDownloaded is null") == 1,
				"Upgrade changed existing local mail");
			Require(Scalar(Store, "select count(*) from MailSegs where Id=4 and MailId=3 and Data=X'000102ff'") == 1,
				"Upgrade changed an existing MIME body");
			Require(Scalar(Store, "select count(*) from sqlite_master where type='index' and name in "
				"('FolderImapAccountRemoteIdx','MailImapFolderUidIdx','MailFolderIdx')") == 3,
				"Upgrade failed to create canonical IMAP uniqueness indexes");
		}
	}

	void TestMergedSchema()
	{
		Fixture Files;
		auto Database = Files.Root / "merged.mail3";
		int64 MailId, FolderId;
		{
			LMail3Store Store(Database.c_str(), NULL, true);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success && Store.GetRoot(),
				"Cannot create merged schema fixture");
			Require(Scalar(Store, "select count(*) from sqlite_master where type='table' "
				"and name in ('ImapMailbox','ImapMessage')") == 0, "Separate IMAP identity tables still exist");
			auto RootId = Scalar(Store, "select Id from Folder where ParentId is null");
			for (int Index = 0; Index < 2; Index++)
			{
				LMail3Folder Local(&Store);
				Local.ParentId = RootId;
				Local.Name = "Local folder";
				Require(Local.Write(MAIL3_TBL_FOLDER, true), "Ordinary Mail3 folders failed to save");
				LMail3Mail Mail(&Store);
				Mail.ParentId = Local.Id;
				Mail.Subject = "Local message";
				Require(Mail.Write(MAIL3_TBL_MAIL, true), "Ordinary Mail3 mail failed to save");
			}
			Require(Scalar(Store, "select count(*) from Folder where ImapAccountId is not null or "
				"ImapRemoteName is not null or ImapUidValidity is not null") == 0,
				"Local folders must leave IMAP fields NULL");
			Require(Scalar(Store, "select count(*) from Mail where ImapUid is not null or "
				"ImapUidValidity is not null or ImapDownloaded is not null") == 0,
				"Local mail must leave IMAP fields NULL");

			LMail3Folder Folder(&Store);
			Folder.ParentId = RootId;
			Folder.Name = "Server folder";
			Folder.ImapAccountId = 7;
			Folder.ImapRemoteName = "/INBOX";
			Folder.ImapDelimiter = "/";
			Folder.ImapUidValidity = UINT32_MAX;
			Folder.ImapUidNext = UINT32_MAX;
			Folder.ImapHighestModSeq = "18446744073709551615";
			Require(Folder.Write(MAIL3_TBL_FOLDER, true), "Canonical IMAP folder failed to save");
			FolderId = Folder.Id;
			LArray<LMail3ImapMailbox> Mailboxes;
			Require(Store.LoadImapMailboxes(7, Mailboxes) && Mailboxes.Length() == 1 &&
				Mailboxes[0].Id == FolderId && Mailboxes[0].UidValidity == UINT32_MAX &&
				Mailboxes[0].HighestModSeq == "18446744073709551615",
				"Mailbox access API is not reading the canonical folder");
			Mailboxes[0].UidNext = 100;
			Require(Store.SaveImapMailbox(Mailboxes[0]), "Mailbox access API could not update the folder");
			LMail3Folder Reloaded(&Store);
			{
				LMail3Store::LStatement Query(&Store, "select * from Folder where Id=?");
				Require(Query.SetInt64(0, FolderId) && Query.Row() && Reloaded.Serialize(Query, false),
					"Cannot reload canonical IMAP folder");
			}
			Require(Reloaded.ImapUidNext == 100 && Reloaded.ImapAccountId == 7 &&
				Reloaded.ImapRemoteName == "/INBOX" && Reloaded.ImapDelimiter == "/",
				"Folder serialization lost IMAP state");
			Reloaded.Name = "Renamed locally";
			Require(Reloaded.Write(MAIL3_TBL_FOLDER, false), "Ordinary folder edit failed");
			LMail3Folder Duplicate(&Store);
			Duplicate.ParentId = RootId;
			Duplicate.ImapAccountId = 7;
			Duplicate.ImapRemoteName = "/INBOX";
			Require(!Duplicate.Write(MAIL3_TBL_FOLDER, true), "Duplicate account/mailbox identity was accepted");

			LMail3Mail Mail(&Store);
			Mail.ParentId = FolderId;
			Mail.AccountId = 7;
			Mail.ImapUidValidity = UINT32_MAX;
			Mail.ImapUid = UINT32_MAX;
			Mail.ImapRemoteFlags = 123;
			Mail.ImapLocalFlags = 456;
			Mail.ImapModSeq = "18446744073709551615";
			Mail.ImapDownloaded = 0;
			Require(Mail.Write(MAIL3_TBL_MAIL, true), "Canonical IMAP mail failed to save");
			MailId = Mail.Id;
			LMail3ImapMessage Mapping;
			Require(Store.FindImapMessage(FolderId, UINT32_MAX, UINT32_MAX, Mapping) &&
				Mapping.MailId == MailId && Mapping.MailboxId == FolderId &&
				Mapping.ModSeq == "18446744073709551615" && Mapping.LocalFlags == 456,
				"Message access API is not reading the canonical mail");
			Mapping.RemoteFlags = 22;
			Require(Store.SaveImapMessage(Mapping), "Message access API could not update canonical mail");
			auto Invalid = Mapping;
			Invalid.MailboxId = RootId;
			Require(!Store.SaveImapMessage(Invalid), "Saving IMAP state unexpectedly reparented canonical mail");
			Invalid = Mapping;
			Invalid.MailId = 999999;
			Require(!Store.SaveImapMessage(Invalid), "Saving IMAP state created an orphan mail row");
			Mailboxes[0].AccountId = 8;
			Require(!Store.SaveImapMailbox(Mailboxes[0]), "Mailbox update reassigned another account's folder");

			LMail3Mail ReloadedMail(&Store);
			{
				LMail3Store::LStatement Query(&Store, "select * from Mail where Id=?");
				Require(Query.SetInt64(0, MailId) && Query.Row() && ReloadedMail.Serialize(Query, false),
					"Cannot reload canonical IMAP mail");
			}
			Require(ReloadedMail.ImapUid == UINT32_MAX && ReloadedMail.ImapUidValidity == UINT32_MAX &&
				ReloadedMail.ImapRemoteFlags == 22 && ReloadedMail.ImapLocalFlags == 456 &&
				ReloadedMail.ImapModSeq == "18446744073709551615" && ReloadedMail.ImapDownloaded == 0,
				"Mail serialization lost IMAP fields");
			ReloadedMail.Label = "ordinary edit";
			Require(ReloadedMail.Write(MAIL3_TBL_MAIL, false), "Ordinary mail edit failed");
			LMail3Mail DuplicateMail(&Store);
			DuplicateMail.ParentId = FolderId;
			DuplicateMail.AccountId = 7;
			DuplicateMail.ImapUidValidity = UINT32_MAX;
			DuplicateMail.ImapUid = UINT32_MAX;
			Require(!DuplicateMail.Write(MAIL3_TBL_MAIL, true), "Duplicate scoped UID was accepted");
		}
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success, "Merged database failed to reopen");
			LMail3ImapMessage Mapping;
			Require(Store.LoadImapMessage(MailId, Mapping) && Mapping.MailboxId == FolderId &&
				Mapping.RemoteFlags == 22 && Mapping.LocalFlags == 456 &&
				Mapping.ModSeq == "18446744073709551615", "Ordinary edits or restart erased IMAP metadata");
			Require(Store.DeleteImapMessage(MailId), "Cannot clear canonical IMAP message metadata");
			Require(!Store.LoadImapMessage(MailId, Mapping), "Cleared mail still has a server identity");
			Require(Scalar(Store, "select count(*) from Mail where Label='ordinary edit' "
				"and ImapUid is null and ImapUidValidity is null and ImapRemoteFlags is null "
				"and ImapLocalFlags is null and ImapModSeq is null and ImapDownloaded is null") == 1,
				"Clearing IMAP identity deleted or damaged canonical mail");
		}
	}

	void TestServerRebuild()
	{
		Fixture Files;
		auto Database = Files.Root / "server.mail3";
		auto Cache = Files.Root / "cache";
		const char *Index =
			"<Folder Sort=\"-6\" Expanded=\"1\" Thread=\"1\" Unread=\"999\">"
			"<Fields><Field Id=\"1\" Width=\"123\"/></Fields><Emails>"
			"<Mail Uid=\"untrusted-cache-id\" Flags=\"\\Seen\" MsgId=\"&lt;unique&gt;\" Label=\"retained\" Colour=\"4278255360\"/>"
			"<Mail Uid=\"2\" MsgId=\"&lt;source-duplicate&gt;\" Label=\"ambiguous 1\"/>"
			"<Mail Uid=\"3\" MsgId=\"&lt;source-duplicate&gt;\" Label=\"ambiguous 2\"/>"
			"<Mail Uid=\"4\" MsgId=\"&lt;server-duplicate&gt;\" Label=\"ambiguous server\"/>"
			"<Mail Uid=\"5\" Label=\"no identity\"/>"
			"</Emails></Folder>";
		Files.Write(Cache / "INBOX" / "Folder.xml", Index);
		Files.Write(Cache / "Gone" / "Folder.xml", "<Folder/>");
		Files.Write(Cache / "INBOX" / "temp_pending.eml", "Retain this local-only upload");
		Files.Write(Cache / "INBOX" / "999.eml", "Corrupt old content must not be imported");
		int64 OriginalId;
		LMail3ImapMailbox Inbox;
		Inbox.AccountId = 42;
		Inbox.RemoteName = "/INBOX";
		Inbox.Delimiter = "/";
		Inbox.UidValidity = UINT32_MAX;
		Inbox.UidNext = 30;
		LArray<LMail3ImapServerMessage> Messages;
		auto Add = [&](int64 Uid, const char *Id)
		{
			auto &Message = Messages.New();
			Message.Uid = Uid;
			Message.Headers = LString::Fmt("From: Server <server@example.com>\r\n"
				"Subject: Fresh from server\r\n%s%s%s", Id ? "Message-ID: " : "", Id ? Id : "", Id ? "\r\n" : "");
			Message.Size = 100;
		};
		Add(10, "<unique>");
		Add(11, "<source-duplicate>");
		Add(12, "<server-duplicate>");
		Add(13, "<server-duplicate>");
		Add(14, NULL);
		{
			LMail3Store Store(Database.c_str(), NULL, true);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success && Store.GetRoot(), "Cannot create rebuild fixture");
			Require(Store.ImportImapSettings(99, (Files.Root / "no-legacy-cache").c_str()),
				"An account without a legacy cache must still be able to rebuild");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailSettingsRecovery before insert on ImapLocalSettings "
				"when new.AccountId=44 and new.RemoteName='/INBOX' "
				"begin select raise(abort, 'fixture settings recovery failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install the settings recovery rollback fixture");
			Require(!Store.ImportImapSettings(44, Cache.c_str()), "Settings recovery failure must abort");
			Require(Scalar(Store, "select count(*) from ImapLocalSettings where AccountId=44") == 0,
				"Failed settings recovery left a partial account snapshot");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailSettingsRecovery",
				NULL, NULL, NULL), NULL), "Cannot remove the settings recovery rollback fixture");
			Require(Store.ImportImapSettings(42, Cache.c_str()), "Cannot recover local settings");
			Require(Scalar(Store, "select count(*) from Mail") == 0 &&
				Scalar(Store, "select count(*) from Folder where ImapRemoteName is not null") == 0,
				"Settings recovery must not import cached mail or folder membership");
			Require(Scalar(Store, "select count(*) from ImapLocalSettings where Settings like '%Uid=%'") == 0,
				"Settings recovery copied unverified UIDs");
			Require(Store.RebuildImapMailbox(Inbox, Messages), "Server rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail") == 5, "Rebuild imported cached membership");
			Require(Scalar(Store, "select count(*) from MailSegs") == 0, "Rebuild imported cached bodies");
			Require(Scalar(Store, "select count(*) from Mail where ImapUidValidity=4294967295 and ImapDownloaded=0") == 5,
				"Rebuild did not establish server-only identities");
			Require(Scalar(Store, "select count(*) from Mail where ImapUid=999") == 0, "Rebuild trusted a cached UID");
			Require(Scalar(Store, "select count(*) from Folder where Name='Gone'") == 0,
				"Settings recovery resurrected a removed server folder");
			Require(Scalar(Store, "select count(*) from Folder where Sort=-6 and Open=1 and Threaded=1") == 1 &&
				Scalar(Store, "select count(*) from FolderFields where Width=123") == 1,
				"Rebuild lost folder display settings");
			Require(Scalar(Store, "select count(*) from Folder where Unread=999") == 0,
				"Rebuild copied a stale cached unread count");
			Require(Scalar(Store, "select count(*) from Mail where Label='retained' and MarkColour=-16711936") == 1,
				"Unique Message-ID did not recover label and colour");
			Require(Scalar(Store, "select count(*) from Mail where Label is not null and Label!=''") == 1,
				"Rebuild restored ambiguous or missing-identity labels");
			Require(Scalar(Store, "select count(*) from Mail where (Flags & 1)!=0") == 0,
				"Rebuild trusted cached flags");
			OriginalId = Scalar(Store, "select Id from Mail where ImapUid=10");
			const std::string Download =
				"From: server@example.com\r\nSubject: Fresh from server\r\nMessage-ID: <unique>\r\n\r\nServer body\r\n";
			{
				LMail3Mail Mail(&Store);
				{
					LMail3Store::LStatement Query(&Store, "select * from Mail where Id=?");
					Require(Query.SetInt64(0, OriginalId) && Query.Row() && Mail.Serialize(Query, false),
						"Cannot load the server download fixture");
				}
				LMemStream Input(Download.data(), Download.size());
				Require(Mail.SetRfc822(&Input, true) == Store3Success && Mail.Write(MAIL3_TBL_MAIL, false),
					"Cannot persist a downloaded server message");
				LMail3ImapMessage Mapping;
				Require(Store.LoadImapMessage(OriginalId, Mapping), "Cannot load the download mapping");
				Mapping.Downloaded = true;
				Require(Store.SaveImapMessage(Mapping), "Cannot persist the download mapping");
			}
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"update Mail set Label='edited', MarkColour=123 where MessageID='<unique>'", NULL, NULL, NULL), NULL),
				"Cannot edit the restored customization");
			Require(Store.RebuildImapMailbox(Inbox, Messages), "Repeated server rebuild failed");
			Require(Scalar(Store, "select Id from Mail where ImapUid=10") == OriginalId &&
				Scalar(Store, "select count(*) from Mail where Label='edited' and MarkColour=123") == 1,
				"Repeated refresh changed IDs or overwrote local edits");
			Require(Export(Store, OriginalId) == Download &&
				Scalar(Store, "select count(*) from Mail where ImapDownloaded=1") == 1,
				"Same-epoch refresh discarded or changed downloaded content");
			auto OriginalHeaders = Messages[0].Headers;
			Messages[0].Headers = "From: server@example.com\r\nMessage-ID: <unexpected-reuse>\r\n";
			Require(!Store.RebuildImapMailbox(Inbox, Messages), "Rebuild accepted contradictory UID identity");
			Require(Export(Store, OriginalId) == Download, "Contradictory identity damaged downloaded content");
			Messages[0].Headers = OriginalHeaders;
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"insert into ImapOperation(AccountId,OperationType,State) values(42,0,0)", NULL, NULL, NULL), NULL),
				"Cannot install the pending-operation fixture");
			Require(!Store.RebuildImapMailbox(Inbox, Messages), "Rebuild must not bypass pending server operations");
			Require(Export(Store, OriginalId) == Download, "Refused rebuild damaged existing content");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "delete from ImapOperation where AccountId=42",
				NULL, NULL, NULL), NULL), "Cannot remove the pending-operation fixture");
			auto Other = Inbox;
			Other.AccountId = 43;
			Require(Store.RebuildImapMailbox(Other, Messages), "Second account rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail where AccountId=43 and Label is not null and Label!=''") == 0,
				"Labels leaked across accounts");
			Other = Inbox;
			Other.RemoteName = "/Other";
			Require(Store.RebuildImapMailbox(Other, Messages), "Second mailbox rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail where Label='edited'") == 1,
				"Labels leaked across mailboxes");

			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailServerRebuild before update of ImapUid on Mail when new.ImapUid=25 "
				"begin select raise(abort, 'fixture server rebuild failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install server rebuild rollback fixture");
			Inbox.UidValidity = 200;
			Messages[0].Uid = 20;
			Messages[1].Uid = 25;
			Require(!Store.RebuildImapMailbox(Inbox, Messages), "Server rebuild failure must abort");
			Require(Scalar(Store, "select count(*) from Mail where ImapUidValidity=200") == 0 &&
				Scalar(Store, "select Id from Mail where ImapUid=10 order by Id limit 1") == OriginalId,
				"Failed server rebuild invalidated the old snapshot");
			Require(Export(Store, OriginalId) == Download, "Failed rebuild damaged downloaded content");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailServerRebuild", NULL, NULL, NULL), NULL),
				"Cannot remove rebuild rollback fixture");
			Require(Store.RebuildImapMailbox(Inbox, Messages), "UIDVALIDITY reset rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail where ParentId=(select Id from Folder "
				"where ImapAccountId=42 and ImapRemoteName='/INBOX') and ImapUidValidity=200") == 5,
				"Epoch reset retained old UID mappings");
			Require(Scalar(Store, "select count(*) from MailSegs") == 0,
				"Epoch reset retained obsolete downloaded content");
			Require(Scalar(Store, "select count(*) from Mail where ParentId=(select Id from Folder "
				"where ImapAccountId=42 and ImapRemoteName='/INBOX') "
				"and ImapUid=20 and Label='edited' and MarkColour=123") == 1,
				"Epoch reset lost uniquely matched current customizations");
			Messages[1].Uid = 20;
			Require(!Store.RebuildImapMailbox(Inbox, Messages), "Duplicate server UIDs must be rejected");
			Messages[1].Uid = 25;
			auto Epoch = Inbox.UidValidity;
			Inbox.UidValidity = 0;
			Require(!Store.RebuildImapMailbox(Inbox, Messages), "Unknown server epochs must be rejected");
			Inbox.UidValidity = Epoch;
			Require(sqlite3_get_autocommit(Store.GetDb()) && sqlite3_next_stmt(Store.GetDb(), NULL) == NULL,
				"Rebuild leaked transactions or statements");
		}
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Files.Write(Cache / "INBOX" / "Folder.xml", "<Folder/>");
			Require(Store.ImportImapSettings(42, Cache.c_str()), "Restarted settings import failed");
			Require(Store.RebuildImapMailbox(Inbox, Messages), "Restarted server rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail where Label='edited'") == 1,
				"Restart lost restored labels");
			LArray<LMail3ImapServerMessage> Empty;
			Require(Store.RebuildImapMailbox(Inbox, Empty), "Empty server mailbox rebuild failed");
			Require(Scalar(Store, "select count(*) from Mail where ParentId=(select Id from Folder "
				"where ImapAccountId=42 and ImapRemoteName='/INBOX')") == 0, "Server deletions were not reflected");
			Messages[0].Uid = 29;
			Require(Store.RebuildImapMailbox(Inbox, Messages), "Restoring a returning server message failed");
			Require(Scalar(Store, "select count(*) from Mail where Label='edited'") == 1,
				"A server deletion discarded the latest recoverable local customization");
		}
		Require(std::filesystem::exists(Cache / "INBOX" / "temp_pending.eml") &&
			std::filesystem::exists(Cache / "INBOX" / "999.eml"), "Rebuild deleted recovery content");
	}

	void Run()
	{
		Fixture Files;
		auto Database = Files.Root / "test.mail3";
		auto Cache = Files.Root / "cache";
		auto Inbox = Cache / "INBOX";
		const std::string Message =
			"From: Sender <sender@example.com>\r\n"
			"To: Recipient <recipient@example.com>\r\n"
			"Subject: Folded\r\n\tmessage\r\n"
			"Message-ID: <cache-fixture@example.com>\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/mixed; boundary=fixture\r\n\r\n"
			"--fixture\r\nContent-Type: text/plain\r\n\r\nBody text\r\n"
			"--fixture\r\nContent-Type: application/octet-stream\r\n"
			"Content-Disposition: attachment; filename=test.bin\r\n"
			"Content-Transfer-Encoding: base64\r\n\r\nAAECAw==\r\n--fixture--\r\n";
		const std::string Index =
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<Folder Sort=\"-6\" Expanded=\"1\" Thread=\"1\" ReadPerm=\"0\" WritePerm=\"0\">"
			"<Fields><Field Id=\"1\" Width=\"123\"/></Fields><Emails>"
			"<Mail Uid=\"1\" Flags=\"\\Seen\" Label=\"kept\" MsgId=\"&lt;cache-fixture@example.com&gt;\">1.eml</Mail>"
			"<Mail Uid=\"2\" Subject=\"Metadata only\" Size=\"99\">2.eml</Mail>"
			"<Mail Uid=\"4294967295\" Subject=\"Maximum UID\" Size=\"4294967295\">4294967295.eml</Mail>"
			"</Emails></Folder>";
		Files.Write(Inbox / "Folder.xml", Index);
		Files.Write(Inbox / "1.eml", Message);
		Files.Write(Inbox / "Nested" / "Folder.xml",
			"<Folder><Emails><Mail Uid=\"1\" Subject=\"Nested UID\">1.eml</Mail></Emails></Folder>");

		int64 ImportedMailId = -1;
		{
			LMail3Store Store(Database.c_str(), NULL, true);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success && Store.GetRoot(),
				"Cannot create Mail3 fixture");
			Require(Store.ImportImapCache(42, Cache.c_str()), Store.GetStr(FIELD_ERROR));
			Require(Scalar(Store, "select count(*) from Folder where ImapRemoteName is not null") == 3, "Wrong folder count");
			Require(Scalar(Store, "select count(*) from Mail") == 4, "Wrong mail count");
			Require(Scalar(Store, "select count(*) from Mail where ImapUidValidity=0") == 4,
				"Imported UIDs must be unverified");
			Require(Scalar(Store, "select count(*) from Folder where ImapUidValidity is not null") == 0,
				"Legacy caches must not invent UIDVALIDITY");
			Require(Scalar(Store, "select count(*) from Mail where ImapDownloaded=1") == 1,
				"Missing messages must remain metadata-only");
			Require(Scalar(Store, "select count(*) from MailSegs join Mail on "
				"MailSegs.MailId=Mail.Id where ImapDownloaded=0") == 0,
				"Metadata-only messages must not acquire dummy MIME segments");
			auto MissingMailId = Scalar(Store, "select Id from Mail where ImapUid=2");
			Require(Scalar(Store, "select count(*) from Mail where Label='kept' "
				"and MessageID='<cache-fixture@example.com>'") == 1,
				"Message labels and IDs were not preserved");
			Require(Scalar(Store, "select count(*) from Mail where ImapUid=4294967295") == 1,
				"Unsigned UIDs must not be truncated");
			Require(Scalar(Store, "select count(*) from Mail where Size=4294967295") == 1,
				"Metadata-only message sizes must retain 64-bit values");
			Require(Scalar(Store, "select count(*) from FolderFields where Width=123") == 1,
				"Folder display fields were not migrated");
			Require(Scalar(Store, "select count(*) from Folder where Sort=-6 and Open=1 and Threaded=1") == 1,
				"Folder settings were not migrated");
			ImportedMailId = Scalar(Store, "select Id from Mail where ImapDownloaded=1");
			Require(Export(Store, ImportedMailId) == Message, "RFC822 bytes changed during migration");
			{
				LMail3Mail Mail(&Store);
				LMail3Store::LStatement Query(&Store, "select * from Mail where Id=?");
				Require(Query.SetInt64(0, ImportedMailId) && Query.Row() && Mail.Serialize(Query, false),
					"Cannot load mail for attachment verification");
				auto Root = dynamic_cast<LDataI*>(Mail.GetObj(FIELD_MIME_SEG));
				Require(Root != NULL, "Cannot parse the imported MIME tree");
				auto Children = Root->GetList(FIELD_MIME_SEG);
				Require(Children && Children->Length() == 2, "Imported MIME children changed");
				auto Attachment = dynamic_cast<LDataI*>((*Children)[1]);
				Require(Attachment != NULL, "Cannot load the imported attachment");
				auto Data = Attachment->GetStream(_FL);
				char Bytes[4];
				Require(Data && Data->GetSize() == 4 && Data->Read(Bytes, sizeof(Bytes)) == sizeof(Bytes) &&
					!memcmp(Bytes, "\0\1\2\3", sizeof(Bytes)), "Decoded attachment bytes changed");
			}
			auto InboxId = Scalar(Store, "select ParentId from Mail where ImapDownloaded=1");
			LMail3ImapMessage Unverified;
			Require(!Store.FindImapMessage(InboxId, 0, 1, Unverified),
				"A provisional mapping must not be treated as a verified server identity");
			Require(Store.ImportImapCache(42, Cache.c_str()), "Repeated import failed");
			Require(Scalar(Store, "select count(*) from Mail") == 4, "Repeated import duplicated messages");
			Require(Scalar(Store, "select count(*) from MailSegs") == 1, "Repeated import duplicated content");

			auto Before = Scalar(Store, "select count(*) from Folder");
			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailImapFields before insert on FolderFields "
				"begin select raise(abort, 'fixture field write failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install rollback fixture");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Field write failure must abort migration");
			Require(Scalar(Store, "select count(*) from Folder") == Before, "Failed import left folder rows");
			Require(Scalar(Store, "select count(*) from Folder where ImapAccountId=43") == 0,
				"Failed import left mailbox mappings");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailImapFields", NULL, NULL, NULL), NULL),
				"Cannot remove rollback fixture");
			Require(sqlite3_get_autocommit(Store.GetDb()) != 0, "Failed import left a transaction open");

			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailImapMapping before insert on Mail when new.ImapUid=4294967295 "
				"begin select raise(abort, 'fixture mapping write failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install late rollback fixture");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Mapping failure must abort migration");
			Require(Scalar(Store, "select count(*) from Folder") == Before &&
				Scalar(Store, "select count(*) from Mail") == 4 &&
				Scalar(Store, "select count(*) from MailSegs") == 1 &&
				Scalar(Store, "select count(*) from Mail where ImapUid is not null") == 4,
				"Late failure did not roll back folders, mail, segments and mappings");
			Require(Export(Store, ImportedMailId) == Message, "Rollback changed existing mail");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailImapMapping", NULL, NULL, NULL), NULL),
				"Cannot remove late rollback fixture");

			for (const char *InvalidUid: { "1x", "-1", "0", "4294967296" })
			{
				Files.Write(Inbox / "Folder.xml",
					std::string("<Folder><Emails><Mail Uid=\"") + InvalidUid + "\"/></Emails></Folder>");
				Require(!Store.ImportImapCache(43, Cache.c_str()), "Invalid UID must be rejected");
			}
			Files.Write(Inbox / "Folder.xml",
				"<Folder><Emails><Mail Uid=\"1\"/><Mail Uid=\"1\"/></Emails></Folder>");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Duplicate UIDs must be rejected");
			Files.Write(Inbox / "Folder.xml", "<NotAFolder/>");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Invalid folder XML must be rejected");
			Files.Write(Inbox / "Folder.xml", "<Folder><Emails><Mail Uid=\"1\"/>");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Truncated folder XML must be rejected");
			Files.Write(Inbox / "Folder.xml", "<Folder><Emails></Folder>");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Mismatched XML elements must be rejected");
			Files.Write(Inbox / "Folder.xml", Index);
			Require(Scalar(Store, "select count(*) from Folder") == Before,
				"Malformed metadata left partial folders");

			Files.Write(Inbox / "2.eml", "Invalid RFC822 image without a header separator");
			Require(!Store.ImportImapCache(42, Cache.c_str()), "Corrupt cached content must fail explicitly");
			Require(Scalar(Store, "select count(*) from Mail where ImapDownloaded=1") == 1,
				"Failed import marked corrupt content downloaded");
			std::filesystem::remove(Inbox / "2.eml");
			Files.Write(Inbox / "2.eml", "From: sender@example.com\r\nSubject: Newly downloaded\r\n\r\nNew body\r\n");
			Require(Store.ImportImapCache(42, Cache.c_str()), "Importing newly available content failed");
			Require(Scalar(Store, "select count(*) from Mail") == 4, "Metadata-only upgrade duplicated mail");
			Require(Scalar(Store, "select count(*) from Mail where ImapDownloaded=1") == 2,
				"Metadata-only message did not acquire content");
			Require(Scalar(Store, "select Id from Mail where ImapUid=2") == MissingMailId,
				"Metadata-only upgrade changed the canonical mail ID");

			Files.Write(Inbox / "temp_pending.eml", Message);
			Require(!Store.ImportImapCache(44, Cache.c_str()), "Unindexed pending APPEND must not be ignored");
			Require(Scalar(Store, "select count(*) from Folder where ImapAccountId=44") == 0,
				"Unindexed content left a partial account import");
			std::filesystem::remove(Inbox / "temp_pending.eml");
			Require(Store.ImportImapCache(43, Cache.c_str()), "Second account import failed");
			Require(Scalar(Store, "select count(*) from Folder where ImapAccountId=43") == 3,
				"Accounts were not isolated");

			std::filesystem::create_directory_symlink(Cache, Files.Root / "linked-cache");
			Require(!Store.ImportImapCache(44, (Files.Root / "linked-cache").c_str()),
				"Symbolic-link cache roots must be rejected");
			std::filesystem::remove(Files.Root / "linked-cache");

			auto Unreadable = Cache / "Unreadable";
			std::filesystem::create_directory(Unreadable);
			std::filesystem::permissions(Unreadable, std::filesystem::perms::none);
			bool RefusedUnreadable = !Store.ImportImapCache(44, Cache.c_str());
			std::filesystem::permissions(Unreadable, std::filesystem::perms::owner_all);
			Require(RefusedUnreadable, "Unreadable cache directories must fail explicitly");
			Require(Scalar(Store, "select count(*) from Folder where ImapAccountId=44") == 0,
				"Unreadable sources left a partial account import");
			std::filesystem::remove(Unreadable);

			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"update Folder set ImapUidValidity=123 where ImapAccountId=43", NULL, NULL, NULL), NULL),
				"Cannot set up verified mailbox fixture");
			Require(!Store.ImportImapCache(43, Cache.c_str()),
				"A verified mailbox must not accept unverified cache mappings");
			Require(sqlite3_get_autocommit(Store.GetDb()) != 0, "Import left an open transaction");
			Require(sqlite3_next_stmt(Store.GetDb(), NULL) == NULL, "Import leaked a prepared statement");

			LMail3Mail Local(&Store);
			Local.ParentId = Scalar(Store, "select Id from Folder where ParentId is null");
			const std::string Plain = "From: sender@example.com\r\nSubject: Local mail\r\n\r\nBody\r\n";
			LMemStream PlainInput(Plain.data(), Plain.size());
			Require(Local.SetRfc822(&PlainInput) == Store3Success && Local.Write(MAIL3_TBL_MAIL, false),
				"Existing parsed-MIME import failed");
			auto LocalBefore = Export(Store, Local.Id);
			LMemStream InvalidInput("invalid", 7);
			Require(Local.SetRfc822(&InvalidInput, true) == Store3Error,
				"Malformed raw replacement must fail");
			Require(Export(Store, Local.Id) == LocalBefore, "Invalid raw replacement deleted existing content");
		}
		{
			LMail3Store Store(Database.c_str(), NULL, false);
			Require(Store.GetInt(FIELD_STATUS) == Store3Success, "Imported database failed to reopen");
			Require(Store.ImportImapCache(42, Cache.c_str()), "Restarted import failed");
			Require(Scalar(Store, "select count(*) from Mail") == 9, "Restarted import duplicated messages");
			Require(Export(Store, ImportedMailId) == Message, "RFC822 changed after database reopen");
			Require(Store.DeleteImapMessage(ImportedMailId) && Export(Store, ImportedMailId) == Message,
				"Clearing IMAP metadata damaged the canonical RFC822 body");
		}
		Require(std::filesystem::exists(Inbox / "Folder.xml") &&
			std::filesystem::exists(Inbox / "1.eml"), "Importer deleted the legacy cache");
	}
}

int LgiMain(OsAppArguments &Arguments)
{
	LApp App(Arguments, "ImapCacheMigrationTests");
	if (!App.IsOk())
		return 1;
	InitStrToDom();
	try
	{
		Run();
		TestSelectEpoch();
		TestMergedSchema();
		TestCanonicalSchemaUpgrade();
		TestCutoverPersistence();
		TestJournalProtocol();
		TestCopyByUid();
		TestJournalBridge();
		TestJournalMoveBridge();
		TestJournalReconciliationProtocol();
		TestJournalReconciliationBridge();
		TestServerRebuild();
		FreeStrToDom();
		printf("IMAP cache migration tests passed\n");
		return 0;
	}
	catch (const std::exception &Error)
	{
		FreeStrToDom();
		fprintf(stderr, "IMAP cache migration test failed: %s\n", Error.what());
		return 1;
	}
}
