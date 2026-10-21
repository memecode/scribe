#include "Store3Mail3/Mail3.h"
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
			Require(Scalar(Store, "select count(*) from ImapMailbox") == 3, "Wrong folder count");
			Require(Scalar(Store, "select count(*) from Mail") == 4, "Wrong mail count");
			Require(Scalar(Store, "select count(*) from ImapMessage where UidValidity=0") == 4,
				"Imported UIDs must be unverified");
			Require(Scalar(Store, "select count(*) from ImapMailbox where UidValidity is not null") == 0,
				"Legacy caches must not invent UIDVALIDITY");
			Require(Scalar(Store, "select count(*) from ImapMessage where Downloaded=1") == 1,
				"Missing messages must remain metadata-only");
			Require(Scalar(Store, "select count(*) from MailSegs join ImapMessage on "
				"MailSegs.MailId=ImapMessage.MailId where Downloaded=0") == 0,
				"Metadata-only messages must not acquire dummy MIME segments");
			auto MissingMailId = Scalar(Store, "select MailId from ImapMessage where Uid=2");
			Require(Scalar(Store, "select count(*) from Mail where Label='kept' "
				"and MessageID='<cache-fixture@example.com>'") == 1,
				"Message labels and IDs were not preserved");
			Require(Scalar(Store, "select count(*) from ImapMessage where Uid=4294967295") == 1,
				"Unsigned UIDs must not be truncated");
			Require(Scalar(Store, "select count(*) from Mail where Size=4294967295") == 1,
				"Metadata-only message sizes must retain 64-bit values");
			Require(Scalar(Store, "select count(*) from FolderFields where Width=123") == 1,
				"Folder display fields were not migrated");
			Require(Scalar(Store, "select count(*) from Folder where Sort=-6 and Open=1 and Threaded=1") == 1,
				"Folder settings were not migrated");
			ImportedMailId = Scalar(Store, "select MailId from ImapMessage where Downloaded=1");
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
			auto InboxId = Scalar(Store, "select MailboxId from ImapMessage where Downloaded=1");
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
			Require(Scalar(Store, "select count(*) from ImapMailbox where AccountId=43") == 0,
				"Failed import left mailbox mappings");
			Require(Store.Check(sqlite3_exec(Store.GetDb(), "drop trigger FailImapFields", NULL, NULL, NULL), NULL),
				"Cannot remove rollback fixture");
			Require(sqlite3_get_autocommit(Store.GetDb()) != 0, "Failed import left a transaction open");

			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"create trigger FailImapMapping before insert on ImapMessage when new.Uid=4294967295 "
				"begin select raise(abort, 'fixture mapping write failure'); end", NULL, NULL, NULL), NULL),
				"Cannot install late rollback fixture");
			Require(!Store.ImportImapCache(43, Cache.c_str()), "Mapping failure must abort migration");
			Require(Scalar(Store, "select count(*) from Folder") == Before &&
				Scalar(Store, "select count(*) from Mail") == 4 &&
				Scalar(Store, "select count(*) from MailSegs") == 1 &&
				Scalar(Store, "select count(*) from ImapMessage") == 4,
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
			Require(Scalar(Store, "select count(*) from ImapMessage where Downloaded=1") == 1,
				"Failed import marked corrupt content downloaded");
			std::filesystem::remove(Inbox / "2.eml");
			Files.Write(Inbox / "2.eml", "From: sender@example.com\r\nSubject: Newly downloaded\r\n\r\nNew body\r\n");
			Require(Store.ImportImapCache(42, Cache.c_str()), "Importing newly available content failed");
			Require(Scalar(Store, "select count(*) from Mail") == 4, "Metadata-only upgrade duplicated mail");
			Require(Scalar(Store, "select count(*) from ImapMessage where Downloaded=1") == 2,
				"Metadata-only message did not acquire content");
			Require(Scalar(Store, "select MailId from ImapMessage where Uid=2") == MissingMailId,
				"Metadata-only upgrade changed the canonical mail ID");

			Files.Write(Inbox / "temp_pending.eml", Message);
			Require(!Store.ImportImapCache(44, Cache.c_str()), "Unindexed pending APPEND must not be ignored");
			Require(Scalar(Store, "select count(*) from ImapMailbox where AccountId=44") == 0,
				"Unindexed content left a partial account import");
			std::filesystem::remove(Inbox / "temp_pending.eml");
			Require(Store.ImportImapCache(43, Cache.c_str()), "Second account import failed");
			Require(Scalar(Store, "select count(*) from ImapMailbox where AccountId=43") == 3,
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
			Require(Scalar(Store, "select count(*) from ImapMailbox where AccountId=44") == 0,
				"Unreadable sources left a partial account import");
			std::filesystem::remove(Unreadable);

			Require(Store.Check(sqlite3_exec(Store.GetDb(),
				"update ImapMailbox set UidValidity=123 where AccountId=43", NULL, NULL, NULL), NULL),
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
