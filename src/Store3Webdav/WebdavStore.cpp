#include "Scribe.h"
#include "CalendarView.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

////////////////////////////////////////////////////////////////////////////////////////
WebdavStore::WebdavStore(ScribeWnd *a, LDataEventsI *cb, LString storeName) :
	LMutex("WebdavStore")
{
	App = a;
	Callback = cb;
	Remote.Name = storeName;
	Root = new WebdavFolder(this);
	OnChanged();
}

WebdavStore::~WebdavStore()
{
	DeleteObj(CalFolder);
	DeleteObj(ContactFolder);
	DeleteObj(Root);
	Events.DeleteObjects();
}

LXmlTag *WebdavStore::LockSettings(const char *File, int Line)
{
	auto Opts = App->GetOptions();
	auto Ms = Opts->LockTag(OPT_MailStores, File, Line);
	if (!Ms)
		return NULL;

	for (auto t: Ms->Children)
	{
		auto nm = t->GetAttr(OPT_MailStoreName);
		if (Remote.Name.Equals(nm))
			return t;
	}

	Opts->Unlock();
	return NULL;
}

void WebdavStore::UnlockSettings()
{
	App->GetOptions()->Unlock();
}

Store3Status WebdavStore::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	int Deleted = 0;

	for (auto it: Items)
	{
		WebdavObj *o = dynamic_cast<WebdavObj*>(it);
		if (!o)
			continue;

		WebdavFolder *p = o->Parent;
		if (p)
		{
			LAssert(p->Items.IndexOf(it) >= 0);
			
			// Start delete...
			if (p->Thread)
			{
				if (p->Thread->Delete(o->GetHref()) > Store3Error)
					Deleted++;
			}
			else
			{
				return Store3Error;
			}
		}
		else
		{
			LAssert(!"Impl me.");
		}
	}

	return Deleted == Items.Length() ? Store3Success : Store3Error;
}

LDataI *WebdavStore::Create(int Type)
{
	switch ((Store3ItemTypes)Type)
	{
		case MAGIC_CALENDAR:
		{
			return new WebdavCalendar(this, NULL);
			break;
		}
		case MAGIC_CONTACT:
		{
			return new WebdavContact(this, NULL);
			break;
		}
		default:
		{
			LgiTrace("%s:%i - Unhandled type %x\n", _FL, Type);
			break;
		}
	}

	return NULL;
}

void WebdavStore::OnChanged()
{
	// Get the current settings
	auto t = LockSettings(_FL);
	if (t)
	{
		ContactUrl  = t->GetAttr(OPT_MailStoreContactUrl);
		CalUrl      = t->GetAttr(OPT_MailStoreCalendarUrl);
		Remote.User = t->GetAttr(OPT_MailStoreUserName);
		Remote.Pass = t->GetAttr(OPT_MailStorePassword);

		UnlockSettings();
	}
	else return;

	#if 1
	if (ContactUrl)
	{
		if (!ContactFolder)
		{
			ContactFolder = new WebdavFolder(this, Root);
			ContactFolder->Url = ContactUrl;
			ContactFolder->Extension = "vcf";
			ContactFolder->SetInt(FIELD_FOLDER_TYPE, MAGIC_CONTACT);
			ContactFolder->SetStr(FIELD_FOLDER_NAME, "Contacts");
		}
		if (ContactFolder && !ContactFolder->Thread)
			ContactFolder->Thread.Reset(new WebdavThread(this, ContactFolder, ContactUrl));
	}
	else
	{
		DeleteObj(ContactFolder);
	}
	#endif

	if (CalUrl)
	{
		if (!CalFolder)
		{
			CalFolder = new WebdavFolder(this, Root);
			CalFolder->Url = CalUrl;
			CalFolder->Extension = "ics";
			CalFolder->SetInt(FIELD_FOLDER_TYPE, MAGIC_CALENDAR);
			CalFolder->SetStr(FIELD_FOLDER_NAME, "Calendar");
		}
		if (CalFolder && !CalFolder->Thread)
			CalFolder->Thread.Reset(new WebdavThread(this, CalFolder, CalUrl));
	}
	else
	{
		DeleteObj(CalFolder);
	}
}

LDataPropI *WebdavStore::GetObj(int id)
{
	return NULL;
}

int64 WebdavStore::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STATUS:
			return Store3Success;
		case FIELD_VERSION:
			return 1;
		case FIELD_READONLY:
			return false;
		case FIELD_STORE_TYPE:
			return Store3Webdav;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return -1;
}

const char *WebdavStore::GetStr(int id)
{
	switch (id)
	{
		case FIELD_STORE_PASSWORD:
			return NULL;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return NULL;
}

LDataFolderI *WebdavStore::GetRoot(bool create)
{
	return Root;
}

void WebdavStore::OnEvent(void *Param)
{
	LAutoPtr<WebdavEvent> e((WebdavEvent*) Param);
	if (!e)
		return;
	
	// Delete from our store of events, as the auto ptr now owns it.	
	if (Lock(_FL))
	{
		LAssert(Events.HasItem(e));
		Events.Delete(e);
		Unlock();
	}

	switch (e->Type)
	{
		case CmdFile:
		{
			WebdavObj *o = NULL;
			switch (e->Folder->ItemType)
			{
				case MAGIC_CONTACT:
				{
					o = new WebdavContact(this, e);
					break;
				}
				case MAGIC_CALENDAR:
				{
					o = new WebdavCalendar(this, e);
					break;
				}
				default:
					break;
			}
			if (o)
			{
				o->Parent = e->Folder;
				e->Folder->Items.State = Store3Loaded;
				e->Folder->Items.Insert(o);

				LArray<LDataI*> a;
				a.Add(o);
				Callback->OnNew(e->Folder, a, -1, false);
			}
			break;
		}
		case CmdSave:
		{
			for (auto o: e->Folder->Items.a)
			{
				if (o->Href == e->Href)
				{
					o->SetInt(FIELD_STATUS, e->Status ? Store3Success : Store3Error);
					o->FireOnChange(FIELD_STATUS);
				}
			}
			break;
		}
		default:
			break;
	}
}

void WebdavStore::Post(LDataStoreI *store, void *Param)
{
	if (Lock(_FL))
	{
		Events.Add((WebdavEvent*)Param); // Own the event memory...
		Unlock();
	}

	if (App && store)
		App->PostEvent(M_STORAGE_EVENT, store->Id, (LMessage::Param)Param);	
	else
		LAssert(0);
}

void WebdavStore::OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
{
}

bool WebdavStore::OnDelete(LDataFolderI *parent, LArray<LDataI*> &items)
{
	return true;
}

bool WebdavStore::OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
{
	return true;
}

bool WebdavStore::OnChange(LArray<LDataI*> &items, int FieldHint)
{
	return true;
}
