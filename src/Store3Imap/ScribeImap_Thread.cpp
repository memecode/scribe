#include <stdarg.h>

#include "lgi/common/Lgi.h"
#include "lgi/common/Mail.h"
#include "lgi/common/OpenSSLSocket.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"

#define DEBUG_OUTPUT_FETCHES			0
#define DEBUG_INPUT_FETCHES				0
#define MAX_LISTING_SIZE				50
#define IDLE_MAX						(28 * 60 * 1000)
#define ERROR_RECONNECT_TIMEOUT			(10 * 1000)

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

static char BodyTag[]			= "BODY.PEEK[]"; // If you change this, check DownloadCallback as well
static char MailListingParts[]	= "FLAGS UID RFC822.SIZE BODYSTRUCTURE BODY.PEEK[HEADER]";
static char OtherListingParts[]	= "FLAGS UID";

bool ListingCallback(MailIMap *Imap, uint32_t Msg, MailIMap::StrMap &Parts, void *UserData);

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

	LAutoPtr<ImapMsg> Listing;
	int64 ListingTime;
	int Listings;
	int Exists;
	int FetchSeq;

	ImapThreadPrivate(	ImapThread *thread,
						ImapStore *s,
						LCapabilityClient *caps,
						LStream *log,
						ProtocolSettingStore *store) : LMutex("ImapThreadPrivate")
	{
		Sep[0] = 0;
	    Caps = caps;
	    SettingStore = store;
		FetchSeq = 1;
		Listings = 0;
		Exists = -1;
		Log = log;
		ListingTime = 0;
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

		if (!CurrentFolder || _stricmp(f, CurrentFolder))
		{
			MailIMap::StrMap Local;

			Exists = -1;
			if (Imap->SelectFolder(f, Values ? Values : &Local))
			{
				CurrentFolder = f;
				LString e = (Values ? *Values : Local).Find("Exists");
				if (e)
					Exists = (int) e.Int();
				
				LString s;
				s.Printf("Select(%s) Exists=%i\n", f, Exists);
				Log->Write(s.Get(), s.Length(), LSocketI::SocketMsgInfo);
			}
			else
				return false;
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

	void CollectSubFolders(LHashTbl<StrKey<char,false>,bool> &t, char *Path)
	{
		LDirectory Dir;
		for (int b=Dir.First(Path); b && !IsCancelled(); b=Dir.Next())
		{
			if (Dir.IsDir())
			{
				char p[MAX_PATH_LEN];
				Dir.Path(p, sizeof(p));
				t.Add(p, true);

				CollectSubFolders(t, p);
			}
		}
	}

	// Do folder sync
	void SyncFolders()
	{
		ImapFolder *Root = CastFld(Store->GetRoot(false));
		if (!Root)
			return;

		// Collect all the imap folders...
		LArray<MailImapFolder*> Folders;
		if (Imap->GetFolders(Folders))
		{
			// Collect all the local folders...
			LHashTbl<StrKey<char,false>,bool> Existing;
			CollectSubFolders(Existing, Root->Local);

			LAutoPtr<ImapMsg> NewFolders;
			
			Folders.Sort(FldCmp);

			// For all the imap folders, check they exist locally...
			for (unsigned i=0; i<Folders.Length(); i++)
			{
				MailImapFolder *f = Folders[i];
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
					LAssert(Existing.Find(p));
					Existing.Delete(p);
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

ImapThread::ImapThread(ImapStore *s, LCapabilityClient *caps, LStream *log, ProtocolSettingStore *store) :
	LThread("ImapThread.Thread"),
	LMutex("ImapThread.Mutex")
{
	if ((d = new ImapThreadPrivate(this, s, caps, log, store)))
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

	char *Flags			= Parts.Find("FLAGS");
	char *Uid			= Parts.Find("UID");
	char *Rfc822Size	= Parts.Find("RFC822.SIZE");
	char *BodyStructure	= Parts.Find("BODYSTRUCTURE");
	char *Header		= Parts.Find("BODY[HEADER]");

	#if DEBUG_INPUT_FETCHES
	const char *k;
	for (char *v = Parts.First(&k); v; v = Parts.Next(&k))
	{
		LgiTrace("%s\n", k);
	}
	#endif

	if (!Uid)
	{
		// This happens when the server sends us FETCH lines outside of what we requested
		// Ideally we'd handle them properly, but for the moment, lets just keep trucking.
		return true;
	}

    uint32_t uid = atoi(Uid);
	#ifdef _DEBUG
    for (unsigned n=0; n<m->Mail.Length(); n++)
    {
        if (m->Mail[n].Uid == uid)
        {
            LAssert(!"Duplicate UID.");
            return true;
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
		ImapMailInfo &i = m->Mail.New();

		i.Seq = Msg;
		i.Flags.Set(Flags);
		i.Uid = atoi(Uid);
		i.Structure = BodyStructure;

		if (Header)
		{
			i.Headers = Header;
			LAutoString Date(InetGetHeaderField(Header, "Date", -1));
			if (Date)
				i.Date = Date;
			#if DEBUG_INPUT_FETCHES
			else LgiTrace("%s:%i - No date field? (Uid=%i)\n", _FL, i.Uid);
			#endif
		}
		#if DEBUG_INPUT_FETCHES
		else LgiTrace("%s:%i - No header?\n", _FL);
		#endif

		if (Rfc822Size)
			i.Size = Atoi(Rfc822Size);
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
	MailProtocolProgress *Prog;

public:
	ImapLogSocket(char *file, LCapabilityClient *caps, LStreamI *logger, MailProtocolProgress *prog) : T(logger, caps)
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
	
	ssize_t Read(void *ptr, ssize_t size, int flags)
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

	ssize_t Write(const void *ptr, ssize_t size, int flags)
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

	while (!d->IsCancelled())
	{
		if (d->Imap &&
			!d->Imap->IsOnline())
		{
			// Lost connection... delete and start again
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
				LUri u(d->Store->Host.Str());
				if (u.Port <= 0 &&
					d->Store->Port > 0)
				{
					u.Port = d->Store->Port;
				}
				
				d->Imap->Logger = d->Store->GetLogger();

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
				LSocketI *Sock = 0;
				bool SslDirect = (d->Store->ConnectFlags & MAIL_SSL) != 0;
				bool StartTls = (d->Store->ConnectFlags & MAIL_USE_STARTTLS) != 0;
				if (SslDirect || StartTls)
				{
					ImapLogSocket<SslSocket> *s = new ImapLogSocket<SslSocket>(	LogFileName[0] ? LogFileName : NULL,
																				d->Caps,
																				d->Store->GetLogger(),
																				d->Store->DataProgress);
					Sock = s;
					if (s)
						s->SetSslOnConnect(SslDirect);
				}
				else
				{
					Sock = new ImapLogSocket<LSocket>(	LogFileName[0] ? LogFileName : NULL,
														d->Caps,
														d->Store->GetLogger(),
														d->Store->DataProgress);
				}

				LOAuth2::Params p = GetOAuth2Params(u.sHost, MAGIC_MAIL);
				if (p.Provider != LOAuth2::Params::None)
				{		
					d->Imap->SetOAuthParams(p);
					d->Imap->SetCancel(d);
					d->Imap->SetParentWindow(dynamic_cast<LViewI*>(d->Store->Callback));
				}

				Sock->SetCancel(d);
					
				if (d->Imap->Open(	Sock,
									u.sHost,
									u.Port,
									d->Store->User.Str(),
									d->Store->Pass.Str(),
									d->SettingStore,
									d->Store->ConnectFlags))
				{
					ErrorStart = 0;
					d->CurrentFolder.Empty();
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
				case IMAP_SELECT_FOLDER:
				{
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
							if (d->Imap->Fetch(	true,
												UidRange,
												ListingParts,
												ListingCallback,
												Resp,
												Raw))
							{
								r.Local = Inf.Local;
								r.Remote = Inf.Remote;
								Resp->Last = 1;
								PostStore(Resp.Release());
							}
							else
								d->Error(_FL, "Fetch failed.");
							#endif
						}
					}
					break;
				}
				case IMAP_DOWNLOAD:
				{
					LAssert(m->Parent);
					if (!d->SelectFolder(m->Parent))
						break;

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
								ImapMsg *Msg = new ImapMsg(IMAP_ON_DEL, _FL);
								if (Msg)
								{
									Msg->Error = Err;
									
									ImapMailInfo &i = Msg->Mail.New();
									i.Uid = mi.Uid;
									Msg->Parent = Inf->Parent;
									PostStore(Msg);
								}
							}

							if (p)
								p->StartTransfer(0);
						}
					}
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
					if (d->SelectFolder(Remote, &NewFolderValues) &&
						m.Reset(new ImapMsg(IMAP_ON_NEW, _FL)))
					{
						m->Parent = Remote;
											
						// Add the new messages to 'm' here
						// Not sure of the UIDs so search them
						LString::Array Uids;
						if (d->Imap->Search(true, Uids, "RECENT"))
						{
							d->DownloadIds(GetListingParts(d->CurrentFolder), true, Uids);
						}
						else
						{
							// Reselect the previous folder...
							d->SelectFolder(OldFolder);
						}
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
					for (auto &mi: m->Mail)
					{
						auto Rfc822 = LReadFile(mi.Local);
						LString NewUid;
						if (d->Imap->Append(m->Parent, &mi.Flags, Rfc822, NewUid))
						{
							if (NewUid)
								mi.Uid = (uint32_t)NewUid.Int();
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
    								    mi.Uid = (uint32_t)Uids[0].Int();
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
