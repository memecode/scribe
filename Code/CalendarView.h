#ifndef __Calendar_VIEW_H
#define __Calendar_VIEW_H

#include "Calendar.h"

#define SX(x)		((x)*Scale)
#define SY(y)		((y)*Scale)
#define SRect(r)	((r).x1 = (int)((double)(r).x1*Scale));\
					((r).y1 = (int)((double)(r).y1*Scale));\
					((r).x2 = (int)((double)(r).x2*Scale));\
					((r).y2 = (int)((double)(r).y2*Scale));


class CalendarView :
	public LLayout,
	public LDragDropTarget,
	public LDragDropSource
{
	friend class Calendar;
	friend class CalendarConfig;

public:
	struct TsRange
	{
		uint64 StartTs;
		uint64 EndTs;
		
		bool Overlap(uint64 s, uint64 e)
		{
			if (EndTs < s || StartTs > e)
				return false;
			return true;
		}

		bool Overlap(TsRange &r)
		{
			if (EndTs < r.StartTs || StartTs > r.EndTs)
				return false;
			return true;
		}
	};
	
	enum EventDragMode
	{
		DragNone,			// No drag operation in effect.
		DragNewEvent,		// Selecting a region for a new event
		DragMoveSelection,	// Moving existing events to a new time
		DragMoveStart,		// Editing the start time of a single event
		DragMoveEnd,		// Editing the end time of a single event
		DragDropObject,		// Dragging an external object over the view
	};

protected:
	// Data
	LArray<TimePeriod> Current;
	LArray<Calendar*> Selection;
	CalendarViewMode Mode;

	// Date/Times
	LDateTime Cursor;
	LDateTime First; // of month
	LDateTime Start; // of visible
	LDateTime SingleClick; // time clicked on

	// Clicking and dragging interaction
	LPoint ClickPt;
	LDateTime DragStart, DragEnd;
	LArray<TsRange> Ranges;
	EventDragMode DragMode;
	uint64 LastTsOffset;
	Calendar *DragEvent;

	// Week view
	int DayStart, DayEnd;

	// DST info
	LArray<LDateTime::GDstInfo> Dst;
	LDateTime::GDstInfo *GetDstForDate(LDateTime t);

	// Layout data
	LRect Title;
	LRect Layout;
	LRect PrintMargin;
	int MonthX, MonthY;

	// Display
	LAutoPtr<LFont> Font;

	// Drag'n'drop Target
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);
	void OnDragExit();

	// Drag'n'drop Source
	void OnDragInit(bool Success);
	char *TypeOf();
	bool GetData(LArray<LDragData> &Data);
	bool GetFormats(LDragFormats &Formats);

	// Internal Methods
	void OnSelect(Calendar *c, bool Ctrl, bool Shift);
	bool Overlap(LDateTime &Start, LDateTime &End, Calendar *a, Calendar *b);
	void SetupScroll();

public:
	static ScribeWnd *App;
	static int SnapMinutes;
	static int FirstDayOfWeek;
	static LArray<CalendarView*> CalendarViews;
	static void OnOptionsChange();

	CalendarView(ScribeFolder *folder, int Id = -1, LRect *r = 0, const char *Name = 0);
	~CalendarView();
	
	const char *GetClass() { return "CalendarView"; }

	// Methods
	Calendar *CalendarAt(int x, int y);
	LDateTime *TimeAt(int x, int y, int SnapMinutes, LPoint *Cell = 0);
	void OnDelete();
	LArray<Calendar*> &GetSelection() { return Selection; }
	void SelectDropTarget(LDateTime *Start = 0, LDateTime *End = 0);
	bool GetEventsBetween(LArray<TimePeriod> &list, LDateTime Start, LDateTime End);
	void LoadUsers();
	void DeleteSource(CalendarSource *cs);
	LCursor GetCursor(int x, int y);
	bool HitTest(int x, int y, EventDragMode &mode, Calendar *&event);
	Calendar *NewEvent(LDateTime &dtStart, LDateTime &dtEnd);

	// Properties
	CalendarViewMode GetViewMode();
	void SetViewMode(CalendarViewMode m);
	LDateTime &GetCursor();
	void SetCursor(LDateTime &c);

	// Overridable
	virtual void OnContentsChanged(CalendarSource *s = 0);
	virtual void OnSourceDelete(CalendarSource *s);
	virtual void OnCursorChange(bool Day = true,
								bool Month = true,
								bool Year = true);

	// Events
	void DrawSelectionBox(LSurface *pDC, LRect &r);
	void OnPaint(LSurface *pDC);
	bool OnPrintPage(LPrintDC *pDC, int PageIndex);

	void OnMouseClick(LMouse &m);
	void OnMouseMove(LMouse &m);
	void OnFocus(bool f);
	bool OnKey(LKey &k);
	void OnCreate();
	void OnPulse();
	int OnNotify(LViewI *v, LNotification n);
	bool OnLayout(LViewLayoutInfo &Inf);
};

class CalendarViewWnd : public LWindow
{
	ScribeWnd *App;
	CalendarView *Cv;
	LSplitter *Split;
	LList *Todo;
	class LMonthView *MonthV;
	LBox *HorBox, *VerBox;

	void OptionsChange();

public:
	LList *CalLst;

	CalendarViewWnd(ScribeFolder *folder);
	~CalendarViewWnd();
	
	LString LoadString(int id);
	LString UnusedKey();
	
	bool OnKey(LKey &k);
	int OnCommand(int Cmd, int Event, OsView WndHandle);
	LMessage::Result OnEvent(LMessage *m);
	int OnNotify(LViewI *c, LNotification n);
	static void OnOptionsChange();
};

#endif
