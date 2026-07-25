#include "lgi/common/Lgi.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Net.h"
#include "lgi/common/Base64.h"

#include "../common/StringClass.h"
#include "smtpServer.h"

const char *appName = "mapiConnector";

struct ImapConnection : public LSocket
{
	Context &ctx;
	LStringPipe rdBuf;

	LString cmdRef, idleCmdRef;
	LString AuthenticateType;
	LString selectPath;
	LDataFolderI *selectFolder = nullptr;

	ImapConnection(Context &c) :
		ctx(c),
		rdBuf(8 << 10)
	{
	}

	~ImapConnection()
	{
		ctx.log.Print("%s:%i - delete connect.\n", _FL);
	}

	void ImapErr(const char *reason, const char *msg)
	{
		auto w = LString::Fmt("%s NO [%s] %s\r\n", cmdRef.Get(), reason, msg);
		Write(w);
	}

	void DoRead()
	{
		auto pos = rdBuf.Find("\n");
		if (pos < 0)
			return;
		if (auto line = rdBuf.Pop().Replace("\r"))
		{
			if (AuthenticateType)
			{
				auto userPass = LToBinary(line);
				LString user, pass;
				if (userPass.Length() > 1)
				{
					if (user = userPass.Get() + 1)
						pass = userPass.Get() + 2 + user.Length();
				}

				auto authOk = ctx.authenticateUser(user, pass);
				if (!authOk)
				{
					printf("%s:%i - auth failed:\n", _FL);
					printf("	user='%s'\n", user.Get());
					printf("	pass='%s'\n", pass.Get());
					return ImapErr("AUTHENTICATIONFAILED", "Authentication failed");
				}

				auto w = LString::Fmt(	"* CAPABILITY IMAP4rev1\r\n"
										"%s OK Logged in\r\n",
										cmdRef.Get());
				Write(w);
				AuthenticateType.Empty(); // exit authentication mode
				return;
			}

			// Turn line into list of tokens:
			LString::Array parts;
			const char *ptr = line.Get();
			while (auto s = LTokLStr(ptr))
				parts.Add(s);

			if (parts.Length() == 1 &&
				parts[0].Equals("DONE"))
			{
				auto w = LString::Fmt("%s OK Idle completed\r\n", idleCmdRef.Get());
				Write(w);
				idleCmdRef.Empty();
				return;
			}
			else if (parts.Length() < 2)
			{
				ctx.log.Print("Error: unexpected part count for '%s'\n", line.Strip().Get());
				return ImapErr("TOOFEW", "unexpected cmd argument count");
			}

			cmdRef = parts[0];
			int cmdIdx = 1;
			bool isUid = false;
			if (parts[cmdIdx].Equals("UID"))
			{
				isUid = true;
				cmdIdx++;
			}
			auto &cmd = parts[cmdIdx++];

			if (cmd.Equals("CAPABILITY"))
			{
				auto w = LString::Fmt(	"* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN] %s ready.\r\n"
										"* CAPABILITY IMAP4rev1 AUTH=PLAIN\r\n"
										"%s OK Pre-login capabilities listed, post-login capabilities have more.\r\n",
										appName,
										cmdRef.Get());
				auto wr = Write(w);
				if (wr != w.Length())
					ctx.log.Print("Error: write failed..\n");
			}
			else if (cmd.Equals("AUTHENTICATE"))
			{
				AuthenticateType = parts[2];
				Write("+\r\n");
			}
			else if (cmd.Equals("LIST"))
			{
				auto flds = ctx.FolderList();
				for (auto &f: flds)
				{
					if (f.depth == 0)
						continue;

					auto imapPath = LString(ctx.sep).Join(f.full.Slice(1, -1));
					LAssert(imapPath);
					auto w = LString::Fmt("* LIST (%s) \"%s\" %s\r\n",
						f.subFolders ? "\\HasChildren" : "\\HasNoChildren",
						ctx.sep,
						imapPath.Get());
					Write(w);
				}
				Write(LString::Fmt("%s OK List completed\r\n", cmdRef.Get()));
			}
			else if (cmd.Equals("SELECT"))
			{
				/*
				A0005 SELECT "INBOX"
					* FLAGS (\Answered \Flagged \Deleted \Seen \Draft unknown-0 unknown-3 unknown-4 unknown-1 unknown-2 $NotJunk $Junk JunkRecorded NonJunk Junk $Forwarded)
					* OK [PERMANENTFLAGS (\Answered \Flagged \Deleted \Seen \Draft unknown-0 unknown-3 unknown-4 unknown-1 unknown-2 $NotJunk $Junk JunkRecorded NonJunk Junk $Forwarded \*)] Flags permitted.
					* 9248 EXISTS
					* 0 RECENT
					* OK [UNSEEN 1049] First unseen.
					* OK [UIDVALIDITY 1326231758] UIDs valid
					* OK [UIDNEXT 56128] Predicted next UID
					* OK [HIGHESTMODSEQ 113969] Highest
					A0005 OK [READ-WRITE] Select completed (0.010 + 0.000 + 0.009 secs).				
				*/
				selectPath = parts[2];
				if (selectFolder = ctx.GetFolder(selectPath))
				{
					// figure out the children count...
					auto &children = selectFolder->Children();
					Write(LString::Fmt("* " LPrintfInt64 " EXISTS\r\n", children.Length()));
					Write(LString::Fmt("* " LPrintfInt64 " RECENT\r\n", selectFolder->GetInt(FIELD_UNREAD)));
					Write(LString::Fmt("%s OK [READ-WRITE] Select completed\r\n", cmdRef.Get()));
					ctx.log.Print("Selected '%s'...\n", selectPath.Get());
				}
				else return ImapErr("UNAVAILABLE", "path doesn't exist");
			}
			else if (cmd.Equals("FETCH"))
			{
				auto pos = line.Find("(");
				if (pos <= 0)
				{
					ctx.log.Print("%s:%i - Error: unexpected FETCH arg.\n", _FL);
					return ImapErr("SERVERBUG", "unexpected fetch arg");
				}
				
				auto arg = parts[cmdIdx++];
				auto fields = line(pos, -1);
				if (!selectFolder)
					return ImapErr("UNAVAILABLE", "no folder selected");
				if (!arg)
					return ImapErr("UNAVAILABLE", "missing argument");
				
				auto resp = ctx.Fetch(selectFolder, isUid, arg, fields);
				for (auto &r: resp)
				{
					// log.Print("Fetch: %s\n", r.Get());
					Write(r);
				}
				Write(LString::Fmt("%s OK Fetch completed\r\n", cmdRef.Get()));
			}
			else if (cmd.Equals("SEARCH"))
			{
				if (!selectFolder)
					return ImapErr("UNAVAILABLE", "no folder selected");

				auto resp = ctx.Search(selectFolder, isUid, parts.Slice(3, -1));
				for (auto &r: resp)
				{
					ctx.log.Print("Search: %s\n", r.Get());
					Write(r);
				}
				Write(LString::Fmt("%s OK Search completed\r\n", cmdRef.Get()));
			}
			else if (cmd.Equals("IDLE"))
			{
				idleCmdRef = cmdRef;
				Write(LString::Fmt("%s OK idle\r\n", cmdRef.Get()));
			}
			else if (cmd.Equals("STORE"))
			{
				if (!selectFolder)
					return ImapErr("UNAVAILABLE", "no folder selected");

				auto resp = ctx.Store(selectFolder, isUid, parts.Slice(3, -1));
				for (auto &r: resp)
				{
					ctx.log.Print("Search: %s\n", r.Get());
					Write(r);
				}
				Write(LString::Fmt("%s OK Store completed\r\n", cmdRef.Get()));
			}
			else
			{
				ctx.log.Print("UnknownImapCmd: %s\n", line.Strip().Get());
			}
		}
	}

	bool Readable()
	{
		char buf[1024];
		auto rd = Read(buf, sizeof(buf));
		// log.Print("Conn: read %i\n", (int)rd);
		if (rd > 0)
		{
			rdBuf.Write(buf, rd);
			DoRead();
		}
		else if (rd == 0)
		{
			// Disconnected?
			return false;
		}

		return true;
	}
};

class MapiConnector :
	public LDataEventsI,
	public LThread,
	public LCancel,
	public Context
{
	LArray<int> dirtyProps;
	LArray<ImapConnection*> imapConnections;

public:
	MapiConnector() :
		LThread("MapiConnector")
	{
		log.Print("MapiConnector starting, reading options: %s\n", optionsPath.Get());

		// create the store and log in:
		auto mapiProfile = options.Get(OptMapiProfile);
		auto mapiUser = options.Get(OptMapiUser);

		log.Print("mapiProfile: %s\n", mapiProfile.Get());
		log.Print("mapiUser: %s\n", mapiUser.Get());

		if (!store.Reset(OpenMapiStore(	mapiProfile,
										mapiUser,
										"",
										0,
										this,
										&log)))
		{
			log.Print("Error: alloc failed.\n");
			LCloseApp();
		}
		else
		{
			Run();
		}
	}

	~MapiConnector()
	{
		Cancel();
		WaitForExit();
	}

	void SetContext(const char *file, int line) override
	{
		LAssert(!"impl me");
	}
	
	void Post(LDataStoreI *store, void *Param) override
	{
		LAssert(!"impl me");
	}

	bool GetSystemPath(int Folder, LVariant &Path) override
	{
		LAssert(!"impl me");
		return false;
	}
	
	LOptionsFile *GetOptions(bool Create = false) override
	{
		LAssert(!"impl me");
		return nullptr;
	}
	
	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new, bool filter) override
	{
		LAssert(!"impl me");
	}

	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items) override
	{
		LAssert(!"impl me");
		return false;
	}

	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items) override
	{
		LAssert(!"impl me");
		return false;
	}

	bool OnChange(LArray<LDataI*> &items, int FieldHint) override
	{
		LAssert(!"impl me");
		return false;
	}

	void OnPropChange(LDataStoreI *Store, int Prop, LVariantType Type) override
	{
		dirtyProps.Add(Prop);
	}

	LStreamI *GetLogger(LDataStoreI *store) override
	{
		return &log;
	}

	bool Match(LDataStoreI *store, LDataPropI *Addr, int ObjectType, LArray<LDom*> &Matches) override
	{
		LAssert(!"impl me");
		return false;
	}

	void ProcessDirtyProps()
	{
		for (auto prop: dirtyProps)
		{
			switch (prop)
			{
				case FIELD_MAPI_PROFILES:
				{
					if (auto profiles = store->GetStr(FIELD_MAPI_PROFILES))
					{
						for (auto p: LString(profiles).SplitDelimit(","))
							log.Print("Profile: %s\n", p.Get());
					}
					break;
				}
			}
		}
		dirtyProps.Empty();
	}

	int Main() override
	{
		// COM
		LScriptArguments args(nullptr);
		auto initOk = store->CallMethod("init", args);
		LAssert(initOk);

		// got log in status:
		auto online = store->GetInt(FIELD_IS_ONLINE);
		if (!online)
		{
			log.Print("Error: not online.\n");
			LCloseApp();
			return -1;
		}

		ProcessDirtyProps();

		// start enumerating the folders...
		if (!(root = store->GetRoot()))
		{
			log.Print("Error: no root folder?\n");
			LCloseApp();
			return -1;
		}

		auto folders = FolderList();
		for (auto &f: folders)
		{
			auto indent = LString("    ") * f.depth;
			auto path = LString("/").Join(f.full);

			log.Print("%s%s, depth=%i, children=%i, type=%s\n",
				indent.Get(),
				f.name.Get(),
				f.depth,
				f.subFolders,
				Store3ItemTypeName((Store3ItemTypes)f.folder->GetInt(FIELD_FOLDER_TYPE)));
		}

		// Set up server:
		LSocket imapListen;
		SmtpServer smtp(*this, [this](const SmtpMessage &msg)
		{
			log.Print("SMTP message received: from='%s', rcptCount=%i, bytes=%i\n",
				msg.mailFrom.Get(),
				(int)msg.rcptTo.Length(),
				(int)msg.data.Length());

			// Hook point for real message delivery into MAPI/store.
			return true;
		});

		auto imapPort = options.Get(OptImapPort);
		auto status = imapListen.Listen(imapPort ? (int)imapPort.Int() : IMAP_PORT);
		if (!status)
		{
			log.Print("Error: failed to listen on IMAP port\n");
			LCloseApp();
			return -1;
		}

		log.Print("Starting main loop...\n");
		while (!IsCancelled())
		{
			if (imapListen.IsReadable(50))
			{
				log.Print("Listen: readable...\n");

				if (auto c = new ImapConnection(*this))
				{
					if (imapListen.Accept(c))
					{
						log.Print("Listen: accepted...\n");
						imapConnections.Add(c);
					}
					else log.Print("Error: accept failed\n");
				}
			}
			else if (imapConnections.Length() > 0)
			{
				LSelect select;
				for (auto c: imapConnections)
					select += c;

				auto readable = select.Readable(10);
				if (readable.Length())
				{
					// log.Print("Select: readable %i\n", (int)readable.Length());
					for (auto r: readable)
					{
						if (auto c = dynamic_cast<ImapConnection*>(r))
						{
							if (!c->Readable())
							{
								// clean up closed connection:
								auto del = imapConnections.Delete(c);
								LAssert(del);
								log.Print("Deleting closed connection..\n");
								delete c;
							}
						}
						else
						{
							log.Print("Error: not a valid object?\n");
						}
					}
				}
			}

			LSleep(10);
		}

		log.Print("Main loop finished.\n");
		return 0;
	}
};

int main(int args, const char **arg)
{
	OsAppArguments appArgs(args, arg);
	LApp app(appArgs, appName);
	if (app)
	{
		MapiConnector connector;
		app.Run();
	}

	return 0;
}

