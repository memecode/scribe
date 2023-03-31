#include "Scribe.h"
#include "CalendarView.h"
#include "resdefs.h"

/////////////////////////////////////////////////////////////////////////////////////
LArray<CalendarSource*> CalendarSource::AllSources;

LString CalendarSource::GetKey()
{
	LString k;
	if (Id)
		k.Printf("%s.%s", OPT_CalendarSources, Id.Get());
	return k;
}

LColour CalendarSource::GetColour()
{
	return Colour;
}

/////////////////////////////////////////////////////////////////////////////////////
FolderCalendarSource::FolderCalendarSource(ScribeWnd *a, const char *id)
{
	Id = id;
	App = a;
	Folder = NULL;
}

FolderCalendarSource::~FolderCalendarSource()
{
}

void FolderCalendarSource::OnPulse()
{
	if (!Folder)
	{
		Folder = App->GetFolder(Path);
		if (Folder)
			OnChange(false);
	}
}

void FolderCalendarSource::OnFolderDelete(ScribeFolder *f)
{
	if (Folder == f)
	{
		Folder = NULL;
		OnChange(true);
	}
}

void FolderCalendarSource::SetColour(LColour c)
{
	Colour = c;
	OnChange(false);
}

bool FolderCalendarSource::Delete()
{
	LString k = GetKey();
	bool r = App->GetOptions()->DeleteTag(k);
	if (r)
	{
		if (Folder)
			App->RemoveThingSrc(Folder);
		App->SaveOptions();
	}
	else
		LAssert(!"Delete failed.");
	return r;
}

void FolderCalendarSource::SetPath(const char *p)
{
	Path = p;	
	Folder = App->GetFolder(Path);
	if (Path)
	{
		Write();
		OnChange(false);
	}
}

void FolderCalendarSource::OnChange(bool IsDelete)
{
	Update();

	if (!GetList())
		return;

	auto w = GetList()->GetWindow();
	if (!w)
		return;

	CalendarView *cv = NULL;
	if (!w->GetViewById(IDC_CALENDAR, cv))
		return;

	if (IsDelete)
		cv->OnSourceDelete(this);
	else
		cv->OnContentsChanged(this);
}

bool FolderCalendarSource::Read()
{
	if (!Folder)
	{
		if (Id)
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

				Path = t->GetAttr("Path");
				Display = t->GetAsInt("Display");
				Folder = App->GetFolder(Path);

				App->GetOptions()->Unlock();
				OnChange(false);

				return true;
			}
		}
		else LAssert(0);
	}

	return Folder != NULL;
}

bool FolderCalendarSource::Write()
{
	LVariant v;

	if (!Id)
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
					Id = Key;
					break;
				}
			}
			
			App->GetOptions()->Unlock();
		}		
	}
	
	if (Id)
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
			SaveAttr(t, CalendarSource::OptPath, Path);
			t->SetAttr(CalendarSource::OptColour, (int64) Colour.c32());
			t->SetAttr(CalendarSource::OptDisplay, Display);
			t->SetAttr(CalendarSource::OptObject, GetClass());

			App->GetOptions()->Unlock();
		}
		else return false;
	}

	return true;
}

Calendar *FolderCalendarSource::NewEvent()
{
	Calendar *c = new Calendar(App);
	if (!c)
	{
		return NULL;
	}

	c->App = App;
	if (!Folder)
	{
		Folder = App->GetFolder(Path);
	}

	if (!Folder)
	{
		LAssert(!"No folder?");
		DeleteObj(c);
		return NULL;
	}

	LDataStoreI *Ms = Folder->GetObject()->GetStore();
	if (!Ms)
	{
		LAssert(!"No mail store?");
		DeleteObj(c);
		return NULL;
	}

	c->SetObject(Ms->Create(c->Type()), false, _FL);
	SetParentFolder(c, Folder);

	return c;
}

bool FolderCalendarSource::Match(char *Email)
{
	bool Status = false;

	return Status;
}

void FolderCalendarSource::EditPath(LView *parent, CalendarView *cv)
{
	if (!GetPath())
		return;

	auto Dlg = new FolderDlg(parent, App, MAGIC_CALENDAR);
	Dlg->DoModal([this, Dlg, cv](auto dlg, auto ctrlId)
	{
		if (ctrlId)
		{
			SetPath(Dlg->Get());
			if (cv)
				cv->OnContentsChanged(this);
		}
		delete dlg;
	});
}

bool FolderCalendarSource::GetEvents(const LDateTime StartTs,
									 const LDateTime EndTs,
									 GetEventCb Callback)
{
	Read();

	if (!Callback)
		return false;

	if (!Display || !Folder)
	{
		LArray<TimePeriod> Empty;
		Callback(Empty);
		return false;
	}
	
	Folder->LoadThings(NULL, [this, StartTs, EndTs, Callback](auto Status)
	{
		LArray<TimePeriod> Events;

		LDateTime Start = StartTs;
		Start.ToUtc();
		LDateTime End = EndTs;
		End.ToUtc();

		LArray<Calendar*> Search;

		for (auto t : Folder->Items)
		{
			Calendar *c = t->IsCalendar();
			if (c)
				Search.Add(c);
		}

		for (auto c: Search)
		{
			LDateTime s;
			if (c->GetCalType() == CalEvent &&
				c->GetField(FIELD_CAL_START_UTC, s))
			{
				LArray<TimePeriod> Times;
				if (c->GetTimes(Start, End, Times))
				{						    
					SetCalendarsSource(c);

					for (auto &t: Times)
						t.src = this;
					
					Events.Add(Times);
				}
			}
		}

		Callback(Events);
	});

	return true;
}

void FolderCalendarSource::OnMouseClick(LMouse &m)
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

void FolderCalendarSource::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
{
	if (i == 0)
	{
		LRect r = Ctx;
		Ctx.pDC->Colour(Ctx.Back);
		for (int i=0; i<4; i++)
		{
			Ctx.pDC->Box(&r);
			r.Inset(1, 1);
		}
		
		Ctx.pDC->Colour(Colour);
		if (Display)
			Ctx.pDC->Rectangle(&r);
		else
		{
			Ctx.pDC->Box(&r);
			r.Inset(1, 1);
			Ctx.pDC->Colour(Ctx.Back);
			Ctx.pDC->Rectangle(&r);
		}
	}
	else
	{
		bool PathErr = (i == 1 && Path && !Folder);
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

const char *FolderCalendarSource::GetText(int i)
{
	if (i == 1)
	{
		if (Folder && !Path)
			Path = Folder->GetPath();

		return Path;
	}

	return NULL;
}

CalendarSource *CalendarSource::CreateIn = 0;
void CalendarSource::SetCreateIn(CalendarSource *New)
{
	if (CreateIn != New)
	{
		CreateIn = New;
		if (CreateIn)
		{
			if (CreateIn->Id)
			{
				LVariant v;
				v = CreateIn->Id.Get();
				CreateIn->App->GetOptions()->SetValue(OPT_CalendarCreateIn, v);
			}
			else if (CreateIn->App)
			{
				CreateIn->App->GetOptions()->DeleteValue(OPT_CalendarCreateIn);
			}
		}
	}
}


