#include "Scribe.h"
#include "Calendar.h"
#include "CalendarView.h"
#include "resdefs.h"

#include "lgi/common/EventTargetThread.h"
#include "lgi/common/Http.h"
#include "lgi/common/vCard-vCal.h"

enum Msgs {
	M_LOAD_URI = M_USER + 1000,
	M_LOADED
};

struct RemoteCalendarSourcePriv : public LEventTargetThread
{
	RemoteCalendarSource *Source;
	LString Uri;
	LString Name;
	bool Error = false;
	bool Loaded = false;
	LArray<Calendar*> Events;

	RemoteCalendarSourcePriv(RemoteCalendarSource *src) :
		Source(src),
		LEventTargetThread("RemoteCalendarSourcePriv")
	{
	}

	~RemoteCalendarSourcePriv()
	{
		for (auto c: Events)
			c->DecRef();
	}

	void Post(int m, LMessage::Param a = 0, LMessage::Param b = 0)
	{
		auto app = Source->GetApp();
		app->PostEvent(	M_CALENDAR_SOURCE_EVENT,
						(LMessage::Param)Source,
						(LMessage::Param)new LMessage(m, a, b));
	}

	LMessage::Result OnEvent(LMessage *Msg)
	{
		switch (Msg->Msg())
		{
			case M_LOAD_URI:
			{
				LString err;
				LStringPipe out;
				auto r = LgiGetUri(this, &out, &err, Uri);
				if (r)
				{
					/*
					auto s = out.NewLStr();
					LgiTrace("s='%s'\n", s.Get());
					*/

					VCal imp;
					while (true)
					{
						Calendar *c = new Calendar(Source->GetApp());
						// LgiTrace("outsize=" LPrintfInt64 "\n", out.GetSize());
						if (imp.Import(c->GetObject(), &out))
							Events.Add(c);
						else
						{
							c->DecRef();
							break;
						}
					}

					Post(M_LOADED);
				}
				break;
			}
		}

		return 0;
	}
};

RemoteCalendarSource::RemoteCalendarSource(ScribeWnd *a, const char *id)
{
	d = new RemoteCalendarSourcePriv(this);
	App = a;
	Id = id;
}

RemoteCalendarSource::~RemoteCalendarSource()
{
	DeleteObj(d);
}

const char *RemoteCalendarSource::GetUri()
{
	return d->Uri;
}

void RemoteCalendarSource::SetUri(const char *uri)
{
	d->Uri = uri;
	Write();
	OnChange(false);
}

LString RemoteCalendarSource::ToString()
{
	LString s;
	s.Printf("%p::RemoteCalendarSource(%s, %s)", this, d->Name.Get(), d->Uri.Get());
	return s;
}

bool RemoteCalendarSource::Read()
{
	if (!Id)
		return false;

	LString k = GetKey();
	LXmlTag *t = App->GetOptions()->LockTag(k, _FL);
	if (!t)
		return false;

	char *Col = t->GetAttr("Colour");
	if (Col)
		Colour.Set((uint32_t)atoi64(Col), 32);
	else
		Colour.Empty();

	d->Uri = t->GetAttr(CalendarSource::OptUri);
	auto c = Atoi(t->GetAttr(CalendarSource::OptColour));
	if (c >= 0)
		Colour.c32((uint32_t)c);
	else
		Colour.Empty();
	Display = t->GetAsInt(CalendarSource::OptDisplay);

	App->GetOptions()->Unlock();
	OnChange(false);

	return true;
}

bool RemoteCalendarSource::Write()
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
	
	if (!Id)
		return false;

	auto Key = GetKey();
	auto t = App->GetOptions()->LockTag(Key, _FL);
	if (!t)
	{
		App->GetOptions()->CreateTag(Key);
		t = App->GetOptions()->LockTag(Key, _FL);
	}
	if (!t)
		return false;

	SaveAttr(t, CalendarSource::OptUri, d->Uri);
	t->SetAttr(CalendarSource::OptColour, (int64_t) Colour.c32());
	t->SetAttr(CalendarSource::OptDisplay, Display);
	t->SetAttr(CalendarSource::OptObject, GetClass());

	App->GetOptions()->Unlock();

	return true;
}

bool RemoteCalendarSource::Delete()
{
	LString k = GetKey();
	bool r = App->GetOptions()->DeleteTag(k);
	if (r)
		App->SaveOptions();
	else
		LAssert(!"Delete failed.");
	return r;
}

Calendar *RemoteCalendarSource::NewEvent()
{
	// Can't create remote events... read only feed.
	return NULL;
}

bool RemoteCalendarSource::Match(char *Email)
{
	return false;
}

bool RemoteCalendarSource::GetEvents(const LDateTime StartTs,
									 const LDateTime EndTs,
									 GetEventCb Callback)
{
	if (!Callback)
		return false;

	LArray<TimePeriod> Events;
	if (!Display)
	{
		Callback(Events);
		return false;
	}

	if (!d->Loaded)
	{
		d->Loaded = true;
		d->PostEvent(M_LOAD_URI);
		
		// FIXME: Should call the callback when loaded...?
		Callback(Events);
		return true;
	}
	
	LDateTime Start = StartTs;
	Start.ToUtc();
	LDateTime End = EndTs;
	End.ToUtc();

	for (auto c: d->Events)
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
				{
					t.src = this;
					Events.Add(t);
				}
			}
		}
	}

	Callback(Events);
	return true;
}

void RemoteCalendarSource::EditPath(LView *parent, CalendarView *cv)
{
	auto Dlg = new LInput(parent, d->Uri);
	Dlg->DoModal([this, Dlg, cv](auto dlg, auto id)
	{
		if (id)
		{
			SetUri(Dlg->GetStr());
			if (cv)
				cv->OnContentsChanged(this);
		}
		delete dlg;
	});
}

LColour RemoteCalendarSource::GetColour()
{
	return Colour;
}

void RemoteCalendarSource::SetColour(LColour c)
{
	Colour = c;
}

const char *RemoteCalendarSource::GetName()
{
	return d->Name;
}

void RemoteCalendarSource::OnMouseClick(LMouse &m)
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

void RemoteCalendarSource::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
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
		bool PathErr = (i == 1 && d->Error);
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

const char *RemoteCalendarSource::GetText(int i)
{
	if (i == 1)
		return d->Uri;

	return NULL;
}

void RemoteCalendarSource::OnFolderDelete(ScribeFolder *f)
{
}

void RemoteCalendarSource::OnPulse()
{
}

void RemoteCalendarSource::OnChange(bool IsDelete)
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

LMessage::Result RemoteCalendarSource::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_LOADED:
		{
			OnChange(false);
			break;
		}
	}

	return 0;
}


