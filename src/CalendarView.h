#ifndef __Calendar_VIEW_H
#define __Calendar_VIEW_H

#include "Calendar.h"

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
		LTimeStamp StartTs;
		LTimeStamp EndTs;
		
		bool Overlap(LTimeStamp s, LTimeStamp e)
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
	CalendarSourceGetEvents *GetEvents = NULL;
	bool SourceEventsDirty = false;

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
	LArray<LDstInfo> Dst;
	LDstInfo *GetDstForDate(LDateTime t);

	// Layout data
	LRect Title; // This is the title at the top of the page...
	LRect ColumnHeading; // Column headings...
	LRect Layout; // Content boxes...
	LRect PrintMargin;
	int MonthX, MonthY;

	// Display
	LAutoPtr<LFont> Font;

	// Drag'n'drop Target
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState) override;
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState) override;
	void OnDragExit() override;

	// Drag'n'drop Source
	void OnDragInit(bool Success) override;
	char *TypeOf();
	bool GetData(LArray<LDragData> &Data) override;
	bool GetFormats(LDragFormats &Formats) override;

	// Internal Methods
	void OnSelect(Calendar *c, bool Ctrl, bool Shift);
	bool Overlap(LDateTime &Start, LDateTime &End, Calendar *a, Calendar *b);
	void SetupScroll();
	void CalDelete(Calendar *c);

public:
	static ScribeWnd *App;
	static int SnapMinutes;
	static int FirstDayOfWeek;
	static LArray<CalendarView*> CalendarViews;
	static void OnOptionsChange();
	static void OnDelete(Calendar *c)
	{
		for (auto cv: CalendarViews)
			cv->CalDelete(c);
	}

	CalendarView(ScribeFolder *folder, int Id = -1, LRect *r = NULL, const char *Name = NULL);
	~CalendarView();
	
	const char *GetClass() override { return "CalendarView"; }

	// Methods
	Calendar *CalendarAt(int x, int y);
	LDateTime *TimeAt(int x, int y, int SnapMinutes, LPoint *Cell = NULL);
	void OnDelete();
	LArray<Calendar*> &GetSelection() { return Selection; }
	void SelectDropTarget(LDateTime *Start = NULL, LDateTime *End = NULL);
	bool GetEventsBetween(LArray<TimePeriod> &list, LDateTime Start, LDateTime End);
	void LoadUsers();
	void DeleteSource(CalendarSource *cs);
	LCursor GetCursor(int x, int y) override;
	bool HitTest(int x, int y, EventDragMode &mode, Calendar *&event);
	Calendar *NewEvent(LDateTime &dtStart, LDateTime &dtEnd);

	// Properties
	CalendarViewMode GetViewMode();
	void SetViewMode(CalendarViewMode m);
	LDateTime &GetCursor();
	void SetCursor(LDateTime &c);

	// Overridable
	virtual void OnContentsChanged(CalendarSource *s = NULL);
	virtual void OnSourceDelete(CalendarSource *s);
	virtual void OnCursorChange(bool Day = true,
								bool Month = true,
								bool Year = true);

	// Events
	void DrawSelectionBox(LSurface *pDC, LRect &r);
	void OnPaint(LSurface *pDC) override;
	bool OnPrintPage(LPrintDC *pDC, int PageIndex);

	void OnMouseClick(LMouse &m) override;
	void OnMouseMove(LMouse &m) override;
	bool OnMouseWheel(double Lines) override;
	void OnFocus(bool f) override;
	bool OnKey(LKey &k) override;
	void OnCreate() override;
	void OnPulse() override;
	int OnNotify(LViewI *v, const LNotification &n) override;
	bool OnLayout(LViewLayoutInfo &Inf) override;
	LMessage::Result OnEvent(LMessage *Msg) override;
};

class CalendarViewWnd :
	public LWindow,
	public LDom
{
	ScribeWnd *App = nullptr;
	CalendarView *Cv = nullptr;
	LList *Todo = nullptr;
	class LMonthView *MonthV = nullptr;
	LBox *HorBox = nullptr, *VerBox = nullptr;

	void OptionsChange();

public:
	LList *CalLst = nullptr;
	static LArray<CalendarViewWnd*> instances;
	static void OnOptionsChange();

	CalendarViewWnd(ScribeFolder *folder);
	~CalendarViewWnd();
	
	LString LoadString(int id);
	LString UnusedKey();
	
	bool OnKey(LKey &k) override;
	int OnCommand(int Cmd, int Event, OsView WndHandle) override;
	LMessage::Result OnEvent(LMessage *m) override;
	int OnNotify(LViewI *c, const LNotification &n) override;
	void OnPrint();
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;
};

extern CalendarViewWnd *OpenCalender(ScribeFolder *folder);

#endif
