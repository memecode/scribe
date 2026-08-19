#include "lgi/common/Lgi.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"

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
					LAutoPtr<ProtocolSettingStore> store)
{
	AccountId = accountid;
	Host = host;
	Port = port;
	User = user;
	Pass = pass;
	ConnectFlags = flags;
	Log = log;
	Callback = callback;
	ItemProgress = prog[0];
	DataProgress = prog[1];
	SettingStore = store;

	LVariant v;
	if (SettingStore && SettingStore->GetValue(OPT_ReceiveFilterIncoming, v))
	{
		FilterIncoming = v.CastInt32() != 0;
	}

	Thread = new ImapThread(this, caps, log, SettingStore, AccountId);
}

ImapStore::~ImapStore()
{
	FolderLoader.Reset();
	DeleteObj(Thread);
	DeleteObj(Root);
	DeleteArray(Cache);
}

bool ImapStore::OnIdle()
{
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
		return Store3Success;
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
    if (!GetCache())
        return NULL;
       
	if (!Root)
	{
		Root = new ImapFolder(this, "/");

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
	if (!Root)
		return;

	Msgs.Add((ImapMsg*)Param);

	while (Msgs.Length())
	{
		int64 Start = LCurrentTime();
		ImapMsg *m = Msgs[0];
		Msgs.DeleteAt(0, true);

		switch (m->Type)
		{
			default:
				break;
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
			case IMAP_FOLDER_LISTING:
			{
				if (m->Fld.Length() != 1)
				{
					LAssert(!"Not the right folder length.");
					break;
				}

				ImapFolder *f = Root->Find(0, m->Fld[0].Remote);
				if (f)
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
