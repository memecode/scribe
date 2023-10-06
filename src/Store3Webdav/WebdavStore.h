#ifndef _WEBDAV_STORE_H_
#define _WEBDAV_STORE_H_

#include "lgi/common/WebDav.h"

//////////////////////////////////////////////////////////////////////
class WebdavFolder;
class WebdavCalendar;
class WebdavThread;
struct WebdavEvent;

class WebdavStore :
	public LDataStoreI,
	public LMutex
{
	friend class WebdavThread;
	friend class WebdavCalendar;
	friend class WebdavContact;

public:
	struct LRemote
	{
		LString Name, Url, User, Pass;
	};

protected:
	ScribeWnd *App;
	LRemote Remote;
	LDataEventsI *Callback;
	LArray<WebdavEvent*> Events;

	LString ContactUrl, CalUrl;
	WebdavFolder *Root = NULL, *ContactFolder = NULL, *CalFolder = NULL;

	LXmlTag *LockSettings(const char *File, int Line);
	void UnlockSettings();

public:
	WebdavStore(ScribeWnd *a, LDataEventsI *cb, LString optsPath);
	~WebdavStore();
	
	const char *GetClass() override { return "WebdavStore"; }

	// Actions
	/*
	bool Read();
	bool Write();
	bool Delete();
	Calendar *NewEvent();
	bool Match(char *Email);
	bool GetEvents(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &Events);
	*/

	// Events
	void OnChanged();

	// LDataPropI
	int64 GetInt(int id) override;
	const char *GetStr(int id) override;
	LDataPropI *GetObj(int id) override;

	// LDataStoreI impl
	uint64 Size() override { return 0; }
	LDataI *Create(int Type) override;
	LDataFolderI *GetRoot(bool create = false) override;
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items) override { return Store3Error; }
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash) override;
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator) override { return Store3Error; }
	void Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus) override { if (OnStatus) OnStatus(true); }
	void OnEvent(void *Param) override;
	bool OnIdle() override { return true; }
	LDataEventsI *GetEvents() override { return NULL; }

	// LDataEventsI impl
	void Post(LDataStoreI *store, void *Param);
	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new);
	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items);
	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items);
	bool OnChange(LArray<LDataI*> &items, int FieldHint);
};

/////////////////////////////////////////////////////////////////////////////////////
class WebdavObj : public LDataI
{
	friend class WebdavStore;

protected:
	WebdavStore *Store;
	LString Href;
	bool Converted;
	WebdavFolder *Parent;

public:
	WebdavObj(WebdavStore *store) { Store = store; Parent = NULL; }

	const char *GetClass() override { return "WebdavObj"; }
	const char *GetHref() { return Href; }
	virtual void FireOnChange(int Fld) {}
	virtual bool ConvertToText() { return false; }

	// LDataI impl
	uint32_t Type() override { return MAGIC_NONE; }
	bool IsOnDisk() override { return false; }
	bool IsOrphan() override { return Store == NULL || Parent == NULL; }
	LDataStoreI *GetStore() override { return Store; }

	// Stubs
	uint64 Size() override { return 0; }
	Store3Status Save(LDataI *Parent = NULL) override { return Store3NotImpl; }
	Store3Status Delete(bool ToTrash = true) override { return Store3NotImpl; }
	LAutoStreamI GetStream(const char *file, int line) override { return LAutoStreamI(NULL); }
	bool SetStream(LAutoStreamI stream) override { return false; }
	bool ParseHeaders() override { return false; }
	LDataPropI *GetObj(int id) override { EmptyVirtual(NULL); }
	Store3Status SetObj(int id, LDataPropI *i) override { EmptyVirtual(Store3Error); }
	LDataIt GetList(int id) override { EmptyVirtual(NULL); }
	Store3Status SetRfc822(LStreamI *Rfc822Msg) override { return Store3Error; }
};

class WebdavFld : public LDataPropI
{
	LDataStoreI *Store;
	WebdavFolder *Parent;
	int Id, Width;

public:
	WebdavFld(LDataStoreI *s, WebdavFolder *p = NULL, int id = 0, int width = 100);
	
	const char *GetClass() override { return "WebdavFld"; }
	const char *GetStr(int id) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
};

class WebdavFolder : public LDataFolderI
{
	friend class WebdavStore;
	WebdavFolder &operator =(const WebdavFolder &f) = delete;

	WebdavStore *Store;
	WebdavFolder *Parent;
	LString Name;
	Store3ItemTypes ItemType = MAGIC_ANY;
	int64 Sort = 0;
	Store3State State = Store3Unloaded;

public:
	LString Url;
	const char *Extension = NULL;
	DIterator<LDataI,       WebdavObj,    WebdavStore> Items;
	DIterator<LDataFolderI, WebdavFolder, WebdavStore> Sub;
	DIterator<LDataPropI,   WebdavFld,    WebdavStore> Field;
	LAutoPtr<WebdavThread> Thread;

	WebdavFolder(WebdavStore *store, WebdavFolder *parent = NULL);

	const char *GetClass() override { return "WebdavFolder"; }
	LString AllocateAddress();

	// LDataPropI impl
	bool CopyProps(LDataPropI &p) override;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;

	// LDataI impl
	uint32_t Type() override { return MAGIC_FOLDER; }
	bool IsOnDisk() override { return false; }
	bool IsOrphan() override { return false; }
	uint64 Size() override { return 0; }
	Store3Status Save(LDataI *Parent = NULL) override;
	Store3Status Delete(bool ToTrash = true) override;
	LDataStoreI *GetStore() override { return Store; }
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override { return false; }
	bool ParseHeaders() override { return false; }

	// LDataFolderI impl
	LDataIterator<LDataFolderI*> &SubFolders() override;
	LDataIterator<LDataI*> &Children() override;
	LDataIterator<LDataPropI*> &Fields() override;
};

/////////////////////////////////////////////////////////////////////////////////////
#define WebdavCalendarDates() \
	_(FIELD_CAL_START_UTC, Start) \
	_(FIELD_CAL_END_UTC, End) \
	_(FIELD_CAL_RECUR_END_DATE, RecurEnd) \
	_(FIELD_CAL_LAST_CHECK, LastCheck) \
	_(FIELD_DATE_MODIFIED, DateMod)

#define WebdavCalendarInts() \
	_(FIELD_CAL_TYPE, ObjType) \
	_(FIELD_CAL_COMPLETED, Completed) \
	_(FIELD_CAL_SHOW_TIME_AS, ShowTimeAs) \
	_(FIELD_CAL_RECUR, Recur) \
	_(FIELD_CAL_RECUR_FREQ, RecurFreq) \
	_(FIELD_CAL_RECUR_INTERVAL, RecurInterval) \
	_(FIELD_CAL_RECUR_END_COUNT, RecurEndCount) \
	_(FIELD_CAL_RECUR_END_TYPE, RecurEndType) \
	_(FIELD_CAL_RECUR_FILTER_DAYS, RecurFilterDays) \
	_(FIELD_CAL_RECUR_FILTER_MONTHS, RecurFilterMonths) \
	_(FIELD_CAL_ALL_DAY, AddDay) \
	_(FIELD_CAL_PRIVACY, Privacy) \
	_(FIELD_COLOUR, Colour) \
	_(FIELD_STATUS, StoreStatus)

#define WebdavCalendarStrings() \
	_(FIELD_UID, Uid) \
	_(FIELD_CAL_TIMEZONE, TimeZone) \
	_(FIELD_CAL_SUBJECT, Subject) \
	_(FIELD_CAL_LOCATION, Location) \
	_(FIELD_CAL_REMINDERS, Reminders) \
	_(FIELD_CAL_RECUR_FILTER_POS, FilterPos) \
	_(FIELD_CAL_RECUR_FILTER_YEARS, FilterYears) \
	_(FIELD_CAL_NOTES, Notes) \
	_(FIELD_CAL_STATUS, CalStatus)

class WebdavCalendar : public WebdavObj
{
	LString vCal, Href;

	#define _(f,v) LDateTime v;
	WebdavCalendarDates()
	#undef _
	#define _(f,v) uint64_t v = 0;
	WebdavCalendarInts()
	#undef _
	#define _(f,v) LString v;
	WebdavCalendarStrings()
	#undef _

public:
	WebdavCalendar(WebdavStore *store, WebdavEvent *e);

	const char *GetClass() override { return "WebdavCalendar"; }

	void FireOnChange(int Fld) override;
	bool ConvertToText() override;

	// Stubs
	uint32_t Type() override { return MAGIC_CALENDAR; }
	uint64 Size() override { return vCal.Length(); }
	
	// LDataI Impl
	Store3Status Save(LDataI *Parent = NULL) override;
	Store3Status Delete(bool ToTrash = true) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool CopyProps(LDataPropI &p) override;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

/////////////////////////////////////////////////////////////////////////////////////
#define WebdavContactDates() \
	_(FIELD_DATE_MODIFIED, DateMod)

#define WebdavContactVariants() \
	_(FIELD_CONTACT_IMAGE, Image)

#define WebdavContactInts() \
	_(FIELD_STATUS, Status)

#define WebdavContactStrings() \
	_(FIELD_UID, Uid) \
	_(FIELD_TITLE, Title) \
	_(FIELD_FIRST_NAME, First) \
	_(FIELD_LAST_NAME, Last) \
	_(FIELD_EMAIL, Email) \
	_(FIELD_ALT_EMAIL, AltEmail) \
	_(FIELD_NICK, Nick) \
	_(FIELD_SPOUSE, Spouse) \
	_(FIELD_NOTE, Notes) \
	_(FIELD_TIMEZONE, TimeZone) \
	\
	_(FIELD_HOME_STREET, HomeStreet) \
	_(FIELD_HOME_SUBURB, HomeSuburb) \
	_(FIELD_HOME_POSTCODE, HomePostCode) \
	_(FIELD_HOME_STATE, HomeState) \
	_(FIELD_HOME_COUNTRY, HomeCountry) \
	_(FIELD_HOME_PHONE, HomePhone) \
	_(FIELD_HOME_MOBILE, HomeMobile) \
	_(FIELD_HOME_IM, HomeIM) \
	_(FIELD_HOME_FAX, HomeFax) \
	_(FIELD_HOME_WEBPAGE, HomeWebpage)	\
	\
	_(FIELD_WORK_STREET, WorkStreet) \
	_(FIELD_WORK_SUBURB, WorkSuburb) \
	_(FIELD_WORK_POSTCODE, WorkPostCode) \
	_(FIELD_WORK_STATE, WorkState) \
	_(FIELD_WORK_COUNTRY, WorkCountry) \
	_(FIELD_WORK_PHONE, WorkPhone) \
	_(FIELD_WORK_MOBILE, WorkMobile) \
	_(FIELD_WORK_IM, WorkIM) \
	_(FIELD_WORK_FAX, WorkFax) \
	_(FIELD_WORK_WEBPAGE, WorkWebpage)	\
	_(FIELD_COMPANY, Company) \
	\
	_(FIELD_CONTACT_JSON, JSON)

class WebdavContact : public WebdavObj
{
	bool Converted;
	LString vCard;

	#define _(f,v) LDateTime v;
	WebdavContactDates()
	#undef _
	#define _(f,v) LVariant v;
	WebdavContactVariants()
	#undef _
	#define _(f,v) int64 v = 0;
	WebdavContactInts()
	#undef _
	#define _(f,v) LString v;
	WebdavContactStrings()
	#undef _

public:
	WebdavContact(WebdavStore *store, WebdavEvent *e);

	const char *GetClass() override { return "WebdavContact"; }

	void FireOnChange(int Fld) override;
	bool ConvertToText() override;

	// Stubs
	uint32_t Type() override { return MAGIC_CONTACT; }
	uint64 Size() override { return vCard.Length(); }
	
	// LDataI Impl
	Store3Status Save(LDataI *Parent = NULL) override;
	Store3Status Delete(bool ToTrash = true) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool CopyProps(LDataPropI &p) override;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	const LVariant *GetVar(int id) override;
	Store3Status SetVar(int id, LVariant *i) override;
};

#endif
