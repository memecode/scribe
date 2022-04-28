#include "Scribe.h"
#include "CalendarView.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

////////////////////////////////////////////////////////////////////////////////////////
WebdavStore::WebdavStore(ScribeWnd *a, LDataEventsI *cb, LString storeName) 
	: ContactFolder(NULL), CalFolder(NULL)
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

void WebdavStore::OnChanged()
{
	// Get the current settings
	auto t = LockSettings(_FL);
	if (t)
	{
		ContactUrl = t->GetAttr(OPT_MailStoreContactUrl);
		CalUrl = t->GetAttr(OPT_MailStoreCalendarUrl);
		Remote.User = t->GetAttr(OPT_MailStoreUserName);
		Remote.Pass = t->GetAttr(OPT_MailStorePassword);

		UnlockSettings();
	}
	else return;

	if (ContactUrl)
	{
		if (!ContactFolder)
		{
			ContactFolder = new WebdavFolder(this, Root);
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

	if (CalUrl)
	{
		if (!CalFolder)
		{
			CalFolder = new WebdavFolder(this, Root);
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

/*
bool WebdavStore::Read()
{
	if (CalendarSource::Id)
	{
		LString k = GetKey();
		LXmlTag *t = App->GetOptions()->LockTag(k, _FL);
		if (t)
		{
			char *Col = t->GetAttr("Colour");
			if (Col)
				Colour.Set((uint32_t)atoi64(Col), 32);
			else
				Colour.Empty();

			Remote.Name = t->GetAttr("Name");
			Remote.Url = t->GetAttr("URL");
			Remote.User = t->GetAttr("User");
			Remote.Pass = t->GetAttr("Pass");
			Display = t->GetAsInt("Display");

			App->GetOptions()->Unlock();
			OnChanged();

			return true;
		}
	}

	return false;
}

bool WebdavStore::Write()
{
	LVariant v;

	if (!CalendarSource::Id)
	{
		LXmlTag *t = App->GetOptions()->LockTag(OPT_CalendarSources, _FL);
		if (t)
		{
			LString Key;
			for (int i=0; i<100; i++)
			{
				Key.Printf("Source-%i", LRand(10000));
				if (!t->GetChildTag(Key))
				{
					CalendarSource::Id = Key;
					break;
				}
			}
			
			App->GetOptions()->Unlock();
		}		
	}
	
	if (CalendarSource::Id)
	{
		LString Key = GetKey();
		LXmlTag *t = App->GetOptions()->LockTag(Key, _FL);
		if (!t)
		{
			App->GetOptions()->CreateTag(Key);
			t = App->GetOptions()->LockTag(Key, _FL);
		}
		if (t)
		{
			SaveAttr(t, "Name", Remote.Name);
			SaveAttr(t, "URL", Remote.Url);
			SaveAttr(t, "User", Remote.User);
			SaveAttr(t, "Pass", Remote.Pass);
			t->SetAttr("Colour", (int64) Colour.c32());
			t->SetAttr("Display", Display);

			App->GetOptions()->Unlock();
		}
		else return false;
	}

	return true;
}

Calendar *WebdavStore::NewEvent()
{
	Calendar *c = new Calendar(App);
	if (!c)
	{
		return NULL;
	}

	c->App = App;
	if (!Thread)
		OnChanged();

	LDateTime Now;
	Now.SetNow();
	Now.ToUtc();
	LWebdav::FileProps f;
	f.Href.Printf("scribe%s-%s.vcs",
		Now.GetDate().Replace("/","").Get(),
		Now.GetTime().Replace(":","").Get());

	WebdavCalendar *obj = new WebdavCalendar(this, f);
	if (obj)
	{
		c->SetObject(obj);
		CalEvents.Add(c);
		printf("Added %p to CalEvents at %i\n", c, (int)CalEvents.Length());
	}

	return c;
}

bool WebdavStore::GetEvents(LDateTime &StartTs, LDateTime &EndTs, LArray<TimePeriod> &Events)
{
	Read();

	if (!Display)
		return false;
	
	LDateTime Start = StartTs;
	Start.ToUtc();
	LDateTime End = EndTs;
	End.ToUtc();

	LArray<Calendar*> Search;
	if (Thread)
	{
		LArray<LWebdav::FileProps> Files; // Only new files...
		if (Thread->Lock(_FL))
		{
			for (auto f: Thread->Files)
			{
				if (!EventMap.Find(f.Href))
					Files.New() = f.Copy();
			}
			Thread->Unlock();
		}

		for (auto &f: Files)
		{
			// Decode vCal into WebdavCalendar objects
			WebdavCalendar *obj = new WebdavCalendar(this, f);
			if (obj)
			{
				EventMap.Add(f.Href, obj);
				Calendar *c = new Calendar(App, obj);
				if (c)
					CalEvents.Add(c);
				else
					delete obj;
			}
		}

		for (auto c: CalEvents)
			Search.Add(c);
	}
	else return false;

	for (auto c: Search)
	{
		LDateTime s, e;
		if (c->GetCalType() == CalEvent &&
			c->GetField(FIELD_CAL_START_UTC, s))
		{
			int Recur = 0;
			c->GetField(FIELD_CAL_RECUR, Recur);

			const char *Sub = NULL;
			c->GetField(FIELD_CAL_SUBJECT, Sub);

			if (Recur)
			{
				LArray<TimePeriod> Times;
				if (c->GetTimes(Start, End, Times))
				{						    
					SetCalendarsSource(c);
					for (unsigned i=0; i<Times.Length(); i++)
					{
						Events.Add(Times[i]);
					}
				}
			}
			else
			{
				if (!c->GetField(FIELD_CAL_END_UTC, e))
				{
					e = s;
					e.AddHours(1);
				}

				#if 1
				printf("%s: %s > %s, %s < %s\n",
					Sub,
					s.Get().Get(),
					End.Get().Get(),
					e.Get().Get(),
					Start.Get().Get());
				#endif
				if (s > End || e < Start)
				{
					// Is before/after the range
				}
				else
				{
					TimePeriod &tp = Events.New();
					tp.c = c;
					tp.s = s;
					tp.e = e;
					tp.ToLocal();
					SetCalendarsSource(c);
				}
			}
		}
	}

	return true;
}

const char *WebdavStore::GetText(int i)
{
	if (i == 1)
	{
		if (Remote.Name && Remote.Url)
			return Remote.Name;
	}

	return NULL;
}

bool WebdavStore::Delete()
{
	LString k = GetKey();
	bool r = App->GetOptions()->DeleteTag(k);
	if (r)
		App->SaveOptions();
	else
		LAssert(!"Delete failed.");
	return r;
}

bool WebdavStore::Match(char *Email)
{
	bool Status = false;

	return Status;
}

void WebdavStore::SetColour(LColour c)
{
	Colour = c;
	Update();
}

void WebdavStore::OnMouseClick(LMouse &m)
{
	if (m.IsContextMenu())
	{
	}
	else if (m.Down() && m.Left() && Parent)
	{
		int Col = Parent->ColumnAtX(m.x);
		if (Col == 0)
		{
			Display = !Display;
			Update();
			Parent->SendNotify(LNotifyValueChanged);
		}
		else if (Col > 0)
		{
			SetCreateIn(this);
		}
	}
}

void WebdavStore::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
{
	if (i == 0)
	{
		LRect r = Ctx;
		Ctx.pDC->Colour(Ctx.Back);
		for (int i=0; i<4; i++)
		{
			Ctx.pDC->Box(&r);
			r.Size(1, 1);
		}
		
		Ctx.pDC->Colour(Colour);
		if (Display)
			Ctx.pDC->Rectangle(&r);
		else
		{
			Ctx.pDC->Box(&r);
			r.Size(1, 1);
			Ctx.pDC->Colour(Ctx.Back);
			Ctx.pDC->Rectangle(&r);
		}
	}
	else
	{
		bool PathErr = !Thread;
		if (PathErr)
			Ctx.Fore = LColour::Red;
		LListItem::OnPaintColumn(Ctx, i, c);
		if (PathErr)
		{
			Ctx.pDC->Colour(Ctx.Fore);
			int Cy = Ctx.y1 + (Ctx.Y() >> 1) + 1;
			Ctx.pDC->Line(Ctx.x1, Cy, Ctx.x2, Cy);
		}
	}
}
*/

void WebdavStore::OnEvent(void *Param)
{
	WebdavEvent *e = (WebdavEvent*) Param;
	if (!e)
		return;
	
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
	App->PostEvent(M_STORAGE_EVENT, store->Id, (LMessage::Param)Param);
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
