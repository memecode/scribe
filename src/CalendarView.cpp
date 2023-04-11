#include "Scribe.h"

#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/ListItemRadioBtn.h"
#include "lgi/common/ColourSelect.h"
#include "lgi/common/DropFiles.h"
#include "lgi/common/MonthView.h"
#include "lgi/common/YearView.h"
#include "lgi/common/Array.h"
#include "lgi/common/ScrollBar.h"
#include "lgi/common/MonthView.h"
#include "lgi/common/SkinEngine.h"
#include "lgi/common/Combo.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/Printer.h"
#include "lgi/common/Notifications.h"
#include "lgi/common/Box.h"
#include "lgi/common/LgiRes.h"

#include "CalendarView.h"
#include "ScribePageSetup.h"
#include "Store3Webdav/WebdavStore.h"
#include "resdefs.h"
#include "resource.h"

char ScribeCalendarObject[] =	"com.memecode.Calendar";

#define BORDER_YEAR				28
#define THRESHOLD_EDGE			3		// Px

#define WEEKEND_TINT_LEVEL		0.97f
#define WEEKEND_TINT_COLOUR		LColour(0, 0, 0xd0)
#define TODAY_TINT_LEVEL		0.95f
#define TODAY_TINT_COLOUR		LColour(255, 0, 0)

/////////////////////////////////////////////////////////////////////////////////////
#define IDM_CAL_DAY				100
#define IDM_CAL_WEEK			101
#define IDM_CAL_MONTH			102
#define IDM_CAL_YEAR			103

#define IDM_PREV				200
#define IDM_BACK				201
#define IDM_TODAY				202
#undef IDM_FORWARD
#define IDM_FORWARD				203
#define IDM_NEXT				204
#define IDM_CONFIG				205
#define IDM_TODO				206
#define IDM_15MIN				207
#define IDM_30MIN				208
#define IDM_1HR					209
#define IDM_NEW_EVENT			210

#define IDC_TODO				300

/////////////////////////////////////////////////////////////////////////////////////
void LoadCalendarStringTable()
{
	ShortDayNames[0] = LLoadString(IDS_CAL_SDAY_SUN);
	ShortDayNames[1] = LLoadString(IDS_CAL_SDAY_MON);
	ShortDayNames[2] = LLoadString(IDS_CAL_SDAY_TUE);
	ShortDayNames[3] = LLoadString(IDS_CAL_SDAY_WED);
	ShortDayNames[4] = LLoadString(IDS_CAL_SDAY_THU);
	ShortDayNames[5] = LLoadString(IDS_CAL_SDAY_FRI);
	ShortDayNames[6] = LLoadString(IDS_CAL_SDAY_SAT);

	FullDayNames[0] = LLoadString(IDS_CAL_LDAY_SUN);
	FullDayNames[1] = LLoadString(IDS_CAL_LDAY_MON);
	FullDayNames[2] = LLoadString(IDS_CAL_LDAY_TUE);
	FullDayNames[3] = LLoadString(IDS_CAL_LDAY_WED);
	FullDayNames[4] = LLoadString(IDS_CAL_LDAY_THU);
	FullDayNames[5] = LLoadString(IDS_CAL_LDAY_FRI);
	FullDayNames[6] = LLoadString(IDS_CAL_LDAY_SAT);

	ShortMonthNames[0] = LLoadString(IDS_CAL_SMONTH_JAN);
	ShortMonthNames[1] = LLoadString(IDS_CAL_SMONTH_FEB);
	ShortMonthNames[2] = LLoadString(IDS_CAL_SMONTH_MAR);
	ShortMonthNames[3] = LLoadString(IDS_CAL_SMONTH_APR);
	ShortMonthNames[4] = LLoadString(IDS_CAL_SMONTH_MAY);
	ShortMonthNames[5] = LLoadString(IDS_CAL_SMONTH_JUN);
	ShortMonthNames[6] = LLoadString(IDS_CAL_SMONTH_JUL);
	ShortMonthNames[7] = LLoadString(IDS_CAL_SMONTH_AUG);
	ShortMonthNames[8] = LLoadString(IDS_CAL_SMONTH_SEP);
	ShortMonthNames[9] = LLoadString(IDS_CAL_SMONTH_OCT);
	ShortMonthNames[10] = LLoadString(IDS_CAL_SMONTH_NOV);
	ShortMonthNames[11] = LLoadString(IDS_CAL_SMONTH_DEC);

	FullMonthNames[0] = LLoadString(IDS_CAL_LMONTH_JAN);
	FullMonthNames[1] = LLoadString(IDS_CAL_LMONTH_FEB);
	FullMonthNames[2] = LLoadString(IDS_CAL_LMONTH_MAR);
	FullMonthNames[3] = LLoadString(IDS_CAL_LMONTH_APR);
	FullMonthNames[4] = LLoadString(IDS_CAL_LMONTH_MAY);
	FullMonthNames[5] = LLoadString(IDS_CAL_LMONTH_JUN);
	FullMonthNames[6] = LLoadString(IDS_CAL_LMONTH_JUL);
	FullMonthNames[7] = LLoadString(IDS_CAL_LMONTH_AUG);
	FullMonthNames[8] = LLoadString(IDS_CAL_LMONTH_SEP);
	FullMonthNames[9] = LLoadString(IDS_CAL_LMONTH_OCT);
	FullMonthNames[10] = LLoadString(IDS_CAL_LMONTH_NOV);
	FullMonthNames[11] = LLoadString(IDS_CAL_LMONTH_DEC);
}

/////////////////////////////////////////////////////////////////////////////////////
class LColourItem : public LListItemColumn
{
	LListItem *Item;
	LView *Colour;

public:
	LColourItem(LListItem *i, int c, COLOUR col = -1) : LListItemColumn(i, c)
	{
		Item = i;
		Colour = LViewFactory::Create("LColourSelect");
		LAssert(Colour != NULL);

		LArray<LColour> colours;
		for (int n=0; n<CountOf(MarkColours32); n++)
		{
			colours[n].Set(MarkColours32[n], 32);
		}

		if (Colour)
		{
			((LColourSelect*)Colour)->SetColourList(&colours);
			Colour->Value(col);
		}
	}

	~LColourItem()
	{
		DeleteObj(Colour);
	}

	void OnPaintColumn(ItemPaintCtx &Ctx, int i, LItemColumn *c)
	{
		if (!Colour->IsAttached())
		{
			Colour->Attach(Item->GetList());
		}
		Colour->SetPos(Ctx);
		Colour->Visible(true);
	}

	void OnMouseClick(LMouse &m)
	{
		Colour->OnMouseClick(m);
	}

	int64 Value()
	{
		return Colour->Value();
	}

	void Value(int64 i)
	{
		Colour->Value(i);
	}

	void Disconnect()
	{
		Colour->Detach();
	}
};

/////////////////////////////////////////////////////////////////////////////////////
CalendarTodoItem::CalendarTodoItem(ScribeWnd *app, Calendar *todo)
{
	App = app;
	Todo = 0;
	Done = 0;
	SetTodo(todo);
	EditingLabel = false;
}

CalendarTodoItem::~CalendarTodoItem()
{
	if (Todo)
	{
		LAssert(Todo->TodoView == this);
		Todo->TodoView = 0;
	}
}

int TodoCompare(LListItem *la, LListItem *lb, NativeInt d)
{
	int Status = 0;
	CalendarTodoItem *a = dynamic_cast<CalendarTodoItem*>(la);
	CalendarTodoItem *b = dynamic_cast<CalendarTodoItem*>(lb);
	if (a && b)
	{
		int Col = abs((int)d) - 1;
		int Mul = d >= 0 ? 1 : -1;

		bool Atodo = a->Todo != 0;
		bool Btodo = b->Todo != 0;
		if (Atodo ^ Btodo)
		{
			Status = Btodo - Atodo;
		}
		else if (a->Todo && b->Todo)
		{
			switch (Col)
			{
				case 0:
				{
					// Completed
					int Acomplete = 0;
					int Bcomplete = 0;
					a->Todo->GetField(FIELD_CAL_COMPLETED, Acomplete);
					b->Todo->GetField(FIELD_CAL_COMPLETED, Bcomplete);
					if (Acomplete != Bcomplete)
					{
						Status = Mul * (Acomplete - Bcomplete);
					}
					else
					{
						goto ByDueDate;
					}
					break;
				}
				case 1:
				{
					// Subject
					BySubject:
					const char *Asub = 0;
					const char *Bsub = 0;
					a->Todo->GetField(FIELD_CAL_SUBJECT, Asub);
					b->Todo->GetField(FIELD_CAL_SUBJECT, Bsub);
					if (Asub && Bsub)
					{
						Status = Mul * _stricmp(Asub, Bsub);
					}
					break;
				}
				case 2:
				{
					// Due date
					ByDueDate:
					LDateTime Adate;
					LDateTime Bdate;
					a->Todo->GetField(FIELD_CAL_START_UTC, Adate);
					b->Todo->GetField(FIELD_CAL_START_UTC, Bdate);
					bool Ad = Adate.Year() != 0;
					bool Bd = Bdate.Year() != 0;
					if (Ad ^ Bd)
					{
						Status = Mul * (Bd - Ad);
					}
					else if (Ad && Bd)
					{
						Status = Mul * Adate.Compare(&Bdate);
					}
					else
					{
						goto BySubject;
					}
					break;
				}
			}
		}
	}
	return Status;
}

void CalendarTodoItem::Resort()
{
	if (GetList())
	{
		int Sort = 1;
		for (int i=0; i<GetList()->GetColumns(); i++)
		{
			LItemColumn *c = GetList()->ColumnAt(i);
			if (c)
			{
				if (c->Mark() == GLI_MARK_UP_ARROW)
				{
					Sort = -(i + 1);
					break;
				}
				else if (c->Mark() == GLI_MARK_DOWN_ARROW)
				{
					Sort = i + 1;
					break;
				}
			}
		}

		GetList()->Sort<NativeInt>(TodoCompare, Sort);
	}
}

void CalendarTodoItem::SetTodo(Calendar *todo)
{
	if (Todo)
	{
		Todo->TodoView = 0;
	}
	Todo = todo;
	if (Todo)
	{
		Todo->TodoView = this;

		int Completed = false;
		Todo->GetField(FIELD_CAL_COMPLETED, Completed);
		Done = new LListItemCheckBox(this, 0, Completed != 0);
	}
	else
	{
		DeleteObj(Done);
	}
	SetImage(Todo ? ICON_TODO : -1);
}

const char *CalendarTodoItem::GetText(int Col)
{
	if (Todo)
	{
		switch (Col)
		{
			case 1:
			{
				// Name
				const char *s;
				if (Todo->GetField(FIELD_CAL_SUBJECT, s))
				{
					return s;
				}
				break;
			}
			case 2:
			{
				// Due
				LDateTime d;
				if (Todo->GetField(FIELD_CAL_START_UTC, d))
				{
					d.ToLocal(true);
					d.Get(DateCache, sizeof(DateCache));
					return DateCache;
				}
				break;
			}
		}
	}
	else if (Col == 1)
	{
		if (EditingLabel)
		{
			EditingLabel = false;
		}
		else
		{
			return (char*)LLoadString(IDS_CLICK_TO_CREATE);
		}
	}

	return 0;
}

bool CalendarTodoItem::SetText(const char *s, int Col)
{
	if (Col == 1)
	{
		if (Todo)
		{
			// Set the name...
			Todo->SetField(FIELD_CAL_SUBJECT, (char*)s);
			Resort();
		}
		else
		{
			if (ValidStr(s))
			{
				// Create new todo
				ScribeFolder *Cal = App->GetFolder(FOLDER_CALENDAR);
				if (Cal)
				{
					Thing *t = App->CreateItem(MAGIC_CALENDAR, Cal, false);
					if (t && t->IsCalendar())
					{
						SetTodo(t->IsCalendar());
						Todo->SetCalType(CalTodo);
						Todo->SetField(FIELD_CAL_SUBJECT, (char*)s);
						GetList()->Insert(new CalendarTodoItem(App));
						Resort();
					}
				}
				else
				{
					LgiMsg(GetList(), "No calendar folder.", AppName);
				}
			}
			else
			{
				return false;
			}
		}
	}

	return LListItem::SetText(s, Col);
}

void CalendarTodoItem::OnPaint(ItemPaintCtx &Ctx)
{
	if (!Todo)
	{
		Ctx.Fore = LColour(L_LOW);
		Ctx.Back = LColour(L_WORKSPACE);
	}
	else if (Done && Done->Value())
	{
		Ctx.Fore = LColour(L_LOW);
	}
	else
	{
		LDateTime d, Now;
		if (Todo->GetField(FIELD_CAL_START_UTC, d))
		{
			d.ToLocal(true);
			Now.SetNow();
			if (Now.Compare(&d) > 0)
			{
				Ctx.Fore.Set(255, 0, 0);
			}
		}
	}

	LListItem::OnPaint(Ctx);
}

void CalendarTodoItem::OnColumnNotify(int Col, int64 Data)
{
	if (Col == 0)
	{
		Todo->SetField(FIELD_CAL_COMPLETED, Data ? 100 : 0);
		Resort();
	}
}

void CalendarTodoItem::OnMouseClick(LMouse &m)
{
	LListItem::OnMouseClick(m);

	if (m.Down())
	{
		if (m.Left())
		{
			int Col = Todo ? GetList()->ColumnAtX(m.x) : 1;
			switch (Col)
			{
				case 1:
				{
					if (!Todo)
					{
						EditingLabel = true;
					}
					EditLabel(1);
					break;
				}
				default:
				{
					if (Todo && m.Double())
					{
						Todo->DoUI();
					}
					break;
				}
			}
		}
		else if (m.Right())
		{
			if (Todo)
			{
				Todo->DoContextMenu(m, GetList());
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////
class LMonthView : public LView, public MonthView
{
	LRect rTitle;
	LRect rCells;
	LRect rLeft, rRight;
	int Cell;
	CalendarView *CalView;
	
public:
	LMonthView(int Id, LDateTime *n, CalendarView *calView);
	
	void OnCellClick(int Cx, int Cy);
	void SeekMonth(int Dir);
	void OnMouseClick(LMouse &m);
	void OnPaint(LSurface *pDC);
};

// This structure encodes the direction to move the cursor
// on different key presses. The 'years' isn't implemented in
// the OnKey handler.
struct CalViewArrow
{
public:
	CalendarViewMode Mode;
	int Key;
	int Flags;

	int Hours;
	int Days;
	int Months;
	int Years;
};

CalViewArrow Arrows[] =
{
	//								Flags,			H	D	M	Y

	// Week view movement
	{CAL_VIEW_WEEK,		LK_LEFT,	0,				0,	-1,	0,	0},
	{CAL_VIEW_WEEK,		LK_RIGHT,	0,				0,	1,	0,	0},
	{CAL_VIEW_WEEK,		LK_UP,		0,				-1,	0,	0,	0},
	{CAL_VIEW_WEEK,		LK_DOWN,	0,				1,	0,	0,	0},
	{CAL_VIEW_WEEK,		LK_PAGEUP,	0,				0,	-7,	0,	0},
	{CAL_VIEW_WEEK,		LK_PAGEDOWN,0,				0,	7,	0,	0},
	{CAL_VIEW_WEEK,		LK_PAGEUP,	LGI_EF_CTRL,	0,	0,	-1,	0},
	{CAL_VIEW_WEEK,		LK_PAGEDOWN,LGI_EF_CTRL,	0,	0,	1,	0},

	// Month view movement
	{CAL_VIEW_MONTH,	LK_LEFT,	0,				0,	-1,	0,	0},
	{CAL_VIEW_MONTH,	LK_RIGHT,	0,				0,	1,	0,	0},
	{CAL_VIEW_MONTH,	LK_UP,		0,				0,	-7,	0,	0},
	{CAL_VIEW_MONTH,	LK_DOWN,	0,				0,	7,	0,	0},
	{CAL_VIEW_MONTH,	LK_PAGEUP,	0,				0,	0,	-1,	0},
	{CAL_VIEW_MONTH,	LK_PAGEDOWN,0,				0,	0,	1,	0},
	{CAL_VIEW_MONTH,	LK_PAGEUP,	LGI_EF_CTRL,	0,	0,	0,	-1},
	{CAL_VIEW_MONTH,	LK_PAGEDOWN,LGI_EF_CTRL,	0,	0,	0,	1},

	// Year view movement
	{CAL_VIEW_YEAR,		LK_LEFT,	0,				0,	-1,	0,	0},
	{CAL_VIEW_YEAR,		LK_RIGHT,	0,				0,	1,	0,	0},
	{CAL_VIEW_YEAR,		LK_UP,		0,				0,	0,	-1,	0},
	{CAL_VIEW_YEAR,		LK_DOWN,	0,				0,	0,	1,	0},
	{CAL_VIEW_YEAR,		LK_PAGEUP,	0,				0,	0,	0,	-1},
	{CAL_VIEW_YEAR,		LK_PAGEDOWN,0,				0,	0,	0,	1}
};

CalViewArrow *GetModeArrow(CalendarViewMode m, LKey &k)
{
	for (int i=0; i<CountOf(Arrows); i++)
	{
		int Mask = LGI_EF_CTRL | LGI_EF_ALT | LGI_EF_SHIFT;
		if (Arrows[i].Mode == m &&
			Arrows[i].Key == k.c16 &&
			((Arrows[i].Flags == 0 && (k.Flags & Mask) == 0) ||
				(Arrows[i].Flags & Mask & k.Flags) != 0))
		{
			return Arrows+i;
		}
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
ScribeWnd *CalendarView::App = NULL;
int CalendarView::SnapMinutes = 30;
int CalendarView::FirstDayOfWeek = 0;
LArray<CalendarView*> CalendarView::CalendarViews;

CalendarView::CalendarView(ScribeFolder *folder, int id, LRect *pos, const char *name)
{
	CalendarViews.Add(this);
	if (Calendar::DayStart < 0)
		InitCalendarView();

	DragMode = DragNone;
	DragEvent = NULL;
	LastTsOffset = 0;
	DayStart = Calendar::DayStart;
	DayEnd = Calendar::DayEnd;

	if (!App)
		App = folder->App;
	Mode = CAL_VIEW_MONTH;

	LVariant v;
	if (App->GetOptions()->GetValue(OPT_CalendarViewMode, v))
		Mode = (CalendarViewMode) v.CastInt32();
	
	if (id > 0) SetId(id);
	if (pos) SetPos(*pos);
	if (name) Name(name);
	SetPourLargest(true);
	Sunken(true);

	MonthX = 7;
	MonthY = 5;

	LFontType Type;
	Type.GetSystemFont("Small");
	Font.Reset(Type.Create());

	Cursor.SetNow();
	LoadUsers();

	OnOptionsChange();
}

CalendarView::~CalendarView()
{
	LVariant v;
	App->GetOptions()->SetValue(OPT_CalendarViewMode, v = (int)Mode);
	CalendarViews.Delete(this);
}

void CalendarView::OnOptionsChange()
{
	LVariant v;
	if (App &&
		App->GetOptions()->GetValue(OPT_CalendarFirstDayOfWeek, v))
		FirstDayOfWeek = v.CastInt32();

	for (auto c: CalendarViews)
		c->OnContentsChanged();
}

CalendarView *Calendar::GetView()
{
	if (CalendarView::CalendarViews.Length() == 0)
		return NULL;
	return CalendarView::CalendarViews[0];
}

void CalendarView::LoadUsers()
{
	LArray<CalendarSource*> Sources;
	App->GetCalendarSources(Sources);

	LVariant v;
	if (App->GetOptions()->GetValue(OPT_CalendarFirstDayOfWeek, v))
		FirstDayOfWeek = v.CastInt32();

	LDateTime dt = Cursor;
	Cursor.Set("1/1/1900");

	SetCursor(dt);

	// Do we really need this?
	// OnContentsChanged();
}

void CalendarView::DeleteSource(CalendarSource *cs)
{
    // Delete events out of 'Current'
	for (unsigned i=0; i<Current.Length(); i++)
	{
		TimePeriod &t = Current[i];
		if (t.c->GetSource() == cs)
		{
			Current.DeleteAt(i--);
		}
	}

	// Delete the reference in the options file...
	cs->Delete();

	// Delete the C++ object and list ref...
	DeleteObj(cs);

	// Refresh the screen.
	Invalidate();
}

bool CalendarView::GetEventsBetween(LArray<TimePeriod> &Events, LDateTime Start, LDateTime End)
{
	bool Status = false;
	LDateTime EndMinute = End;
	EndMinute.AddMinutes(-1);

	for (unsigned i=0; i<Current.Length(); i++)
	{
		if (Current[i].Overlap(Start, End))
		{
			Events.Add(Current[i]);
			Status = true;
		}
	}

	return Status;
}

void CalendarView::OnCreate()
{
	SetWindow(this);
	SetupScroll();
	SetPulse(3000);
}

void CalendarView::OnPulse()
{
	CalendarViewWnd *w = dynamic_cast<CalendarViewWnd*>(GetWindow());
	LArray<CalendarSource*> all;
	if (w->CalLst->GetAll(all))
	{
		for (auto a: all)
			a->OnPulse();
	}	
}

bool CalendarView::OnLayout(LViewLayoutInfo &Inf)
{
	if (Inf.Width.Max == 0)
	{
		Inf.Width.Min = -1;
		Inf.Width.Max = -1;
	}
	else
	{
		Inf.Height.Min = -1;
		Inf.Height.Max = -1;
	}
	
	return true;
}

int CalendarView::OnNotify(LViewI *v, LNotification n)
{
	switch (v->GetId())
	{
		case IDC_VSCROLL:
		{
    	    if (n.Type == LNotifyScrollBarCreate)
    	    {
    	        SetupScroll();
    	    }

		    Invalidate();
			break;
		}
	}

	return 0;
}

void CalendarView::SelectDropTarget(LDateTime *start, LDateTime *end)
{
	/*
	if (start && DropStart)
	{
		if (*start == *DropStart)
		{
			// the same
			return;
		}
	}

	DeleteObj(DropStart);
	DeleteObj(DropEnd);
	if (start)
	{
		DropStart = new LDateTime;
		if (DropStart)
		{
			*DropStart = *start;
		}

		if (end)
		{
			DropEnd = new LDateTime;
			if (DropEnd)
			{
				*DropEnd = *end;
			}
		}
	}

	Invalidate();
	*/
}

void CalendarView::OnSelect(Calendar *c, bool Ctrl, bool Shift)
{
	if (!Ctrl)
	{
		Selection.Length(0);
	}
	else
	{
		Selection.Delete(c);
	}

	if (c)
	{
		Selection.Add(c);
		Invalidate(&c->ViewPos);
	}
}

CalendarViewMode CalendarView::GetViewMode()
{
	return Mode;
}

void CalendarView::SetupScroll()
{
	SetScrollBars(false, Mode == CAL_VIEW_WEEK);
	if (VScroll)
	{
		int Page = (int)(Calendar::DayEnd - Calendar::DayStart);
		VScroll->SetRange(24);
		VScroll->SetPage(Page);
		VScroll->Value(Calendar::DayStart);
	}
}

void CalendarView::CalDelete(Calendar *c)
{
	Selection.Delete(c);

	for (size_t i=0; i<Current.Length(); i++)
	{
		if (Current[i].c == c)
			Current.DeleteAt(i--); // Could be multiple number of them
	}
}

void CalendarView::SetViewMode(CalendarViewMode m)
{
	Mode = m;

	SetCursor(Cursor);
	Invalidate();
	SetupScroll();
	OnCursorChange();
}

LDateTime &CalendarView::GetCursor()
{
	return Cursor;
}

void CalendarView::SetCursor(LDateTime &c)
{
	LDateTime Old = Cursor;
	Cursor = c;
	SendNotify(LNotifyCursorChanged);

	switch (Mode)
	{
		default:
			LAssert(0);
			break;
		/*
		case CAL_VIEW_DAY:
		{
			break;
		}
		case CAL_VIEW_WEEKDAY:
		{
			break;
		}
		*/
		case CAL_VIEW_WEEK:
		{
			Start = Cursor;
			int DayOfWeek = Start.DayOfWeek();
			int Diff = FirstDayOfWeek - DayOfWeek;
			if (Diff > 0)
				Diff -= 7;
			Start.AddDays(Diff);
			First = Start;
			break;
		}
		case CAL_VIEW_MONTH:
		{
			First = Cursor;
			First.Day(1);
			
			Start = First;
			int DayOfWeek = Start.DayOfWeek();
			int Diff = FirstDayOfWeek - DayOfWeek;
			if (Diff > 0)
				Diff -= 7;
			Start.AddDays(Diff);
			break;
		}
		case CAL_VIEW_YEAR:
		{
			YearView v(&Cursor);
			v.SetCursor(0, 0);
			First = v.Get();
			Start = Cursor;
			Start.Day(1);
			Start.Month(1);
			break;
		}
	}

	bool YearCh = Old.Year() != Cursor.Year();
	bool MthCh = Old.Month() != Cursor.Month() || YearCh;
	bool DayCh = Old.Day() != Cursor.Day() || MthCh;
	OnCursorChange(DayCh, MthCh, YearCh);
}

void CalendarView::OnSourceDelete(CalendarSource *s)
{
	for (size_t i=0; i<Current.Length(); i++)
	{
		auto &c = Current[i];
		LAssert(c.src != NULL);
		if (c.src == s)
			Current.DeleteAt(i--);
	}

	Invalidate();
}

void CalendarView::OnContentsChanged(CalendarSource *Source)
{
	Current.Length(0);

	LDateTime End = Start;
	End.AddDays(MonthX * MonthY);
	
	new CalendarSourceGetEvents(App, Start, End, CalendarSource::GetSources(), [this](auto Events)
	{
		Current += Events;

		LHashTbl<PtrKey<Calendar*>, bool> InCur;
		for (auto &c: Current)
		{
			// LgiTrace("%s\n", c.ToString().Get());
			InCur.Add(c.c, true);
		}
		for (unsigned i=0; i<Selection.Length(); i++)
		{
			if (!InCur.Find(Selection[i]))
				Selection.DeleteAt(i--);
		}

		OnCursorChange();
	});
}

void CalendarView::OnCursorChange(bool Day, bool Month, bool Year)
{
	if ((Mode == CAL_VIEW_WEEK && Day) || Month || Year)
	{
		int DayOfWeek = First.DayOfWeek() - FirstDayOfWeek;
		if (DayOfWeek < 0) DayOfWeek += 7;
		MonthY = (Cursor.DaysInMonth() + DayOfWeek - 1) / 7 + 1;

		LDateTime s = Start;
		// s.Day(1);
		s.Hours(0);
		s.Minutes(0);
		s.Seconds(0);
		s.Thousands(0);
		LDateTime e = Start;

		if (Mode == CAL_VIEW_YEAR)
		{
			e.AddMonths(12);
		}
		else
		{
			e.AddDays(MonthX * MonthY);
		}

		for (unsigned i=0; i<Current.Length(); i++)
		{
			Current[i].c->Source = 0;
		}

		new CalendarSourceGetEvents(App, s, e, CalendarSource::GetSources(), [this](auto Events)
		{
			Current = Events;
			Invalidate();
		});
		
		LDateTime::GetDaylightSavingsInfo(Dst, s, &e);

		LWindow *Wnd = GetWindow();
		if (Wnd)
		{
			char s[256];
			sprintf_s(s, sizeof(s), "%s [%s %i]", LLoadString(IDS_CAL_VIEW), FullMonthNames[Cursor.Month()-1], Cursor.Year());
			Wnd->Name(s);
		}
	}

	Invalidate();
}

bool CalendarView::OnPrintPage(LPrintDC *pDC, int PageIndex)
{
	LVariant Bx1, By1, Bx2, By2;
	LOptionsFile *Options = App->GetOptions();
	LFontType FontType("Courier New", 8);
	FontType.GetSystemFont("small");

	Bx1 = By1 = Bx2 = By2 = 1.0; // cm
	if (Options)
	{
		// read any options out..
		#define GetMargin(opt, var) \
			{ LVariant v; if (Options->GetValue(opt, v)) var = v.CastDouble(); }
		GetMargin(OPT_MarginX1, Bx1);
		GetMargin(OPT_MarginY1, By1);
		GetMargin(OPT_MarginX2, Bx2);
		GetMargin(OPT_MarginY2, By2);
	}

	LAutoPtr<LFont> ScreenFont = Font;

	if (pDC)
	{
		// setup device context
		double CmToInch = 0.393700787;
		auto ScreenDpi = LScreenDpi();
		auto DcDpi = pDC->GetDpi();
		double ScaleX = (double)ScreenDpi.x / DcDpi.x;
		double ScaleY = (double)ScreenDpi.y / DcDpi.y;
		// LRect c = GetClient();
		PrintMargin.x1 = (int) (( (Bx1.CastDouble() * CmToInch) * DcDpi.x ) * ScaleX);
		PrintMargin.y1 = (int) (( (By1.CastDouble() * CmToInch) * DcDpi.y ) * ScaleY);
		PrintMargin.x2 = (int) (( pDC->X() - ((Bx2.CastDouble() * CmToInch) * DcDpi.x) ) * ScaleX);
		PrintMargin.y2 = (int) (( pDC->Y() - ((By2.CastDouble() * CmToInch) * DcDpi.y) ) * ScaleY);

		// setup font
		Font.Reset(FontType.Create(pDC));
		if (Font)
		{
			Font->Colour(L_BLACK, L_WHITE);
			Font->Create(0, 0, pDC);
			LDisplayString ds(Font, " ");
			Font->TabSize(ds.X() * 8);

			OnPaint(pDC);
		}
	}

	Font = ScreenFont;
	
	return false;
}

int EventSorter(TimePeriod *a, TimePeriod *b)
{
	return a->s.Compare(&b->s);
}

bool CalendarView::Overlap(LDateTime &Start, LDateTime &End, Calendar *a, Calendar *b)
{
	if (a && b)
	{
		if (a->Overlap(b))
		{
			LArray<TimePeriod> Ap, Bp;
			a->GetTimes(Start, End, Ap);
			b->GetTimes(Start, End, Bp);

			for (unsigned i=0; i<Ap.Length(); i++)
			{
				for (unsigned n=0; n<Bp.Length(); n++)
				{
					if (Ap[i].Overlap(Bp[n]))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

struct ColumnPaintInfo
{
public:
	LFont *Font;
	const char *Txt;
	int x;
	int y;
};

void CalendarColumnPaint(void *UserData, LSurface *pDC, LRect &r, bool FillBackground)
{
	ColumnPaintInfo *i = (ColumnPaintInfo*)UserData;
	i->Font->Colour(L_TEXT, L_MED);
	i->Font->Transparent(true);
	LDisplayString ds(i->Font, (char*)i->Txt);
	ds.Draw(pDC, i->x, i->y, &r);
}

void CalendarView::DrawSelectionBox(LSurface *pDC, LRect &r)
{
	int Edge = 4;
	// int Width = 2;

	#ifdef WINDOWS
	int Op = pDC->Op(GDC_XOR);
	pDC->Colour(Rgba32(0xff, 0xff, 0xff, 0), 32);
	#else
	// Other platforms don't have a working XOR operator... so black is
	// a good default against their default White background.
	pDC->Colour(Rgb24(0, 0, 0), 24);
	#endif

	LRect p;
	
	// Top
	p.Set(r.x1, r.y1, r.x2, r.y1 + Edge - 1);
	PatternBox(pDC, p);
	
	// Bottom
	p.Set(r.x1, r.y2 - Edge + 1, r.x2, r.y2);
	PatternBox(pDC, p);
	
	// Left
	p.Set(r.x1, r.y1 + Edge, r.x1 + Edge - 1, r.y2 - Edge);
	PatternBox(pDC, p);
	
	// Right
	p.Set(r.x2 - Edge + 1, r.y1 + Edge, r.x2, r.y2 - Edge);
	PatternBox(pDC, p);
	
	// WriteDC("c:\\temp\\cal.bmp", pDC);
	
	#ifdef WINDOWS
	pDC->Op(Op);
	#endif
}

void CalendarView::OnPaint(LSurface *pDC)
{
	#ifndef MAC // Mac is double buffered anyway
	LDoubleBuffer Buf(pDC);
	#endif
	
	LColour InMonth(L_WORKSPACE);
	LColour OutMonth = GdcMixColour(LColour(L_HIGH), LColour(L_WORKSPACE), 0.5);
	LColour CellEdge(0xc0, 0xc0, 0xc0);
	LSkinEngine *SkinEngine = LAppInst->SkinEngine;

	pDC->Colour(Rgb32(255, 255, 255), 32);
	pDC->Rectangle();

	LRect c = GetClient();
	c.Offset(-c.x1, -c.y1);
	float _Sx = 1.0;
	float _Sy = 1.0;
	if (pDC->IsPrint())
	{
		c = PrintMargin;
		auto ScreenDpi = LScreenDpi();
		auto DcDpi = pDC->GetDpi();
		_Sx = (float)DcDpi.x / ScreenDpi.x;
		_Sy = (float)DcDpi.y / ScreenDpi.y;
	}
	
	float Scale = _Sx < _Sy ? _Sx : _Sy;

	SRect(c);
	Layout = c;

	if (Mode != CAL_VIEW_YEAR)
	{
		Title = c;
		Title.y2 = Title.y1 + Font->GetHeight() + (int)SY(6);
		Layout.y1 = Title.y2 + 1;
	}
	else
	{
		Title.ZOff(-1, -1);
	}

	for (auto &c: Current)
	{
		c.c->ViewPos.Empty();
	}

	switch (Mode)
	{
		/*
		case CAL_VIEW_DAY:
		{
			break;
		}
		case CAL_VIEW_WEEKDAY:
		{
			break;
		}
		*/
		case CAL_VIEW_WEEK:
		{
			// Recalc day start/end
			int DayVisible = DayEnd - DayStart;
			DayStart = VScroll ? (int)VScroll->Value() : 6;
			DayEnd = DayStart + DayVisible;

			// Setup...
			LDateTime Dt = Start, Now;
			Dt.SetTime("0:0:0.0");
			Now.SetNow();
			LDisplayString ds(Font, "22:00p");
			int TimeX = (int)((float)ds.X() + SX(10));

			Layout.Set(TimeX, Title.y2 + 1, c.x2, c.y2);

			// d=0 is the hours column, d=1 is the first day (either sun or mon), etc... d=7 is last day
			for (int d=0; d<8; d++)
			{
				LDateTime Tomorrow = Dt;
				Tomorrow.AddDays(1);
				uint64 TodayTs, TomorrowTs;
				Dt.Get(TodayTs);
				Tomorrow.Get(TomorrowTs);

				// Heading
				int x1 = d ? TimeX + ((d-1) * (c.X()-TimeX) / 7) : 0;
				int x2 = d ? TimeX + ((d * (c.X()-TimeX) / 7) - 1) : TimeX - 1;
				LRect p(	x1,
							Title.y1,
							x2,
							Title.y2);

				if (d)
				{
					int NameIdx = (FirstDayOfWeek+d-1) % 7;
					if (SkinEngine)
					{
						ColumnPaintInfo i = { Font, FullDayNames[NameIdx], (int)SX(2), (int)SY(3) };

						LSkinState State;
						State.pScreen = pDC;
						State.Rect = p;
						State.View = this;
						SkinEngine->OnPaint_ListColumn(CalendarColumnPaint, &i, &State);
					}
					else
					{
						LWideBorder(pDC, p, DefaultRaisedEdge);
						Font->Colour(L_TEXT, L_MED);
						Font->Transparent(false);
						LDisplayString ds(Font, (char*)FullDayNames[NameIdx]);
						ds.Draw(pDC, p.x1 + (int)SX(2), p.y1 + (int)SY(2), &p);
					}
				}
				else
				{
					pDC->Colour(L_LOW);
					pDC->Rectangle(&p);
				}

				// Content area
				p.Set(	x1,
						Title.y2 + 1,
						x2,
						c.y2);

				pDC->Colour(CellEdge);
				pDC->Line(p.x2, p.y1, p.x2, p.y2);

				int Divisions = DayEnd - DayStart;
				int DayOfWeek = Dt.DayOfWeek();

				#define HourToY(hour) (p.y1 + (((hour)-(double)DayStart) * p.Y() / Divisions))

				for (int h=DayStart; h<=DayEnd; h++)
				{
					LRect Hour(	x1,
								(int)HourToY(h),
								x2,
								(int)HourToY(h+1)-1);

					LColour Back =	DayOfWeek == 0 ||
									DayOfWeek == 6 ?
									GdcMixColour(LColour(L_WORKSPACE), WEEKEND_TINT_COLOUR, WEEKEND_TINT_LEVEL) :
									OutMonth;
					
					if (DayOfWeek >= Calendar::WorkWeekStart &&
						DayOfWeek <= Calendar::WorkWeekEnd &&
						h >= Calendar::WorkDayStart &&
						h < Calendar::WorkDayEnd)
						Back = InMonth;

					/*
					bool Select =	Focus() &&
									Cur->Day() == Dt.Day() &&
									Cur->Hours() == h;
					if (Select)
					{
						Back = LC_FOCUS_SEL_BACK;
					}
					*/

					bool Today =	Now.Day() == Dt.Day() &&
									Now.Month() == Dt.Month() &&
									Now.Year() == Dt.Year();
					if (Today)
					{
						Back = GdcMixColour(Back, TODAY_TINT_COLOUR, TODAY_TINT_LEVEL);
					}

					if (d)
					{
						// Draw hourly blocks

						// Background
						pDC->Colour(Back);
						pDC->Rectangle(Hour.x1, Hour.y1, Hour.x2-1, Hour.y2-1);
						pDC->Colour(CellEdge);
						pDC->Line(Hour.x1, Hour.y2, Hour.x2, Hour.y2);

						// Date at the top..
						if (h == DayStart)
						{
							char s[32];
							Dt.GetDate(s, sizeof(s));
							Font->Colour(/*Select ? LC_FOCUS_SEL_FORE :*/ L_LOW, L_MED);
							Font->Transparent(true);
							LDisplayString ds(Font, s);
							ds.Draw(pDC, Hour.x1 + 2, Hour.y1 + 2);
						}
					}
					else
					{
						// Draw times in the first column
						char s[32];
						if (Now.GetFormat() & GDTF_24HOUR)
                            sprintf_s(s, sizeof(s), "%i:00", h);
						else
						    sprintf_s(s, sizeof(s), "%i:00%c", h == 0 ? 12 : h > 12 ? h - 12 : h, h >= 12 ? 'p' : 'a');

						LRect Temp = Hour;
						LWideBorder(pDC, Temp, DefaultRaisedEdge);
						Font->Colour(L_TEXT, L_MED);
						Font->Transparent(false);
						LDisplayString ds(Font, s);
						ds.Draw(pDC, Temp.x1 + (int)SX(2), Temp.y1, &Temp);
					}
				}

				if (d)
				{
					// Draw events
					LArray<TimePeriod> All;
					for (uint32_t i=0; i<Current.Length(); i++)
					{
						if (Current[i].Overlap(Dt, Tomorrow))
							All.Add(Current[i]);
					}
					All.Sort(EventSorter);

					// Sort the entries into overlapping groups
					typedef LArray<TimePeriod*> EventArray;
					LArray<EventArray*> Groups;

					for (uint32_t e=0; e<All.Length(); e++)
					{
						TimePeriod *Ev = &All[e];

						if (Groups.Length() > 0)
						{
							// Check if it overlaps anything in the previous group
							EventArray *a = Groups[Groups.Length()-1];
							for (uint32_t i=0; i<a->Length(); i++)
							{
								TimePeriod *t = (*a)[i];
								if (t->Overlap(*Ev))
								{
									// It does... so add it.
									a->Add(Ev);
									Ev = 0;
									break;
								}
							}
						}

						if (Ev)
						{
							// Create new group
							EventArray *g = new EventArray;
							if (g)
							{
								g->Add(Ev);
								Groups.Add(g);
							}
						}
					}
						
					// Lay the groups of entries out and then paint them
					for (uint32_t g=0; g<Groups.Length(); g++)
					{
						EventArray &Group = *(Groups[g]);

						// Work out how many columns we need to display the group
						// int Columns = 1;

						// Now do layout and painting
						for (uint32_t i=0; i<Group.Length(); i++)
						{
							TimePeriod &t = *Group[i];
							auto obj = t.c->GetObject();

							auto allDay = obj ? obj->GetInt(FIELD_CAL_ALL_DAY) : false;
							double StartH = (double) t.s.Hours() + ((double)t.s.Minutes() / 60), EndH;
							if (t.e.IsSameDay(t.s) && t.e.Hours())
								EndH = ((double)t.e.Hours()) + ((double)t.e.Minutes() / 60);
							else
								EndH = 24;

							int x1 = p.x1 + (int)SX(4);
							int x2 = p.x2 - (int)SX(5);
							double dx = x2 - x1 + SX(3);
							int StartX = x1 + (int)((double)i * dx / (double)Group.Length());
							int EndX = StartX + (int)((dx / (double)Group.Length()) - SX(5));
							
							LRect Vp(	StartX,
										(int)HourToY(StartH) - 1,
										EndX,
										(int)HourToY(EndH) - 3);

							// LgiTrace("paint: %s %s\n", Vp.GetStr(), t.c->ToString().Get());

							t.c->OnPaintView(pDC, Font, &Vp, &t);
						}
					}
					
					// Clean up the memory
					Groups.DeleteObjects();

					for (unsigned i=0; i<Ranges.Length(); i++)
					{
						TsRange &rng = Ranges[i];
						if (rng.Overlap(TodayTs, TomorrowTs))
						{							
							int StartSec, EndSec;

							if (rng.StartTs < TodayTs)
								StartSec = 0;
							else
								StartSec = (int)((rng.StartTs - TodayTs) / LDateTime::Second64Bit);
							
							if (rng.EndTs > TomorrowTs)
								EndSec = (int)((TomorrowTs - TodayTs) / LDateTime::Second64Bit);
							else
								EndSec = (int)((rng.EndTs - TodayTs) / LDateTime::Second64Bit);
								
							int StartY = (int) HourToY((double)StartSec / LDateTime::HourLength);
							int EndY = (int) HourToY((double)EndSec / LDateTime::HourLength);
							
							/*
							printf("%f,%f - %i,%i - %i,%i\n",
								(double)StartSec / LDateTime::HourLength,
								(double)EndSec / LDateTime::HourLength, StartY, EndY, DayStart, Divisions);
							*/
							
							LRect r(p.x1 + 4, StartY, p.x2 - 4, EndY);
							DrawSelectionBox(pDC, r);
						}
					}

					// Increment date of day we're painting
					Dt = Tomorrow;
				}
			}
			break;
		}
		case CAL_VIEW_MONTH:
		{
			LDateTime *Cur = (DragStart.IsValid()) ? &DragStart : &Cursor;
			int ObjY = Font->GetHeight() + (int)SY(4);
			LDateTime i = Start, Now, Tomorrow;

			Now.SetNow();
			i.SetTime("0:0:0.0");
			Tomorrow = i;
			Tomorrow.AddDays(1);

			char Str[256];

			for (int h=0; h<7; h++)
			{
				LRect p(	Title.x1 + (h * Title.X() / MonthX),
							Title.y1,
							Title.x1 + (((h+1) * Title.X() / MonthX) - 1),
							Title.y2);
				int NameIdx = (h + FirstDayOfWeek) % 7;
				if (SkinEngine)
				{
					ColumnPaintInfo i = { Font, FullDayNames[NameIdx], (int)SX(2), (int)SY(3) };

					LSkinState State;
					State.pScreen = pDC;
					State.Rect = p;
					State.View = this;

					SkinEngine->OnPaint_ListColumn(CalendarColumnPaint, &i, &State);
				}
				else
				{
					LWideBorder(pDC, p, DefaultRaisedEdge);
					Font->Colour(L_TEXT, L_MED);
					Font->Transparent(false);
					LDisplayString ds(Font, (char*)FullDayNames[NameIdx]);
					ds.Draw(pDC, p.x1 + (int)SX(2), p.y1 + (int)SY(2), &p);
				}
			}

			for (int y=0; y<MonthY; y++)
			{
				for (int x=0; x<MonthX; x++)
				{
					LRect p(	x * Layout.X() / MonthX,
								y * Layout.Y() / MonthY,
								((x+1) * Layout.X() / MonthX) - 1,
								((y+1) * Layout.Y() / MonthY) - 1);
					p.Offset(Layout.x1, Layout.y1);

					i.GetDate(Str, sizeof(Str));
					
					LColour Back(L_BLACK);
					int DayOfWeek = i.DayOfWeek();
					bool IsInMonth = i.Month() == Cur->Month();
					bool Today =	Now.Day() == i.Day() &&
									Now.Month() == i.Month() &&
									Now.Year() == i.Year();

					if (i.Day() == Cur->Day() &&
						i.Month() == Cur->Month() &&
						i.Year() == Cur->Year())
					{
						// Is cursor day
						Back = Focus() ? LColour(L_FOCUS_SEL_BACK) : GdcMixColour(LColour(L_FOCUS_SEL_BACK), LColour(L_WORKSPACE));
						Font->Fore(L_FOCUS_SEL_FORE);
					}
					else
					{
						// normal day
						Back = (IsInMonth) ? InMonth : OutMonth;
						Font->Fore(L_TEXT);
					}

					if (DayOfWeek == 0 ||
						DayOfWeek == 6)
						Back = GdcMixColour(Back, WEEKEND_TINT_COLOUR, WEEKEND_TINT_LEVEL);
					
					if (Today)
						Back = GdcMixColour(Back, TODAY_TINT_COLOUR, TODAY_TINT_LEVEL);

					int Edge = (int)SX(1);
					if (!pDC->IsPrint() || Back != LColour(L_WORKSPACE))
					{
						pDC->Colour(Back);
						pDC->Rectangle(p.x1, p.y1, p.x2-Edge, p.y2-Edge);
					}

					pDC->Colour(CellEdge);
					pDC->Rectangle(p.x2-Edge+1, p.y1, p.x2, p.y2);
					pDC->Rectangle(p.x1, p.y2-Edge+1, p.x2-Edge, p.y2);
					if (pDC->IsPrint() && x == 0)
					{
						pDC->Rectangle(p.x1, p.y1, p.x1+Edge, p.y2-Edge);
					}

					Font->Transparent(true);
					Font->Back(Back);
					LDisplayString ds(Font, Str);
					ds.Draw(pDC, p.x1 + (int)SX(2), p.y1 + (int)SX(2));

					LRect Clip = p;
					Clip.Inset(Edge, Edge);
					pDC->ClipRgn(&Clip);

					int CalY = ObjY + (int)SY(2);

					LArray<TimePeriod> All;
					uint32_t n;
					for (n=0; n<Current.Length(); n++)
					{
						if (Current[n].Overlap(i, Tomorrow))
							All.Add(Current[n]); // Current[n].c->GetTimes(i, Tomorrow, All);
					}
					All.Sort(EventSorter);

					for (n=0; n<All.Length(); n++)
					{
						TimePeriod &t = All[n];
						LRect Vp(p.x1 + (int)SX(3), p.y1 + CalY, p.x2 - (int)SX(4), p.y1 + CalY + ObjY);
						t.c->OnPaintView(pDC, Font, &Vp, &t);
						CalY += ObjY + (int)SY(2);
					}

					for (auto rng : Ranges)
					{
						if (rng.Overlap(i, Tomorrow))
						{							
							LRect Vp(p.x1 + (int)SX(3), p.y1 + CalY, p.x2 - (int)SX(4), p.y1 + CalY + ObjY);
							DrawSelectionBox(pDC, Vp);
							CalY += ObjY + (int)SY(2);
							break;
						}
					}

					pDC->ClipRgn(0);
					i = Tomorrow;
					Tomorrow.AddDays(1);
				}
			}
			break;
		}
		case CAL_VIEW_YEAR:
		{
			LDateTime *Cur = (DragStart.IsValid()) ? &DragStart : &Cursor;
			// int ObjY = Font->GetHeight() + (int)SY(2);
			LDateTime i = Start, Now, Tomorrow;
			YearView v(Cur);

			Now.SetNow();
			i.Hours(0);
			i.Minutes(0);
			i.Seconds(0);
			i.Thousands(0);
			Tomorrow = i;
			Tomorrow.AddDays(1);

			Layout.x1 += (int)SX(BORDER_YEAR);

			int Fy = Font->GetHeight();
			char Str[256];

			for (int y=0; y<v.Y(); y++)
			{
				int y1 = y * c.Y() / v.Y();
				int y2 = ((y+1) * c.Y() / v.Y()) - 1;

				LRect T(0, y1, Layout.x1-1, y2);
				LWideBorder(pDC, T, DefaultRaisedEdge);
				Font->Transparent(false);
				Font->Colour(L_BLACK, L_MED);
				LDisplayString ds(Font, (char*)ShortMonthNames[y]);
				ds.Draw(pDC, T.x1 + (int)SX(2), T.y1, &T);

				for (int x=0; x<v.X(); x++)
				{
					LRect p(x * Layout.X() / v.X(),
							y1,
							((x+1) * Layout.X() / v.X()) - 1,
							y2);
					p.Offset(Layout.x1, 0);

					v.SetCursor(x, y);
					LDateTime t = v.Get();

					LColour Back;
					LColour Fore;

					if (v.IsMonth() &&
						t.Day() == Cur->Day() &&
						t.Month() == Cur->Month() &&
						t.Year() == Cur->Year())
					{
						// Is cursor day
						Back = Focus() ? LColour(L_FOCUS_SEL_BACK) : GdcMixColour(LColour(L_FOCUS_SEL_BACK), LColour(L_WORKSPACE));
						Fore = LColour(L_FOCUS_SEL_FORE);
					}
					else
					{
						// Other day..
						Fore = LColour(L_LOW);
						Back = v.IsMonth() ? InMonth : OutMonth;
					}

					// Weekend tint
					int Day = t.DayOfWeek();
					if (Day == 0 || Day == 6)
					{
						Back = GdcMixColour(Back, WEEKEND_TINT_COLOUR, WEEKEND_TINT_LEVEL);
					}

					// Today tint..
					if (v.IsMonth() &&
						Now.Day() == t.Day() &&
						Now.Month() == t.Month() &&
						Now.Year() == t.Year())
					{
						Back = GdcMixColour(Back, TODAY_TINT_COLOUR, TODAY_TINT_LEVEL);
					}

					// Fill cell background
					pDC->Colour(Back);
					pDC->Rectangle(p.x1, p.y1, p.x2-1, p.y2-1);
					pDC->Colour(CellEdge);
					pDC->Line(p.x2, p.y1, p.x2, p.y2);
					pDC->Line(p.x1, p.y2, p.x2, p.y2);

					// Draw day number
					if (v.IsMonth())
					{
						Font->Transparent(true);
						Font->Colour(Fore, Back);
						sprintf_s(Str, sizeof(Str), "%i", t.Day());
						LDisplayString ds(Font, Str);
						ds.Draw(pDC, p.x1+1, p.y1-1);
					}

					// Paint events					
					LArray<TimePeriod> e;
					LDateTime Tomorrow(t);
					Tomorrow.AddDays(1);

					int Cy = p.y1 + Fy;
					if (v.IsMonth() &&
						GetEventsBetween(e, t, Tomorrow))
					{
						LRect Safe = p;
						Safe.y2--;

						for (uint32_t i=0; i<e.Length(); i++)
						{
							Calendar *c = e[i].c;
							if (Cy > Safe.y2)
							{
								c->ViewPos.ZOff(-1, -1);
							}
							else
							{
								LRect Vp(p.x1 + (int)SX(1), Cy, p.x2 - (int)SX(2), Cy + Fy);
								Vp.Bound(&Safe);
								c->OnPaintView(pDC, Font, &Vp, &e[i]);
								Cy += Fy + 1;
							}
						}
					}

					for (auto rng : Ranges)
					{
						if (rng.Overlap(t, Tomorrow))
						{							
							LRect Vp(p.x1 + (int)SX(1), Cy, p.x2 - (int)SX(2), Cy + Fy);
							DrawSelectionBox(pDC, Vp);
							Cy += Fy + 1;
							break;
						}
					}
				}
			}
			break;
		}
		default:
		{
			pDC->Colour(L_WHITE);
			pDC->Rectangle();
			break;
		}
	}
}

bool CalendarView::OnKey(LKey &k)
{
	switch (k.vkey)
	{
		case LK_ESCAPE:
		{
			if (IsCapturing() && k.Down())
			{
				DragStart.Empty();
				DragEnd.Empty();
				Ranges.Length(0);
				DragMode = DragNone;
				Invalidate();
			}
			return true;
		}
		default:
		{
			switch (k.c16)
			{
				case 'w':
				case 'W':
				{
					if (k.CtrlCmd() && k.Down() && GetWindow())
					{
						GetWindow()->Quit();
						return true;
					}
					break;
				}
			}
		}
	}

	/*
	if (k.c16 != 17 && k.Down())
	{
		// Arrow behaviour
		CalViewArrow *Arrow = GetModeArrow(Mode, k);
		if (Arrow)
		{
			LDateTime c = Cursor;
			c.AddHours(Arrow->Hours);
			c.AddDays(Arrow->Days);
			c.AddMonths(Arrow->Months);
			c.AddMonths(Arrow->Years * 12);
			SetCursor(c);
			return true;
		}

		// Other commands
		switch (k.c16)
		{
			case LK_DELETE:
			{
				LVariant ConfirmDelete;
				App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

				if (!ConfirmDelete.CastInt32() ||
					LgiMsg(this, LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
				{
					OnDelete();
				}
				return true;
				break;
			}
		}
	}
	*/
	
	return false;
}

void CalendarView::OnDelete()
{
	Calendar *c;
	while ((c=Selection.First()))
	{
		c->OnDelete();
		Selection.Delete(c);
	}
}

Calendar *CalendarView::CalendarAt(int x, int y)
{
	switch (Mode)
	{
		default:
			LAssert(0);
			break;
		/*
		case CAL_VIEW_DAY:
		{
			break;
		}
		case CAL_VIEW_WEEKDAY:
		{
			break;
		}
		*/
		case CAL_VIEW_WEEK:
		case CAL_VIEW_MONTH:
		case CAL_VIEW_YEAR:
		{
			if (Layout.Overlap(x, y))
			{
				for (uint32_t i=0; i<Current.Length(); i++)
				{
					if (Current[i].c->ViewPos.Overlap(x, y))
					{
						return Current[i].c;
					}
				}
			}
			break;
		}
	}

	return 0;
}

LDateTime *CalendarView::TimeAt(int x, int y, int SnapMinutes, LPoint *Cell)
{
	LPoint cell;
	if (!Cell) Cell = &cell;

	Cell->x = -1;
	Cell->y = -1;

	switch (Mode)
	{
		default:
			break;
		/*
		case CAL_VIEW_DAY:
		{
			break;
		}
		case CAL_VIEW_WEEKDAY:
		{
			break;
		}
		*/
		case CAL_VIEW_WEEK:
		{
			if (Layout.Overlap(x, y))
			{
				LDateTime *Day = new LDateTime;
				if (Day)
				{
					int64 Page = (int)(Calendar::DayEnd - Calendar::DayStart);
					int FirstHr = 0;
					if (VScroll)
					{
						FirstHr = (int) VScroll->Value();
						Page = VScroll->Page();
					}

					Cell->x = ((x - Layout.x1) * 7) / Layout.X();		// day in week
					int MinuteOffset = ((y - Layout.y1) * (int)Page * 60) / Layout.Y();
					Cell->y = ((y - Layout.y1) * (int)Page) / Layout.Y();	// hour of day
					LAssert(MinuteOffset / 60 == Cell->y);
					int Minutes = (FirstHr * 60) + MinuteOffset;					
					if (SnapMinutes)
					{
						int Snap = Minutes % SnapMinutes;
						if (Snap)
						{
							Minutes -= Snap;
						}
					}
				
					*Day = Start;
					Day->AddDays(Cell->x);
					Day->Hours(Minutes / 60);
					Day->Minutes(Minutes % 60);
					Day->Seconds(0);
					Day->Thousands(0);
					return Day;
				}
			}
			break;
		}
		case CAL_VIEW_MONTH:
		{
			if (Layout.Overlap(x, y))
			{
				LDateTime *Day = new LDateTime;
				if (Day)
				{
					Cell->x = ((x - Layout.x1) * MonthX) / Layout.X();
					Cell->y = ((y - Layout.y1) * MonthY) / Layout.Y();
				
					*Day = Start;
					Day->AddDays( (Cell->y * MonthX) + Cell->x );
					Day->SetTime("0:0:0");
					return Day;
				}
			}
			break;
		}
		case CAL_VIEW_YEAR:
		{
			if (Layout.Overlap(x, y))
			{
				LDateTime *Day = new LDateTime;
				if (Day)
				{
					YearView v(&Cursor);
					Cell->x = (x - Layout.x1) * v.X() / Layout.X();
					Cell->y = (y - Layout.y1) * v.Y() / Layout.Y();
					v.SetCursor(Cell->x, Cell->y);
					*Day = v.Get();
					Day->SetTime("0:0:0");
					return Day;
				}
			}			
			break;
		}
	}

	return 0;
}

LDateTime::GDstInfo *CalendarView::GetDstForDate(LDateTime t)
{
	uint64 ts = t;

	for (uint32_t i=0; i<Dst.Length(); i++)
	{
		if (i + 1 < Dst.Length())
		{
			LDateTime Prev = Dst[i].GetLocal();
			LDateTime Next = Dst[i+1].GetLocal();
			if (ts >= Prev &&
				ts < Next)
			{
				return &Dst[i];
			}
		}
		else if (ts > Dst[i].UtcTimeStamp)
		{
			return &Dst[i];
		}
	}

	return 0;
}

bool CalendarView::HitTest(int x, int y, EventDragMode &mode, Calendar *&event)
{
	if (!Layout.Overlap(x, y))
		return false;

	for (uint32_t i=0; i<Current.Length(); i++)
	{
		Calendar *c = Current[i].c;

		if (Mode <= CAL_VIEW_WEEK) // In month and year modes you can't edit start/end with dnd
		{
			for (LRect *r = c->ViewPos.First(); r; r = c->ViewPos.Next())
			{
				if (x >= r->x1 &&
					x <= r->x2)
				{
					if (abs(y-r->y1) < THRESHOLD_EDGE)
					{
						event = c;
						mode = DragMoveStart;
						return true;
					}
					
					if (abs(y-r->y2) < THRESHOLD_EDGE)
					{
						event = c;
						mode = DragMoveEnd;
						return true;
					}				
				}
			}
		}
		
		if (c->ViewPos.Overlap(x, y))
		{
			event = c;
			mode = DragMoveSelection;
			return true;
		}
	}
	
	return false;
}

Calendar *CalendarView::NewEvent(LDateTime &dtStart, LDateTime &dtEnd)
{
	CalendarSource *Src = FolderCalendarSource::GetCreateIn();
	if (!Src)
		return NULL;

	Calendar *c = Src->NewEvent();
	if (!c)
		return NULL;

	LDateTime Start, End;
	if (dtStart < dtEnd)
	{
		Start = dtStart;
		End = dtEnd;
	}
	else
	{
		Start = dtEnd;
		End = dtStart;
	}
	
	LDateTime n = Start;
	LDateTime::GDstInfo *CurDst = GetDstForDate(Start);
	if (CurDst)
		n.SetTimeZone(CurDst->Offset, false);

	
	char s[64];
	sprintf_s(s, sizeof(s), "%+.1f", (double)n.GetTimeZone() / 60.0);
	c->SetField(FIELD_CAL_TIMEZONE, s);

	Start.ToUtc(true);
	c->SetField(FIELD_CAL_START_UTC, Start);
	End.ToUtc(true);
	c->SetField(FIELD_CAL_END_UTC, End);
	
	LgiTrace("Start=%s, End=%s\n", Start.Get().Get(), End.Get().Get());
	
	c->OnCreate();

	return c;	
}

void CalendarView::OnMouseClick(LMouse &m)
{
	EventDragMode HitMode = DragNone;
	Calendar *c = NULL;
	HitTest(m.x, m.y, HitMode, c);
	bool AlreadySelected = (c) ? Selection.HasItem(c) : false;
	LAutoPtr<LDateTime> Hit(TimeAt(m.x, m.y, SnapMinutes));

	/*
	if (Hit)
		printf("Hit=%s\n", Hit->Get().Get());
	*/

	if (m.IsContextMenu())
	{
		Focus(true);

		// Select the item if not already selected when asking for a context menu.
		if (c && !Selection.HasItem(c))
		{
			Selection.Add(c);
			Invalidate(&c->ViewPos);
		}
		
		if (!c)
		{
			char sHit[64] = "";
			if (Hit)
				Hit->GetTime(sHit, sizeof(sHit));
			LString NewMsg;
			NewMsg.Printf("New event at %s", sHit);
			
			LSubMenu s;
			s.AppendItem(NewMsg, IDM_NEW_EVENT);
			LSubMenu *snap = s.AppendSub("Snap");
			if (snap)
			{
				LMenuItem *it = snap->AppendItem("15 minutes", IDM_15MIN);
				if (it && SnapMinutes == 15) it->Checked(true);
				it = snap->AppendItem("30 minutes", IDM_30MIN);
				if (it && SnapMinutes == 30) it->Checked(true);
				it = snap->AppendItem("1 hour", IDM_1HR);
				if (it && SnapMinutes == 60) it->Checked(true);
			}
			
			m.ToScreen();
			int Cmd = s.Float(this, m);
			switch (Cmd)
			{
				case IDM_NEW_EVENT:
				{
					LDateTime End = *Hit;
					End.AddHours(1);
					
					Calendar *ev = NewEvent(*Hit, End);
					if (ev)
						ev->DoUI();
					break;
				}
				case IDM_15MIN:
				{
					SnapMinutes = 15;
					break;
				}
				case IDM_30MIN:
				{
					SnapMinutes = 30;
					break;
				}
				case IDM_1HR:
				{
					SnapMinutes = 60;
					break;
				}
			}
		}
	}
	else
	{
		Capture(m.Down());
		if (m.Down())
		{
			Focus(true);
			DragEvent = NULL;

			if (m.Left())
			{
				Ranges.Length(0);
				if (!AlreadySelected)
				{
					OnSelect(c, m.Ctrl(), m.Shift());
				}

				ClickPt.x = m.x;
				ClickPt.y = m.y;
				
				if (Hit)
				{
					DragStart = *Hit;
					
					if (HitMode == DragMoveStart)
					{
						DragMode = HitMode;
						DragStart = *c->GetObject()->GetDate(FIELD_CAL_END_UTC);
						DragEnd = *c->GetObject()->GetDate(FIELD_CAL_START_UTC);
						DragStart.ToLocal(true);
						DragEnd.ToLocal(true);
						DragEvent = c;
						
						TsRange &r = Ranges.New();
						DragStart.Get(r.StartTs);
						DragEnd.Get(r.EndTs);
					}
					else if (HitMode == DragMoveEnd)
					{
						DragMode = HitMode;
						DragStart = *c->GetObject()->GetDate(FIELD_CAL_START_UTC);
						DragEnd = *c->GetObject()->GetDate(FIELD_CAL_END_UTC);
						DragStart.ToLocal(true);
						DragEnd.ToLocal(true);
						DragEvent = c;
						
						TsRange &r = Ranges.New();
						DragStart.Get(r.StartTs);
						DragEnd.Get(r.EndTs);
					}
					else if (Selection.Length())
					{
						DragMode = DragMoveSelection;
						DragEnd = *Hit;
						
						for (unsigned i=0; i<Selection.Length(); i++)
						{
							Calendar *s = Selection[i];
							TsRange &r = Ranges.New();
							
							LDateTime dt = *s->GetObject()->GetDate(FIELD_CAL_START_UTC);
							dt.ToLocal(true);
							dt.Get(r.StartTs);
							dt = *s->GetObject()->GetDate(FIELD_CAL_END_UTC);
							if (dt.IsValid())
							{
								dt.ToLocal(true);
								dt.Get(r.EndTs);
							}
							else
							{
								r.EndTs = r.StartTs + ((uint64)60 * 60 * LDateTime::Second64Bit);
							}
							
							// Does this new range overlap any existing range?
							for (unsigned n=0; n<Ranges.Length()-1; n++)
							{
								TsRange &nr = Ranges[n];
								if (nr.Overlap(r))
								{
									// Merge ranges and delete dupe...
									nr.StartTs = MIN(nr.StartTs, r.StartTs);
									nr.EndTs = MAX(nr.EndTs, r.EndTs);
									Ranges.Length(Ranges.Length()-1);
									break;
								}
							}
						}
					}
					else
					{
						DragMode = DragNewEvent;
						DragEnd = *Hit;
						DragEnd.AddHours(1);
						// printf("Drag: %s -> %s\n", DragStart.Get().Get(), DragEnd.Get().Get());
						
						TsRange &r = Ranges.New();
						DragStart.Get(r.StartTs);
						DragEnd.Get(r.EndTs);
					}
				}
				else
				{
					DragStart.Empty();
					DragEnd.Empty();
				}
				
				Invalidate();
			}
		}
		else // up
		{
			if (DragStart.IsValid() &&
				DragEnd.IsValid())
			{
				switch (DragMode)
				{
					case DragNewEvent:
					{
						// New event...
						// printf("DragStart=%s, DragEnd=%s\n", DragStart.Get().Get(), DragEnd.Get().Get());
						c = NewEvent(DragStart, DragEnd);
						if (c)
							c->DoUI();

						OnContentsChanged(NULL);
						break;
					}
					case DragMoveSelection:
					{
						// Adjust the times of the selection by the offset.
						int64 TsOffset = DragEnd.Ts() - DragStart.Ts();
						if (TsOffset != 0)
						{
							// TsOffset = 0;
							for (unsigned i=0; i<Selection.Length(); i++)
							{
								Calendar *s = Selection[i];
								LDataI *o = s->GetObject();

								LDateTime start_dt = *o->GetDate(FIELD_CAL_START_UTC);
								start_dt.Set(start_dt.Ts() + TsOffset);
								o->SetDate(FIELD_CAL_START_UTC, &start_dt);

								LDateTime end_dt = *o->GetDate(FIELD_CAL_END_UTC);
								end_dt.Set(end_dt.Ts() + TsOffset);
								o->SetDate(FIELD_CAL_END_UTC, &end_dt);								
								
								o->SetInt(FIELD_STATUS, Store3Delayed);
								
								s->SetDirty();
							}
							
							OnContentsChanged(NULL);
						}
						break;
					}
					case DragMoveStart:
					{
						if (DragEvent)
						{
							if (DragEnd > DragStart)
							{
								LDateTime dt = DragStart;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_START_UTC, &dt);
								dt = DragEnd;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_END_UTC, &dt);
							}
							else
							{
								LDateTime dt = DragEnd;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_START_UTC, &dt);
							}
							DragEvent->SetDirty();
							OnContentsChanged(NULL);
						}
						break;
					}
					case DragMoveEnd:
					{
						if (DragEvent)
						{
							if (DragEnd < DragStart)
							{
								LDateTime dt = DragEnd;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_START_UTC, &dt);
								dt = DragStart;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_END_UTC, &dt);
							}
							else
							{
								LDateTime dt = DragEnd;
								dt.ToUtc(true);
								DragEvent->GetObject()->SetDate(FIELD_CAL_END_UTC, &dt);
							}
							DragEvent->SetDirty();
							OnContentsChanged(NULL);
						}
						break;
					}
					default:
						break;
				}
			}
		
			if (AlreadySelected)
			{
				OnSelect(c, m.Ctrl(), m.Shift());
			}

			if (DragStart.IsValid() || Ranges.Length())
			{
				Ranges.Length(0);
				DragStart.Empty();
				DragEnd.Empty();
				Invalidate();
			}
			
			DragEvent = NULL;
		}
	}

	if (c)
	{
		c->OnMouseClick(m);
	}
}

void CalendarView::OnMouseMove(LMouse &m)
{
	if (IsCapturing())
	{
		LAutoPtr<LDateTime> Hit(TimeAt(m.x, m.y, SnapMinutes));

		if
		(
			Hit &&
			(
				abs(m.x - ClickPt.x) > 4 ||
				abs(m.y - ClickPt.y) > 4
			)
		)
		{
			#if 1

			switch (DragMode)
			{
				default:
					break;
				case DragMoveStart:
				case DragMoveEnd:
				case DragNewEvent:
				{
					LDateTime End = *Hit;
					if (End >= DragStart)
						End.AddMinutes(SnapMinutes);
					if (End != DragEnd)
					{
						DragEnd = End;
						if (DragStart < DragEnd)
						{
							DragStart.Get(Ranges[0].StartTs);
							DragEnd.Get(Ranges[0].EndTs);
						}
						else
						{
							DragStart.Get(Ranges[0].EndTs);
							DragEnd.Get(Ranges[0].StartTs);
						}
						Invalidate();
					}
					break;
				}
				case DragMoveSelection:
				{
					DragEnd = *Hit;
					int64 TsOffset = DragEnd.Ts() - DragStart.Ts();
					if (LastTsOffset != TsOffset)
					{
						LastTsOffset = TsOffset;
						
						Ranges.Length(0);
						
						for (unsigned i=0; i<Selection.Length(); i++)
						{
							Calendar *s = Selection[i];
							TsRange &r = Ranges.New();
							LDateTime dt = *s->GetObject()->GetDate(FIELD_CAL_START_UTC);
							dt.ToLocal(true);
							dt.Get(r.StartTs);
							dt = *s->GetObject()->GetDate(FIELD_CAL_END_UTC);
							if (dt.IsValid())
							{
								dt.ToLocal(true);
								dt.Get(r.EndTs);
							}
							else
								r.EndTs = r.StartTs + ((uint64)60 * 60 * LDateTime::Second64Bit);
							
							r.StartTs += TsOffset;
							r.EndTs += TsOffset;

							// Does this new range overlap any existing range?
							for (unsigned n=0; n<Ranges.Length()-1; n++)
							{
								TsRange &nr = Ranges[n];
								if (nr.Overlap(r))
								{
									// Merge ranges and delete dupe...
									nr.StartTs = MIN(nr.StartTs, r.StartTs);
									nr.EndTs = MAX(nr.EndTs, r.EndTs);
									Ranges.Length(Ranges.Length()-1);
									break;
								}
							}
						}
						
						Invalidate();
					}					
					break;
				}
			}
		}
		
		#else
		
		// start drag'n'drop operation
		Drag(this, DROPEFFECT_MOVE | DROPEFFECT_COPY);
		
		#endif
	}
}

bool CalendarView::OnMouseWheel(double Lines)
{
	if (!VScroll)
		return false;

	auto v = VScroll->Value();
	v += (int) ceil(Lines / 3.0);
	VScroll->Value(v);

	return true;
}

LCursor CalendarView::GetCursor(int x, int y)
{
	EventDragMode mode = DragNone;
	Calendar *event = NULL;
	if (HitTest(x, y, mode, event))
	{
		if (mode == DragMoveStart ||
			mode == DragMoveEnd)
			return LCUR_SizeVer;
	}
	
	return LView::GetCursor(x, y);
}

void CalendarView::OnFocus(bool f)
{
	Invalidate();
}

//////////////////////////////////////////////////////////////////////////////
int CalendarView::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;

	LDateTime *Hit = TimeAt(Pt.x, Pt.y, SnapMinutes);
	SelectDropTarget(Hit);
	if (Hit)
	{
		DeleteObj(Hit);

		if (Formats.HasFormat(ScribeCalendarObject))
			Formats.Supports(ScribeCalendarObject);
		else
			Formats.SupportsFileDrops();
		if (Formats.GetSupported().Length())
			Status = DROPEFFECT_MOVE;
	}

	return Status;
}

void CalendarView::OnDragExit()
{
	SelectDropTarget();
}

int CalendarView::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	for (unsigned di=0; di<Data.Length(); di++)
	{
		LDragData &dd = Data[di];
		
		if (_stricmp(dd.Format, LGI_FileDropFormat) == 0)
		{
			LDropFiles Files(dd);
			CalendarSource *First = CalendarSource::GetCreateIn();
			if (First)
			{
				for (auto f: Files)
				{
					LAutoPtr<LFile> in(new LFile);
					auto type = LGetFileMimeType(f);
					if (in->Open(f, O_READ))
					{
						auto c = First->NewEvent();
						if (c)
							c->Import(c->AutoCast(in), type);
					}
				}
			}
			else LgiTrace("%s:%i - Do data sources?\n", _FL);
		}
		else if (_stricmp(dd.Format, ScribeCalendarObject) == 0)
		{
			if (dd.Data.Length() == 0)
				continue;
			LVariant *v = &dd.Data[0];
			if (v->Type == GV_BINARY &&
				v->Value.Binary.Length >= sizeof(NativeInt)*2 &&
				DragStart.IsValid())
			{
				NativeInt *d = (NativeInt*) v->Value.Binary.Data;
				if (d[0] == MAGIC_CALENDAR &&
					d[1] > 0)
				{
					char Str[32];
					if (Mode == CAL_VIEW_WEEK ||
						Mode == CAL_VIEW_DAY)
					{
						DragStart.Get(Str, sizeof(Str));
					}
					else
					{
						DragStart.GetDate(Str, sizeof(Str));
					}

					Calendar **c = (Calendar**) (d + 2);
					for (int i=0; i<d[1]; i++)
					{
						LDateTime s, e;
						if (c[i]->GetField(FIELD_CAL_START_UTC, s))
						{
							bool HasEnd = c[i]->GetField(FIELD_CAL_END_UTC, e);
							LDateTime Diff;
							if (HasEnd)
							    Diff = e - s;
							else
							    Diff.Hours(1);

							s.Set(Str);

							c[i]->SetField(FIELD_CAL_START_UTC, s);
							if (HasEnd)
							{
								e = s + Diff;
								c[i]->SetField(FIELD_CAL_END_UTC, e);
							}

							c[i]->Save();
						}
					}

					SetCursor(DragStart);
				}
			}
		}
	}

	SelectDropTarget();

	return 0;
}

void CalendarView::OnDragInit(bool Success)
{
}

char *CalendarView::TypeOf()
{
	return 0;
}

bool CalendarView::GetData(LArray<LDragData> &Data)
{
	if (Selection.Length() <= 0)
		return false;

	bool Status = false;
	
	for (unsigned di=0; di<Data.Length(); di++)
	{
		LDragData &dd = Data[di];
		if (_stricmp(dd.Format, LGI_FileDropFormat) == 0)
		{
			LString::Array Files;
			bool Status = false;

			for (unsigned i=0; i<Selection.Length(); i++)
			{
				Calendar *s = Selection[i];
				Thing *t = dynamic_cast<Thing*>(s);
				if (t)
				{
					Status |= t->GetDropFiles(Files);
				}
			}

			if (Status)
			{
				LMouse m;
				GetMouse(m, true);
				Status |= CreateFileDrop(&dd, m, Files);
			}
		}
		else if (_stricmp(dd.Format, ScribeCalendarObject) == 0)
		{
			ssize_t Size = (2 + Selection.Length()) * sizeof(NativeInt);
			LArray<NativeInt> d;
			if (d.Length(Size))
			{
				d[0] = MAGIC_CALENDAR;
				d[1] = Selection.Length();
				
				int n=0;
				Calendar **l = (Calendar**) (&d[2]);
				for (unsigned i=0; i<Selection.Length(); i++)
				{
					Calendar *s = Selection[i];
					l[n++] = s;
				}

				Status |= dd.Data[0].SetBinary(Size, &d[0]);
			}
		}
	}

	return false;
}

bool CalendarView::GetFormats(LDragFormats &Formats)
{
	Formats.SupportsFileDrops();
	Formats.Supports(ScribeCalendarObject);

	return Formats.Length() > 0;
}


class CalendarViewPrint : public LPrintEvents
{
	CalendarView *cv;
	
public:
	CalendarViewPrint(CalendarView *v)
	{
		cv = v;
	}
	
	bool OnPrintPage(LPrintDC *pDC, int PageIndex)
	{
		return cv->OnPrintPage(pDC, PageIndex);
	}
};

//////////////////////////////////////////////////////////////////////////////
LMonthView::LMonthView(int Id, LDateTime *n, CalendarView *calView) : MonthView(n)
{
	FirstDayOfWeek = CalendarView::FirstDayOfWeek;
	SetId(Id);
	rTitle.ZOff(-1, -1);
	rCells.ZOff(-1, -1);
	Cell = 1;
	CalView = calView;

	Set(n);
}
	
void LMonthView::OnCellClick(int Cx, int Cy)
{
	SetCursor(Cx, Cy);
	Invalidate();
	SendNotify(LNotifyCursorChanged);
}
	
void LMonthView::SeekMonth(int Dir)
{
	LDateTime t = Cursor;
	t.AddMonths(Dir);
	Set(&t);
	Invalidate();
	SendNotify(LNotifyCursorChanged);
}
	
void LMonthView::OnMouseClick(LMouse &m)
{
	if (m.IsContextMenu())
	{
	}
	else if (m.Left() && m.Down())
	{
		if (rCells.Overlap(m.x, m.y))
		{
			int x = (m.x - rCells.x1) / Cell;
			int y = (m.y - rCells.y1) / Cell;
			OnCellClick(x, y);
		}
		else if (rLeft.Overlap(m.x, m.y))
		{
			SeekMonth(-1);
		}
		else if (rRight.Overlap(m.x, m.y))
		{
			SeekMonth(1);
		}
	}
}
	
void LMonthView::OnPaint(LSurface *pDC)
{
	pDC->Colour(L_WORKSPACE);
	pDC->Rectangle();
		
	Cell = LView::X() / MonthView::X();
	LSysFont->Colour(L_TEXT, L_WORKSPACE);
	LSysFont->Transparent(false);
		
	LRect Client = GetClient();
	LDisplayString n(LSysBold, Title());
	LSysBold->Colour(L_TEXT, L_WORKSPACE);
	LSysBold->Transparent(true);
	n.Draw(pDC, 6, 0);
	rTitle.ZOff(Client.X()-1, n.Y()*3/2-1);
	rRight = rTitle;
	rRight.x1 = rRight.x2 - LSysFont->GetHeight() + 1;
	rLeft = rRight;
	rLeft.Offset(-rLeft.Y(), 0);
	rCells.ZOff(Cell * MonthView::X(), Cell * MonthView::Y());
	rCells.Offset(0, rTitle.Y());
		
	LDisplayString DsLeft(LSysFont, "<");
	DsLeft.Draw(pDC, rLeft.x1, rLeft.y1);

	LDisplayString DsRight(LSysFont, ">");
	DsRight.Draw(pDC, rRight.x1, rRight.y1);
		
	LDateTime t = Start;
	auto Mode = CalView->GetViewMode();
	auto CursorPos = MonthView::GetCursor();
	for (int y=0; y<MonthView::Y(); y++)
	{
		for (int x=0; x<MonthView::X(); x++)
		{
			char s[32];
			sprintf_s(s, sizeof(s), "%i", t.Day());
			LDisplayString Ds(LSysFont, s);

			bool InMonth = Cursor.Month() == t.Month();
			bool IsCursor = Cursor.IsSameDay(t);
			if (IsCursor)
				LSysFont->Colour(L_FOCUS_SEL_FORE, L_FOCUS_SEL_BACK);
			else
			{
				LColour Fore, Back;
					
				if (InMonth)
				{
					Fore = LColour(L_TEXT);
					Back.Rgb(0xf7, 0xf7, 0xf7);
				}
				else
				{
					Fore.Rgb(192, 192, 192);
					Back = LColour(L_WORKSPACE);
				}
					
				if (Mode == CAL_VIEW_WEEK && y == CursorPos.y)
				{
					Back = Back.Mix(LColour(L_FOCUS_SEL_BACK), 0.1f);
				}
					
				LSysFont->Colour(Fore, Back);
			}
				
			LRect r;
			r.ZOff(Cell-2, Cell-2);
			r.Offset(x*Cell, y*Cell+rCells.y1);
			int Cx = Cell - Ds.X();
			int Cy = Cell - Ds.Y();
			Ds.Draw(pDC, r.x1+(Cx>>1), r.y1+(Cy>>1), &r);
				
			if (!t.AddDays(1))
			{
				LAssert(!"Add days failed.");
				break;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
LArray<CalendarViewWnd*> CalendarViewWindows;

CalendarViewWnd::CalendarViewWnd(ScribeFolder *folder)
{
	CalendarViewWindows.Add(this);
	App = folder ? folder->App : 0;
	Cv = 0;
	Split = 0;
	Todo = 0;
	HorBox = NULL;
	VerBox = NULL;
	CalLst = NULL;
	MonthV = NULL;
	Name("Calendar View");

	if (!SerializeState(App->GetOptions(), OPT_CalendarViewPos, true))
	{
		LRect p(0, 0, 600, 500);
		SetPos(p);
		MoveToCenter();
	}
		
	LDateTime Now;
	Now.SetNow();
	OnOptionsChange();
		
	#if !defined(WINDOWS)
	SetIcon("_cal.png");
	#endif
		
	auto ToolBar = folder->App->LoadToolbar(this,
											folder->App->GetResourceFile(ResToolbarFile),
											folder->App->GetToolbarImgList());
	if (ToolBar)
	{
		AddView(ToolBar);
		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_WEEK)), IDM_CAL_WEEK, TBT_RADIO, true, IMG_CAL_WEEK);
		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_MONTH)), IDM_CAL_MONTH, TBT_RADIO, true, IMG_CAL_MONTH);
		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_YEAR)), IDM_CAL_YEAR, TBT_RADIO, true, IMG_CAL_YEAR);
		ToolBar->AppendSeparator();

		ToolBar->AppendButton(0, IDM_PREV, TBT_PUSH, true, IMG_CAL_PREV);
		ToolBar->AppendButton(0, IDM_BACK, TBT_PUSH, true, IMG_CAL_BACK);
		ToolBar->AppendButton(LLoadString(IDS_TODAY), IDM_TODAY, TBT_PUSH, true, IMG_CAL_TODAY);
		ToolBar->AppendButton(0, IDM_FORWARD, TBT_PUSH, true, IMG_CAL_FORWARD);
		ToolBar->AppendButton(0, IDM_NEXT, TBT_PUSH, true, IMG_CAL_NEXT);
		ToolBar->AppendSeparator();

		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_TODO)), IDM_TODO, TBT_TOGGLE, true, IMG_CAL_TODO);
		// ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_CONFIGURE)), IDM_CONFIG, TBT_PUSH, true, IMG_CAL_CONFIG);
		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_PRINT)), IDM_PRINT, TBT_PUSH, true, IMG_PRINT);
		ToolBar->AppendButton(RemoveAmp(LLoadString(IDS_HELP)), IDM_HELP, TBT_PUSH, true, IMG_HELP);
	}
		
	// Month control
	#if defined(WINDOWS)
	int ColPixels = 160;
	#else
	int ColPixels = 180;
	#endif
	LCss::Len ColPx(LCss::LenPx, (float)ColPixels);
	LCss::Len Auto("auto");
	LCss::Len Pad("10px");
		
	Cv = new CalendarView(folder, IDC_CALENDAR, NULL, "Calendar View");

	AddView(HorBox = new LBox);
	HorBox->AddView(VerBox = new LBox);
	HorBox->GetCss(true)->BackgroundColor(LColour(L_WORKSPACE));
	VerBox->SetVertical(true);
	VerBox->GetCss(true)->Width(ColPx);
	VerBox->GetCss(true)->BackgroundColor(LColour(L_WORKSPACE));
	VerBox->AddView(MonthV = new LMonthView(IDC_MONTH_VIEW, &Now, Cv));
	MonthV->GetCss(true)->Height(ColPx);

	LRect r(0, 0, ColPixels-1, ColPixels-1);
	MonthV->SetPos(r);
	// c->Padding(Pad);

	// List of calendar sources...
	VerBox->AddView(CalLst = new LList(IDC_LIST, 0, 0, 100, 100, "Calendar Sources"));
	CalLst->AddColumn("x", 20);
	CalLst->AddColumn("Calendar", 150);
		
	// Main layout view
	HorBox->AddView(Cv);
	if (Cv)
	{
		Cv->OnCursorChange(false, true, false);
		Cv->Visible(true);
			
		switch (Cv->GetViewMode())
		{
			default:
				break;
			case CAL_VIEW_WEEK:
				SetCtrlValue(IDM_CAL_WEEK, 1);
				break;
			case CAL_VIEW_MONTH:
				SetCtrlValue(IDM_CAL_MONTH, 1);
				break;
			case CAL_VIEW_YEAR:
				SetCtrlValue(IDM_CAL_YEAR, 1);
				break;
		}
	}

	if (Cv && CalLst)
	{
		for (unsigned i=0; i<CalendarSource::GetSources().Length(); i++)
		{
			CalendarSource *s = CalendarSource::GetSources().ItemAt(i);
			auto *Li = dynamic_cast<LListItem*>(s);
			CalLst->Insert(Li);

			bool IsCreateIn = FolderCalendarSource::GetCreateIn() == s;
			Li->Select(IsCreateIn);
		}
	}

	#if WINNATIVE
	CreateClassW32("Calendar", LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_CALENDER)));
	#endif
	if (Attach(0))
	{			
		AttachChildren();		
		Visible(true);

		LVariant ViewTodo;
		App->GetOptions()->GetValue(OPT_CalendarViewTodo, ViewTodo);
		SetCtrlValue(IDM_TODO, ViewTodo.CastInt32());
		if (ViewTodo.CastInt32())
		{
			// Layout();
		}
	}
}
	
CalendarViewWnd::~CalendarViewWnd()
{
	if (CalLst)
		CalLst->RemoveAll(); // The CalendarView owns the list items.
			
	SerializeState(App->GetOptions(), OPT_CalendarViewPos, false);
		
	LVariant s;
	App->GetOptions()->SetValue(OPT_CalendarViewTodo, s = (int)GetCtrlValue(IDM_TODO));
	CalendarViewWindows.Delete(this);
}

void CalendarViewWnd::OptionsChange()
{
	LVariant v;
	int FirstDayOfWeek = 0;
	if (App->GetOptions()->GetValue(OPT_CalendarFirstDayOfWeek, v))
		FirstDayOfWeek = v.CastInt32();
	
	if (MonthV)
	{
		MonthV->FirstDayOfWeek = CalendarView::FirstDayOfWeek;
		MonthV->Invalidate();
	}
}

void CalendarViewWnd::OnOptionsChange()
{
	if (!CalendarView::App &&
		CalendarViewWindows.Length() > 0)
	{
		CalendarView::App = CalendarViewWindows[0]->App;
	}
	CalendarView::OnOptionsChange();

	for (auto w: CalendarViewWindows)
		w->OptionsChange();
}

bool CalendarViewWnd::OnKey(LKey &k)
{
	switch (k.vkey)
	{
		case 'w':
		case 'W':
		{
			if (k.CtrlCmd() && k.Down())
			{
				Quit();
				return true;
			}
			break;
		}
	}
		
	return LWindow::OnKey(k);
}

int CalendarViewWnd::OnCommand(int Cmd, int Event, OsView WndHandle)
{
	CalViewArrow *Va = 0;

	switch (Cmd)
	{
		case IDM_CAL_DAY:
		{
			Cv->SetViewMode(CAL_VIEW_DAY);
			break;
		}
		case IDM_CAL_WEEK:
		{
			Cv->SetViewMode(CAL_VIEW_WEEK);
			break;
		}
		case IDM_CAL_MONTH:
		{
			Cv->SetViewMode(CAL_VIEW_MONTH);
			break;
		}
		case IDM_CAL_YEAR:
		{
			Cv->SetViewMode(CAL_VIEW_YEAR);
			break;
		}
		case IDM_PREV:
		{
			LKey k;
			k.c16 = LK_PAGEUP;
			k.Flags = LGI_EF_CTRL;
			Va = GetModeArrow(Cv->GetViewMode(), k);
			break;
		}
		case IDM_BACK:
		{
			LKey k;
			k.c16 = LK_PAGEUP;
			Va = GetModeArrow(Cv->GetViewMode(), k);
			break;
		}
		case IDM_TODAY:
		{
			LDateTime Dt;
			Dt.SetNow();
			Dt.Minutes(0);
			Dt.Seconds(0);
			Dt.Thousands(0);
			Cv->SetCursor(Dt);
			break;
		}
		case IDM_FORWARD:
		{
			LKey k;
			k.c16 = LK_PAGEDOWN;
			Va = GetModeArrow(Cv->GetViewMode(), k);
			break;
		}
		case IDM_NEXT:
		{
			LKey k;
			k.c16 = LK_PAGEDOWN;
			k.Flags = LGI_EF_CTRL;
			Va = GetModeArrow(Cv->GetViewMode(), k);
			break;
		}
		case IDM_PRINT:
		{
			auto *Printer = Cv && App ? App->GetPrinter() : NULL;
			if (Printer)
			{
				CalendarViewPrint Cvp(Cv);
				Printer->Print(&Cvp, NULL, "Scribe Calendar", -1, this);
			}				
			break;
		}
		case IDM_HELP:
		{
			App->LaunchHelp("calendar.html");
			break;
		}
	}

	if (Va)
	{
		LDateTime Cursor = Cv->GetCursor();
		Cursor.AddHours(Va->Hours);
		Cursor.AddDays(Va->Days);
		Cursor.AddMonths(Va->Months);
		Cursor.AddMonths(Va->Years * 12);
		Cv->SetCursor(Cursor);			
	}

	return 0;
}

LMessage::Result CalendarViewWnd::OnEvent(LMessage *m)
{
	switch (m->Msg())
	{
		case M_CHANGE:
		{
			if (m->A() == IDC_LIST)
			{
				// One of the calendar source's has changed...
				// Check it's still in our list and 'valid'
				CalendarSource *s = (CalendarSource*)m->B();
				LArray<CalendarSource*> All;
				CalLst->GetAll(All);
				if (All.IndexOf(s) >= 0)
					Cv->OnContentsChanged(s);
			}
			break;
		}
		#ifdef WIN32
		case WM_CLOSE:
		{
			Quit();
			return 0;
		}
		#endif
	}

	return LWindow::OnEvent(m);
}

LString CalendarViewWnd::LoadString(int id)
{
	return LString(LLoadString(id)).Replace("&","");
}

LString CalendarViewWnd::UnusedKey()
{
	LString Key;
									
	// Find an unused index for the new source...
	while (true)
	{
		Key.Printf("%s.Source-%i", OPT_CalendarSources, LRand(1000));
		if (App->GetOptions()->LockTag(Key, _FL))
			App->GetOptions()->Unlock();
		else
			break;
	}
	
	return Key;
}

int CalendarViewWnd::OnNotify(LViewI *c, LNotification n)
{
	static bool Processing = false;
	if (Processing)
		return 0;
		
	Processing = true;
	switch (c->GetId())
	{
		case IDC_CALENDAR:
		{
			if (Cv &&
				MonthV &&
				n.Type == LNotifyCursorChanged)
			{
				LDateTime t = Cv->GetCursor();
				MonthV->Set(&t);
				MonthV->Invalidate();
			}
			break;
		}
		case IDC_MONTH_VIEW:
		{
			if (Cv &&
				MonthV &&
				n.Type == LNotifyCursorChanged)
			{
				LDateTime t;
				t = MonthV->Get();
				Cv->SetCursor(t);
			}
			break;
		}
		case IDC_LIST:
		{
			if (!CalLst)
				break;

			switch (n.Type)
			{
				default:
					break;
				case LNotifyValueChanged:
				{
					if (Cv)
					{
						FolderCalendarSource *Src = dynamic_cast<FolderCalendarSource*>(CalLst->GetSelected());
						if (Src)
							Cv->OnContentsChanged(Src);
					}
					break;
				}
				case LNotifyItemContextMenu:
				{							
					LMouse m;
					GetMouse(m);
					m.ToScreen();
					LListItem *Sel = CalLst->GetSelected();
						
					LSubMenu s;
					if (Sel)
					{
						LSubMenu *ColMenu = s.AppendSub("Colour");
						if (ColMenu)
						{
							BuildMarkMenu(	ColMenu,
											MS_None,
											0);
						}
					}
					s.AppendItem(LLoadString(IDS_ADD_LOCAL_CAL_FOLDER), IDM_ADD_LOCAL_CAL);
					s.AppendItem(LLoadString(IDS_ADD_CAL_URL), IDM_ADD_CAL_URL);
					s.AppendSeparator();
					s.AppendItem(LoadString(IDS_EDIT), IDM_EDIT, Sel != NULL);
					s.AppendItem(LoadString(IDS_DELETE), IDM_DELETE, Sel != NULL);
						
					int Id = s.Float(this, m);
					switch (Id)
					{
						case IDM_ADD_LOCAL_CAL:
						{
							auto Dlg = new FolderDlg(this, App, MAGIC_CALENDAR);
							Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
							{
								if (ctrlId)
								{
									auto Key = UnusedKey();
									auto Parts = Key.SplitDelimit(".");

									// Create the source...
									FolderCalendarSource *cs = new FolderCalendarSource(App, Parts.Last());
									if (cs)
									{
										cs->SetPath(Dlg->Get());
										cs->SetColour(CalendarSource::FindUnusedColour());
										CalLst->Insert(cs); // CalLst doesn't own the ptr
										cs->Write();
									}
									
									App->SaveOptions();
								}
								delete dlg;
							});
							break;
						}
						case IDM_ADD_CAL_URL:
						{
							auto dlg = new LInput(this, "", LLoadString(IDC_CAL_URL, "Calendar URL:"), AppName);
							dlg->DoModal([this, dlg](auto dialog, auto ok)
							{
								if (ok)
								{
									auto Key = UnusedKey();
									auto Parts = Key.SplitDelimit(".");
									auto Url = dlg->GetStr();
									
									// Create the source...
									RemoteCalendarSource *cs = new RemoteCalendarSource(App, Parts.Last());
									if (cs)
									{
										cs->SetColour(CalendarSource::FindUnusedColour());
										cs->SetUri(Url);
										CalLst->Insert(cs); // CalLst doesn't own the ptr
										cs->Write();
									}
									
									App->SaveOptions();
								}
								delete dialog;
							});
							break;
						}
						case IDM_EDIT:
						{
							CalendarSource *Src = dynamic_cast<CalendarSource*>(Sel);
							if (!Src)
								break;

							Src->EditPath(this, Cv);
							break;
						}
						case IDM_DELETE:
						{
							CalendarSource *Src = dynamic_cast<CalendarSource*>(Sel);
							if (Cv && Src)
							{
								auto *Lst = Src->LListItem::GetList();
								if (Lst)
									Lst->Remove(Src);
								Cv->DeleteSource(Src);
							}
							break;
						}
						default:
						{
							int Idx = Id - IDM_MARK_BASE;
							if (Idx >= 0 && Idx < CountOf(MarkColours32))
							{
								LColour Mc(MarkColours32[Idx], 32);
								CalendarSource *Src = dynamic_cast<CalendarSource*>(Sel);
								if (Src)
								{
									Src->SetColour(Mc);
									Src->Write();
								}
							}
							break;
						}
					}
					break;
				}
			}
			break;
		}
		case IDC_TODO:
		{
			if (Todo &&
				n.Type == LNotifyItemColumnClicked)
			{
				int Col = 0;
				LMouse m;
				if (Todo->GetColumnClickInfo(Col, m))
				{
					int Sort = 0;

					for (int i=0; i<Todo->GetColumns(); i++)
					{
						LItemColumn *c = Todo->ColumnAt(i);
						if (c)
						{
							if (i == Col)
							{
								if (c->Mark() == GLI_MARK_DOWN_ARROW)
								{
									c->Mark(GLI_MARK_UP_ARROW);
									Sort = -(i + 1);
								}
								else
								{
									c->Mark(GLI_MARK_DOWN_ARROW);
									Sort = i + 1;
								}
							}
							else
							{
								c->Mark(GLI_MARK_NONE);
							}
						}
					}

					Todo->Sort<NativeInt>(TodoCompare, Sort);
				}
			}
			break;
		}
	}
	Processing = false;

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
void OpenCalender(ScribeFolder *folder)
{
	if (!CalendarView::CalendarViews.Length())
	{
		new CalendarViewWnd(folder);
	}
}

void CalendarSource::FolderDelete(ScribeFolder *f)
{
	for (auto s: AllSources)
		s->OnFolderDelete(f);
}

LColour CalendarSource::FindUnusedColour()
{
	// MarkColours32
	LArray<bool> Used;
	Used.Length(IDM_MARK_MAX);
	for (auto &s: AllSources)
	{
		auto c = s->GetColour();
		uint32_t c32 = c.c32();
		for (int i=0; i<IDM_MARK_MAX; i++)
		{
			if (MarkColours32[i] == c32)
			{
				Used[i] = true;
				break;
			}
		}
	}
	for (int i=0; i<IDM_MARK_MAX; i++)
	{
		if (!Used[i])
			return LColour(MarkColours32[i], 32);
	}

	return LColour();
}

