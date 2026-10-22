#include "Store3Mail3/Mail3.h"
#include "lgi/common/Mail.h"
#include "Store3Imap/ScribeImap.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

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
