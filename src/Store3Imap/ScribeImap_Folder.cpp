#include "lgi/common/Lgi.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"
#include "DomType.h"

#if IMAP_PROTOBUF
	#include <fstream> 
#endif

#ifdef _DEBUG
#define DEBUG_RENAME_OLD_XML		1
#else
#define DEBUG_RENAME_OLD_XML		0
#endif
#define TIMEOUT_IDLE_STATUS			(60 * 1000)

ImapFolder::ImapFolder(ImapStore *store, const char *path)
	#if !IMAP_PROTOBUF
	: Meta(TAG_FOLDER)
	#endif
{
	LastIdleSelect = LCurrentTime();
	Store = store;
	Remote = path;

	#if IMAP_PROTOBUF
		ThreadView = false;
		PermRead = PermRequireNone;
		PermWrite = PermRequireNone;
		SortField = 0;
		Expanded = false;
		UnreadCount = 0;
	#endif
	
	if (Remote)
	{
		if (!_stricmp(Remote, "/"))
			ItemType = MAGIC_NONE;
		else if (!_stricmp(Remote, "/Trash") ||
				 !_stricmp(Remote, "/Deleted Items") ||
				 !_stricmp(Remote, "/Deleted Messages"))
		{
			ItemType = MAGIC_ANY;
			System = Store3SystemTrash;
		}

		char s[MAX_PATH_LEN] = "";
		LMakePath(s, sizeof(s), Store->GetCache(), Remote);
		Local = s;

		char *Last = strrchr(Remote, '/');
		if (Last++)
		{
			if (!_stricmp(Last, "Inbox"))
				System = Store3SystemInbox;
			else if (!_stricmp(Last, "Sent"))
				System = Store3SystemSent;
			else if (!_stricmp(Last, "Outbox"))
				System = Store3SystemOutbox;
		}

		Serialize(false);
	}
}

ImapFolder::~ImapFolder()
{
	LoadThread.Reset();
	Sub.DeleteObjects();
	Field.DeleteObjects();
	Mail.DeleteObjects();
	UidMap.DeleteObjects();
}

void ImapFolder::SetParent(ImapFolder *p)
{
	if (_Parent)
	{
		if (_Parent->Sub.State != Store3Loaded)
			LgiTrace("%s:%i - %p->Parent(%p) Sub not loaded :(\n", _FL, this, _Parent);
		else if (_Parent->Sub.IndexOf(this) < 0)
			LgiTrace("%s:%i - Folder(%p) not in parent(%p) Sub :(\n", _FL, this, _Parent);
		else
			_Parent->Sub.Delete(this);
	}

	_Parent = p;

	if (_Parent)
	{
		LAssert(_Parent->Sub.State != Store3Unloaded);
		_Parent->Sub.Insert(this, -1, true);
	}
}

void ImapFolder::SetDirty(bool b)
{
	Dirty = b;
	if (Dirty && Mail.State == Store3Unloaded)
	{
		LAssert(!"State needs to be loaded before we modify it.");
	}
}

struct ImapFolderWriterThread : public LThread
{
	LString File;
	LString Data;
	
	ImapFolderWriterThread(const char *file, LString data) : LThread("WriterThread")
	{
		File = file;
		Data = data;
		Run();
	}
	
	~ImapFolderWriterThread()
	{
		uint64 Start = LCurrentTime();
		while (!IsExited())
		{
			LSleep(20);
			uint64 Now = LCurrentTime();
			if (Now - Start > 1000)
			{
				LgiTrace("ImapFolder::WriterThread shutting down: %ims\n", (int)(Now-Start));
			}
		}
	}
	
	int Main()
	{
		#if DEBUG_RENAME_OLD_XML
		if (LFileExists(File))
		{
			// Don't overwrite... just rename
			auto parts = LString(File).RSplit(DIR_STR, 1);
			auto leaf = parts.Last().RSplit(".", 1);
			auto now = LDateTime::Now();
			auto newleaf = leaf[0] + " " + now.Get().Replace(":","-").Replace("/","-") + "." + leaf[1];
			auto dstFile = parts[0] + DIR_STR + newleaf;
			LError err;
			if (!FileDev->Move(File, dstFile, &err))
				LgiTrace("%s:%i - Failed to rename '%s' -> '%s': %s\n", _FL,
					File.Get(), dstFile.Get(), err.GetMsg().Get());
		}
		#endif

		LFile f;
		if (f.Open(File, O_WRITE))
		{
			f.SetSize(0);
			ssize_t Wr = f.Write(Data, Data.Length());
			f.Close();
			if (Wr != Data.Length())
			{
				LAssert(!"Failed to write all the XML");
				LgiTrace("%s:%i - ImapFolder::WriterThread wrote %i of %i\n",
					_FL,
					(int)Wr,
					(int)Data.Length());
			}
		}
		return 0;		
	}
};

bool ImapFolder::Serialize(bool Write)
{
    bool Debug = false; // System == Store3SystemInbox;
	if (!ValidStr(Local))
	    return false;

	LFile::Path XmlPath(Local);
	XmlPath += FOLDER_META_NAME;
	LXmlTree t;
	LFile f;

	#if IMAP_PROTOBUF

		bool Status = false;
		if (Write)
		{
			LProfile p("ImapFolderWrite");		
			scribe::Folder folder;

			p.Add("Meta");
			folder.set_readperm(PermRead);
			folder.set_writeperm(PermWrite);
			folder.set_sortfield(SortField);
			folder.set_itemtype(ItemType);
			folder.set_expanded(Expanded);
			folder.set_threadview(ThreadView);

			p.Add("Fields");
			for (auto in: Field.a)
			{
				auto out = folder.add_fields();
				out->set_id(in->GetInt(FIELD_ID));
				out->set_width(in->GetInt(FIELD_WIDTH));
			}

			p.Add("Email");
			for (auto in: Mail.a)
			{
				try
				{
					#if 1
					auto m = in->GetMeta();
					if (m)
					{
						LAssert(m->IsInitialized());
						folder.mutable_mail()->Add(std::move(*m));
					}
					#else
					auto out = folder.add_mail();
					if (in->Uid)
						out->set_uid(in->Uid);
					out->set_remoteflags(in->RemoteFlags.All);
					out->set_localflags(in->LocalFlags);
					if (in->DateSent.IsValid())
						out->set_datesent(in->DateSent.Ts());
					if (in->DateReceived.IsValid())
						out->set_datereceived(in->DateReceived.Ts());
					if (in->Label)
						out->set_label(in->Label);
					if (in->Subject)
						out->set_subject(in->Subject);
					out->set_size(in->DataSize);
					if (in->Path)
						out->set_filename(in->Path);
					out->set_priority(in->Priority);
					if (in->Structure)
						out->set_structure(in->Structure);

					if (in->From.Addr || in->From.Name)
					{
						auto a = new scribe::Address();
						if (in->From.Addr)
							a->set_email(in->From.Addr.Get());
						if (in->From.Name)
							a->set_name(in->From.Name.Get());
						out->set_allocated_from(a);
					}

					for (auto to: in->To.a)
					{
						if (to->Addr || to->Name)
						{
							auto a = out->add_to();
							if (to->Addr)
								a->set_email(to->Addr.Get());
							if (to->Name)
								a->set_name(to->Name.Get());
						}
					}
			
					if (in->Colour.IsValid())
						out->set_colour(in->Colour.c32());
					LAssert(out->IsInitialized());
					#endif
				}
				catch (...)
				{
					LAssert(0);
				}
			}

			p.Add("FileWrite");
			{
				LAssert(folder.IsInitialized());

				std::ofstream file(s, std::ios_base::binary);
				if (Status = folder.SerializeToOstream(&file))
					Dirty = false;
			}

			#if 0
			{
				scribe::Folder f;
				std::ifstream file(s, std::ios_base::binary);
				bool r = f.ParseFromIstream(&file);
				LAssert(r);
			}
			#endif
		}
		else
		{
			Field.DeleteObjects();
			Mail.DeleteObjects();

			scribe::Folder folder;
			std::ifstream file(s, std::ios_base::binary);
			if (folder.ParseFromIstream(&file))
			{
				// Meta
				PermRead = (ScribePerm)folder.readperm();
				PermWrite = (ScribePerm)folder.writeperm();
				SortField = folder.sortfield();
				ItemType = folder.itemtype();
				Expanded = folder.expanded();
				ThreadView = folder.threadview();

				// Fields:
				for (int i=0; i<folder.fields_size(); i++)
				{
					auto in = folder.fields(i);
					ImapFolderFld *out = new ImapFolderFld(GetStore(), this, in.id(), in.width());
					if (out)
						Field.Insert(out, -1, true);
				}
				Field.State = Store3Loaded;

				// Email:
				for (int i=0; i<folder.mail_size(); i++)
				{
					auto in = folder.mail(i);
					ImapMail *out = new ImapMail(Store, in.uid());
					if (out)
					{
						out->RemoteFlags.All = in.remoteflags();
						out->LocalFlags = in.localflags();
						out->DateSent.Set(in.datesent());
						out->DateReceived.Set(in.datereceived());
						out->Label = in.label().c_str();
						out->Subject = in.subject().c_str();
						out->DataSize = in.size();
						out->Path = in.filename().c_str();
						out->Priority = in.priority();
						out->Structure = in.structure().c_str();

						out->Parent = this;
						Mail.Insert(out, -1, true);
						// if (out->Uid) UidMap.Add(out->Uid, in);
					}
				}
				if (Mail.a.Length())
					Mail.State = Store3Loaded;
				Status = true;
			}

			if (Field.Length() == 0)
			{
				// Set the default fields.
				Field.State = Store3Loaded;
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_PRIORITY, 10));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_FLAGS, 10));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_FROM, 120));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_SUBJECT, 200));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_SIZE, 46));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_DATE_SENT, 120));

				#if 0 // def _DEBUG
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_CACHE_FLAGS, 100));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_CACHE_FILENAME, 140));
				#endif

				Field.State = Store3Loaded;
				SetInt(FIELD_SORT, -6);
				SetDirty();
			}
		}

		return Status;

	#else

		if (Write)
		{
			auto e = Meta.GetChildTag(TAG_FIELDS, true);
			uint64 t1;
			if (e)
			{
				e->EmptyChildren();
				t1 = LCurrentTime();
				for (unsigned i=0; i<Field.Length(); i++)
				{
					ImapFolderFld *f1 = Field.a[i];
					e->InsertTag(new LXmlTag(*f1));
				}
			}
			else
			{
				LAssert(0);
				return false;
			}

			if (Mail.State != Store3Loaded)
			{
				LAssert(!"Not loaded? We can't overwrite the valid XML with invalid...");
				return false;
			}

			if ((e = Meta.GetChildTag(TAG_EMAILS, true)))
			{
				for (auto it : UidMap)
					e->InsertTag(it.value);

				#if 0 // def _DEBUG
				if (!Stricmp(Local.Get(), "C:\\Users\\Matthew\\AppData\\Roaming\\Scribe\\ImapCache\\1045946855\\INBOX"))
				{
					e->Children.Sort([](auto a, auto b)
						{
							auto idA = (*a)->GetAttr("Uid");
							auto idB = (*b)->GetAttr("Uid");
							return (int) (Atoi(idA) - Atoi(idB));
						});
					while (e->Children.Length())
					{
						auto l = e->Children.Last();
						if (Atoi(l->GetAttr("Uid")) > 549)
							e->Children.Delete(l);
						else
							break;
					}
				}
				#endif

				if (Debug)
					LgiTrace("%s:%i - Meta has " LPrintfSizeT " email.\n", _FL, e->Children.Length());
			}

			LStringPipe Buf(16<<10);
			if (!t.Write(&Meta, &Buf))
			{
				LAssert(0);
				return false;
			}

			if (e)
			{
				e->Children.Length(0);
			}

			if (Debug)
				LgiTrace("%s:%i - Buf has %s.\n", _FL, LFormatSize(Buf.GetSize()).Get());

			auto Xml = Buf.NewLStr();
			if (WriteThread.Reset(new ImapFolderWriterThread(XmlPath, Xml)))
			{
				if (e)
					e->Children.Empty();

				Dirty = false;
			}
			else
			{
				LAssert(0);
				return false;
			}
		}
		else // read
		{
			Field.DeleteObjects();
		
			if (LFileExists(XmlPath))
			{
				LFile f;
				if (f.Open(XmlPath, O_READ) &&
					t.Read(&Meta, &f, 0))
				{
					if (Debug)
						LgiTrace("%s:%i - In file has %s.\n", _FL, LFormatSize(f.GetSize()).Get());

					// Fields
					LXmlTag *t = Meta.GetChildTag(TAG_FIELDS);
					if (t && t->Children.Length() > 0)
					{
						for (auto c: t->Children)
						{
							int Id = c->GetAsInt("Id");
							int Width = c->GetAsInt("Width");
							ImapFolderFld *ff = new ImapFolderFld(GetStore(), this, Id, Width);
							if (ff)
							{
								Field.Insert(ff, -1, true);
							}
						}
					}
					Field.State = Store3Loaded;

					// Email
					if ((t = Meta.GetChildTag(TAG_EMAILS)))
					{
						// Move all the tags into the Sid hash table
						for (auto c: t->Children)
						{
							auto Uid = c->GetAsInt(ATTR_UID);
							if (!Uid)
							{
								LAssert(!"Not a valid email tag.");
								DeleteObj(c);
							}
							else
							{
								UidMap.Add(Uid, c);
							}
						}
						if (Debug)
							LgiTrace("%s:%i - Meta read " LPrintfSizeT " email.\n", _FL, UidMap.Length());

						t->Children.Empty();
					}
				}
			}

			if (Field.Length() == 0)
			{
				// Set the default fields.
				Field.State = Store3Loaded;
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_PRIORITY, 10));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_FLAGS, 10));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_FROM, 120));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_SUBJECT, 200));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_SIZE, 46));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_DATE_SENT, 120));

				#if 0 // def _DEBUG
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_CACHE_FLAGS, 100));
				Field.Insert(new ImapFolderFld(GetStore(), this, FIELD_CACHE_FILENAME, 140));
				#endif

				SetInt(FIELD_SORT, -6);
				SetDirty();
			}
		}

		return true;
	
	#endif
}

Store3Status ImapFolder::Move(LArray<LDataI*> &Items)
{
	Store3Status Status = Store3Error;

	LHashTbl<IntKey<uint32_t>, bool> Has;
	LArray<LDataI*> Moved;
	LAutoPtr<ImapMsg> Mv(new ImapMsg(IMAP_MOVE_EMAIL, _FL));
	for (unsigned n=0; n<Items.Length(); n++)
	{
		if (Items[n]->Type() == MAGIC_MAIL)
		{
			ImapMail *m = CastMail(Items[n]);
			if (m)
			{
			    if (Has.Find(m->Uid))
			    {
			        LAssert(!"Duplicate UID.");
			        continue;
			    }
			    Has.Add(m->Uid, true);
			    
				if (m->Parent)
				{
					if (!m->RemoteFlags.ImapDeleted)
					{
						if (!Mv->Parent)
							Mv->Parent = m->Parent->Remote.Get();
						
						if (_stricmp(Mv->Parent, m->Parent->Remote) ||
							Mv->Mail.Length() >= IMAP_BLOCK_SIZE)
						{
							Store->PostThread(Mv.Release(), false);
							Mv.Reset(new ImapMsg(IMAP_MOVE_EMAIL, _FL));
							Mv->Parent = m->Parent->Remote.Get();
						}

						ImapMailInfo &Inf = Mv->Mail.New();
						Inf.Uid = m->Uid;
						if (!Mv->NewRemote)
							Mv->NewRemote = Remote.Get();

						m->RemoteFlags.ImapDeleted = true; // It'll be set soon anyway...
						m->SetState(ImapMail::ImapMailMoving, _FL);
						Moved.Add(m);
						Status = Store3Delayed;
					}
					else
						LgiTrace("%s:%i - Can't move deleted email '%i'\n", _FL, m->Uid);
				}
				else LAssert(!"No parent.");
			}
			else LAssert(!"Not an imap mail.");
		}
		else if (Items[n]->Type() == MAGIC_FOLDER)
		{
			/*
			ImapFolder *f = dynamic_cast<ImapFolder*>(Items[n]);
			if (f)
			{
				if (f->Parent)
				{
					char p[MAX_PATH_LEN], *Leaf = strrchr(Local, DIR_CHAR);
					if (Leaf)
					{
						Leaf++;

						LMakePath(p, sizeof(p), Local, Leaf);
						if (FileDev->Move(f->Remote, p))
						{
							// Adjust path
							f->Remote.Reset(NewStr(p));

							// Adjust containers and pointers
							LAssert(f->Parent->Sub.IndexOf(f) >= 0);
							f->Parent->Sub.Delete(f);
							f->Parent = this;
							LAssert(Sub.IndexOf(f) < 0);
							Sub.Insert(f);

							Status = true;

							// Adjust all the path's of the email in the ImapFolder
							for (int i=0; i<f->Mail.Length(); i++)
							{
								ImapMail *m = dynamic_cast<ImapMail*>(f->Mail[i]);
								if (m)
								{
									char *MailLeaf = strchr(m->Path, DIR_CHAR);
									if (MailLeaf)
									{
										char mpath[MAX_PATH_LEN];
										LMakePath(mpath, sizeof(mpath), f->Remote, MailLeaf+1);
										m->Path.Reset(NewStr(mpath));
									}
									else LAssert(!"Not a valid path.");
								}
							}

							Status = true;
						}
					}
					else LAssert(!"Not a valid path.");
				}
				else LAssert(!"No parent.");
			}
			else LAssert(!"Not a valid imap folder.");
			*/
		}
		else LAssert(!"Not a valid object type.");
	}

	if (Mv->Parent &&
		Mv->Mail.Length())
	{
		Store->PostThread(Mv.Release(), false);
		Status = Store3Delayed;
	}
	
	/* This should be handled by the OnNew callbacks
	if (NewUnread)
	{
	    SetInt(FIELD_UNREAD, GetInt(FIELD_UNREAD) + NewUnread);
	    
	    LArray<LDataI*> a;
	    a.Add(this);
	  
		Store->OnChange(_FL, a, FIELD_UNREAD);
	}
	*/
	
	if (Moved.Length())
	{
		// This allows the UI to display them as "Moving..."
		Store->OnChange(_FL, Moved, 0);
	}

	return Status;
}

/*
Store3CopyImpl(ImapFolder)
{
	ImapFolder *f = CastFld(&p);
	if (f)
	{
		SetStr(FIELD_FOLDER_NAME, f->GetStr(FIELD_FOLDER_NAME));

		SetInt(FIELD_FOLDER_THREAD, f->GetInt(FIELD_FOLDER_THREAD));
		SetInt(FIELD_FOLDER_PERM_READ, f->GetInt(FIELD_FOLDER_PERM_READ));
		SetInt(FIELD_FOLDER_PERM_WRITE, f->GetInt(FIELD_FOLDER_PERM_WRITE));
		SetInt(FIELD_SORT, f->GetInt(FIELD_SORT));
		SetInt(FIELD_FOLDER_OPEN, f->GetInt(FIELD_FOLDER_OPEN));
		SetInt(FIELD_UNREAD, f->GetInt(FIELD_UNREAD));
		SetInt(FIELD_FOLDER_TYPE, f->GetInt(FIELD_FOLDER_TYPE));
	}

	return true;
}
*/

Store3CopyImpl(ImapFolder)
{
	SetStr(FIELD_FOLDER_NAME, p.GetStr(FIELD_FOLDER_NAME));

	SetInt(FIELD_SORT, p.GetInt(FIELD_SORT));
	SetInt(FIELD_FOLDER_TYPE, p.GetInt(FIELD_FOLDER_TYPE));
	SetInt(FIELD_UNREAD, p.GetInt(FIELD_UNREAD));
	SetInt(FIELD_FOLDER_INDEX, p.GetInt(FIELD_FOLDER_INDEX));
	SetInt(FIELD_FOLDER_OPEN, p.GetInt(FIELD_FOLDER_OPEN));
	SetInt(FIELD_FOLDER_THREAD, p.GetInt(FIELD_FOLDER_THREAD));
	SetInt(FIELD_FOLDER_PERM_READ, p.GetInt(FIELD_FOLDER_PERM_READ));
	SetInt(FIELD_FOLDER_PERM_WRITE, p.GetInt(FIELD_FOLDER_PERM_WRITE));

	Field.DeleteObjects();
	ImapFolder *Folder = dynamic_cast<ImapFolder*>(&p);
	if (Folder)
	{
		LDataIterator<LDataPropI*> &it = Folder->Fields();
		Folder->Field.State = Store3Loaded;
		
		for (LDataPropI *f = it.First(); f; f = it.Next())
		{
			Field.Insert(new ImapFolderFld(	GetStore(),
											this,
											(int)f->GetInt(FIELD_ID),
											(int)f->GetInt(FIELD_WIDTH)));
		}
	}

	return true;
}

LString ImapFolder::MakeImapPath(char *From)
{
	LString Ret;

	if (From)
	{
		size_t CacheLen = strlen(Store->GetCache());
		size_t FromLen = strlen(From);
		if (CacheLen >= FromLen)
		{
			LAssert(!"From path too short");
		}
		else
		{
			Ret = From + CacheLen;
			Ret = Ret.Replace("\\", "/");
		}
	}

	return Ret;
}

uint64 ImapFolder::Size()
{
	return 0;
}

void ImapFolder::ScanFolder()
{
	if (ScanState == 0)
	{
		ScanState = 1;
		LDirectory d;
		for (int b = d.First(Local); b; b = d.Next())
		{
			if (d.IsDir())
				Folders.New() = d.GetName();
		}
	}
}

void ImapFolder::LoadSub(bool All)
{
	if (Sub.State == Store3Unloaded)
	{
		Sub.State = Store3Loading;
		
		// Look at the local folders...
		ScanFolder();

		for (unsigned i=0; i<Folders.Length(); i++)
		{
			char s[MAX_PATH_LEN];
			LMakePath(s, sizeof(s), Local, Folders[i]);

			ImapFolder *Fld = Find(s, 0);
			if (!Fld)
			{
				// Load a folder
				LString i = MakeImapPath(s);
				if (i)
				{
					Fld = new ImapFolder(Store, i);
					if (Fld)
					{
						Fld->SetParent(this);
					}
					else LgiTrace("%s:%i - alloc err\n", _FL);
				}
				else LgiTrace("%s:%i - '%s' can't make path\n", _FL, s);
			}

			if (All)
				Fld->LoadSub(true);
		}

		// uint64 End = LCurrentTime();
		// LgiTrace("LoadSub '%s' Time: %i, Items: %i\n", Remote.Get(), (int)(End-Start), Items);
		Sub.State = Store3Loaded;
	}
}

struct ImapFolderLoadThread :
	public LThread,
	public ImapFolderData
{
	ImapFolder *f;
	LString Local, Remote;
	LArray<ImapMail*> PostDel;

	ImapFolderLoadThread(ImapFolder *folder) :
		LThread("ImapFolderLoadThread"),
		f(folder)
	{
		// This object will own the 'UidMap' and 'Mail' containers for it's life time
		// Returning the ownership to the actual folder is in 'ImapFolder::OnLoadMail'
		Swap(*f);

		Local = f->Local.Get();
		Remote = f->Remote.Get();

		Run();
	}

	void LoadMail()
	{
	}

	void SetDirty(bool b = true)
	{
	}

	int Main()
	{
		// auto StartTs = LCurrentTime();

		// In same cases like an "OnNew" event mail can show up in a folder
		// before we load it. That is allowed but we have to de-duplicate it
		// here, so create a hash table of existing entries and don't create
		// new ImapMail objects for them in the lower loop
		LHashTbl<IntKey<uint32_t>, ImapMail*> Map;
		for (auto m: Mail.a)
		{
			LAssert(m->Uid != 0);
			if (m->Uid)
				Map.Add(m->Uid, m);
		}

		auto startTs = LCurrentTime();
		ssize_t pos = 0;
		for (auto it: UidMap)
		{
			#if IMAP_PROTOBUF
				LAssert(0);
			#else
				auto ServerId = it.value->GetAsInt(ATTR_UID);

				if (ServerId && !it.value->GetContent() && Local)
				{
					char p[MAX_PATH_LEN], leaf[32];
			
					sprintf_s(leaf, sizeof(leaf), "%i.eml", ServerId);
					LMakePath(p, sizeof(p), Local, leaf);

					// Files may not exist if they haven't downloaded yet...
					it.value->SetContent(leaf);
				}

				if (it.value->GetContent() && ServerId)
				{
					// Check the format of the msgid, old versions of Scribe used to
					// leave new-lines in the text... this fixes that on the fly.
					auto MsgId = it.value->GetAttr(ATTR_MSGID);
					if (MsgId && strchr(MsgId, '\n'))
					{
						LAutoString a(TrimStr(MsgId, " \t\r\n"));
						it.value->SetAttr(ATTR_MSGID, a);
						f->SetDirty();
					}

					auto Flags = it.value->GetAttr(ATTR_FLAGS);
					if (Flags)
					{
						ImapMailFlags f(Flags);
						if (f.ImapDeleted)
							continue;
					}
		    
					// Load an email from the cache file if it's not already in the
					// Mail array. Check there is no existing object:
					auto m = Map.Find(ServerId);
					if (!m)
					{
						// No? Well create one:
						m = new ImapMail(f->Store, _FL, ServerId);
						if (m)
						{
							m->Data = this;
							m->Parent = f;
							if (!AddMail(m, NULL))
							{
								DeleteObj(m);
							}
						}
						else if (m->RemoteFlags.ImapDeleted)
						{
							PostDel.Add(m);
						}
					}
				}
				else LAssert(!"Invalid tag...");
			#endif

			pos++;
			auto now = LCurrentTime();
			if (now - startTs >= 500)
			{
				startTs = now;
				if (f->Mail.Prog)
					f->Mail.Prog(pos, UidMap.Length());
			}
		}

		for (auto del: PostDel)
			DelMail(del);

		// LgiTrace("ImapFolderLoadThread::Main(%s): " LPrintfInt64 "ms.\n",
		// 	Remote.Get(), LCurrentTime() - StartTs);

		auto Store = f->GetStore();
		LAutoPtr<ImapMsg> msg(new ImapMsg(IMAP_LOAD_FOLDER, _FL));
		auto &fi = msg->Fld.New();
		fi.Local = Local.Get();
		fi.Remote = Remote.Get();
		Store->GetEvents()->Post(Store, msg.Release());
		return 0;
	}
};

void ImapFolder::LoadMail()
{
	if (Mail.State != Store3Unloaded)
		return;

	// We really do not want to be creating a second load thread, while one is already running.
	LAssert(!LoadThread);

	LoadThread.Reset(new ImapFolderLoadThread(this));
	
	// This happens after the thread creation, because the thread steals the state data
	// from the Mail container using a Swap.
	Mail.State = Store3Loading;

	// Wait and see if it loads quickly:
	auto StartTs = LCurrentTime();
	while (	LCurrentTime() - StartTs < TIMEOUT_FOLDER_LOAD &&
			!LoadThread->IsExited())
		LSleep(1);

	// LgiTrace("ImapFolder::LoadMail(%s) threaded=%i\n", Remote.Get(), !LoadThread->IsExited());
	if (LoadThread->IsExited())
	{
		// Yes it did... finish the processing:
		OnLoadMail(false);
	}
}

void ImapFolder::OnLoadMail(bool Threaded)
{
	if (Mail.State == Store3Loaded)
		return;

	if (LoadThread)
	{
		// Return all the data to the main folder
		ImapFolderData::Swap(*LoadThread.Get());

		// NULL out the data pointers
		for (auto m: Mail.a)
			m->Data = nullptr;

		LoadThread.Reset();
	}
	else LAssert(!"Where is the thread?");

	if (_Parent)
	{
		if (auto m = new ImapMsg(IMAP_FOLDER_LISTING, _FL))
		{
			ImapFolderInfo &i = m->Fld.New();
			i.Remote = Remote.Get();
			i.Local = Local.Get();
			i.LastUid = GetLastUid();
			Store->PostThread(m, false);
		}
	}

	// Set this collection to loaded even if we get updates from the
	// server later. We can update the UI through the OnNew/OnDelete 
	// events.
	Mail.State = Store3Loaded;

	// Call any onload callbacks...
	for (auto &cb: OnLoad)
		cb(Store3Delayed);
	OnLoad.Empty();

	if (Threaded)
	{
		// Tell the UI thread about this... it'll need to update the screen
		LArray<LDataI*> Items;
		Items.Add(this);
		Store->OnChange(_FL, Items, FIELD_STATUS);
	}
}

Store3Status ImapFolder::WhenLoaded(std::function<void(Store3Status)> cb)
{
	if (Mail.State == Store3Loaded)
	{
		cb(Store3Success);
		return Store3Success;
	}

	if (cb)
	{
		// Save the callback for later...
		OnLoad.Add(std::move(cb));
	}
	else LAssert(!"No callback?");

	if (Mail.State == Store3Unloaded)
		LoadMail(); // Start the loading process...

	return Store3Delayed;
}

Store3Status ImapFolder::Save(LDataI *Into)
{
	if (!IsOnDisk() && Into)
	{
		if (!LeafName)
		{
			LAssert(!"No leaf name set yet.");
			return Store3Error;
		}

		if (auto f = dynamic_cast<ImapFolder*>(Into))
		{
			f->LoadSub();

			char s[MAX_PATH_LEN];
			LMakePath(s, sizeof(s), f->Local, LeafName);
			Local = s;
			Remote = MakeImapPath(Local);
			if (LDirExists(Local))
			{
				LAssert(!"Sub-folder already exists.");

				Remote.Empty();
				Local.Empty();

				return Store3Error;
			}

			FileDev->CreateFolder(Local);

			auto m = new ImapMsg(IMAP_CREATE_FOLDER, _FL);
			if (!m)
				return Store3Error;

			ImapFolderInfo &Inf = m->Fld.New();
			Inf.Local = Local.Get();
			Inf.Remote = Remote.Get();
			Store->PostThread(m, false);

			SetParent(f);
			Field.State = Store3Loaded;
			Mail.State = Store3Loaded;
		}
		else
		{
			LAssert(!"Not a valid parent folder.");
			return Store3Error;
		}
	}

	if (Mail.State != Store3Loaded)
	{
		OnLoad.New() = [this, Into](auto status)
			{
				Save(Into);
			};

		LoadMail();
		return Store3Delayed;
	}

	return Serialize(true) ? Store3Success : Store3Error;
}

Store3Status ImapFolder::Delete(bool ToTrash)
{
	ImapMsg *m = new ImapMsg(IMAP_DELETE_FOLDER, _FL);
	if (m)
	{
		ImapFolderInfo &Inf = m->Fld.New();
		Inf.Local = Local.Get();
		Inf.Remote = Remote.Get();
		if (Store->PostThread(m, false))
			return Store3Delayed;
	}
	
	return Store3Error;
}

class ImapDownload : public LStream
{
	LVariant Path;
	LFile *f;

public:
	ImapDownload(char *p)
	{
		Path = p;
		f = 0;
	}

	~ImapDownload()
	{
		DeleteObj(f);
	}

	int Open(const char *Str, int Int) { LAssert(0); return false; }
	bool IsOpen() { return f ? f->IsOpen() : 0; }
	int Close() { LAssert(0); return false; }
	int64 GetSize() { return f ? f->GetSize() : -1; }
	int64 SetSize(int64 Size) { return f ? f->SetSize(Size) : -1; }
	int64 GetPos() { return f ? f->GetPos() : -1; }
	int64 SetPos(int64 Pos) { return f ? f->SetPos(Pos) : -1; }
	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0) { return f ? f->Read(Buffer, Size, Flags) : 0; }
	
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		if (!f)
		{
			if ((f = new LFile))
			{
				if (!f->Open(Path.Str(), O_WRITE))
				{
					LgiTrace("%s:%i - Error: Failed to open IMAP file '%s'\n", _FL, Path.Str());
				}
			}
		}

		return f ? f->Write(Buffer, Size, Flags) : 0;
	}
};

#if !IMAP_PROTOBUF
int	IdxCmp(LXmlTag *a, LXmlTag *b, int d)
{
	char *aa = a->GetAttr(ATTR_UID);
	char *bb = b->GetAttr(ATTR_UID);
	LAssert(aa && bb);
	return atoi(aa) - atoi(bb);
}
#endif

static const char *Ws = " \t\r\n";
#define Skip(s)	while (*s && strchr(Ws, *s)) s++

char *ImapTok(char *&s)
{
	Skip(s);
	if (*s == '\"' || *s == '\'')
	{
		char d = *s++;
		char *Start = s;
		char *End = s;
		for (; *End; End++)
		{
			if (*End == d)
				break;
			if (*End == '\\')
				End++;
		}
		

		if (!End) return 0;
		s = End + 1;
		return NewStr(Start, End - Start);
	}
	else if (*s == '{')
	{
		s++;
		char *e = strchr(s, '}');
		if (e)
		{
			int Size = atoi(s);
			s = e + 1;
			Skip(s);
			char *Str = NewStr(s, Size);
			s += Size;
			return Str;
		}
		else
		{
			LAssert(!"Parse error");
			return 0;
		}							
	}
	else
	{
		char *Start = s;
		while (*s && !strchr(" \r\t\n)", *s)) s++;
		return NewStr(Start, s - Start);
	}
}

struct ImapPair
{
    LAutoString Name;
    LAutoString Value;
};

bool SkipStructure(char *&s)
{
    int Depth = 0;
    while (s && *s)
    {
        if (*s == '(')
        {
            s++;
            Depth++;
        }
        else if (*s == ')')
        {
            if (Depth == 0)
                break;
            s++;
            Depth--;
        }
        else if (*s == '\"' || *s == '\'')
        {
            char delim = *s++;
            while (*s && *s != delim)
                s++;
            if (*s == delim)
                s++;
        }
        else s++;
    }
    
    return true;
}

bool ImapParseStructure(LXmlTag *t, char *&s)
{
	LXmlTag *c = new LXmlTag("Seg");
	if (!c)
		return false;
	t->InsertTag(c);
	char m[256];

	while (s && *s && *s != ')')
	{
		// Check for nesting...
		Skip(s);
		if (*s == '(')
		{
		    // Read nested segment(s)
			s++;
			if (!ImapParseStructure(c, s))
			    return false;
            Skip(s);
		    if (*s == ')')
		    {
		        // Scan past the end marker
		        s++;
		    }
		    else
		    {
                LgiTrace("Error: Parsing IMAP structure - wrong token after sub\n");
		        return false;
		    }
		}
		else // Read token stream that represents a MIME segment
		{
		    LArray<ImapPair> p;
		    while (s && *s && *s != ')')
		    {
		        Skip(s);
		        if (*s == '(')
		        {
		            // Named values
		            s++;
		            
                    Skip(s);
		            while (s && *s && *s != ')')
		            {
		                ImapPair &v = p.New();
		                v.Name.Reset(ImapTok(s));
                        Skip(s);
                        if (*s == '(')
                        {
                            // Sublist of values... argh.
                            // This might need to be recursive?
                            // ssize_t DelIndex = (ssize_t)p.Length()-1;
                            s++;
                            Skip(s);
                            while (s && *s && *s != ')')
                            {
                                ImapPair &vv = p.New();
                                LAutoString Name(ImapTok(s));
                                sprintf_s(m, sizeof(m), "%s(%s)", v.Name.Get(), Name.Get());
                                vv.Name.Reset(NewStr(m));
                                Skip(s);
                                if (*s == ')')
                                {
                                    LgiTrace("Error: Parsing IMAP structure - wrong token in named value\n");
                                    return false;
                                }
                                vv.Value.Reset(ImapTok(s));
                                Skip(s);
                            }
                            if (*s == ')')
                            {
                                s++;
                            }
                            else
                            {
                                LgiTrace("Error: Parsing IMAP structure - wrong token after named value\n");
                                return false;
                            }
                            
                            Skip(s);
                            v.Name.Reset();
                            continue;
                        }
                        else if (*s == ')')
                        {
							// Move the name to the value... a single token is always just a value.
							v.Value = v.Name;
                        }
						else
						{
							v.Value.Reset(ImapTok(s));
						}
		                Skip(s);
		            }
		            
		            if (*s == ')')
		            {
		                s++;
		            }
		            else
		            {
                        LgiTrace("Error: Parsing IMAP structure - wrong token after named value\n");
                        return false;
		            }
		        }
		        else
		        {
		            // Unnamed value
                    ImapPair &v = p.New();
                    v.Value.Reset(ImapTok(s));
                    
                    if (p.Length() == 7 &&
                        !_stricmp(p[0].Value, "message"))
                    {
                        if (!SkipStructure(s))
                            return false;
                    }
		        }
		    }
		    
		    unsigned i = 0;
		    if (p.Length() > 1)
		    {
		        if (c->Children.Length())
		        {
                    ImapPair &a = p[i++];
                    if (a.Value)
                    {
                        sprintf_s(m, sizeof(m), "multipart/%s", a.Value.Get());
                        if (!c->SetAttr("mimetype", StrLwr(m)))
                            return false;
                    }
		        }
		        else
		        {
                    ImapPair &a = p[i++];
                    ImapPair &b = p[i++];
                    if (a.Value && b.Value)
                    {
                        sprintf_s(m, sizeof(m), "%s/%s", a.Value.Get(), b.Value.Get());
                        if (!c->SetAttr("mimetype", StrLwr(m)))
                            return false;
                    }
                }
		    }
		    
		    while (i < p.Length())
		    {
		        ImapPair &v = p[i++];
		        if (v.Name)
		        {
		            if (_stricmp(v.Name, "boundary") &&
						!strchr(v.Name, '*'))
		            {
                        if (!c->SetAttr(v.Name, v.Value))
                            return false;
                    }
		        }
		        else if (v.Value)
		        {
                    if (i == 6)
                    {
                        if (!c->SetAttr("encoding", v.Value))
                            return false;
                    }
                    else if (i == 7)
                    {
                        if (!c->SetAttr("size", atoi(v.Value)))
                            return false;
                    }
                }
		    }
		}
	}
	
	return true;
}

struct UidTbl : public LHashTbl<IntKey<uint32_t>,ImapMail*>
{
	UidTbl(DIterator<LDataI, ImapMail, ImapStore> &Mail)
	{
		for (unsigned i=0; i<Mail.Length(); i++)
		{
			ImapMail *m = Mail.a[i];
			if (m)
			{
				if (m->Uid)
				{
					Add(m->Uid, m);
				}
				else
				{
					// There is no UID in the case that there's a new message
					// being appended to the IMAP folder and hasn't got a UID
					// yet... just ignore it. Add the UID to the table when
					// the IMAP_APPEND message gets back from the worker thread.
				}
			}
			else LAssert(0);
		}
	}
};

LString ImapFolder::MailPath(uint32_t Uid, bool CheckExists)
{
	char f[32], p[MAX_PATH_LEN];
	sprintf_s(f, sizeof(f), "%i.eml", Uid);
	LMakePath(p, sizeof(p), Local, f);
	if (CheckExists)
	{
		LAssert(!LFileExists(p));
	}
	return p;
}

int32_t ImapFolder::GetSequence(ImapMail *mail)
{
	return (int32_t) Mail.a.IndexOf(mail);
}

#define IMAP_ONLISTING_LOG		0

void ImapFolder::OnListing(ImapMsg *m)
{
	// This code does a replication between the list of mail on the server and the local
	// database, removing local email that doesn't exist on the server, updating existing
	// mail and creating new mail for objects only on the server.
	LArray<LDataI*> NewMail, OldMail, DelMail, ChangedMail;

	// Build a hash of our local mail...
	UidTbl Tbl(Mail);

	#if IMAP_ONLISTING_LOG
	LgiTrace("OnListing(%s)\n", Remote.Get());
	#endif
	
	ImapFolderInfo &Fld = m->Fld[0];
	bool Unclean = false;

	// Go through the incoming list of mail...
	for (auto &Inf: m->Mail)
	{
		if (!Inf.Uid)
		{
			LAssert(!"No UID.");
			continue;
		}
		
		// Do we have the mail in our local mail?
		ImapMail *Existing = Tbl.Find(Inf.Uid);
		if (Existing)
		{
			// Already exists.
			Tbl.Delete(Inf.Uid); // Clear the ref, we've "seen" this email on the server.

			// Update flags?
			auto ExistingMeta = UidMap.Find(Inf.Uid);
			if (!ExistingMeta)
			{
				LAssert(!"No meta for this email.");
				continue;
			}

			bool ItemDirty = false;

			#if IMAP_ONLISTING_LOG
			bool dTypes[3] = {0};
			#endif

			if (Inf.Size > 0)
			{
				#if IMAP_PROTOBUF
					auto mSize = ExistingMeta->size();
					if (mSize != Inf.Size)
					{
						ExistingMeta->set_size(Inf.Size);
						ItemDirty = Unclean = true;
						#if IMAP_ONLISTING_LOG
						dTypes[0] = true;
						#endif
					}
				#else
					char *sSize = ExistingMeta->GetAttr(ATTR_SIZE);
					if (!sSize || atoi64(sSize) != Inf.Size)
					{
						LString s;
						s = Inf.Size;
						Unclean |= ExistingMeta->SetAttr(ATTR_SIZE, s);
						ItemDirty = true;

						#if IMAP_ONLISTING_LOG
						dTypes[0] = true;
						#endif
					}
				#endif
			}
				
			#if IMAP_ONLISTING_LOG
			LString sDate;
			#endif
			if (Inf.Date)
			{
				LDateTime Dt;
				if (Dt.Decode(Inf.Date))
				{
					#if IMAP_ONLISTING_LOG
					auto Tmp = Dt;
					#endif

					Dt.ToUtc();

					#if IMAP_ONLISTING_LOG
					sDate.Printf("[%s, %s, %s, %s]", Inf.Date.Get(), Tmp.Get().Get(), Dt.Get().Get(), Existing->DateSent.Get().Get());
					#endif

					if (Dt != Existing->DateSent)
					{
						ValidateImapDate(Dt);
						Existing->DateSent = Dt;

						#if IMAP_PROTOBUF
							ExistingMeta->set_datesent(Dt.Ts());
							Unclean = true;
						#else
							char c[32];
							Dt.SetFormat(GDTF_YEAR_MONTH_DAY);
							Dt.Get(c, sizeof(c));
							Unclean |= ExistingMeta->SetAttr(ATTR_DATE, c);
						#endif
						ItemDirty = true;

						#if IMAP_ONLISTING_LOG
						dTypes[1] = true;
						#endif
					}
				}
			}

			#if IMAP_PROTOBUF
				ImapMailFlags CurFlags;
				CurFlags.All = ExistingMeta->remoteflags();
			#else
				ImapMailFlags CurFlags(ExistingMeta->GetAttr(ATTR_FLAGS));
			#endif
			if (CurFlags.All != Inf.Flags.All)
			{
				if (Inf.Flags.ImapDeleted)
				{
					// Becoming deleted...
					DelMail.Add(Existing);
						
					// But don't let it fall into the 'Changed' items..
					ItemDirty = false;
				}
				else
				{
					// Flags changing..
					#if IMAP_PROTOBUF
						ExistingMeta->set_remoteflags(Inf.Flags.All);
					#else
						Unclean |= ExistingMeta->SetAttr(ATTR_FLAGS, Inf.Flags.Get());
					#endif
					ItemDirty = true;

					#if IMAP_ONLISTING_LOG
					dTypes[2] = true;
					#endif
				}
			}

			if (ItemDirty)
			{
				LAssert(!DelMail.HasItem(Existing));
				ChangedMail.Add(Existing);

				#if IMAP_ONLISTING_LOG
				LgiTrace("%s:%i %i:%i existing changed %i,%i,%i %s\n", _FL, i, Inf.Uid, dTypes[0], dTypes[1], dTypes[2], sDate.Get());
				#endif
			}
		}
		else if (!Inf.Flags.ImapDeleted)
		{
			// Doesn't exist, so must be new.
			ImapMail *New = new ImapMail(Store, _FL, Inf.Uid);
			if (New)
			{
				#if IMAP_ONLISTING_LOG
				LString sDate;
				#endif

				New->Path = MailPath(Inf.Uid, false);
				if (Inf.Date)
				{
					if (New->DateSent.Decode(Inf.Date))
					{
						#if IMAP_ONLISTING_LOG
						auto Tmp = New->DateSent;
						#endif

						New->DateSent.ToUtc();
						ValidateImapDate(New->DateSent);

						#if IMAP_ONLISTING_LOG
						sDate.Printf("[%s, %s, %s]", Inf.Date.Get(), Tmp.Get().Get(), New->DateSent.Get().Get());
						#endif
					}
					else
					{
						LgiTrace("%s:%i - Date parse failed '%s'\n", _FL, Inf.Date.Get());
						New->DateSent.Decode(Inf.Date);
					}
				}

				New->SetRemoteFlags(Inf.Flags);
				New->Parent = this;

				bool IsNewMail =
								/*
								The problem with using the recent flag for IsNew is that
								other mail clients can be reading the inbox causing the recent
								flag to be consumed. This means Scribe sees the unread mail as
								old and doesn't trigger the required filters. At least that's 
								my understanding.

								New->RemoteFlags.ImapRecent &&								
								*/
								!New->RemoteFlags.ImapSeen
								&&
								System == Store3SystemInbox;
				#if IMAP_ONLISTING_LOG
				LgiTrace("%s:%i - Imap.IsNewMail %i, %i, %i(%i) '%s'\n", _FL,
					New->RemoteFlags.ImapRecent,
					New->RemoteFlags.ImapSeen,
					System == Store3SystemInbox,
					System,
					Inf.Flags.Get());
				#endif
				
				// auto meta = New->GetMeta(false);
				AddMail(New, IsNewMail ? &NewMail : &OldMail);

				auto t = New->GetMeta();
				LAssert(t != NULL);
				
				#if IMAP_ONLISTING_LOG
				LgiTrace("%s:%i idx=%i uid=%i new mail %i %s\n", _FL, i, Inf.Uid, IsNewMail, sDate.Get());
				#endif
				
				if (t)
				{
					if (Inf.Size > 0)
					{
						#if IMAP_PROTOBUF
						t->set_size(Inf.Size);
						Unclean = true;
						#else
						LString s;
						s = Inf.Size;
						Unclean |= t->SetAttr(ATTR_SIZE, s);
						#endif
					}

					char *Struct = Inf.Structure;
					#if !IMAP_PROTOBUF
					if (Struct && !ImapParseStructure(t, Struct))
					{
						#ifdef _DEBUG
						LFile f;
						if (f.Open(
							#ifdef WIN32
							"c:\\temp\\"
							#endif
							"imap-struct.txt", O_WRITE))
						{
						    f.SetSize(0);
						    f.Write(Inf.Structure, strlen(Inf.Structure));
						    f.Close();
						    LAssert(!"Imap structure parse failed.");
						}
						#endif
					}
					#endif
				}
			}
		}
	}

	// Now 'Tbl' contains all the local mail NOT on the server...
	// for (ImapMail *Old = Tbl.First(); Old; Old = Tbl.Next())
	for (auto it : Tbl)
	{
	    auto OldUid = it.value->Uid;
		
		// Why is this condition here???
	    if (Fld.LastUid == 0 ||		// Ie we are doing a full folder refresh or initial load
			OldUid > Fld.LastUid)	// Or we are updated all the email after Fld.LastUid
	    {
			#if IMAP_ONLISTING_LOG
			LgiTrace("%s:%i uid:%i deleted (notify)\n", _FL, it.key);
			#endif
		    DelMail.Add(it.value);
		}
		else
		{
			#if IMAP_ONLISTING_LOG
			LgiTrace("%s:%i uid:%i deleted (no-notify)\n", _FL, it.key);
			#endif
		}
	}

	// Process all the callback events
	if (OldMail.Length())
		Store->OnNew(_FL, this, OldMail, -1, false);
	if (NewMail.Length())
		Store->OnNew(_FL, this, NewMail, -1, true);

	if (DelMail.Length() && Store->OnDelete(_FL, this, DelMail))
	{
		DelMail.DeleteObjects();
		Unclean = true;
	}

	if (ChangedMail.Length())
		Store->OnChange(_FL, ChangedMail, 0);

	if (m->Last)
		Mail.State = Store3Loaded;
	if (Unclean)
		SetDirty();
	IsOnline = true;
}

void ImapFolder::Swap(ImapFolder &f)
{
	LAssert(WriteThread.Get() == NULL); // Probably can't swap this.

	LSwap(LastIdleSelect, f.LastIdleSelect);
	LSwap(IsOnline, f.IsOnline);
	LSwap(ScanState, f.ScanState);
	LSwap(Dirty, f.Dirty);
	LSwap(Store, f.Store);
	LSwap(System, f.System);
	LSwap(ItemType, f.ItemType);

	auto p1 = GetParent();
	SetParent(f.GetParent());
	f.SetParent(p1);

	#if IMAP_PROTOBUF
	LSwap(ThreadView, f.ThreadView);
	LSwap(PermRead, f.PermRead);
	LSwap(PermWrite, f.PermWrite);
	LSwap(SortField, f.SortField);
	LSwap(Expanded, f.Expanded);
	LSwap(UnreadCount, f.UnreadCount);
	#else
	Meta.Swap(f.Meta);
	#endif
	RootName.Swap(f.RootName);
	Folders.Swap(f.Folders);
	Files.Swap(f.Files);
	UidMap.Swap(f.UidMap);
	Mail.Swap(f.Mail);
	Remote.Swap(f.Remote);
	Local.Swap(f.Local);
	LeafName.Swap(f.LeafName);
	Sub.Swap(f.Sub);
	Field.Swap(f.Field);
}

int ImapFolder::GetLastUid()
{
    uint32_t Max = 0;
    
	for (auto it : UidMap)
		Max = MAX(Max, it.key);
    
    return Max;
}

void ImapFolder::OnSelect(bool b)
{
	if (Mail.State == Store3Loaded)
	{
		ImapMsg *m = new ImapMsg(IMAP_SELECT_FOLDER, _FL);
		if (m)
		{
			ImapFolderInfo &i = m->Fld.New();
			i.Remote = Remote.Get();
			i.Local = Local.Get();
			i.LastUid = GetLastUid();
			Store->PostThread(m, false);
		}
	}
}

void ImapFolder::OnDeleteComplete()
{
	LgiTrace("OnDeleteComplete %s\n", Local.Get());

	// Clear out child folders...
	while (Sub.Length())
	{
		ImapFolder *f = CastFld(Sub.First());
		if (f)
		{
			f->OnDeleteComplete();
			DeleteObj(f);
		}
		else break;
	}

	// Delete all the mail files..
	while (Mail.Length())
	{
		ImapMail *m = Mail.a[0];
		if (m)
		{
			m->OnDelete();
			DeleteObj(m);
		}
		else break;
	}

	// Remove ourselves from the parent folder...
	SetParent(NULL);
	if (Local)
	{
		FileDev->RemoveFolder(Local, true);
	}
}

LAutoStreamI ImapFolder::GetStream(const char *file, int line)
{
	LAssert(0);
	return LAutoStreamI(0);
}

bool ImapFolder::SetStream(LAutoStreamI stream)
{
	LAssert(0);
	return false;
}

const char *ImapFolder::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
		{
			if (IsRoot())
			{
				if (!RootName && Store->SettingStore)
				{
					LVariant v;
					if (Store->SettingStore->GetValue(OPT_AccountName, v))
					{
						RootName = v.Str();
					}
				}
				
				if (!RootName)
				{
					char *User = Store->User.Str();
					char *At = User ? strchr(User, '@') : 0;
					RootName.Printf("%.*s@%s", (int)(At ? At - User : strlen(User)), Store->User.Str(), Store->Host.Str());
				}
				
				return RootName;
			}

			char *d = strrchr(Local, DIR_CHAR);
			return d ? d + 1 : (char*)"Error";
		}
		case FIELD_FOLDER_PATH:
		{
			return Remote;
		}
	}

	LAssert(0);
	return NULL;
}

Store3Status ImapFolder::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
		{
			if (!IsOnDisk())
			{
				// There is nothing to load...?
				LeafName = str;
				return Store3Success;
			}

			return WhenLoaded([this, str](auto Status)
			{
				if (IsOnDisk())
				{
					LAutoPtr<ImapMsg> RenameMsg(new ImapMsg(IMAP_RENAME_FOLDER, _FL));
					if (!RenameMsg)
						return Store3Error;

					LString l = Local.Get();
					ssize_t last = l.RFind(DIR_STR);
					if (last)
						l.Length(last);
					
					char s[MAX_PATH_LEN];
					LMakePath(s, sizeof(s), l, str);
				
					RenameMsg->Parent = Remote.Get();
					RenameMsg->NewRemote = MakeImapPath(s);
				
					Store->PostThread(RenameMsg.Release(), true);				
					return Store3Delayed;
				}
				else
				{
					LAssert(!"Should have been handled outside of 'WhenLoaded'?");
					return Store3Error;
				}
			});
			break;
		}
	}

	LAssert(0);
	return Store3Error;
}

#define GetI(p) \
	return Meta.GetAsInt(p); \
	break;

int64 ImapFolder::GetInt(int id)
{
	switch (id)
	{
	    case FIELD_SYSTEM_FOLDER:
	        return System;
	    case FIELD_IS_ONLINE:
			return IsOnline || GetParent() == NULL;
		case FIELD_STORE_TYPE:
			return Store3Imap;
		case FIELD_FOLDER_THREAD:
			#if IMAP_PROTOBUF
				return ThreadView;
			#else
				if (Meta.GetAsInt(PROP_THREAD) < 0)
					Meta.SetAttr(PROP_THREAD, 0);
				GetI(PROP_THREAD);
			#endif
		case FIELD_FOLDER_PERM_READ:
			#if IMAP_PROTOBUF
				return PermRead;
			#else
				GetI(PROP_READ);
			#endif
		case FIELD_FOLDER_PERM_WRITE:
			#if IMAP_PROTOBUF
				return PermWrite;
			#else
				GetI(PROP_WRITE);
			#endif
		case FIELD_SORT:
			#if IMAP_PROTOBUF
				return SortField;
			#else
				if (SortCache == INVALID_CACHE)
					SortCache = Meta.GetAsInt(PROP_SORT);
				return SortCache;
			#endif
		case FIELD_FOLDER_OPEN:
			#if IMAP_PROTOBUF
				return Expanded;
			#else
				GetI(PROP_EXPANDED);
			#endif
		case FIELD_UNREAD:
			#if IMAP_PROTOBUF
				return UnreadCount;
			#else
				GetI(PROP_UNREAD);
			#endif
		case FIELD_FOLDER_TYPE:
			return ItemType;
		case FIELD_LOADED:
			return Mail.State == Store3Loaded;
		case FIELD_FOLDER_INDEX:
			return -1;
	}

	LAssert(0);
	return -1;
}

#define SetI(p) \
	Meta.SetAttr(p, (int)i); \
	SetDirty(true); \
	return Store3Success; \

Store3Status ImapFolder::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_FOLDER_THREAD:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					ThreadView = i;
					return Store3Success;
				#else
					SetI(PROP_THREAD);
				#endif
			});
		case FIELD_FOLDER_PERM_READ:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					PermRead = (ScribePerm)i;
					return Store3Success;
				#else
					SetI(PROP_READ);
				#endif
			});
		case FIELD_FOLDER_PERM_WRITE:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					PermWrite = (ScribePerm)i;
					return Store3Success;
				#else
					SetI(PROP_WRITE);
				#endif
			});
		case FIELD_SORT:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					SortField = i;
					return Store3Success;
				#else
					SortCache = (int)i;
					SetI(PROP_SORT);
				#endif
			});
		case FIELD_FOLDER_OPEN:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					Expanded = i != 0;
					return Store3Success;
				#else
					SetI(PROP_EXPANDED);
				#endif
			});
		case FIELD_UNREAD:
			return WhenLoaded([this, i](auto Status)
			{
				#if IMAP_PROTOBUF
					UnreadCount = i;
					return Store3Success;
				#else
					SetI(PROP_UNREAD);
				#endif
			});
		case FIELD_FOLDER_TYPE:
			if (!IsOnDisk())
			{
				ItemType = (int)i;
				return Store3Success;
			}
			return WhenLoaded([this, i](auto Status)
			{
				ItemType = (int)i;
			});
			break;
		case FIELD_FOLDER_INDEX:
			return Store3Error;
		case FIELD_LOADED:
		{
		    if (i)
		    {
		        LoadMail();
		    }
		    else
		    {
    		    // Unload folder...
				Mail.DeleteObjects();
				Mail.State = Store3Unloaded;
			}
		    break;
		}
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

const LDateTime *ImapFolder::GetDate(int id)
{
	LAssert(0);
	return NULL;
}

Store3Status ImapFolder::SetDate(int id, const LDateTime *i)
{
	LAssert(0);
	return Store3Error;
}

LDataPropI *ImapFolder::GetObj(int id)
{
	switch (id)
	{
		case FIELD_PARENT:
			return _Parent;
	}

	LAssert(0);
	return NULL;
}

LDataIt ImapFolder::GetList(int id)
{
	LAssert(0);
	return NULL;
}

LDataIterator<LDataFolderI*> &ImapFolder::SubFolders()
{
	LoadSub();
	return Sub;
}

LDataIterator<LDataI*> &ImapFolder::Children()
{
	LoadMail();
	return Mail;
}

LDataIterator<LDataPropI*> &ImapFolder::Fields()
{
	return Field;
}

Store3Status ImapFolder::FreeChildren()
{
	if (Store)
	{
		LArray<LDataI*> Lst;
		for (unsigned i=0; i<Mail.Length(); i++)
		{
			ImapMail *t = Mail.a[i];
			if (t->UserData)
				Lst.Add(t);
		}
		if (!Store->Callback->OnDelete(this, Lst))
			return Store3Error;
	}

	Sub.DeleteObjects();
	UidMap.DeleteObjects();
	Mail.DeleteObjects();

	Mail.State = Store3Unloaded;
	return Store3Success;
}

Store3Status ImapFolder::DeleteAllChildren()
{
    // Delete cmd
	LAutoPtr<ImapMsg> Msg(new ImapMsg(IMAP_DELETE, _FL));
	Msg->Parent = Remote.Get();
	if (!Store->PostThread(Msg.Release(), false))
	    return Store3Error;

    // Immediately expunge
	Msg.Reset(new ImapMsg(IMAP_EXPUNGE_FOLDER, _FL));
	Msg->Fld[0].Remote = Remote.Get();
	if (!Store->PostThread(Msg.Release(), false))
	    return Store3Error;
	
    return Store3Delayed;
}

ImapFolder *ImapFolder::Find(const char *local, const char *remote)
{
	if (Local.Equals(local) || Remote.Equals(remote))
		return this;

	LoadSub();
	for (auto cf: Sub.a)
	{
		LAssert(cf->GetParent() == this);
		if (auto r = cf->Find(local, remote))
			return r;
	}

	return nullptr;
}

ImapMail *ImapFolder::FindMail(const char *File, int32 Uid)
{
	for (auto m: Mail.a)
	{
		if (File && m->Path)
		{
			if (!_stricmp(m->Path, File))
				return m;
		}
		if (m->Uid == Uid)
			return m;
	}

	return nullptr;
}

ImapMail::IMeta ImapFolderData::GetMeta(uint32_t id, bool Create)
{
	if (!id)
		return NULL;

	auto t = UidMap.Find(id);
	#if IMAP_PROTOBUF
		if (!t && Create && (t = new scribe::Email))
		{
			t->set_uid(id);
			UidMap.Add(id, t);
		}
	#else
		if (!t && Create && (t = new LXmlTag(TAG_MAIL)))
		{
			t->SetAttr(ATTR_UID, (int)id);
			UidMap.Add(id, t);
		}
	#endif

	return t;
}

bool ImapFolderData::DeleteMeta(uint32_t id)
{
	if (!id)
		return false;

	auto t = UidMap.Find(id);
	if (!t)
		return false;

	#ifdef _DEBUG
	ImapMail *m = GetMail(id);
	LAssert(m == 0);
	if (m)
		DelMail(m);
	#endif

	UidMap.Delete(id);
	DeleteObj(t);
	return true;
}

ImapMail *ImapFolderData::GetMail(uint32_t Uid)
{
	if (!Uid)
		return 0;

	for (auto m: Mail.a)
	{
		if (m->Uid == Uid)
			return m;
	}

	return 0;
}

bool ImapFolderData::DelMail(ImapMail *m)
{
	if (!m)
	{
		LAssert(!"Null ptr.");
		return false;
	}

	LoadMail();
	if (Mail.IndexOf(m) < 0)
	{
		LAssert(!"Doesn't exist.");
		return false;
	}

	auto t = UidMap.Find(m->Uid);
	if (t)
	{
		#if 0 // def _DEBUG
		LStackTrace("%s DelMail %s\n", Remote.Get(), m->Uid.Get());
		#endif
		UidMap.Delete(m->Uid);
		DeleteObj(t);
	}
	else
	{
		static bool First = true;
		if (First)
		{
			First = false;
			LAssert(!"There really should be a meta tag.");
		}
	}

	Mail.Delete(m);
	SetDirty();

	return true;
}

#define DEBUG_PROF_ADD_MAIL		0
#if DEBUG_PROF_ADD_MAIL
	#define PROFILE(str) Prof.Add(str)
#else
	#define PROFILE(str)
#endif

bool ImapFolderData::AddMail(ImapMail *m, LArray<LDataI*> *OnNew)
{
#if DEBUG_PROF_ADD_MAIL
LProfile Prof("ImapFolder::AddMail", 1);
#endif

	if (!m)
	{
		LAssert(!"Null ptr.");
		return false;
	}

PROFILE("Index chk");
	if (Mail.IndexOf(m, true) >= 0)
	{
		LAssert(!"Already added.");
		return false;
	}

PROFILE("Mail insert");
	if (!m->Parent)
	{
		m->Parent = dynamic_cast<ImapFolder*>(this);
		LAssert(m->Parent);
	}
	Mail.Insert(m, -1, true);

	if (m->Uid)
	{
		#if IMAP_PROTOBUF
		// UidMap.Add(m->Uid, m);
		#else
PROFILE("GetMeta");
		LXmlTag *t = m->GetMeta(false);
		if (!t && (t = m->GetMeta()))
		{
			// Creating a new meta tag... set it's variables.
PROFILE("Set attr");
			t->SetAttr(ATTR_UID, (int)m->Uid);

			// UidMap will already have the entry by now...

PROFILE("Serialize");
			m->Serialize(t, true);
		}
		else
		{
PROFILE("Serialize");
			m->Serialize(t, false);
		}
		#endif
	}
	// else its a temp object for appending to a mail box

PROFILE("DIR chk");
	char *Leaf = m->Path ? strrchr(m->Path, DIR_CHAR) : 0;
	if (!Leaf)
	{
		LAssert(!"Not a valid path.");
	}

	if (OnNew)
	{
PROFILE("OnNewAdd");
		OnNew->Add(m);
	}

	return true;
}

void ImapFolder::OnPulse()
{
	// int64 Now = LCurrentTime();
	if (Dirty)
	{
		Save();
	}

	if (Sub.GetState() == Store3Loaded)
	{
		for (LDataFolderI *c=Sub.First(); c; c=Sub.Next())
		{
			ImapFolder *f = dynamic_cast<ImapFolder*>(c);
			f->OnPulse();
		}
	}
}

void ImapFolder::OnCommand(const char *Name)
{
	if (!Remote)
		return;
	
	ScribeDomType Method = StrToDom(Name);
	switch (Method)
	{
		case SdExpunge:
		{
			ImapMsg *m = new ImapMsg(IMAP_EXPUNGE_FOLDER, _FL);
			m->Fld[0].Remote = Remote.Get();
			Store->PostThread(m, false);
			break;
		}
		case SdUndelete:
		{
			ImapMsg *m = new ImapMsg(IMAP_UNDELETE, _FL);
			m->Parent = Remote.Get();

			for (auto it : UidMap)
			{
				#if IMAP_PROTOBUF
				LAssert(0);
				#else
				char *Flags = it.value->GetAttr(ATTR_FLAGS);
				if (Flags && stristr(Flags, "/deleted"))
				{
					ImapMailInfo &Inf = m->Mail.New();
					Inf.Uid = it.key;
				}
				#endif
			}
			
			Store->PostThread(m, false);
			break;
		}
		case SdRefresh:
		{
			ImapMsg *m = new ImapMsg(IMAP_FOLDER_LISTING, _FL);
			ImapFolderInfo &f = m->Fld[0];
			f.Remote = Remote.Get();
			f.LastUid = 0;
			Store->PostThread(m, false);
			break;
		}
		default:
		{
			LAssert(!"Unknown command");
			break;
		}
	}
}

void ImapFolder::OnExpunge()
{
	LArray<LXmlTag*> Del;
	for (auto it : UidMap)
	{
		#if IMAP_PROTOBUF
			LAssert(0);
		#else
			char *Flags = it.value->GetAttr(ATTR_FLAGS);
			if (Flags && stristr(Flags, "/deleted"))
				Del.Add(it.value);
		#endif
	}

	for (unsigned i=0; i<Del.Length(); i++)
	{
		#if IMAP_PROTOBUF
			LAssert(0);
		#else
			auto Uid = Del[i]->GetAsInt(ATTR_UID);
			LAssert(Uid > 0);
			if (Uid)
			{
				UidMap.Delete(Uid);
				DeleteObj(Del[i]);
			}
		#endif
	}
}

bool ImapFolder::OnRename(const char *NewRemote)
{
	if (!NewRemote || !GetParent())
	{
		LAssert(!"Param error.");
		return false;
	}
	
	char s[MAX_PATH_LEN];
	LMakePath(s, sizeof(s), Store->Cache, NewRemote);
	
	if (!FileDev->Move(Local, s))
	{
		LAssert(!"Failed to move folder.");
		return false;
	}
	
	Local = s;
	Remote = NewRemote;
	
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
ImapFolderFld::ImapFolderFld(LDataStoreI *s, ImapFolder *p, int id, int width) : LXmlTag("Field")
{
	Parent = p;
	SetAttr("Id", id);
	SetAttr("Width", width);
}

const char *ImapFolderFld::GetStr(int id)
{
	if (id == FIELD_NAME)
	{
		switch (GetAsInt("Id"))
		{
			case FIELD_CACHE_FILENAME:
				return "Filename";
			case FIELD_CACHE_FLAGS:
				return "ImapFlags";
		}
	}

	return 0;
}

int64 ImapFolderFld::GetInt(int id)
{
	switch (id)
	{
		case FIELD_ID:
			return GetAsInt("Id");
		case FIELD_WIDTH:
			return GetAsInt("Width");
	}

	return -1;
}

Store3Status ImapFolderFld::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_ID:
			SetAttr("Id", (int)i);
			break;
		case FIELD_WIDTH:
			SetAttr("Width", (int)i);
			break;
		default:
			return Store3Error;
	}

	if (Parent)
		Parent->SetDirty();
	return Store3Success;
}

