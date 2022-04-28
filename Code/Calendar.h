

#ifndef __CALENDER_H
#define __CALENDER_H

// Forward class decl
#include "lgi/common/vCard-vCal.h"
#include "ScribeListAddr.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/ListItemRadioBtn.h"

class Calendar;
class CalendarUi;
class CalendarView;
class Attendee;
class CalendarSource;
class GTimeLine;

extern void InitCalendarView();

enum CalRecurEndType {
	CalEndError,
	CalEndNever,
	CalEndOnCount,
	CalEndOnDate,
};

enum CalRecurFreq {
	CalFreqDays,
	CalFreqWeeks,
	CalFreqMonths,
	CalFreqYears,
};

// Classes
struct TimePeriod
{
	Calendar *c;
	CalendarSource *src;
	LDateTime s, e;

	TimePeriod()
	{
		c = NULL;
		src = NULL;
	}

	void ToLocal()
	{
		s.ToLocal();
		e.ToLocal();
	}

	bool Overlap(TimePeriod &p)
	{
		if (s >= p.e || e <= p.s)
			return false;

		return true;
	}

	bool Overlap(LDateTime &start, LDateTime &end)
	{
		if (s >= end || e <= start)
			return false;

		return true;
	}
};

#include "Attendee.h"

class ScribeClass Calendar :
	public Thing
{
	friend class CalendarUi;
	friend class CalendarView;
	friend class CalendarSource;
	friend class CalendarTodoItem;

	// Static
	static List<Calendar> Reminders;

	// Data
	CalendarUi *Ui;
	LRegion ViewPos;
	CalendarSource *Source;
	class CalendarTodoItem *TodoView;
	LDateTime RecurAfter;
	LDateTime RemindTs;

	// Member
	bool GetParentSelection(LList *Lst, List<LListItem> &s);
	void OnPaintView(LSurface *pDC, LFont *Font, LRect *Pos, TimePeriod *Period);

public:
	static void CheckReminders();
	static bool SummaryOfToday(ScribeWnd *App, LVariant &v);

	// Week view
	static int DayStart;
	static int DayEnd;
	static int WorkDayStart;
	static int WorkDayEnd;
	static int WorkWeekStart;
	static int WorkWeekEnd;

	// Object
	Calendar(ScribeWnd *app, LDataI *object = 0);
	~Calendar();

	// Properties
	Store3ItemTypes Type() override { return MAGIC_CALENDAR; }
	void OnSerialize(bool Write) override;
	LColour GetColour();
	CalendarView *GetView();
	CalendarType GetCalType();
	void SetCalType(CalendarType Type);
	CalendarSource *GetSource() { return Source; }
	bool GetTimes(LDateTime StartLocal, LDateTime EndLocal, LArray<TimePeriod> &Times);
	LDateTime *GetRemindTs() { return &RemindTs; }

	// Thing
	uint32_t GetFlags() override;
	ThingUi *DoUI(MailContainer *c = 0) override;
	ThingUi *GetUI() override;
	bool SetUI(ThingUi *ui = 0) override;
	void DoContextMenu(LMouse &m, LView *Parent = 0) override;
	void OnCreate() override;
	bool OnDelete() override;
	int Compare(LListItem *Arg, ssize_t FieldId) override;
	Calendar *IsCalendar() override { return this; }

	Thing &operator =(Thing &c) override;
	bool operator ==(Thing &c);

	// ListItem
	void OnMouseClick(LMouse &m) override;
	const char *GetText(int i) override;
	int *GetDefaultFields() override;
	const char *GetFieldText(int Field) override;

	// Misc
	bool Save(ScribeFolder *Folder = 0) override;
	bool Overlap(Calendar *c);

	// Import/Export
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	bool Import(LStreamI &f, const char *MimeType) override;
	bool Export(LStreamI &f, const char *MimeType) override;
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;

	// Property storage
	bool GetObjects(List<LDataI> &l);
	LDataI *NewObject(int Type);

	// Printing
	void OnPrintHeaders(struct ScribePrintContext &Context) override;
	void OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages) override;

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LVariant *ReturnValue, LArray<LVariant*> &Args) override;
};

class CalendarUi :
	public ThingUi,
	public LDefaultDocumentEnv,
	public LResourceLoad
{
	friend class Calendar;
	struct CalendarUiPriv *d;
	
	Calendar *Item;
	LTabView *Tabs;
	LTabPage *Appointment;
	LTabPage *AttendTab;
	LScriptUi Commands;
	class GAttendees *Attendees;
	bool NotifyOn;

	bool OnViewKey(LView *v, LKey &k);	
	void CheckConsistancy();
	LDateTime CurrentStart();
	LDateTime CurrentEnd();
	void UpdateStartRelative();
	void UpdateEndRelative();
	void UpdateRelative();

public:
	CalendarUi(Calendar *item);
	~CalendarUi();

	const char *GetClass() { return "CalendarUi"; }
	
	Calendar *GetCal() { return Item; }
	int OnCommand(int Cmd, int Event, OsView Window);
	int OnNotify(LViewI *Ctrl, LNotification n);
	void OnPosChange();

	void OnLoad();
	void OnSave();
};

class CalendarSource : public LListItem
{
protected:
	ScribeWnd *App;
	LString Id;
	int Display;
	LColour Colour;

	void SaveAttr(LXmlTag *t, const char *attr, const char *val)
	{
		if (ValidStr(val))
			t->SetAttr(attr, val);
		else
			t->DelAttr(attr);
	}

	void SetParentFolder(Calendar *c, ScribeFolder *f)
	{
		if (c)
			c->SetParentFolder(f);
	}

	void SetCalendarsSource(Calendar *c)
	{
		if (c)
			c->Source = this;
	}

	static CalendarSource *CreateIn;
	static LArray<CalendarSource*> AllSources;
	LString GetKey();

public:
	static CalendarSource *GetCreateIn() { return CreateIn; }
	static void SetCreateIn(CalendarSource *New);
	static const LArray<CalendarSource*> &GetSources() { return AllSources; }
	static LColour FindUnusedColour();
	static void FolderDelete(ScribeFolder *f);

	static constexpr const char *OptPath	= "Path";
	static constexpr const char *OptUri		= "Uri";
	static constexpr const char *OptDisplay	= "Display";
	static constexpr const char *OptColour	= "Colour";
	static constexpr const char *OptObject	= "Object";

	static CalendarSource *Create(ScribeWnd *app, const char *ObjName, const char *Id);

	CalendarSource()
	{
		App = NULL;
		Display = true;
		AllSources.Add(this);
	}
	
	virtual ~CalendarSource()
	{
		AllSources.Delete(this);
		if (CreateIn == this)
			SetCreateIn(NULL);
	}
	
	auto *GetApp() { return App; }

	virtual void EditPath(LView *parent, CalendarView *cv) = 0;
	virtual bool Delete() = 0;
	virtual bool Write() = 0;
	virtual bool Match(char *Email) = 0;
	virtual LColour GetColour();
	virtual void SetColour(LColour c) = 0;
	virtual const char *GetName() = 0;
	virtual bool Read() = 0;
	virtual bool GetEvents(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &Events) = 0;
	virtual Calendar *NewEvent() = 0;
	virtual void OnFolderDelete(ScribeFolder *f) = 0;
	virtual void OnPulse() = 0;
};

class FolderCalendarSource :
	public CalendarSource
{
protected:
	LString Path;
	ScribeFolder *Folder;

public:
	FolderCalendarSource(ScribeWnd *a, const char *id = NULL);
	~FolderCalendarSource();

	const char *GetClass() { return "FolderCalendarSource"; }

	// Actions
	bool Read();
	bool Write();
	bool Delete();
	Calendar *NewEvent();
	bool Match(char *Email);
	bool GetEvents(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &Events);
	void EditPath(LView *parent, CalendarView *cv);

	// Props
	const char *GetPath() { return Path; }
	void SetPath(const char *Path);
	void SetColour(LColour c);
	int GetDisplay() { return Display; }
	void SetDisplay(int d) { Display = d; Update(); }
	const char *GetName() { return Path; }

	// Events
	void OnMouseClick(LMouse &m);
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c);
	const char *GetText(int i);
	void OnChange(bool IsDelete);
	void OnFolderDelete(ScribeFolder *f);
	void OnPulse();
};

class RemoteCalendarSource :
	public CalendarSource
{
	struct RemoteCalendarSourcePriv *d;

public:
	RemoteCalendarSource(ScribeWnd *a, const char *id = NULL);
	~RemoteCalendarSource();

	const char *GetClass() { return "RemoteCalendarSource"; }

	// Actions
	bool Read();
	bool Write();
	bool Delete();
	Calendar *NewEvent();
	bool Match(char *Email);
	bool GetEvents(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &Events);
	void EditPath(LView *parent, CalendarView *cv);

	// Props
	const char *GetUri();
	void SetUri(const char *uri);
	LColour GetColour();
	void SetColour(LColour c);
	const char *GetName();

	// Events
	void OnMouseClick(LMouse &m);
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c);
	const char *GetText(int i);
	void OnFolderDelete(ScribeFolder *f);
	void OnPulse();
	void OnChange(bool IsDelete);
	LMessage::Result OnEvent(LMessage *Msg);
};

class CalendarTodoItem : public LListItem
{
	friend int TodoCompare(LListItem *la, LListItem *lb, NativeInt d);

	ScribeWnd *App;
	Calendar *Todo;
	bool EditingLabel;
	char DateCache[40];
	LListItemCheckBox *Done;

	void SetTodo(Calendar *todo);
	void OnColumnNotify(int Col, int64 Data);

public:
	CalendarTodoItem(ScribeWnd *app, Calendar *todo = 0);
	~CalendarTodoItem();

	const char *GetText(int Col);
	bool SetText(const char *s, int Col);
	void OnPaint(ItemPaintCtx &Ctx);
	void OnMouseClick(LMouse &m);
	Calendar *GetTodo() { return Todo; }
	void Resort();
};

#endif
