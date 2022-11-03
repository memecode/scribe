#include "Store3Mail2.h"
#include "resdefs.h"

/////////////////////////////////////////////////////////////////////////////////////
Store3Status ThingData::Delete()
{
	LArray<LDataI*> Lst;
	Lst.Add(this);
	Kit->Callback->OnDelete(GetParent(), Lst);

	if (Store && Store->GetTree()->SeparateItem(Store))
	{
		FolderData *Parent = GetParent();
		if (Parent)
		{
			Parent->Things.Delete(this);
			delete this;
		}

		return Store3Success;
	}

	return Store3Error;
}

Store3Status ThingData::Save(LDataI *Folder)
{
	if (!Store)
	{
		FolderData *f = dynamic_cast<FolderData*>(Folder);
		if (f && f->Store)
		{
			StorageItem *n = f->Store->CreateSub(this);
			if (n)
			{
				Store = n;

				LAssert(f->Things.IndexOf(this) < 0);
				f->Things.Insert(this);

				LArray<LDataI*> lst;
				lst.Add(this);
				Kit->Callback->OnNew(f, lst, -1, false);

				return Store3Success;
			}
			// else we have been deleted.
		}
		else LAssert(!"Not a valid mail2 folder.");
	}
	else
	{
		return Store->Save() ? Store3Success : Store3Error;
	}

	LAssert(0);
	return Store3Error;
}

GAutoStreamI ThingData::GetStream(const char *file, int line)
{
	GAutoStreamI Ret;

	if (Store)
	{
		Ret.Reset(Store->GotoObject(file, line));
	}
	else  LAssert(0);
	
	return Ret;
}
/////////////////////////////////////////////////////////////////////////////////////
LMail2Store::LMail2Store(char *file, LDataEventsI *callback) : Storage2::StorageKitImpl(file)
{
	Callback = callback;
	Mailbox = 0;
	
	if (!FileExists(file))
	{
		char Msg[MAX_PATH_LEN];
		sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ERROR_FILE_DOESNT_EXIST), file);
		ErrorMsg.Reset(NewStr(Msg));
	}
}

LMail2Store::~LMail2Store()
{
}

uint64 LMail2Store::Size()
{
	return GetFileSize();
}

char *LMail2Store::GetStr(int id)
{
	switch (id)
	{
		case FIELD_ERROR:
			return ErrorMsg;
		case FIELD_STORE_PASSWORD:
		{
			GPassword p;
			if (GetPassword(&p))
			{
				static char s[128];
				p.Get(s);
				return s;
			}
			break;
		}
	}

	return 0;
}

bool LMail2Store::SetStr(int id, const char *str)
{
	return 0;
}

int64 LMail2Store::GetInt(int id)
{
	switch (id)
	{
		case FIELD_READONLY:
			return StorageKitImpl::GetReadOnly();
		case FIELD_STATUS:
			return StorageKitImpl::GetStatus() ? DsOk : DsError;
		case FIELD_VERSION:
			return 2;
	}

	return -1;
}

bool LMail2Store::SetInt(int id, int64 i)
{
	return 0;
}

LDataI *LMail2Store::Create(int Type)
{
	switch (Type)
	{
		case MAGIC_FOLDER:
			return new FolderData(this);
		case MAGIC_MAIL:
			return new MailData(this);
		case MAGIC_CONTACT:
			return new ContactData(this);
		case MAGIC_CALENDAR:
			return new CalendarData(this);
		case MAGIC_FILTER:
			return new FilterData(this);
		case MAGIC_ATTACHMENT:
			return new AttachmentData(this);
		case MAGIC_GROUP:
			return new GroupData(this);
	}

	return 0;
}

LDataFolderI *LMail2Store::GetRoot(bool Create)
{
	if (!Mailbox && GetStatus())
	{
		Mailbox = new FolderData(this);		
		Mailbox->Store = StorageKitImpl::GetRoot();
		if (!Mailbox->Store && Create)
		{
			Mailbox->Store = CreateRoot(Mailbox);
		}
		if (Mailbox &&
			Mailbox->Store)
		{
			Mailbox->Store->Object = Mailbox;
		}
	}

	return Mailbox;
}

FolderData *LMail2Store::GetFolder(char *Path)
{
	StorageItem *root = StorageKitImpl::GetRoot();
	FolderData *r = CastFolder(root);
	if (!r)
	{
		LAssert(!"No root node.");
		LgiTrace("%s:%i - Couldn't cast root to folder.\n", _FL);
		return NULL;
	}

	LToken t(Path, "/");
	if (t.Length())
	{
		for (unsigned i=0; i<t.Length(); i++)
		{
			char *p = t[i];
			r->Load(true, false);

			FolderData *m = 0;
			r->SubFolders();
			for (unsigned n=0; n<r->Sub.Length(); n++)
			{
				FolderData *c = r->Sub.a[n];

				if (c->Name &&
					_stricmp(c->Name, p) == 0)
				{
					m = c;
					break;
				}
			}

			if (m)
			{
				r = m;
			}
			else
			{
				LgiTrace("%s:%i - No match for '%s'.\n", _FL, p);
				return 0;
			}
		}
	}
	else LgiTrace("%s:%i - No parts to path.\n", _FL);

	return r;
}

FolderData *LMail2Store::CastFolder(StorageItem *i)
{
	if (!i)
		return 0;

	if (i->GetType() != MAGIC_FOLDER &&
		i->GetType() != MAGIC_FOLDER_OLD &&
		i->GetParent() != 0)
		return 0;

	return (FolderData*)i->Object;
}

Store3Status LMail2Store::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	if (Items.Length() == 0)
	{
		LgiTrace("%s:%i - Nothing to delete.\n", _FL);
		return Store3Error;
	}

	LVariant TrashPath;
	if (!Callback->GetSystemPath(FOLDER_TRASH, TrashPath))
	{
		LgiTrace("%s:%i - Couldn't get trash folder path.\n", _FL);
		return Store3Error;
	}

	FolderData *Trash = GetFolder(TrashPath.Str());
	if (!Trash)
	{
		LgiTrace("%s:%i - Couldn't get ptr to '%s' folder.\n", _FL, TrashPath.Str());
		return Store3Error;
	}

	Store3Status Status = Store3Success;
	LArray<LDataI*> Mv;
	for (unsigned n=0; n<Items.Length(); n++)
	{
		FolderData *f = dynamic_cast<FolderData*>(Items[n]);
		if (f)
		{
			FolderData *Par = f->GetParent();
			if (!Par || Par == Trash)
			{
				if (!f->Delete())
					Status = Store3Error;
			}
			else
			{
				Mv.Add(f);
			}
		}
		else
		{
			ThingData *i = dynamic_cast<ThingData*>(Items[n]);
			if (i)
			{
				FolderData *Par = i->GetParent();
				if (!Par || Par == Trash)
				{
					if (!i->Delete())
						Status = Store3Error;
				}
				else
				{
					Mv.Add(i);
				}
			}
		}
	}

	if (Mv.Length())
		return Move(Trash, Mv);

	return Status;
}

Store3Status LMail2Store::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	Store3Status Status = Store3Error;

	if (NewFolder &&
		NewFolder->GetStore() == this)
	{
		FolderData *f = dynamic_cast<FolderData*>(NewFolder);
		if (f)
 		{
			FolderData *OldPar = 0;

			for (unsigned n=0; n<Items.Length(); n++)
			{
				ThingData *i = dynamic_cast<ThingData*>(Items[n]);
				if (i)
				{
					OldPar = i->GetParent();

					if (i->Store && f->Store)
					{
						f->Load(false, true);

						if (f->Store->GetTree()->AttachItem(i->Store, f->Store))
						{
							Status = Store3Success;
							LAssert(OldPar->Things.IndexOf(i) >= 0);
							OldPar->Things.Delete(i);
							if (f->Things.IndexOf(i) < 0)
								f->Things.Insert(i);
							else
								LAssert(!"Already has item.");
						}
					}
				}
				else
				{
					FolderData *moving = dynamic_cast<FolderData*>(Items[n]);
					if (moving)
					{
						OldPar = moving->GetParent();
						if (moving->Store && f->Store)
						{
							if (OldPar == f)
							{
								// Already parented to that folder.
								Status = Store3Success;
							}
							else if (f->Store->GetTree()->AttachItem(moving->Store, f->Store))
							{
								// Update the containers...
								LAssert(OldPar->Sub.IndexOf(moving) >= 0);
								OldPar->Sub.Delete(moving);
								if (f->Sub.IndexOf(moving) < 0)
									f->Sub.Insert(moving);
								else
									LAssert(!"Folder already exists in Sub");

								Status = Store3Success;
							}
						}
					}
				}
			}

			if (Status && Callback)
			{
				Callback->OnMove(NewFolder, OldPar, Items);
			}
		}
	}

	return Status;
}

Store3Status LMail2Store::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value)
{
	if (Items.Length() == 0)
		return Store3Success;

	if (PropId == FIELD_FLAGS)
	{
		int32 Flags = Value.CastInt32();
		if (Flags & 0x80000000)
		{
			// Remove a flag
			for (unsigned i=0; i<Items.Length(); i++)
			{
				MailData *m = dynamic_cast<MailData*>(Items[i]);
				if (m)
				{
					m->Flags &= Flags;
					m->Save();
				}
				else Items.DeleteAt(i--);
			}
		}
		else
		{
			// Set a flag
			for (unsigned i=0; i<Items.Length(); i++)
			{
				MailData *m = dynamic_cast<MailData*>(Items[i]);
				if (m)
				{
					m->Flags |= Flags;
					m->Save();
				}
				else Items.DeleteAt(i--);
			}
		}

		if (Callback)
			Callback->OnChange(Items, PropId);
	}
	else LAssert(!"Not impl.");

	return Store3Error;
}

class Mail2ProgressAdapter : public Progress
{
	LDataPropI *Props;
public:
	Mail2ProgressAdapter(LDataPropI *p)
	{
		Props = p;
	}

	void SetDescription(const char *d = 0) { Props->SetStr(Store3UiStatus, d); }
	void SetLimits(int64 l, int64 h)  { Props->SetInt(Store3UiMaxPos, (int)h); }
	int64 Value() { return Val; }
	void Value(int64 v) { Val = v; Props->SetInt(Store3UiCurrentPos, (int)v); }
	
	bool Cancel() { return Props->GetInt(Store3UiCancel) != 0; }
	void Cancel(bool i) { Props->SetInt(Store3UiCancel, i); }
};

bool LMail2Store::Compact(LViewI *Parent, LDataPropI *Props)
{
	Mail2ProgressAdapter Prog(Props);
	return StorageKitImpl::Compact(&Prog, Props->GetInt(Store3UiInteractive) != 0);
}

void LMail2Store::OnEvent(void *Param)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////
LDataStoreI *OpenMail2(char *Mail2Folders, LDataEventsI *Callback, bool Create)
{
	LDataStoreI *s = 0;

	if (FileExists(Mail2Folders) || Create)
	{
		s = new LMail2Store(Mail2Folders, Callback);
		if (s)
		{
			s->GetRoot(Create);
		}
	}

	return s;
}
