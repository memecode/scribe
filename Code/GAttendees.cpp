#include <math.h>

#include "Lgi.h"
#include "Scribe.h"
#include "Calendar.h"
#include "LToolButton.h"
#include "LDisplayString.h"
#include "../Resources/resdefs.h"

/////////////////////////////////////////////////////////////////////
class Identity
{
	LVariant Name;
	LVariant Email;
	LVariant ReplyTo;

public:
	Identity(ScribeWnd *App)
	{
		ScribeAccount *Ident = App->GetAccounts()->ItemAt(App->GetCurrentIdentity());
		if (Ident)
		{
			Name = Ident->Identity.Name();
			Email = Ident->Identity.Email();
			ReplyTo = Ident->Identity.ReplyTo();
		}
		else
		{
			LAssert(!"No identity selected");
		}
	}

	char *GetName() { return Name.Str(); }
	char *GetEmail() { return Email.Str(); }
	char *GetReplyTo() { return ReplyTo.Str(); }
};

//////////////////////////////////////////////////////////////////////////////////////
#define ScrollPosStart				20
#define ScrollDaySize				10
#define ScrollTotal					100

class LTimeLine : public LLayout
{
	static int DayStart, DayEnd;

	CalendarViewMode Mode;
	Calendar *Cal;
	LList *Lst;
	LFont *Small;
	List<Attendee> Src;

	bool UpdateSrc()
	{
		Src.Empty();
		if (Lst && Cal)
		{
			List<LListItem> Items;
			if (Lst->GetAll(Items))
			{
				for (auto i: Items)
				{
					Attendee *a = dynamic_cast<Attendee*>(i);
					if (a && a->GetAttendeeType() != ANew)
					{
						Src.Insert(a);
					}
				}
			}

			return true;
		}
		return false;
	}

	int GetModeDays()
	{
		switch (Mode)
		{
			case CAL_VIEW_DAY:
			{
				return 1;
			}
			case CAL_VIEW_WEEKDAY:
			{
				return 5;
			}
			case CAL_VIEW_WEEK:
			{
				return 7;
			}
			case CAL_VIEW_MONTH:
			{
				return 30;
			}
			case CAL_VIEW_YEAR:
			{
				return 365;
			}
		}

		return 1;
	}

public:
	LTimeLine(Calendar *cal, LList *lst, CalendarViewMode m)
	{
		Lst = lst;
		Cal = cal;
		SetMode(m);

		Small = 0;
		LFontType t;
		if (t.GetSystemFont("small"))
		{
			Small = t.Create();
		}
	}

	void SetMode(CalendarViewMode m)
	{
		Mode = m;
		Invalidate();
	}

	void OnCreate()
	{
		SetScrollBars(true, false);
		if (HScroll)
		{
			HScroll->SetLimits(0, ScrollTotal);
			HScroll->SetPage(ScrollDaySize);
			HScroll->Value(ScrollPosStart);
		}
	}

	int OnNotify(LViewI *c, int f)
	{
		switch (c->GetId())
		{
			case IDC_HSCROLL:
			{
				Invalidate();
				break;
			}
		}

		return 0;
	}

	void OnPaint(LSurface *pDC)
	{
		LDateTime Start;
		if (UpdateSrc() && Cal->GetField(FIELD_CAL_START_UTC, Start))
		{
			double Scroll = 0;
			if (HScroll)
			{
				Scroll = ((double) HScroll->Value() - ScrollPosStart) / HScroll->Page();
			}
			
			LDateTime From = Start, To, Now;

			Now.SetNow();

			From.Hours(0);
			From.Minutes(0);
			From.Seconds(0);
			From.Thousands(0);
			From.AddDays(-1 * GetModeDays());
			double e = Scroll > 0 ? ceil(Scroll) : floor(Scroll);
			From.AddHours((int)(e * 24));

			To = From;
			To.AddDays(3 * GetModeDays());

			int dx = 300;
			int StartX = 10 - (int)(Scroll * (double)dx);
			int x = StartX - (int)((double)dx * (1 - (double)e));
			int y = 17;

			pDC->Colour(LC_WORKSPACE, 24);
			pDC->Rectangle(0, 0, X(), y);

			Small->Transparent(true);
			for (LDateTime i=From; i<=To; i.AddDays(1), x+=dx)
			{
				#define MapX(Hour) ((limit(Hour, DayStart, DayEnd) - DayStart) * dx / Hours)

				// Today
				COLOUR Background = LC_WORKSPACE;
				if (Now.Day() == i.Day() &&
					Now.Month() == i.Month() &&
					Now.Year() == i.Year())
				{
					Background = GdcMixColour(Background, Rgb24(255, 0, 0), 0.94f);
				}
				pDC->Colour(Background, 24);
				pDC->Rectangle(x, y, x+dx-1, Y());
				
				// Draw backdrop
				char s[64];
				int Hours = DayEnd - DayStart;
				for (int n=DayStart; n<DayEnd; n++)
				{
					COLOUR c = n == 12 ? LC_LOW : LC_MED;
					int xn = MapX(n);
					if (n > DayStart)
					{
						pDC->Colour(c, 24);
						pDC->Line(x + xn, y + 1, x + xn, Y());
					}

					sprintf_s(s, sizeof(s), "%i%c", n, n<12?'a':'p');
					Small->Fore(c);
					LDisplayString ds(Small, s);
					ds.Draw(pDC, x + xn + 2, y - 10);
				}

				pDC->Colour(LC_TEXT, 24);
				pDC->Line(x, y, x, Y());
				pDC->Line(x, y, x+dx-1, y);

				i.GetDate(s, sizeof(s));
				Small->Fore(LC_TEXT);
				LDisplayString ds(Small, s);
				ds.Draw(pDC, x, -1);

				// Draw events
				for (auto a: Src)
				{
					LRect *p = a->GetPos();
					if (p)
					{
						LRect r(x, p->y1, x + dx - 1, p->y2);
						LArray<TimePeriod> Cals;
						LDateTime End = i;
						End.AddDays(1);
						if (a->GetFreeBusy(i, End, Cals))
						{
							// Draw events
							for (unsigned i=0; i<Cals.Length(); i++)
							{
								Calendar *c = Cals[i].c;
								LColour Busy = c->GetColour();
								COLOUR Tentitive = GdcMixColour(Busy.c24(), LC_WORKSPACE, 0.3f);
								COLOUR Out = GdcMixColour(Busy.c24(), LC_WORKSPACE, 0.6f);

								LDateTime Cs, Ce; // Calendar event start, end
								if (c->GetField(FIELD_CAL_START_UTC, Cs))
								{
									if (!c->GetField(FIELD_CAL_END_UTC, Ce))
									{
										Ce = Cs;
										Ce.AddHours(1);
									}

									double h = (double)Cs.Hours() + ((double)Cs.Minutes() / 60);
									int Xs = x + ((int) ((h - (double)DayStart) * dx) / Hours);
									h = (double)Ce.Hours() + ((double)Ce.Minutes() / 60);
									int Xe = x + ((int) ((h - (double)DayStart) * dx) / Hours);
									CalendarShowTimeAs ShowTime = CalFree;
									c->GetField(FIELD_CAL_SHOW_TIME_AS, (int&)ShowTime);
									switch (ShowTime)
									{
										default:
										case CalFree:
										{
											// Don't display anything
											continue;
										}
										case CalTentative:
										{
											pDC->Colour(Tentitive, 24);
											break;
										}
										case CalBusy:
										{
											pDC->Colour(Busy);
											break;
										}
										case CalOut:
										{
											pDC->Colour(Out, 24);
											break;
										}
									}

									LRect b(Xs, r.y1, Xe, r.y2-1);
									pDC->Rectangle(&b);

									char *Name;
									if (c->GetField(FIELD_CAL_SUBJECT, Name))
									{
										if (abs((int) (GdcGreyScale(LC_TEXT) - GdcGreyScale(pDC->Colour()))) < 100)
										{
											LSysFont->Fore(LC_WORKSPACE);
										}
										else
										{
											LSysFont->Fore(LC_TEXT);
										}
										LSysFont->Transparent(true);
										LDisplayString ds(LSysFont, Name);
										ds.Draw(pDC, b.x1+1, b.y1, &b);
									}
								}
							}
						}
						else
						{
							// Draw no info
							pDC->Colour(LC_MED, 24);
							for (int k=r.y1%10-10; k<r.x2; k += 10)
							{
								pDC->Line(k, r.y1, k + r.Y() - 1, r.y2);
							}
						}
					}
				}
			}			
		}
		else
		{
			pDC->Colour(LC_WORKSPACE, 24);
			pDC->Rectangle();
		}
	}
};

int LTimeLine::DayStart = 7;
int LTimeLine::DayEnd = 23;

//////////////////////////////////////////////////////////////////////////////////////
LAttendees::LAttendees() : ResObject(Res_Custom)
{
	Cal = 0;
	Users = 0;
	TimeLine = 0;
	App = NULL;
	Splitter = new LSplitter;
	if (Splitter)
	{
		Splitter->IsVertical(true);
		Splitter->Value(230);
		Splitter->Raised(false);
		
		Splitter->SetViewA(Users = new LList(IDC_LIST, 0, 0, 100, 100));
	}
}

void LAttendees::Save(List<Attendee> &To)
{
	if (Cal && Users)
	{
		To.DeleteObjects();
		
		List<Attendee> All;
		Users->GetAll(All);
		for (auto a: All)
		{
			if (a->GetAttendeeType() != ANew)
			{
				a->OnSerialize(true);
				To.Insert(a);
			}
		}

		Users->RemoveAll();
	}
}

void LAttendees::Load(Calendar *cal, List<Attendee> &From)
{
	Cal = cal;
	if (Splitter && Cal && Users && App)
	{
		Splitter->SetViewB(TimeLine = new LTimeLine(Cal, Users, CAL_VIEW_DAY));

		Identity Id(App);
		if (Id.GetEmail() &&
			Id.GetName())
		{
			Users->Insert(new Attendee(Cal, AMeetingOrganiser, Id.GetName(), Id.GetEmail()));

			for (auto a: From)
			{
				if (a->GetAttendeeType() != ANew)
				{
					Users->Insert(new Attendee(App, a));
				}
			}

			Users->Insert(new Attendee(Cal, ANew));
		}
	}
}

void LAttendees::OnCreate()
{
	if (Splitter)
	{
		Splitter->Attach(this);
	}

	ThingUi *Ui = dynamic_cast<ThingUi*>(GetWindow());
	LAssert(Ui != NULL);
	if (Users && Ui && Ui->App)
	{
		App = Ui->App;
		Users->SetImageList(Ui->App->GetIconImgList(), false);
		Users->Sunken(false);
		Users->AddColumn("", 20);
		Users->AddColumn("All Attendees", (int)Splitter->Value()-9-(16*2));
	}
}

void LAttendees::OnPosChange()
{
	LRect c = GetClient();
	Splitter->SetPos(c, true);
}

int LAttendees::OnNotify(LViewI *c, int f)
{
	switch (c->GetId())
	{
		case IDC_LIST:
		{
			switch (f)
			{
				case LNotifyItemInsert:
				case LNotifyItemDelete:
				case LNotifyItemChange:
				{
					SendNotify();
					break;
				}
			}
			break;
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////
class LAttendeesFactory : public GViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (Class &&
			_stricmp(Class, "LAttendees") == 0)
		{
			return new LAttendees;
		}

		return 0;
	}
} AttendeesFactory;
