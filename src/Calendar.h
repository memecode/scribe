

#ifndef __CALENDER_H
#define __CALENDER_H

// Forward class decl
#include "lgi/common/vCard-vCal.h"
#include "ScribeListAddr.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/ListItemRadioBtn.h"
#include "lgi/common/TabView.h"

class Calendar;
class CalendarUi;
class CalendarView;
class Attendee;
class CalendarSource;
class LTimeLine;
class CalendarSourceGetEvents;

extern void InitCalendarView();
extern const char *RelativeTime(LDateTime &Then);

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
	Calendar *c = NULL;
	CalendarSource *src = NULL;
	LDateTime s, e;

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

	LString ToString();
	void Set(CalendarSource *source, Calendar *cal, LDateTime start, LDateTime end);
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
	static CalendarSourceGetEvents *GetEvents;

	// Data
	CalendarUi *Ui = NULL;
	LRegion ViewPos;
	CalendarSource *Source = NULL;
	class CalendarTodoItem *TodoView = NULL;
	LDateTime RecurAfter;
	LDateTime RemindTs;

	// Member
	bool GetParentSelection(LList *Lst, List<LListItem> &s);
	void OnPaintView(LSurface *pDC, LFont *Font, LRect *Pos, TimePeriod *Period);

public:
	static void CheckReminders();
	static void SummaryOfToday(ScribeWnd *App, std::function<void(LString)> Callback);

	// Week view
	static int DayStart;
	static int DayEnd;
	static int WorkDayStart;
	static int WorkDayEnd;
	static int WorkWeekStart;
	static int WorkWeekEnd;

	// Object
	Calendar(ScribeWnd *app, LDataI *object = NULL);
	~Calendar();
	
	const char *GetClass() override { return "Calendar"; }

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
	LString ToString();

	// Thing
	uint32_t GetFlags() override;
	ThingUi *DoUI(MailContainer *c = NULL) override;
	ThingUi *GetUI() override;
	bool SetUI(ThingUi *ui = NULL) override;
	void DoContextMenu(LMouse &m, LView *Parent = NULL) override;
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
	bool Save(ScribeFolder *Folder = NULL) override;
	bool Overlap(Calendar *c);

	// Import/Export
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	IoProgress Import(IoProgressFnArgs) override;
	IoProgress Export(IoProgressFnArgs) override;
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;

	// File attachment storage
	LArray<LDataI*> GetAttachments();
	LDataI *ImportAttachment(LString Path);
	bool DeleteAttachment(LDataI *attachment);

	// Printing
	void OnPrintHeaders(struct ScribePrintContext &Context) override;
	void OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages) override;

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;
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
	class LAttendees *Attendees;
	bool NotifyOn = false;
	bool FirstLayout = true;

	bool OnViewKey(LView *v, LKey &k) override;	
	void CheckConsistancy();
	LDateTime CurrentStart();
	LDateTime CurrentEnd();
	void UpdateStartRelative();
	void UpdateEndRelative();
	void UpdateRelative();

public:
	CalendarUi(Calendar *item);
	~CalendarUi();

	const char *GetClass() override { return "CalendarUi"; }
	
	Calendar *GetCal() { return Item; }
	int OnCommand(int Cmd, int Event, OsView Window) override;
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
	void OnPosChange() override;
	LMessage::Result OnEvent(LMessage *Msg) override;

	void OnLoad() override;
	void OnSave() override;
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
	typedef std::function<void(LArray<TimePeriod>&)> GetEventCb;
	virtual bool GetEvents(	const LDateTime Start,
							const LDateTime End,
							GetEventCb Callback) = 0;
	virtual Calendar *NewEvent(LError *err) = 0;
	virtual void OnFolderDelete(ScribeFolder *f) = 0;
	virtual void OnPulse() = 0;
	virtual LString ToString() = 0;
	virtual bool IsWritable() = 0;
};

/// Helper class to collect events from multiple CalendarSource objects
class CalendarSourceGetEvents : public LView::ViewEventTarget
{
	ScribeWnd *App = NULL;
    LArray<CalendarSource*> Sources;
	LArray<TimePeriod> Events;
	LDateTime Start, End;
	CalendarSource::GetEventCb Callback;
	CalendarSourceGetEvents **Owner = NULL;

	#ifdef _DEBUG
	LHashTbl<PtrKey<CalendarSource*>,bool> GotCb;
	#endif

public:
	CalendarSourceGetEvents(ScribeWnd *app,
							CalendarSourceGetEvents **owner,
							LDateTime start,
							LDateTime end,
							LArray<CalendarSource*> sources,
							CalendarSource::GetEventCb callback);
	~CalendarSourceGetEvents();

	void OnState();
	LMessage::Result OnEvent(LMessage *Msg);
};

class FolderCalendarSource :
	public CalendarSource
{
protected:
	LString Path;
	ScribeFolder *Folder = NULL;
	bool IsReading = false;

public:
	FolderCalendarSource(ScribeWnd *a, const char *id = NULL);
	~FolderCalendarSource();

	const char *GetClass() { return "FolderCalendarSource"; }
	LString ToString()
	{
		LString s;
		s.Printf("%p::FolderCalendarSource(%s)", this, Path.Get());
		return s;
	}

	// Actions
	bool Read();
	bool Write();
	bool Delete();
	Calendar *NewEvent(LError *err);
	bool Match(char *Email);
	bool GetEvents(	const LDateTime Start,
					const LDateTime End,
					GetEventCb Callback);
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
	bool IsWritable() { return true; }
};

// Loads a read only iCal feed from a remote URI.
class RemoteCalendarSource :
	public CalendarSource
{
	class RemoteCalendarSourcePriv *d;

public:
	RemoteCalendarSource(ScribeWnd *a, const char *id = NULL);
	~RemoteCalendarSource();

	const char *GetClass() { return "RemoteCalendarSource"; }
	LString ToString();
	bool IsWritable() { return false; }

	// Actions
	bool Read();
	bool Write();
	bool Delete();
	Calendar *NewEvent(LError *err);
	bool Match(char *Email);
	bool GetEvents(const LDateTime Start, const LDateTime End, GetEventCb Callback);
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
	int Compare(LListItem *To, ssize_t Field) override;

public:
	CalendarTodoItem(ScribeWnd *app, Calendar *todo = NULL);
	~CalendarTodoItem();

	const char *GetText(int Col);
	bool SetText(const char *s, int Col);
	void OnPaint(ItemPaintCtx &Ctx);
	void OnMouseClick(LMouse &m);
	Calendar *GetTodo() { return Todo; }
	void Resort();
};

#endif
