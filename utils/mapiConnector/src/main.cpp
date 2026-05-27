#include "lgi/common/Lgi.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Store3.h"
#include "lgi/common/Net.h"
#include "lgi/common/Json.h"
#include "../common/StringClass.h"

const char *appName = "mapiConnector";

struct PrintLog : public LStream
{
	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0) override
	{
		printf("%.*s", (int)Size, (const char*)Ptr);
		return Size;
	}
};

struct FolderMeta
{
	// Storage:
	LString path;
	bool dirty = false;

	// Map the message ID's to UID's
	constexpr static int INVALID = -1;
	int nextUid = 0;
	LHashTbl<ConstStrKey<char>, int> uidMap;

	FolderMeta(LString fullPath) :
		path(fullPath),
		uidMap(0, INVALID)
	{
	}

	~FolderMeta()
	{
		save();
	}

	void save()
	{
		if (dirty)
		{
			if (serialize(true))
				dirty = false;
		}
	}

	bool serialize(bool write)
	{
		LFile f(path, write ? O_WRITE : O_READ);
		if (write)
		{
			for (auto p: uidMap)
				f.Print("uid,%s,%i\n", p.key, p.value);
		}
		else
		{
			uidMap.Empty();
			nextUid = 0;

			if (!f)
				return false;

			auto lines = f.Read().SplitDelimit("\n");
			for (auto &l: lines)
			{
				auto p = l.SplitDelimit(",");
				if (p[0].Equals("uid"))
				{
					if (p.Length() == 3)
					{
						auto uid = (int)p[2].Int();
						uidMap.Add(p[1], uid);
						nextUid = MAX(uid, nextUid);
					}
					else LAssert(!"invalid token count");
				}
			}
		}
		return true;
	}

	int getUid(const char *msgId)
	{
		auto uid = uidMap.Find(msgId);
		if (uid == INVALID)
		{
			uid = ++nextUid;
			uidMap.Add(msgId, uid);
			dirty = true;
		}
		return uid;
	}
};

struct Context
{
	constexpr static const char *OptMapiProfile = "profile";
	constexpr static const char *OptMapiUser = "mapiUser";

	constexpr static const char *OptImapUser = "imapUser";
	constexpr static const char *OptImapPass = "imapPass";
	constexpr static const char *OptImapPort = "imapPort";

	LJson options;
	LString optionsPath;
	const char *sep = ".";
	LAutoPtr<LDataStoreI> store;
	PrintLog log;
	LDataFolderI *root = nullptr;
	LHashTbl<PtrKey<LDataFolderI*>, FolderMeta*> folderMetaData;

	LString fullPath(LDataFolderI *f)
	{
		LString::Array p;
		while (f)
		{
			p.Add(f->GetStr(FIELD_FOLDER_NAME));
			f = dynamic_cast<LDataFolderI*>(f->GetObj(FIELD_PARENT));
		}
		return LString(sep).Join(p.Reverse().Slice(1, -1)) + ".csv";
	}

	FolderMeta *getMeta(LDataFolderI *f)
	{
		if (!f)
			return nullptr;
		auto m = folderMetaData.Find(f);
		if (m)
			return m;

		LFile::Path inst(LSP_APP_INSTALL);
		auto full = fullPath(f);
		LAssert(full);

		m = new FolderMeta((inst / full).GetFull());
		m->serialize(false);
		folderMetaData.Add(f, m);

		return m;
	}

	Context()
	{
		LFile::Path p(LSP_APP_INSTALL);
		optionsPath = p / "options.json";
		if (LFileExists(optionsPath))
			options.SetJson(LReadFile(optionsPath));
		validateOptions();
	}

	~Context()
	{
	}

	bool saveOptions()
	{
		LFile out(optionsPath, O_WRITE);
		if (out)
			out.Write(options.GetJson());
		else
			return false;
		return true;
	}

	bool validateOptions()
	{
		bool modified = false;
		auto chkOpt = [&](const char *name, const char *defVal) {
			if (!options.Get(name))
			{
				options.Set(name, defVal);
				modified = true;
			}
		};

		chkOpt(OptMapiProfile, "Outlook");
		chkOpt(OptMapiUser, "----");

		chkOpt(OptImapUser, "----");
		chkOpt(OptImapPass, "----");
		chkOpt(OptImapPort, LString::Fmt("%i", IMAP_PORT));

		if (modified)
			saveOptions();
		return false;
	}

	LDataFolderI *GetFolder(LString path)
	{
		auto parts = path.SplitDelimit(sep);
		LDataFolderI *f = root;
		for (auto &p: parts)
		{
			// find 'p' in the children of 'f'
			LDataFolderI *match = nullptr;
			auto &it = f->SubFolders();
			if (it.GetState() != Store3Loaded)
				continue;
			for (auto c = it.First(); c; c = it.Next())
			{
				if (auto cFolder = dynamic_cast<LDataFolderI*>(c))
				{
					auto name = cFolder->GetStr(FIELD_FOLDER_NAME);
					if (p.Equals(name))
					{
						match = cFolder;
						break;
					}
				}
			}
			if (match)
				f = match;
			else
				return nullptr;
		}

		return f;
	}

	struct FolderInfo {
		LDataFolderI *folder = nullptr;
		LString name;
		LString::Array full;
		int subFolders = 0;
		int depth = 0;
	};

	void ForAllFolders(LArray<FolderInfo> &info, LDataFolderI *f, LString::Array path, int depth = 0)
	{
		LAssert(path.Length() == depth);

		size_t idx = info.Length();
		{
			auto &i = info.New();
			i.folder = f;
			i.name = f->GetStr(FIELD_FOLDER_NAME);
			i.full = path;
			i.full.Add(i.name);
			i.depth = depth;
		}
		
		auto &it = f->SubFolders();
		if (it.GetState() == Store3Loaded)
			for (auto c = it.First(); c; c = it.Next())
			{
				if (auto cFolder = dynamic_cast<LDataFolderI*>(c))
				{
					auto &i = info[idx];
					ForAllFolders(info, cFolder, i.full, depth + 1);
					i.subFolders++;
				}
			}
	}

	void SegToStructure(LStringPipe &p, LDataPropI *seg, int depth = 0)
	{
		if (!seg)
			return;
		auto children = seg->GetList(FIELD_MIME_SEG);
		bool hasChild = children->Length() > 0;

		if (hasChild || depth == 0)
			p.Print("(");

		for (auto child = children->First(); child; child = children->Next())
		{
			SegToStructure(p, child, depth + 1);
			p.Print(" ");
		}

		auto mimeType = seg->GetStr(FIELD_MIME_TYPE);
		auto mimeParts = LString(mimeType).SplitDelimit("/");
		auto multi = mimeParts[0].Equals("multipart");
		if (multi)
		{
			LString hdrs = seg->GetStr(FIELD_INTERNET_HEADER);
			auto contentType = LGetHeaderField(hdrs, "Content-Type");
			auto boundary = LGetSubField(contentType, "boundary");

			p.Print("\"%s\" (\"boundary\" \"%s\")  NIL NIL NIL", mimeParts[1].Get(), boundary.Get());
		}
		else // single
		{
			p.Print("(\"%s\" \"%s\"", mimeParts[0].Get(), mimeParts[1].Get());

			// figure out what fields to print
			LString::Array fields;
			if (auto charSet = seg->GetStr(FIELD_CHARSET))
				fields.New().Printf("\"charset\" \"%s\"", charSet);
			if (auto name = seg->GetStr(FIELD_NAME))
				fields.New().Printf("\"name\" \"%s\"", name);

			if (fields.Length())
				p.Print(" (%s)", LString(" ").Join(fields).Get());
			else
				p.Print(" NIL");

			if (auto contentId = seg->GetStr(FIELD_CONTENT_ID))
				p.Print(" \"%s\"", contentId);
			else
				p.Print(" NIL");

			p.Print(" NIL"); // Content description

			// FIXME:
			p.Print(" NIL"); // Content-Transfer-Encoding

			auto size = seg->GetInt(FIELD_SIZE);
			p.Print(" " LPrintfSizeT ")", size);
		}

		if (hasChild || depth == 0)
			p.Print(")");
	}

	LString BodyStructure(LDataI *mail)
	{
		LStringPipe p;
		
		if (auto root = mail->GetObj(FIELD_MIME_SEG))
			SegToStructure(p, root);
		else
			LAssert(!"no seg?");

		return p.NewLStr();
	}

	LArray<FolderInfo> FolderList()
	{
		LArray<FolderInfo> a;
		LString::Array full;
		if (root)
			ForAllFolders(a, root, full);
		return a;
	}

	LString::Array Fetch(LDataFolderI *folder, bool isUid, LString arg, LString fieldSpec)
	{
		auto argRange = arg.SplitDelimit(":");
		LString::Array a;
		if (!folder)
		{
			log.Print("%s:%i - error: no folder.\n", _FL);
			return a;
		}

		auto meta = getMeta(folder);
		if (!meta)
		{
			log.Print("%s:%i - error: no meta.\n", _FL);
			return a;
		}

		LAssert(isUid); // don't support not UID yet...

		auto fields = fieldSpec.Strip("()").SplitDelimit();
		auto &it = folder->Children();
		for (auto i = it.First(); i; i = it.Next())
		{
			if (i->Type() != MAGIC_MAIL)
				continue;

			if (auto msgId = i->GetStr(FIELD_MESSAGE_ID))
			{
				auto uid = meta->getUid(msgId);
				if (uid != FolderMeta::INVALID)
				{
					// is the UID in range?
					bool match = false;
					if (argRange.Length() == 2)
					{
						match = (argRange[0].Equals("*") || uid >= argRange[0].Int())
								&&
								(argRange[1].Equals("*") || uid <= argRange[1].Int());
					}
					else if (argRange.Length() == 1)
					{
						match = argRange[0].Int() == uid;
					}
					else LAssert(!"unexpected arg range count");

					if (!match)
						continue;

					// Create a result record...
					LStringPipe record;
					record.Print("* %i FETCH (", uid);

					int idx = 0;
					for (auto &fld: fields)
					{
						auto space = idx++ ? " " : "";
						if (fld.Equals("FLAGS"))
						{
							auto flags = i->GetInt(FIELD_FLAGS);
							LString::Array imapFlags;
							if (flags & MAIL_READ)
								imapFlags.Add("/Seen");
							record.Print("%sFLAGS (%s)", space, LString(" ").Join(imapFlags).Get());
						}
						else if (fld.Equals("UID"))
						{
							record.Print("%sUID %i", space, uid);
						}
						else if (fld.Equals("RFC822.SIZE"))
						{
							auto sz = i->GetInt(FIELD_SIZE);
							record.Print("%sRFC822.SIZE " LPrintfInt64, space, sz);
						}
						else if (fld.Equals("BODYSTRUCTURE"))
						{
							// FIXME
							record.Print("%sBODYSTRUCTURE %s", space, BodyStructure(i).Get());
						}
						else if (fld.Equals("BODY.PEEK[HEADER]"))
						{
							auto inetHdr = i->GetStr(FIELD_INTERNET_HEADER);
							auto len = Strlen(inetHdr);
							record.Print("%sBODY.PEEK[HEADER] {" LPrintfInt64 "}\r\n%s", space, len, inetHdr);
						}
						else if (fld.Equals("BODY.PEEK[]"))
						{
							if (auto rfc822 = i->GetStream(_FL))
							{
								auto len = rfc822->GetSize();
								record.Print("%sBODY[] {" LPrintfInt64 "}\r\n", space, len);
								LCopyStreamer copy;
								copy.Copy(rfc822, &record);
							}
							else
								LAssert(0);
						}
						else
						{
							LAssert(!"not implemented");
						}
					}

					record.Print(")\r\n");
					a.Add(record.NewLStr());
				}
			}
		}

		meta->save();

		return a;
	}

	LString::Array Search(LDataFolderI *folder, bool isUid, LArray<LString> params)
	{
		LString::Array a;
		if (!folder)
		{
			log.Print("%s:%i - error: no folder.\n", _FL);
			return a;
		}

		auto meta = getMeta(folder);
		if (!meta)
		{
			log.Print("%s:%i - error: no meta.\n", _FL);
			return a;
		}

		LAssert(isUid); // don't support not UID yet...

		auto &it = folder->Children();
		for (auto i = it.First(); i; i = it.Next())
		{
			if (i->Type() != MAGIC_MAIL)
				continue;

			if (auto msgId = i->GetStr(FIELD_MESSAGE_ID))
			{
				auto uid = meta->getUid(msgId);
				if (uid != FolderMeta::INVALID)
					continue;
			
				// Does 'i' match the search params?
				bool match = false;
				for (auto &p: params)
				{
					if (p.Equals("RECENT"))
					{
						auto flags = i->GetInt(FIELD_FLAGS);
						if (!(flags & MAIL_READ))
						{
							match = true;
						}
					}
					else
					{
						LAssert(!"Impl support for field");
					}
				}

				if (match)
					a.New().Printf("* SEARCH %i\r\n", uid);
			}
		}

		return a;
	}
};

struct ImapConnection : public LSocket
{
	Context *ctx;
	LStream &log;
	LStringPipe rdBuf;

	LString cmdRef;
	LString AuthenticateType;
	LString selectPath;
	LDataFolderI *selectFolder = nullptr;

	ImapConnection(Context *c) :
		ctx(c),
		log(c->log),
		rdBuf(8 << 10)
	{
	}

	~ImapConnection()
	{
		log.Print("%s:%i - delete connect.\n", _FL);
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

				auto optUser = ctx->options.Get(Context::OptImapUser);
				auto optPass = ctx->options.Get(Context::OptImapPass);
				auto authOk = optUser == user && optPass == pass;
				if (!authOk)
					return ImapErr("AUTHENTICATIONFAILED", "Authentication failed");

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
			if (parts.Length() < 2)
			{
				log.Print("Error: unexpected part count for '%s'\n", line.Strip().Get());
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
					log.Print("Error: write failed..\n");
			}
			else if (cmd.Equals("AUTHENTICATE"))
			{
				AuthenticateType = parts[2];
				Write("+\r\n");
			}
			else if (cmd.Equals("LIST"))
			{
				auto flds = ctx->FolderList();
				for (auto &f: flds)
				{
					if (f.depth == 0)
						continue;

					auto imapPath = LString(ctx->sep).Join(f.full.Slice(1, -1));
					LAssert(imapPath);
					auto w = LString::Fmt("* LIST (%s) \"%s\" %s\r\n",
						f.subFolders ? "\\HasChildren" : "\\HasNoChildren",
						ctx->sep,
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
				if (selectFolder = ctx->GetFolder(selectPath))
				{
					// figure out the children count...
					auto &children = selectFolder->Children();
					Write(LString::Fmt("* " LPrintfInt64 " EXISTS\r\n", children.Length()));
					Write(LString::Fmt("* " LPrintfInt64 " RECENT\r\n", selectFolder->GetInt(FIELD_UNREAD)));
					Write(LString::Fmt("%s OK [READ-WRITE] Select completed\r\n", cmdRef.Get()));
					log.Print("Selected '%s'...\n", selectPath.Get());
				}
				else return ImapErr("UNAVAILABLE", "path doesn't exist");
			}
			else if (cmd.Equals("FETCH"))
			{
				auto pos = line.Find("(");
				if (pos <= 0)
				{
					log.Print("%s:%i - Error: unexpected FETCH arg.\n", _FL);
					return ImapErr("SERVERBUG", "unexpected fetch arg");
				}
				
				auto arg = parts[cmdIdx++];
				auto fields = line(pos, -1);
				if (!selectFolder)
					return ImapErr("UNAVAILABLE", "no folder selected");
				if (!arg)
					return ImapErr("UNAVAILABLE", "missing argument");
				
				auto resp = ctx->Fetch(selectFolder, isUid, arg, fields);
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

				auto resp = ctx->Search(selectFolder, isUid, parts.Slice(3, -1));
				for (auto &r: resp)
				{
					log.Print("Search: %s\n", r.Get());
					Write(r);
				}
				Write(LString::Fmt("%s OK Search completed\r\n", cmdRef.Get()));
			}
			else if (cmd.Equals("IDLE"))
			{
				Write(LString::Fmt("%s OK idle\r\n", cmdRef.Get()));
			}
			else
			{
				log.Print("UnknownImapCmd: %s\n", line.Strip().Get());
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
	LArray<ImapConnection*> connections;

public:
	MapiConnector() :
		LThread("MapiConnector")
	{
		log.Print("MapiConnector starting...\n");

		// create the store and log in:
		auto mapiProfile = options.Get(OptMapiProfile);
		auto mapiUser = options.Get(OptMapiUser);

		if (!store.Reset(OpenMapiStore(	mapiProfile,
										mapiUser,
										"",
										0,
										this)))
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
		LSocket listen;
		auto serverPort = options.Get(OptImapPort);
		auto status = listen.Listen(serverPort ? (int)serverPort.Int() : IMAP_PORT);
		if (!status)
		{
			log.Print("Error: failed to listen on IMAP port\n");
			LCloseApp();
			return -1;
		}

		log.Print("Starting main loop...\n");
		while (!IsCancelled())
		{
			if (listen.IsReadable(50))
			{
				log.Print("Listen: readable...\n");

				if (auto c = new ImapConnection(this))
				{
					if (listen.Accept(c))
					{
						log.Print("Listen: accepted...\n");
						connections.Add(c);
					}
					else log.Print("Error: accept failed\n");
				}
			}
			else if (connections.Length() > 0)
			{
				LSelect select;
				for (auto c: connections)
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
								auto del = connections.Delete(c);
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

