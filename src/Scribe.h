/*hdr
**      FILE:           Scribe.h
**      AUTHOR:         Matthew Allen
**      DATE:           22/10/97
**      DESCRIPTION:    Scribe email application
**
**      Copyright (C) 1998-2003 Matthew Allen
**              fret@memecode.com
*/

// Includes
#include <stdio.h>
#include <functional>

#include "lgi/common/Lgi.h"
#include "lgi/common/DragAndDrop.h"
#include "lgi/common/DateTime.h"
#include "lgi/common/Password.h"
#include "lgi/common/vCard-vCal.h"
#include "lgi/common/WordStore.h"
#include "lgi/common/XmlTreeUi.h"
#include "lgi/common/Mime.h"
#include "lgi/common/OptionsFile.h"
#include "lgi/common/TextLog.h"
#include "lgi/common/Menu.h"
#include "lgi/common/ToolBar.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Printer.h"

// Gui controls
#include "lgi/common/Panel.h"
#include "lgi/common/DocView.h"
#include "lgi/common/List.h"
#include "lgi/common/Tree.h"
#include "lgi/common/ListItemCheckBox.h"

// Storage
#include "lgi/common/Store3.h"

// App Includes
#include "ScribeInc.h"
#include "ScribeUtils.h"
#include "ScribeDefs.h"
#include "DomType.h"

// Defines

// All functions in a LWindow or LView should use one of these at the top.
// Until such time as a function has been checked for thread safety it should
// use THREAD_UNSAFE([returnValue]) at the top. Otherwise THREAD_SAFE denotes
// that the function IS safe to use in any thread.
#define THREAD_UNSAFE(...)			if (!InThread()) \
									{ \
										LStackTrace("%s call out of thread.\n", __func__); \
										LAssert(!"not thread safe"); \
										return __VA_ARGS__; \
									}
#define THREAD_SAFE(...)

////////////////////////////////////////////////////////////////////////////////////////////
// Classes
class MailTree;
class LMailStore;
class ScribeWnd;
class Thing;
class Mail;
class Contact;
class ThingUi;
class MailUi;
class ScribeFolder;
class ContactUi;
class FolderPropertiesDlg;
class ScribeAccount;
class Filter;
class Attachment;
class Calendar;
class CalendarSource;

class AttachmentList;
class FolderDlg;
class Filter;
class ContactGroup;
class ScribeBehaviour;
class AccountletThread;
class ThingList;
class LSpellCheck;
class ListAddr;

// The field definition type
struct ItemFieldDef
{
	const char *DisplayText;
	ScribeDomType Dom;
	LVariantType Type;
	int FieldId; // Was 'Id'
	int CtrlId;
	const char *Option;
	bool UtcConvert;
};

////////////////////////////////////////////////////////////////////////
// Scripting support
#include "lgi/common/Scripting.h"

/// Script callback types. See 'api.html' in the Scripts folder for more details.
enum LScriptCallbackType
{
	LCallbackNull,
	LToolsMenu,
	LThingContextMenu,
	LThingUiToolbar,
	LApplicationToolbar,
	LMailOnBeforeSend, // "OnBeforeMailSend"
	LMailOnAfterReceive,
	LBeforeInstallBar,
	LInstallComponent,
	LFolderContextMenu,
	LOnTimer,
	LRenderMail,
	LOnLoad
};

struct LScript;
struct LScriptCallback
{
	static constexpr int INVALID_CALLBACK = 0;

	LScriptCallbackType Type = LCallbackNull;
	LScript *Script = NULL;
	LFunctionInfo *Func = NULL;
	int Uid = INVALID_CALLBACK;
	int Param = 0;
	double fParam = 0.0;
	LVariant Data;
	uint64 PrevTs = 0;
	bool OnSecond = false;
};

struct LScript
{
	LAutoPtr<LCompiledCode> Code;
	LArray<LScriptCallback> Callbacks;
};

typedef void (*ConsoleClosingCallback)(class LScriptConsole *Console, void *user_data);

class LScriptConsole : public LWindow
{
	ScribeWnd *App;
	LTextLog *Txt;
	ConsoleClosingCallback Callback;
	void *CallbackData;

	bool OnViewKey(LView *v, LKey &k);

public:
	LScriptConsole(ScribeWnd *app, ConsoleClosingCallback callback, void *callback_data);
	~LScriptConsole();

	void Write(const char *s, int64 Len);
	bool OnRequestClose(bool OsShuttingDown);
};

/// This class is a wrapper around a user interface element used for
/// Scripting. The script engine needs to be able to store information 
/// pertaining to the menu item's callbacks along with the sub menu.
class LScriptUi : public LDom
{
public:
	LScriptUi *Parent;
	LSubMenu *Sub;
	LToolBar *Toolbar;
	LArray<LScriptCallback> Callbacks;
	LArray<LScriptUi*> Subs;

	LScriptUi()
	{
		Parent = 0;
		Sub = 0;
		Toolbar = 0;
	}

	LScriptUi(LSubMenu *s)
	{
		Parent = 0;
		Toolbar = 0;
		Sub = s;
	}

	LScriptUi(LToolBar *t)
	{
		Parent = 0;
		Toolbar = t;
		Sub = 0;
	}

	~LScriptUi()
	{
		Subs.DeleteObjects();
	}
	
	const char *GetClass() override { return "LScriptUi"; }

	bool GetVariant(const char *Name, LVariant &Value, const char *Arr = NULL) override
	{
		if (Sub)
			return Sub->GetVariant(Name, Value, Arr);

		LDomProperty Method = LStringToDomProp(Name);
		if (Method == ObjLength)
		{
			if (Toolbar)
				Value = (int64)Toolbar->Length();
		}
		else return false;

		return true;
	}

	bool CallMethod(const char *MethodName, LScriptArguments &Args) override
	{
		if (Sub)
			return Sub->CallMethod(MethodName, Args);

		return false;
	}

	bool SetupCallbacks(ScribeWnd *App, ThingUi *Parent, Thing *t, LScriptCallbackType Type);
	bool ExecuteCallbacks(ScribeWnd *App, ThingUi *Parent, Thing *t, int Cmd);
};

class LScribeScript : public LScriptContext
{
	LScriptEngine *Eng;
	struct LScribeScriptPriv *d;

public:
	ScribeWnd *App;

	static LScribeScript *Inst;

	LScribeScript(ScribeWnd *app);
	~LScribeScript();

	void ShowScriptingWindow(bool show);
	LAutoString GetDataFolder() override;
	LStream *GetLog() override;
	LHostFunc *GetCommands() override;	
	LString GetIncludeFile(const char *FileName) override;
	void SetEngine(LScriptEngine *eng);

	// System
	bool MsgBox(LScriptArguments &Args);

	// Paths
	bool GetSystemPath(LScriptArguments &Args);
	bool GetScribeTempPath(LScriptArguments &Args);
	bool JoinPath(LScriptArguments &Args);

	// Folders
	bool GetFolder(LScriptArguments &Args);
	bool GetSourceFolders(LScriptArguments &Args);
	bool CreateSubFolder(LScriptArguments &Args);
	bool LoadFolder(LScriptArguments &Args);
	bool FolderSelect(LScriptArguments &Args);
	bool BrowseFolder(LScriptArguments &Args);

	// Things
	bool CreateThing(LScriptArguments &Args);
	bool MoveThing(LScriptArguments &Args);
	bool SaveThing(LScriptArguments &Args);
	bool DeleteThing(LScriptArguments &Args);
	bool ShowThingWindow(LScriptArguments &Args);
	bool FilterDoActions(LScriptArguments &Args);
	bool LookupContact(LScriptArguments &Args);

	// Callbacks
	bool AddToolsMenuItem(LScriptArguments &Args);
	bool AddCallback(LScriptArguments& Args);
	bool RemoveCallback(LScriptArguments& Args);

	// UI
	bool MenuAddItem(LScriptArguments &Args);
	bool MenuAddSubmenu(LScriptArguments &Args);
	bool ToolbarAddItem(LScriptArguments &Args);
};

////////////////////////////////////////////////////////////////////////
class ScribePassword
{
	class ScribePasswordPrivate *d;

public:
	ScribePassword(LOptionsFile *p, const char *opt, int check, int pwd, int confirm);
	~ScribePassword();

	bool IsOk();
	bool Load(LView *dlg);
	bool Save();
	void OnNotify(LViewI *c, const LNotification &n);
};


class ImportExportDlg : public LDialog
{
    ScribeWnd *App = NULL;
	int Type = 0;
	bool Export = false;
	LList *Src = NULL;
	LEdit *Dst = NULL;

	void InsertFile(const char *f);

public:
	LString DestFolder;
	LString::Array SrcFiles;
	bool IncSubFolders = false;

	ImportExportDlg
	(
		ScribeWnd *parent,
		bool IsExport,
		const char *Title,
		const char *Msg,
		LString::Array *SrcFiles = NULL,
		const char *DefFolder = NULL,
		int FolderType = MAGIC_MAIL
	);

	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
};

#define IoProgressImplArgs		LAutoPtr<LStreamI> stream, const char *mimeType, IoProgressCallback cb
#define IoProgressFnArgs		IoProgressImplArgs = NULL
#define IoProgressError(err)	\
	{ \
		IoProgress p(Store3Error, err); \
		if (cb) cb(&p, stream); \
		return p; \
	}
#define IoProgressSuccess()	\
	{ \
		IoProgress p(Store3Success); \
		if (cb) cb(&p, stream); \
		return p; \
	}
#define IoProgressNotImpl()	\
	{ \
		IoProgress p(Store3NotImpl); \
		if (cb) cb(&p, stream); \
		return p; \
	}

class ScribeClass ThingType :
	public LDom,
	public LDataUserI
{
	bool Dirty     = false;
	bool WillDirty = true;
	bool Loaded    = false;

protected:
	bool OnError(const char *File, int Line)
	{
		_lgi_assert(false, "Object Missing", File, Line);
		return false;
	}

	// Callbacks
	struct ThingEventInfo
	{
		const char *File = NULL;
		int Line = 0;
		std::function<void(Store3Status)> Callback;
	};
	LArray<ThingEventInfo*> OnLoadCallbacks;

public:
	#ifdef _DEBUG
	bool _debug = false;
	#endif

	struct IoProgress;
	typedef std::function<void(IoProgress*,LStreamI*)> IoProgressCallback;
	struct IoProgress
	{
		// This is the main result to look at:
		//		Store3NotImpl - typically means the mime type is wrong.
		//		Store3Error   - an error occurred.
		//		Store3Delayed - means the operation will take a long time.
		//			However progress is report via 'prog' if not NULL.
		//			And the 'onComplete' handler will be called at the end.
		//		Store3Success - the operation successfully completed.
		Store3Status status = Store3NotImpl;
		
		// Optional progress for the operation. Really only relevant for
		// status == Store3Delayed.
		Progress *prog = NULL;
		
		// Optional error message for the operation. Relevant if
		// status == Store3Error.
		LString errMsg;
		
		IoProgress(Store3Status s, const char *err = NULL)
		{
			status = s;
			if (err)
				errMsg = err;
		}
		
		operator bool()
		{
			return status > Store3Error;
		}

		bool IsCancelled()
		{
			return prog ? prog->IsCancelled() : false;
		}
	};
	
	template<typename T>
	LAutoPtr<LStreamI> AutoCast(LAutoPtr<T> ap)
	{
		return LAutoPtr<LStreamI>(ap.Release());
	}

	static LArray<ThingType*> DirtyThings;
	ScribeWnd *App = NULL;

	ThingType();
	virtual ~ThingType();

	virtual Store3ItemTypes Type() { return MAGIC_NONE; }
	bool GetDirty() { return Dirty; }
	virtual bool SetDirty(bool b = true);
	void SetWillDirty(bool c) { WillDirty = c; }

	virtual bool Save(ScribeFolder *Into) { return false; }
	virtual void OnProperties(int Tab = -1) {}
	virtual ScribeFolder *GetFolder() = 0;
	virtual void SetFolder(ScribeFolder *f, std::function<void(Store3Status)> callback) = 0;
	virtual bool IsPlaceHolder() { return false; }

	// Events
	void WhenLoaded(const char *file, int line, std::function<void(Store3Status)> Callback, int index = -1, bool waitForResults = true);
	bool IsLoaded(int Set = -1);

	// Printing
	virtual void OnPrintHeaders(struct ScribePrintContext &Context) { LAssert(!"Impl me."); }
	virtual void OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages) { LAssert(!"Impl me."); }
	virtual int OnPrintHtml(ScribePrintContext &Context, LPrintPageRanges &Pages, LSurface *RenderedHtml) { LAssert(!"Impl me."); return 0; }
};

class MailContainerIter;
class ScribeClass MailContainer
{
	friend class MailContainerIter;

	List<MailContainerIter> Iters;

public:
	virtual ~MailContainer();

	virtual size_t Length() { return 0; }
	virtual ssize_t IndexOf(Mail *m) { return -1; }
	virtual Mail *operator [](size_t i) { return NULL; }
};

class ScribeClass MailContainerIter
{
	friend class MailContainer;

protected:
	MailContainer *Container;

public:
	MailContainerIter();
	~MailContainerIter();

	void SetContainer(MailContainer *c);
};

class ScribeClass ThingStorage // External storage information
{
public:
	int Data;

	ThingStorage() { Data = 0; }
	virtual ~ThingStorage() {}
};

class ThingUi;

class ScribeClass Thing :
	public ThingType,
	public LListItem,
	public LDragDropSource,
	public LRefCount
{
	friend class ScribeWnd;
	friend class ScribeFolder;

	ScribeFolder *_ParentFolder = NULL;

protected:
	LArray<int> FieldArray;
	LAutoString DropFileName;

	// This structure allows the app to move objects between
	// mail stores. After a delayed write to the new mail store
	// the old item needs to be removed. This keeps track of
	// where that old item is. Don't assume that the Obj pointer
	// is valid... check in the folder's items first.
	struct ThingReference
	{
		LString Path;
		Thing *Obj = NULL;
		std::function<void(Store3Status)> Callback;

	}	DeleteOnAdd;

public:
	LString ErrMsg;
	ThingStorage *Data = NULL;

	Thing(ScribeWnd *app, LDataI *object = NULL);
	~Thing();

	// Dom
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// D'n'd
	bool GetData(LArray<LDragData> &Data) override;
	bool GetFormats(LDragFormats &Formats) override;
	bool OnBeginDrag(LMouse &m) override;

	// Import / Export
	virtual bool GetFormats(bool Export, LString::Array &MimeTypes) { return false; }

	// Anything implementing 2 functions should mostly be using one of these 
	// to "return" an IoProgress and process any callback:
	//		IoProgressError(msg)
	//		IoProgressNotImpl()
	//		IoProgressSuccess()
	virtual IoProgress Import(IoProgressFnArgs) = 0;
	virtual IoProgress Export(IoProgressFnArgs) = 0;
	
	/// This exports all the selected items
	void ExportAll(LViewI *Parent, const char *ExportMimeType, std::function<void(bool)> Callback);
	void ExportAllProcess(LFileSelect *Select, LViewI *Parent, LArray<Thing*> Sel, LString ExportMimeType, std::function<void(bool)> Callback);

	// UI
	bool OnKey(LKey &k) override;

	// Thing
	ScribeFolder *GetFolder() override { return _ParentFolder; }
	void SetParentFolder(ScribeFolder *f);
	void SetFolder(ScribeFolder *f, std::function<void(Store3Status)> callback) override;
	LDataI *DefaultObject(LDataI *arg = NULL);

	virtual ThingUi *DoUI(MailContainer *c = NULL) { return NULL; }
	virtual ThingUi *GetUI() { return NULL; }
	virtual bool SetUI(ThingUi *ui = NULL) { return false; }

	virtual uint32_t GetFlags() { return 0; }
	virtual void OnCreate() override;
	virtual bool OnDelete();
	virtual int *GetDefaultFields() { return NULL; }
	virtual const char *GetFieldText(int Field) { return 0; }
	virtual void DoContextMenu(LMouse &m, LView *Parent = NULL) {}
	virtual Thing &operator =(Thing &c) { LAssert(0); return *this; }
	virtual char *GetDropFileName() = 0;
	virtual bool GetDropFiles(LString::Array &Files) { return false; }
	virtual void OnSerialize(bool Write) {}
	virtual void Reparse();

	// Interfaces
	virtual Mail *IsMail() { return 0; }
	virtual Contact *IsContact() { return 0; }
	virtual ContactGroup *IsGroup() { return 0; }
	virtual Filter *IsFilter() { return 0; }
	virtual Attachment *IsAttachment() { return 0; }
	virtual Calendar *IsCalendar() { return 0; }

	void SetFieldArray(LArray<int> &i) { FieldArray = i; }
	void OnMove();

	bool SetField(int Field, int n);
	bool SetField(int Field, double n);
	bool SetField(int Field, char *n);
	bool SetField(int Field, LDateTime &n);
	bool SetDateField(int Field, LVariant &v);

	bool GetField(int Field, int &n);
	bool GetField(int Field, double &n);
	bool GetField(int Field, const char *&n);
	bool GetField(int Field, LDateTime &n);
	bool GetDateField(int Field, LVariant &v);

	bool DeleteField(int Field);
};

class ThingUi : public LWindow
{
	friend class MailUiGpg;

	bool _Dirty = false;
	LString _Name;

protected:
	Thing *_Item = NULL;
	bool _Running = false;
	void SetItem(Thing *i);

public:
	ScribeWnd *App = NULL;
	static LArray<ThingUi*> All;

	ThingUi(Thing *item, const char *name);
	~ThingUi();

	enum DirtyOptions {		
		NoUi,
		WithUi,
		NoSave, // Don't resave and no Ui
	};
	virtual bool SetDirty(bool d, DirtyOptions ui = WithUi);
	bool IsDirty() { return _Dirty; }
	bool OnRequestClose(bool OsShuttingDown);
	bool OnViewKey(LView *v, LKey &k);

	virtual void OnDirty(bool Dirty) {}
	virtual void OnLoad() = 0;
	virtual void OnSave() = 0;
	virtual void OnChange() {}
	virtual AttachmentList *GetAttachments() { return 0; }
	virtual bool AddRecipient(AddressDescriptor *Addr) { return false; }
};

class ThingFilter
{
public:
	virtual bool TestThing(Thing *Thing) = 0;
};

class ScribeClass Attachment : public Thing
{
	friend class Mail;

protected:
	Mail *Msg;
	Mail *Owner;
	bool IsResizing;
	LString Buf;

	// D'n'd
	LAutoString DropSourceFile;
	bool GetFormats(LDragFormats &Formats) override;
	bool GetData(LArray<LDragData> &Data) override;

	void _New(LDataI *object);
	void DoSave(LFileSelect *Select, const LArray<LListItem*> Files);

public:
	enum Encoding
	{
		OCTET_STREAM,
		PLAIN_TEXT,
		BASE64,
		QUOTED_PRINTABLE,
	};	
	
	Attachment(ScribeWnd *App, Attachment *import = NULL);
	Attachment(ScribeWnd *App, LDataI *object, const char *import = NULL);
	~Attachment();

	const char *GetClass() override { return "Attachment"; }

	bool ImportFile(const char *FileName);
	bool ImportStream(const char *FileName, const char *MimeType, LAutoStreamI Stream);

	Thing &operator =(Thing &c) override;

	LDATA_INT64_PROP(Size, FIELD_SIZE);
	LDATA_STR_PROP(Name, FIELD_NAME);
	LDATA_STR_PROP(MimeType, FIELD_MIME_TYPE);
	LDATA_STR_PROP(ContentId, FIELD_CONTENT_ID);
	LDATA_STR_PROP(Charset, FIELD_CHARSET);
	LDATA_STR_PROP(InternetHeaders, FIELD_INTERNET_HEADER);

	// LDom support
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	void OnOpen(LView *Parent, char *Dest = NULL);
	void OnDeleteAttachment(LView *Parent, bool Ask);
	void OnSaveAs(LView *Parent);
	void OnMouseClick(LMouse &m) override;
	bool OnKey(LKey &k) override;

	Store3ItemTypes Type() override { return MAGIC_ATTACHMENT; }
	bool Get(char **ptr, ssize_t *size);
	bool Set(char *ptr, ssize_t size);
	bool Set(LAutoStreamI Stream);
	Attachment *IsAttachment() override { return this; }
	LString MakeFileName();
	bool GetIsResizing();
	void SetIsResizing(bool b);
	bool IsMailMessage();
	bool IsVCalendar();
	bool IsVCard();

	// The owner is the mail that this is attached to
	Mail *GetOwner() { return Owner; }
	void SetOwner(Mail *msg);

	// The msg is the mail that this message/rfc822 attachment is rendered into
	Mail *GetMsg();
	void SetMsg(Mail *m);

	LStreamI *GotoObject(const char *file, int line);
	IoProgress Import(IoProgressFnArgs) override { return Store3Error; }
	IoProgress Export(IoProgressFnArgs) override { return Store3Error; }

	bool SaveTo(char *FileName, bool Quite = false, LView *Parent = NULL);

	const char *GetText(int i) override;
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;
};

class ScribeClass Contact :
	public Thing
{
	friend class ContactUi;

protected:
	class ContactPriv *d = NULL;
	ContactUi *Ui = NULL;

public:
	static List<Contact> Everyone;
	static Contact *LookupEmail(const char *Email);
	static LHashTbl<ConstStrKey<char,false>, int> PropMap;
	static int DefaultContactFields[];

	Contact(ScribeWnd *app, LDataI *object = NULL);
	~Contact();
	
	const char *GetClass() override { return "Contact"; }

	LDATA_STR_PROP(First, FIELD_FIRST_NAME);
	LDATA_STR_PROP(Last, FIELD_LAST_NAME);
	LDATA_STR_PROP(Email, FIELD_EMAIL);

	bool Get(const char *Opt, const char *&Value);
	bool Set(const char *Opt, const char *Value);
	bool Get(const char *Opt, int &Value);
	bool Set(const char *Opt, int Value);

	// operators
	Thing &operator =(Thing &c) override;
	Contact *IsContact() override { return this; }

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// Events
	void OnMouseClick(LMouse &m) override;

	// Printing
	void OnPrintHeaders(struct ScribePrintContext &Context) override;
	void OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages) override;

	// Misc
	Store3ItemTypes Type() override { return MAGIC_CONTACT; }
	ThingUi *DoUI(MailContainer *c = NULL) override;
	int Compare(LListItem *Arg, ssize_t Field) override;
	bool IsAssociatedWith(char *PluginName);
	LString GetLocalTime(const char *TimeZone = NULL);
	
	// Email address
	int GetAddrCount();
	LString::Array GetEmails();
	LString GetAddrAt(int i);
	bool HasEmail(LString email);

	// Serialization
	bool Save(ScribeFolder *Into = NULL) override;

	// ListItem
	const char *GetText(int i) override;
	int *GetDefaultFields() override;
	const char *GetFieldText(int Field) override;
	int GetImage(int Flags = 0) override { return ICON_CONTACT; }

	// Import/Export
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	IoProgress Import(IoProgressFnArgs) override;
	IoProgress Export(IoProgressFnArgs) override;
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;
};

#define ContactGroupObj					"ContactGroup"
#define ContactGroupName				"Name"
#define ContactGroupList				"List"
#define ContactGroupDateModified		"DateModified"
extern ItemFieldDef GroupFieldDefs[];

class ContactGroup : public Thing
{
	friend class GroupUi;
	class GroupUi *Ui;
	class ContactGroupPrivate *d;
	LString DateCache;

public:
	LDateTime UsedTs;

	LDATA_STR_PROP(Name, FIELD_GROUP_NAME);	

	ContactGroup(ScribeWnd *app, LDataI *object = NULL);
	~ContactGroup();

	const char *GetClass() override { return "ContactGroup"; }

	// operators
	Thing &operator =(Thing &c) override;
	ContactGroup *IsGroup() override { return this; }

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// Events
	void OnMouseClick(LMouse &m) override;
	void OnSerialize(bool Write) override;

	// Misc
	Store3ItemTypes Type() override { return MAGIC_GROUP; }
	ThingUi *DoUI(MailContainer *c = NULL) override;
	int Compare(LListItem *Arg, ssize_t Field) override;
	bool GetAddresses(List<char> &a);
	LString::Array GetAddresses();

	// Serialization
	bool Save(ScribeFolder *Into = NULL) override;

	// ListItem
	const char *GetText(int i) override;
	int *GetDefaultFields() override;
	const char *GetFieldText(int Field) override;
	int GetImage(int Flags = 0) override { return ICON_CONTACT_GROUP; }

	// Import / Export
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	IoProgress Import(IoProgressFnArgs) override;
	IoProgress Export(IoProgressFnArgs) override;
};

struct LGroupMapArray : public LArray<ContactGroup*>
{
	LString toString()
	{
		LString::Array a;
		for (auto i: *this)
			a.Add(i->GetName());
		return LString(",").Join(a);
	}
};

class LGroupMap : public LHashTbl<ConstStrKey<char,false>,LGroupMapArray*>
{
	ScribeWnd *App;

	void Index(ContactGroup *grp);

public:
	LGroupMap(ScribeWnd *app);
	~LGroupMap();
};


/////////////////////////////////////////////////////////////
// Mail threading
//
//    See: http://www.jwz.org/doc/threading.html
struct ThingSortParams
{
	int SortAscend;
	int SortField;
};

class MContainer
{
	int			Lines;
	int			Depth;
	bool		Open;
	bool		Next;

	void Dump(LStream &s, int Depth = 0);

public:
    typedef LHashTbl<StrKey<char>, MContainer*> ContainerHash;

	// Container data
	Mail		*Message;
	MContainer	*Parent;
	LArray<MContainer*> Children;
	List<MContainer> Refs;
	int			Index;

	// Cache data
	LString::Array RefCache;
	
	// Debug
	#ifdef _DEBUG
	LAutoString MsgId;
	#endif

	// Methods
	MContainer(const char *Id, Mail *m = NULL);
	~MContainer();

	void SetMail(Mail *m);
	Mail *GetTop();

	bool HasChild(MContainer *m);
	void AddChild(MContainer *m);
	void RemoveChild(MContainer *m);
	int CountMessages();
	
	void Pour(int &index, int depth, int tree, bool next, ThingSortParams *folder);
	void OnPaint(LSurface *pDC, LRect &r, LItemColumn *c, LColour Fore, LColour Back, LFont *Font, const char *Txt);

	static void Prune(int &ParentIndex, LArray<MContainer*> &L);
	static void Thread(List<Mail> &In, LArray<MContainer*> &Out);
};

extern int ContainerIndexer(Thing *a, Thing *b, NativeInt Data);
extern int GetFolderVersion(const char *Path);
extern bool CreateMailHeaders(ScribeWnd *App, LStream &Out, LDataI *Mail, MailProtocol *Protocol);
extern void Base36(char *Out, uint64 In);

////////////////////////////////////////////////////////////
// Thing sorting

// The old way
extern int ContainerCompare(MContainer **a, MContainer **b);
extern int ThingCompare(Thing *a, Thing *b, NativeInt Data);

// The new way
extern int ContainerSorter(MContainer *&a, MContainer *&b, ThingSortParams *Params);
extern int ThingSorter(Thing *a, Thing *b, ThingSortParams *Data);

/////////////////////////////////////////////////////////////
class MailViewOwner : public LCapabilityTarget
{
public:
    virtual LDocView *GetDoc(const char *MimeType) = 0;
    virtual bool SetDoc(LDocView *v, const char *MimeType) = 0;
};

/////////////////////////////////////////////////////////////
// The core mail object
struct MailPrintContext;

class ScribeClass Mail :
	public Thing,
	public MailContainer,
	public LDefaultDocumentEnv
{
	friend class MailUi;
	friend class MailPropDlg;
	friend class ScribeFolder;
	friend class Attachment;
	friend class ScribeWnd;

private:
	class MailPrivate *d = NULL;
	static LHashTbl<ConstStrKey<char>,Mail*> MessageIdMap;

	// List item preview
	int PreviewCacheX;
	List<LDisplayString> PreviewCache;
	
	int64_t TotalSizeCache = 0;
	int64_t FlagsCache = 0;
	MailUi *Ui = NULL;
	int Cursor; // Stores the cursor position in reply/forward format until the UI needs it
	Attachment *ParentFile = NULL;
	List<Attachment> Attachments;
	Mail *PreviousMail = NULL; // the mail we are replying to / forwarding

	void _New();
	void _Delete();
	bool _GetListItems(List<LListItem> &l, bool All); // All=false is just the selected items
	void SetListRead(bool Read);
	void SetFlagsCache(int64_t NewFlags, bool IgnoreReceipt, bool UpdateScreen);

	// LDocumentEnv impl
	List<Filter> Actions;
	bool OnNavigate(LDocView *Parent, const char *Uri) override;
	bool AppendItems(LSubMenu *Menu, const char *Param, int Base = 1000) override;
	bool OnMenu(LDocView *View, int Id, void *Context) override;
	LoadType GetContent(LAutoPtr<LoadJob> &j) override;

public:
	static bool PreviewLines;
	static bool AdjustDateTz;
	static int DefaultMailFields[];
	static bool RunMailPipes;
	static List<Mail> NewMailLst;
	constexpr static float MarkColourMix = 0.9f;

	uint8_t SendAttempts = 0;
	
	enum NewEmailState
	{
		NewEmailNone,
		NewEmailLoading,
		NewEmailFilter,
		NewEmailBayes,
		NewEmailGrowl,
		NewEmailTray
	};
	
	NewEmailState NewEmail;

	Mail(ScribeWnd *app, LDataI *object = NULL);
	~Mail();

	const char *GetClass() override { return "Mail"; }
	
	bool SetObject(LDataI *o, bool InDataDestuctor, const char *File, int Line) override;

	LDATA_STR_PROP(Label, FIELD_LABEL);
	LDATA_STR_PROP(FwdMsgId, FIELD_FWD_MSG_ID);
	LDATA_STR_PROP(BounceMsgId, FIELD_BOUNCE_MSG_ID);
	LDATA_STR_PROP(Subject, FIELD_SUBJECT);
	LDATA_STR_PROP(Body, FIELD_TEXT);
	LDATA_STR_PROP(BodyCharset, FIELD_CHARSET);
	LDATA_STR_PROP(Html, FIELD_ALTERNATE_HTML);
	LDATA_STR_PROP(HtmlCharset, FIELD_HTML_CHARSET);
	LDATA_STR_PROP(InternetHeader, FIELD_INTERNET_HEADER);
	LDATA_INT_TYPE_PROP(EmailPriority, Priority, FIELD_PRIORITY, MAIL_PRIORITY_NORMAL);
	LDATA_INT32_PROP(AccountId, FIELD_ACCOUNT_ID);
	LDATA_INT64_PROP(MarkColour, FIELD_COLOUR);
	LDATA_STR_PROP(References, FIELD_REFERENCES);
	LDATA_DATE_PROP(DateReceived, FIELD_DATE_RECEIVED);
	LDATA_DATE_PROP(DateSent, FIELD_DATE_SENT);
	LDATA_INT_TYPE_PROP(Store3State, Loaded, FIELD_LOADED, Store3Loaded);
	LVariant GetServerUid();
	bool SetServerUid(LVariant &v);

	const char *GetFromStr(int id)
	{
		LDataPropI *From = GetObject() ? GetObject()->GetObj(FIELD_FROM) : 0;
		return From ? From->GetStr(id) : NULL;
	}

	LDataPropI *GetFrom()
	{
		return GetObject() ? GetObject()->GetObj(FIELD_FROM) : 0;
	}

	LDataPropI *GetReply()
	{
		return GetObject() ? GetObject()->GetObj(FIELD_REPLY) : 0;
	}

	LDataIt GetTo()
	{
		return GetObject() ? GetObject()->GetList(FIELD_TO) : 0;
	}

	bool GetAttachmentObjs(LArray<LDataI*> &Objs);
	LDataI *GetFileAttachPoint();

	// Operators
	Mail *IsMail() override { return this; }
	Thing &operator =(Thing &c) override;
	OsView Handle();
	ThingUi *GetUI() override;
	bool SetUI(ThingUi *ui) override;

	// References and ID's
	MContainer *Container;
	const char *GetMessageId(bool Create = false);
	bool SetMessageId(const char *val);
	LAutoString GetThreadIndex(int TruncateChars = 0);
	
	static Mail *GetMailFromId(const char *Id);
	bool MailMessageIdMap(bool Add = true);
	bool GetReferences(LString::Array &Ids);
	void GetThread(List<Mail> &Thread);
	LString GetMailRef();
	bool ResizeImage(Attachment *a);

	// MailContainer
	size_t Length() override;
	ssize_t IndexOf(Mail *m) override;
	Mail *operator [](size_t i) override;

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// Events
	void OnCreate() override;
	bool OnBeforeSend(struct ScribeEnvelope *Out);
	void OnAfterSend();
	bool OnBeforeReceive();
	bool OnAfterReceive(LStreamI *Msg);
	void OnMouseClick(LMouse &m) override;
	void OnProperties(int Tab = -1) override;
	void OnInspect();
	void OnReply(Mail *m, bool All, bool MarkOriginal);
	bool OnForward(Mail *m, bool MarkOriginal, int WithAttachments = -1);
	bool OnBounce(Mail *m, bool MarkOriginal, int WithAttachments = -1);
	void OnReceipt(Mail *m);
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;

	// Printing
	void OnPrintHeaders(ScribePrintContext &Context) override;
	void OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages) override;
	int OnPrintHtml(ScribePrintContext &Context, LPrintPageRanges &Pages, LSurface *RenderedHtml) override;

	// Misc	
	uint32_t GetFlags() override;
	void SetFlags(ulong i, bool IgnoreReceipt = false, bool Update = true);
	void DeleteAsSpam(LView *View);
	const char *GetFieldText(int Field) override;
	LAutoString GetCharSet();
	char *GetNewText(int Max = 64 << 10, const char *AsCp = "utf-8");
	int *GetDefaultFields() override;
	Store3ItemTypes Type() override { return MAGIC_MAIL; }
	void DoContextMenu(LMouse &m, LView *Parent = NULL) override;
	int Compare(LListItem *Arg, ssize_t Field) override;
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;
	LAutoString GetSig(bool HtmlVersion, ScribeAccount *Account = NULL);
	bool LoadFromFile(char *File);
	void PrepSend();
	void NewRecipient(char *Email, char *Name = NULL);
	void ClearCachedItems();
	bool Send(bool Now);
	void CreateMailHeaders();
	bool AddCalendarEvent(LViewI *Parent, bool AddPopupReminder, LString *Msg);
	LArray<Attachment*> GetCalendarAttachments();
	void Reparse() override;

	// UI
	LDocView *CreateView(MailViewOwner *Owner, LString MimeType, bool Sunken, size_t MaxBytes, bool NoEdit = false);
	ThingUi *DoUI(MailContainer *c = NULL) override;

	// Alt HTML
	bool HasAlternateHtml(Attachment **Attach = NULL);
	char *GetAlternateHtml(List<Attachment> *Refs = NULL); // dynamically allocated ptr
	bool WriteAlternateHtml(char *File = NULL, int FileLen = 0); // defaults to TEMP dir

	// Account stuff
	void ProcessTextForResponse(Mail *From, LOptionsFile *Options, ScribeAccount *Account);
	void WrapAndQuote(LStringPipe &Pipe, const char *QuoteStr, int WrapAt = -1);
	ScribeAccount *GetAccountSentTo();

	// Access
	int64 TotalSizeof();
	bool Save(ScribeFolder *Into = NULL) override;
	
	// Attachments
	Attachment *AttachFile(LView *Parent, const char *FileName);
	bool AttachFile(Attachment *File);
	bool DeleteAttachment(Attachment *File);
	LArray<Attachment*> GetAttachments();
	bool GetAttachments(List<Attachment> *Attachments);
	bool HasAttachments() { return Attachments.Length() > 0; }
	bool UnloadAttachments();

	// Import / Export
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	IoProgress Import(IoProgressFnArgs) override;
	IoProgress Export(IoProgressFnArgs) override;

	// ListItem
	void Update() override;
	const char *GetText(int i) override;
	int GetImage(int SelFlags = 0) override;
	void OnMeasure(LPoint *Info) override;
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c) override;
	void OnPaint(LItem::ItemPaintCtx &Ctx) override;
};

inline const char *toString(Mail::NewEmailState s)
{
	#define _(s) case Mail::s: return #s;
	switch (s)
	{
		_(NewEmailNone)
		_(NewEmailLoading)
		_(NewEmailFilter)
		_(NewEmailBayes)
		_(NewEmailGrowl)
		_(NewEmailTray)
	}
	#undef _
	LAssert(0);
	return "#invalidNewEmailState";
}

// this is where the items reside
// and it forms a leaf on the mail box tree
// to the user it looks like a folder
class ScribeClass ScribeFolder :
	public ThingType,
	public LTreeItem,
	public LDragDropSource,
	public MailContainer
{
	friend class MailTree;
	friend class FolderPropertiesDlg;
	friend class ThingList;
	friend class ScribeWnd;
	friend class AsyncOperationState;
	friend struct MboxExportTask;

protected:
	class ScribeFolderPriv *d;
	ThingList *View();
	LString DropFileName;

	LString GetDropFileName();

	// UI cache
	LAutoString NameCache;
	int ChildUnRead = 0;
	LTreeItem *LoadOnDemand = NULL;
	LAutoPtr<LListItem> Loading;

	LArray<int> FieldArray;
	void SerializeFieldWidths(bool Write = false);
	void EmptyFieldList();
	void SetLoadFolder(Thing *t) { if (t) t->SetParentFolder(this); }
	bool HasFieldId(int Id);
	void ContinueLoading(int OldUnread, std::function<void(Store3Status)> Callback, bool waitForResults);

	// Tree item stuff
	void _PourText(LPoint &Size) override;
	void _PaintText(LItem::ItemPaintCtx &Ctx) override;
	int _UnreadChildren();

	void UpdateOsUnread();

	// Debugging state
	enum FolderState
	{
		FldState_Idle,
		FldState_Loading,
		FldState_Populating,
	}	CurState = FldState_Idle;

public:
	List<Thing> Items;
	std::function<void()> BeforeDelete;

	ScribeFolder();
	~ScribeFolder();

	// Object
	const char *GetClass() override { return "ScribeFolder"; }
	LArray<int> &GetFieldArray() { return FieldArray; }
	LDataFolderI *GetFldObj() { return dynamic_cast<LDataFolderI*>(GetObject()); }
	bool SetObject(LDataI *o, bool InDataDestuctor, const char *File, int Line) override;

	// ThingType
	Store3ItemTypes Type() override { return GetObject() ? (Store3ItemTypes)GetObject()->Type() : MAGIC_NONE; }
	ScribeFolder *GetFolder() override { return dynamic_cast<ScribeFolder*>(LTreeItem::GetParent()); }
	ScribeFolder *GetChildFolder() { return dynamic_cast<ScribeFolder*>(LTreeItem::GetChild()); }
	ScribeFolder *GetNextFolder() { return dynamic_cast<ScribeFolder*>(LTreeItem::GetNext()); }
	ScribeFolder *FindChild(const char *name);
	void SetFolder(ScribeFolder *f, std::function<void(Store3Status)> callback) override;
	ScribeFolder *IsFolder() { return this; }
	Store3Status CopyTo(ScribeFolder *NewParent, int NewIndex = -1);

	// MailContainer
	size_t Length() override;
	ssize_t IndexOf(Mail *m) override;
	Mail *operator [](size_t i) override;

	/// Update the unread count
	void OnUpdateUnRead
	(
		/// Increments the count, or zero if a child folder is changing.
		int Offset,
		/// Re-scan the folder
		bool ScanItems
	);							

	// Methods
	LDATA_INT32_PROP(UnRead, FIELD_UNREAD);
	LDATA_INT_TYPE_PROP(Store3ItemTypes, ItemType, FIELD_FOLDER_TYPE, MAGIC_MAIL);
	LDATA_INT32_PROP(Open, FIELD_FOLDER_OPEN);
	LDATA_INT32_PROP(SortIndex, FIELD_FOLDER_INDEX);
	LDATA_INT64_PROP(Items, FIELD_FOLDER_ITEMS); // Cached item count
	LDATA_INT_TYPE_PROP(ScribePerm, ReadAccess, FIELD_FOLDER_PERM_READ, PermRequireNone);
	LDATA_INT_TYPE_PROP(ScribePerm, WriteAccess, FIELD_FOLDER_PERM_WRITE, PermRequireNone);
	LDATA_ENUM_PROP(SystemFolderType, FIELD_SYSTEM_FOLDER, Store3SystemFolder);

	void SetSort(int Col, bool Ascend, bool CanDirty = true);
	int GetSortAscend() { return GetObject()->GetInt(FIELD_SORT) > 0; }
	int GetSortCol() { return abs((int)GetObject()->GetInt(FIELD_SORT)) - 1; }
	int GetSortField();
	void ReSort();
	bool Save(ScribeFolder *Into = NULL) override;
	bool ReindexField(int OldIndex, int NewIndex);
	void CollectSubFolderMail(ScribeFolder *To = NULL);
	bool InsertThing(Thing *Item);
	void MoveTo(LArray<Thing*> &Items, bool CopyOnly, std::function<void(bool, LArray<Store3Status>&)> Callback = NULL);
	bool Delete(LArray<Thing*> &Items, bool ToTrash);
	void SetDefaultFields(bool Force = false);
	bool Thread();
	ScribePerm GetFolderPerms(ScribeAccessType Access); 
	void SetFolderPerms(LView *Parent, ScribeAccessType Access, ScribePerm Perm, std::function<void(bool)> Callback); 
	bool GetThreaded();
	void SetThreaded(bool t);
	// void Update();
	void GetMessageById(const char *Id, std::function<void(Mail*)> Callback);
	void SetLoadOnDemand();
	void SortSubfolders();
	void DoContextMenu(LMouse &m);
	void OnItemType(bool load);
	bool IsInTrash();
	bool SortItems();
	
	/// 
	/// These methods can be used in a synchronous or asynchronous manner:
	///		sync:	Call with 'Callback=NULL' and use the return value.
	///				If the function needs to show a dialog (like to get permissions from
	///				the user) then it'll return Store3Delayed immediately.
	///		async:	Call with a valid callback, and the method will possibly wait 
	///				for the user and then either return Store3Error or Store3Success.
	///
	/// If waitForResults is false, LoadThings won't wait for things to load over the network, but
	/// return Store3Delayed in the status immediately. Currently this is used by CalendarSourceGetEvents
	/// to return a combined list of events available immediately. Some remote calendar or Webdav sources
	/// can take a while to get their content.
	Store3Status LoadThings(LViewI *Parent = NULL,	std::function<void(Store3Status)> Callback = NULL, bool waitForResults = true);
	Store3Status WriteThing(Thing *t,				std::function<void(Store3Status)> Callback = NULL);
	Store3Status DeleteThing(Thing *t,				std::function<void(Store3Status)> Callback = NULL);
	Store3Status DeleteAllThings(					std::function<void(Store3Status)> Callback = NULL);
	bool LoadFolders();
	bool UnloadThings();
	bool IsWriteable() { return true; }
	bool IsPublicFolders() { return false; }

	void OnProperties(int Tab = -1) override;
	ScribeFolder *GetSubFolder(const char *Path);
	ScribeFolder *CreateSubFolder(const char *Name, int Type);
	void OnRename(char *NewName);
	void OnDelete();
	LString GetPath();
	void Populate(ThingList *List);
	bool CanHaveSubFolders(Store3ItemTypes Type = MAGIC_MAIL) { return GetItemType() != MAGIC_ANY; }
	void OnRethread();

	// Name
	void SetName(const char *Name, bool Encode);
	LString GetName(bool Decode);

	// Tree Item
	const char *GetText(int i=0) override;
	int GetImage(int Flags = 0) override;
	void OnExpand(bool b) override;
	bool OnKey(LKey &k) override;
	void Update() override;

	// Drag'n'drop
	bool GetFormats(LDragFormats &Formats) override;
	bool OnBeginDrag(LMouse &m) override;
	void OnEndData() override;
	bool GetData(LArray<LDragData> &Data) override;
	void OnReceiveFiles(LArray<const char*> &Files);

	// Import/Export
	bool GetFormats(bool Export, LString::Array &MimeTypes);
	IoProgress Import(IoProgressFnArgs);
	IoProgress Export(IoProgressFnArgs);
	void ExportAsync(LAutoPtr<LStreamI> f, const char *MimeType, std::function<void(LProgressDlg*)> Callback = NULL);
	const char *GetStorageMimeType();

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;
};

//////////////////////////////////////////////////////////////
class Filter;

class FilterCondition
{
protected:
	bool TestData(Filter *F, LVariant &v, LStream *Log, LStream *errLog);

public:
	// Data
	LString Source; // Data Source (used to be "int Field")
	LString Value; // Constant
	char Op = 0;
	uint8_t Not = false;

	// Methods
	FilterCondition();
	bool Set(class LXmlTag *t);

	// Test condition against email
	bool Test(Filter *F, Mail *m, LStream *Log, LStream *errLog);
	FilterCondition &operator =(const FilterCondition &c);

	// Object
	ThingUi *DoUI(MailContainer *c = NULL);
};

class FilterAction : public LListItem, public LDataPropI
{
	LAutoPtr<LCombo>  TypeCbo;
	LAutoPtr<LEdit>   ArgEdit;
	LAutoPtr<LButton> Btn;
	Filter *owner = NULL;
	LArray<LRect> iconPos;

	LImageList *GetIcons();

public:
	// Data
	FilterActionTypes Type = ACTION_MOVE_TO_FOLDER;
	LString Arg1;

	// Methods
	FilterAction(Filter *Owner, LDataStoreI *Store);
	~FilterAction();
	
	const char *GetClass() override { return "FilterAction"; }

	bool Set(LXmlTag *t);
	bool Get(LXmlTag *t);
	bool Do(Filter *F, ScribeWnd *App, Mail *&m, LStream *log, LStream *errLog);
	void Browse(ScribeWnd *App, LView *Parent);
	void DescribeHtml(Filter *Flt, LStream &s);

	LDataPropI &operator =(LDataPropI &p);

	// List item
	const char *GetText(int Col = 0) override;
	void OnMeasure(LPoint *Info) override;
	bool Select() override;
	void Select(bool b) override;
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c) override;
	int OnNotify(LViewI *c, const LNotification &n) override;
	void OnMouseClick(LMouse &m) override;
	void OnIconClick(int icon);

	// Object
	ThingUi *DoUI(MailContainer *c = NULL);
};

class ScribeClass Filter : public Thing
{
	friend class FilterUi;
	
protected:
	class FilterUi *Ui;
	class FilterPrivate *d;
	static int MaxIndex;

	// Ui
	LListItemCheckBox *ChkIncoming;
	LListItemCheckBox *ChkOutgoing;
	LListItemCheckBox *ChkInternal;
	bool IgnoreCheckEvents;

	// Current
	Mail **Current;

	// XML I/O
	LAutoPtr<LXmlTag> ConditionsCache;
	LAutoPtr<LXmlTag> Parse(bool Actions);

	// Methods
	bool EvaluateTree(LXmlTag *n, Mail *m, bool &Stop, LStream *Log, LStream *errLog);
	bool EvaluateXml(Mail *m, bool &Stop, LStream *Log, LStream *errLog);

public:
	Filter(ScribeWnd *app, LDataI *object = NULL);
	~Filter();
	
	const char *GetClass() override { return "Filter"; }

	LDATA_STR_PROP(Name, FIELD_FILTER_NAME);
	LDATA_STR_PROP(ConditionsXml, FIELD_FILTER_CONDITIONS_XML);
	LDATA_STR_PROP(ActionsXml, FIELD_FILTER_ACTIONS_XML);
	LDATA_STR_PROP(Script, FIELD_FILTER_SCRIPT);
	LDATA_INT32_PROP(Index, FIELD_FILTER_INDEX);
	LDATA_INT32_PROP(StopFiltering, FIELD_STOP_FILTERING);
	LDATA_INT32_PROP(Incoming, FIELD_FILTER_INCOMING);
	LDATA_INT32_PROP(Outgoing, FIELD_FILTER_OUTGOING);
	LDATA_INT32_PROP(Internal, FIELD_FILTER_INTERNAL);

	int Compare(LListItem *Arg, ssize_t Field) override;
	Thing &operator =(Thing &c) override;
	Filter *IsFilter() override { return this; }
	static void Reindex(ScribeFolder *Folder);
	int *GetDefaultFields() override;
	const char *GetFieldText(int Field) override;
	LImageList *GetIcons();

	// Methods
	void Empty();
	LAutoString DescribeHtml();

	// Import / Export
	char *GetDropFileName() override;
	bool GetDropFiles(LString::Array &Files) override;
	bool GetFormats(bool Export, LString::Array &MimeTypes) override;
	IoProgress Import(IoProgressFnArgs) override;
	IoProgress Export(IoProgressFnArgs) override;

	// Dom
	bool Evaluate(char *s, LVariant &v);
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// Filter
	bool Test(Mail *m, bool &Stop, LStream *Log = NULL, LStream *errLog = NULL);
	bool DoActions(Mail *&m, bool &Stop, LStream *Log = NULL, LStream *errLog = NULL);
	Mail *GetCurrent() { return Current?*Current:0; }
	
	/// This filters all the mail in 'Email'. Anything that is handled by a filter
	/// is removed from the list, leaving just the unfiltered mail.
	static int ApplyFilters
	(
	    /// [In] The window of the filtering caller
	    LView *Parent,
	    /// [In] List of all the filter to test and/or apply
	    List<Filter> &Filters,
	    /// [In/Out] The email to filter. After the call anything that has been
	    /// acted on by a filter will be removed from the list.
	    List<Mail> &Email
	);

	// Object
	Store3ItemTypes Type() override { return MAGIC_FILTER; }
	ThingUi *DoUI(MailContainer *c = NULL) override;
	bool Serialize(LFile &f, bool Write);
	bool Save(ScribeFolder *Into = NULL) override;
	void OnMouseClick(LMouse &m) override;
	void AddAction(FilterAction *a);
	
	// List item
	const char *GetText(int i) override;
	int GetImage(int Flags) override;
	void OnPaint(ItemPaintCtx &Ctx) override;
	void OnColumnNotify(int Col, int64 Data) override;

	// Index
	Filter *GetFilterAt(size_t Index);
};

//////////////////////////////////////////////////////////////////////
class Accountlet;

enum AccountThreadState
{
	ThreadIdle,
	ThreadSetup,
	ThreadConnecting,
	ThreadTransfer,
	ThreadWaiting,
	ThreadDeleting,
	ThreadDone,
	ThreadCancel,
	ThreadError
};

enum ReceiveAction
{
	MailNoop,
	MailDelete,
	MailDownloadAndDelete,
	MailDownload,
	MailUpload,
	MailHeaders,

};

enum ReceiveStatus
{
	MailReceivedNone,
	MailReceivedWaiting,	// This has been given to the main thread
	MailReceivedOk,			// and one of "Ok" or "Error" has to be
	MailReceivedError,		// set to continue.
	MailReceivedMax,
};

ScribeFunc const char *AccountThreadStateName(AccountThreadState i);
ScribeFunc const char *ReceiveActionName(ReceiveAction i);
ScribeFunc const char *ReceiveStatusName(ReceiveStatus i);

class LScribeMime : public LMime
{
public:
	LScribeMime() : LMime(ScribeTempPath())
	{
	}
};

class LMimeStream : public LTempStream, public LScribeMime
{
public:
	LMimeStream();
	bool Parse();
};

class MailTransferEvent
{
public:
	// Message
	LAutoPtr<LStreamI> Rfc822Msg;
	ReceiveAction Action = MailNoop;
	ReceiveStatus Status = MailReceivedNone;
	int Index = 0;
	bool Explicit = false;
	LString Uid;
	int64 Size = 0;
	int64 StartWait = 0;
	
	// Header Listing
	class AccountMessage *Msg = NULL;
	LList *GetList();

	// Sending
	ScribeEnvelope *Send = NULL;
	LString OutgoingHeaders;

	// Other
	class Accountlet *Account = NULL;

	MailTransferEvent()
	{
	}

	~MailTransferEvent()
	{
		#ifndef LGI_STATIC
		// LStackTrace("%p::~MailTransferEvent\n", this);
		#endif
	}
};

#define RoProp(Type, Name)					\
	protected:								\
		Type Name;							\
	public:									\
		Type Get##Name() { return Name; }

class AccountThread : public LThread, public LCancel
{
protected:
	Accountlet *Acc;
	void OnAfterMain();

public:
	// Object
	AccountThread(Accountlet *acc);
	~AccountThread();

	// Api
	Accountlet *GetAccountlet() { return Acc; }
	
	// 1st phase
	virtual void Disconnect();

	// 2nd phase
	virtual bool Kill();
};

#define AccStrOption(func, opt) \
	LVariant func(const char *Set = NULL) { LVariant v; StrOption(opt, v, Set); return v; }

#define AccIntOption(name, opt) \
	int name(int Set = -1) { LVariant v; IntOption(opt, v, Set); return v.CastInt32(); }

class ScribeClass Accountlet : public LStream
{
	friend class ScribeAccount;
	friend class AccountletThread;
	friend class AccountThread;

public:
	struct AccountletPriv
	{
		LArray<LogEntry*> Log;
	};

	class AccountletLock
	{
		LMutex *l;

	public:
		bool Locked;
		AccountletPriv *d;

		AccountletLock(AccountletPriv *data, LMutex *lck, const char *file, int line)
		{
			d = data;
			l = lck;
			Locked = lck->Lock(file, line);
		}

		~AccountletLock()
		{
			if (Locked)
				l->Unlock();
		}
	};

	typedef LAutoPtr<AccountletLock> I;

private:
	AccountletPriv d;
	LMutex PrivLock;

protected:
	// Data
	ScribeAccount *Account = NULL;
	LAutoPtr<AccountThread> Thread;
	bool ConnectionStatus = true;
	MailProtocol *Client = NULL;
	uint64 LastOnline = 0;
	LString TempPsw;
	bool Quiet = false;
	LView *Parent = NULL;

	// Pointers
	ScribeFolder *Root = NULL;
	LDataStoreI *DataStore = NULL;
	LMailStore *MailStore = NULL; // this memory is owned by ScribeWnd

	// Options
	const char *OptPassword = NULL;

	// Members
	LSocketI *CreateSocket(bool Sending, LCapabilityClient *Caps, bool RawLFCheck);
	bool WaitForTransfers(List<MailTransferEvent> &Files);

	void StrOption(const char *Opt, LVariant &v, const char *Set);
	void IntOption(const char *Opt, LVariant &v, int Set);

	// LStringPipe Line;
	ssize_t Write(const void *buf, ssize_t size, int flags);

public:
	MailProtocolProgress Group;
	MailProtocolProgress Item;

	Accountlet(ScribeAccount *a);
	~Accountlet();

	Accountlet &operator =(const Accountlet &a)
	{
		LAssert(0);
		return *this;
	}

	I Lock(const char *File, int Line)
	{
		I a(new AccountletLock(&d, &PrivLock, File, Line));
		if (!a->Locked)
			a.Reset();
		return a;
	}

	// Methods
	bool			Connect(LView *Parent, bool Quiet);
	bool			Lock();
	void			Unlock();
	virtual bool	IsConfigured() 	{ return false; }
	bool			GetStatus()		{ return ConnectionStatus; }
	uint64			GetLastOnline()	{ return LastOnline; }
	ScribeAccount*	GetAccount() 	{ return Account; }
	bool			IsCancelled();
	void			IsCancelled(bool b);
	ScribeWnd*		GetApp();
	const char*		GetStateName();
	ScribeFolder* GetRootFolder() { return Root; }
	RoProp(AccountThreadState, State);

	bool IsOnline()
	{
		if (DataStore)
			return DataStore->GetInt(FIELD_IS_ONLINE) != 0;
		
		return Thread != 0;
	}

	void OnEndSession()
	{
		if (DataStore)
			DataStore->SetInt(FIELD_IS_ONLINE, false);
	}

	// Commands
	void Disconnect();
	void Kill();

	// Data
	LString OptionName(const char *Opt, ssize_t Index = -1);
	void Delete();
	LThread *GetThread() { return Thread; }
	LMailStore *GetMailStore() { return MailStore; }
	LDataStoreI *GetDataStore() { return DataStore; }

	// General options
	virtual int UseSSL(int Set = -1) = 0;

	AccStrOption(Name, OPT_AccountName);
	AccIntOption(Disabled, OPT_AccountDisabled);
	AccIntOption(Id, OPT_AccountUID);
	AccIntOption(Expanded, OPT_AccountExpanded);
	bool GetPassword(LPassword *p);
	void SetPassword(LPassword *p);
	bool IsCheckDialup();
	
	// Events
	void OnThreadDone();
	void OnOnlineChange(bool Online);
	virtual void OnBeforeDelete();

	// LDom impl
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL);

	// Virtuals
	virtual void Main(AccountletThread *Thread) = 0;
	virtual LVariant Server(const char *Set = NULL) = 0;
	virtual LVariant UserName(const char *Set = NULL) = 0;
	virtual void Enabled(bool b) = 0;
	virtual void OnPulse(char *s, int s_len) {}
	virtual bool IsReceive() { return false; }
	virtual bool InitMenus() { return false; }
	virtual void CreateMaps() = 0;
	virtual ScribeAccountletStatusIcon GetStatusIcon() { return STATUS_ERROR; }
};

#undef RoProp

class AccountletThread;

class AccountIdentity : public Accountlet
{
public:
	AccountIdentity(ScribeAccount *a);

	AccStrOption(Name, OPT_AccIdentName);
	AccStrOption(Email, OPT_AccIdentEmail);
	AccStrOption(ReplyTo, OPT_AccIdentReply);
	AccStrOption(TextSig, OPT_AccIdentTextSig);
	AccStrOption(HtmlSig, OPT_AccIdentHtmlSig);

	// AccIntOption(Sort, OPT_AccountSort);

	int UseSSL(int Set = -1) { return 0; }
	void Main(AccountletThread *Thread) {}
	LVariant Server(const char *Set = NULL) { return LVariant(); }
	LVariant UserName(const char *Set = NULL) { return LVariant(); }
	void Enabled(bool b) {}
	void CreateMaps();
	bool IsValid();

	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
};

struct ScribeEnvelope
{
	LString MsgId;
	LString SourceFolder;
	LString From;
	LArray<LString> To;

	LString References;
	LString FwdMsgId;
	LString BounceMsgId;

	LString Rfc822;
};	

class SendAccountlet : public Accountlet
{
	friend class ScribeAccount;
	friend class ScribeWnd;

	LMenuItem *SendItem;

public:
	LArray<ScribeEnvelope*> Outbox;

	SendAccountlet(ScribeAccount *a);
	~SendAccountlet();

	// Methods
	void Main(AccountletThread *Thread);
	void Enabled(bool b);
	bool InitMenus();
	void CreateMaps();
	ScribeAccountletStatusIcon GetStatusIcon();

	// Sending options
	AccStrOption(Server, OPT_SmtpServer);
	AccIntOption(Port, OPT_SmtpPort);
	AccStrOption(Domain, OPT_SmtpDomain);
	AccStrOption(UserName, OPT_SmtpName);
	AccIntOption(RequireAuthentication, OPT_SmtpAuth);
	AccIntOption(AuthType, OPT_SmtpAuthType);
	AccStrOption(PrefCharset1, OPT_SendCharset1);
	AccStrOption(PrefCharset2, OPT_SendCharset2);
	AccStrOption(HotFolder, OPT_SendHotFolder);
	AccIntOption(OnlySendThroughThisAccount, OPT_OnlySendThroughThis);
	
	/// Get/Set the SSL mode
	/// \sa #SSL_NONE, #SSL_STARTTLS or #SSL_DIRECT
	AccIntOption(UseSSL, OPT_SmtpSSL);

	bool IsConfigured()
	{
		LVariant hot = HotFolder();
		if (hot.Str())
		{
			bool Exists = LDirExists(hot.Str());
			printf("%s:%i - '%s' exists = %i\n", _FL, hot.Str(), Exists);
			return Exists;
		}

		return ValidStr(Server().Str());
	}

	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
};

typedef LHashTbl<ConstStrKey<char>, LXmlTag*> MsgListHash;
class MsgList : protected MsgListHash
{
	LOptionsFile *Opts;
	LString Tag;
	bool Loaded;

	bool Load();
	LXmlTag *LockId(const char *id, const char *file, int line);
	void Unlock();

public:
	typedef MsgListHash Parent;
	bool Dirty;

	MsgList(LOptionsFile *Opts, char *Tag);
	~MsgList();

	// Access methods
	bool Add(const char *id);
	bool Delete(const char *id);
	int Length();
	bool Find(const char *id);
	void Empty();
	LString::Array CopyKeys();

	// Dates
	bool SetDate(char *id, LDateTime *dt);
	bool GetDate(char *id, LDateTime *dt);
};

class ReceiveAccountlet : public Accountlet
{
	friend class ScribeAccount;
	friend class ScribeWnd;
	friend class ImapThread;
	friend class ScpThread;

	LArray<ReceiveAction> Actions;
	LList *Items;
	int	SecondsTillOnline;
	LMenuItem *ReceiveItem;
	LMenuItem *PreviewItem;
	List<char> *IdTemp;
	LAutoPtr<ProtocolSettingStore> SettingStore;
	
	LAutoPtr<MsgList> Msgs;
	LAutoPtr<MsgList> Spam;

public:
	ReceiveAccountlet(ScribeAccount *a);
	~ReceiveAccountlet();

	// Props
	LList *GetItems() { return Items; }
	bool SetItems(LList *l);
	bool SetActions(LArray<ReceiveAction> *a = NULL);

	bool IsReceive() { return true; }
	bool IsPersistant();

	// Methods
	void Main(AccountletThread *Thread);
	void OnPulse(char *s, int s_len);
	bool OnIdle();
	void Enabled(bool b);
	bool InitMenus();
	int GetCheckTimeout();
	void CreateMaps();
	ScribeAccountletStatusIcon GetStatusIcon();

	// Message list
	bool HasMsg(const char *Id);
	void AddMsg(const char *Id);
	void RemoveMsg(const char *Id);
	void RemoveAllMsgs();
	int GetMsgs();

	// Spam list
	void DeleteAsSpam(const char *Id);
	bool RemoveFromSpamIds(const char *Id);
	bool IsSpamId(const char *Id, bool Delete = false);

	// Receive options
	AccStrOption(Protocol, OPT_Pop3Protocol);
	ScribeProtocol ProtocolType() { return ProtocolToEnum(Protocol().Str()); }
	AccStrOption(Server, OPT_Pop3Server);
	AccIntOption(Port, OPT_Pop3Port);
	AccStrOption(UserName, OPT_Pop3Name);
	AccStrOption(DestinationFolder, OPT_Pop3Folder);
	AccIntOption(AutoReceive, OPT_Pop3AutoReceive);
	AccStrOption(CheckTimeout, OPT_Pop3CheckEvery);
	
	AccIntOption(LeaveOnServer, OPT_Pop3LeaveOnServer);
	AccIntOption(DeleteAfter, OPT_DeleteAfter);
	AccIntOption(DeleteDays, OPT_DeleteDays);
	AccIntOption(DeleteLarger, OPT_DeleteIfLarger);
	AccIntOption(DeleteSize, OPT_DeleteIfLargerSize);
	AccIntOption(DownloadLimit, OPT_MaxEmailSize);
	
	AccStrOption(Assume8BitCharset, OPT_Receive8BitCs);
	AccStrOption(AssumeAsciiCharset, OPT_ReceiveAsciiCs);
	AccIntOption(AuthType, OPT_ReceiveAuthType);
	AccStrOption(HotFolder, OPT_ReceiveHotFolder);
	AccIntOption(SecureAuth, OPT_ReceiveSecAuth);
	
	/// Get/Set the SSL mode
	/// \sa #SSL_NONE, #SSL_STARTTLS or #SSL_DIRECT
	AccIntOption(UseSSL, OPT_Pop3SSL);

	bool IsConfigured()
	{
		LVariant hot = HotFolder();
		if (hot.Str() && LDirExists(hot.Str()))
		{
			return true;
		}
		
		LVariant v = Server();
		bool s = ValidStr(v.Str());
		v = Protocol();
		if (!v.Str() ||
			_stricmp(v.Str(), PROTOCOL_POP_OVER_HTTP) != 0)
		{
			v = UserName();
			s &= ValidStr(v.Str());
		}
		return s;
	}

	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL);
};

class ScribeAccount :
	public LDom,
	public LXmlTreeUi,
	public LCapabilityClient
{
	friend class ScribeWnd;
	friend class ScribePopViewer;
	friend class AccountStatusPanel;
	friend class Accountlet;
	friend class SendAccountlet;
	friend class ReceiveAccountlet;
	friend class AccountDlg;

protected:
	class ScribeAccountPrivate *d;
	ScribeWnd *Parent;
	
	ScribeFolder *&GetRoot();
	void SetIndex(size_t i);

public:
	// Data
	AccountIdentity Identity;
	SendAccountlet Send;
	ReceiveAccountlet Receive;
	LArray<LListItem*> Views;

	// Object
	ScribeAccount(ScribeWnd *parent, int index);
	~ScribeAccount();

	const char *GetClass() override { return "ScribeAccount"; }
	int Compare(ScribeAccount *b);

	// Lifespan
	bool IsValid();
	bool Create();
	bool Delete();

	// Properties
	ScribeWnd *GetApp() { return Parent; }
	ssize_t GetIndex();
	bool IsOnline();
	void SetCheck(bool c);
	LMenuItem *GetMenuItem();
	void SetMenuItem(LMenuItem *i);
	void OnEndSession()
	{
		Send.OnEndSession();
		Receive.OnEndSession();
	}
	ScribeFolder *GetFolder(LString path);

	// Commands
	void Stop();
	bool Disconnect();
	void Kill();
	void SetDefaults();

	// User interface
	void InitUI(LView *Parent, int Tab, std::function<void(bool)> callback);
	bool InitMenus();
	void SerializeUi(LView *Wnd, bool Load);
	int OnNotify(LViewI *Ctrl, const LNotification &n);

	// Worker
	void OnPulse(char *s = NULL, int s_len = 0);
	bool ReIndex(ssize_t i);
	void CreateMaps();

	// LDom interface
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;
};

//////////////////////////////////////////////////////////////////////
class ScribeClass ScribeDom : public LDom
{
	ScribeWnd *App;

public:
	Mail *Email;
	Contact *Con;
	ContactGroup *Grp;
	Calendar *Cal;
	Filter *Fil;

	ScribeDom(ScribeWnd *a);
	
	const char *GetClass() override { return "ScribeDom"; }
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
};

//////////////////////////////////////////////////////////////////////
#include "BayesianFilter.h"
#include "Components.h"

class LMailStore
{
	bool InRootDelete = false;
	ScribeFolder *Root = NULL;

public:
	bool Default = false, Expanded = true;
	LString Name;
	LString Path;
	LAutoPtr<LDataStoreI> Store;

	ScribeFolder *GetRoot() const;
	void SetRoot(ScribeFolder *f);
	void DeleteRoot();
	bool IsOk() const;
	int Priority();
	void Empty();
	LMailStore &operator =(LMailStore &a);
};

struct OptionsInfo
{
	LString File;
	char *Leaf = NULL;
	int Score = 0;
	uint64 Mod = 0;
	bool Usual = false;
	
	OptionsInfo();
	
	OptionsInfo &operator =(char *p);
	LAutoPtr<LOptionsFile> Load();
};

class ScribeClass ScribeWnd :
	public LWindow,
	public LDom,
	public LDataEventsI,
	public BayesianFilter,
	public CapabilityInstaller,
	public LCapabilityTarget
{
	friend class ScribeAccount;
	friend class Accountlet;
	friend class SendAccountlet;
	friend class ReceiveAccountlet;
	friend class AccountStatusPanel;
	friend class ScribeFolder;
	friend class OptionsDlg;
	friend class LoadWordStoreThread;
	friend struct ScribeReplicator;
	friend struct LoadMailStoreState;
	friend struct UnitTestState;

public:
	enum LayoutMode
	{
		OptionsLayout = 0,
		
		///--------------------
		///         |         |
		/// Folders |  List   |
		///         |         |
		///         |---------|
		///         | Preview |
		///--------------------
		FoldersListAndPreview = 1,

		///-----------------
		///         |      |
		/// Folders | List |
		///         |      |
		///-----------------
		///     Preview    |
		///-----------------
		PreviewOnBottom,

		///-----------------
		///         |      |
		/// Folders | List |
		///         |      |
		///-----------------
		FoldersAndList,

		///---------------------------
		///         |      |         |
		/// Folders | List | Preview |
		///         |      |         |
		///---------------------------
		ThreeColumn,
	};

	enum AppState
	{
	    ScribeConstructing, // In Construct1 + Construct2
	    ScribeConstructed,	// Finished Construct2 and ready for Construct3
	    ScribeInitializing,	// In Construct3
	    ScribeRunning,
	    ScribeExiting,
	    ScribeLoadingFolders,
	    ScribeUnloadingFolders,
	};
	
	AppState GetScribeState()
	{
	    return ScribeState;
	}

protected:
	class ScribeWndPrivate *d = NULL;
	
	// Ipc
	class ScribeIpc *Ipc = NULL;

	// Accounts
	List<ScribeAccount> Accounts;

	// New Mail stuff
	class LNewMailDlg *NewMailDlg = NULL;

	static AppState ScribeState;
	DoEvery			Ticker;
	int64			LastDrop = 0;

	// Static 
	LSubMenu		*File = NULL;
	LSubMenu		*ContactsMenu = NULL;
	LSubMenu		*Edit = NULL;
	LSubMenu		*Help = NULL;
	LToolBar		*Commands = NULL;

	// Dynamic
	LSubMenu		*IdentityMenu = NULL;
	LMenuItem		*DefaultIdentityItem = NULL;
	LSubMenu		*MailMenu = NULL;
	LSubMenu		*SendMenu = NULL, *ReceiveMenu = NULL, *PreviewMenu = NULL;
	LMenuItem		*SendItem = NULL, *ReceiveItem = NULL, *PreviewItem = NULL;
	LSubMenu		*NewTemplateMenu = NULL;
	LMenuItem		*WorkOffline = NULL;

	// Commands
	LCommand		CmdSend;
	LCommand		CmdReceive;
	LCommand		CmdPreview;

	// Storage
	LArray<LMailStore> Folders;
	LArray<LEventTargetI*> FolderTasks;
	
	List<ScribeFolder> PostValidateFree;

	// Main view
	LAutoPtr<LImageList> ImageList;
	LAutoPtr<LImageList> ToolbarImgs;
	class LBox		*Splitter = NULL;
	ThingList		*MailList = NULL;
	class DynamicHtml *TitlePage = NULL;
	class LSearchView *SearchView = NULL;
	MailTree		*Tree = NULL;
	class LPreviewPanel	*PreviewPanel = NULL;
	class AccountStatusPanel *StatusPanel = NULL;
	LAutoPtr<LView>	ListPane; // temporary holding spot for a content view.
							  // 'SetLayout' is responsible for attaching it to a view hierarchy.
							  // If it does so then this should be released, cause the tree owns it.

	// Security
	ScribePerm		CurrentAuthLevel = PermRequireNone;

	// Methods
	void			SetupUi();
	void			SetupAccounts();
	int				AdjustAllObjectSizes(LDataI *Item);
	bool			CleanFolders(ScribeFolder *f);
	void			LoadFolders(std::function<void(bool)> Callback);
	void			LoadMailStores(std::function<void(bool)> Callback);
	bool			ProcessFolder(LDataStoreI *Store, int StoreIdx, char *StoreName);
	bool			UnLoadFolders();
	void			AddFolderToMru(char *FileName);
	void			AddContactsToMenu(LSubMenu *Menu);
	bool			FindWordDb(char *Out, int OutSize, char *Name);
	void			OnFolderChanged(LDataFolderI *folder);
	bool			ValidateFolder(LMailStore *s, int Id);
    void            GrowlOnMail(Mail *m);
	void			GrowlInfo(LString title, LString text);
	bool			OnTransfer();
	ScribeDomType	StrToDom(const char *Var) { return ::StrToDom(Var); }
	const char*		DomToStr(ScribeDomType p) { return ::DomToStr(p); }
	void			LoadImageResources();
	void			DoOnTimer(LScriptCallback *c);

	enum CmdLineEvent
	{
		IpcEvent,
		StartupEvent
	};
	void			OnCommandLineEvent(CmdLineEvent event);

public:
	ScribeWnd();
	void Construct0(LOptionsFile::PortableType Type);
	void Construct1();
	void Construct2();
	void Construct3();
	void SetLanguage();

	~ScribeWnd();

	const char *GetClass() override { return "ScribeWnd"; }
	void DoDebug(char *s);
	void Validate(LMailStore *s);

	// Unit testing.
	static bool IsUnitTest;
	#ifdef _DEBUG
	void UnitTests(std::function<void(bool)> Callback);
	#endif

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// ---------------------------------------------------------------------
	// Methods
	LAutoString		GetDataFolder();
	LDataStoreI		*CreateDataStore(const char *Full, bool CreateIfMissing);
	Thing			*CreateThingOfType(Store3ItemTypes Type, LDataI *obj = NULL);
	Thing			*CreateItem(int Type, ScribeFolder *Folder = NULL, bool Ui = true);
	Mail			*CreateMail(Contact *c = NULL, const char *Email = NULL, const char *Name = NULL);
	Mail			*LookupMailRef(const char *MsgRef, bool TraceAllUids = false);
	bool			CreateFolders(LAutoString &FileName);
	bool			CompactFolders(LMailStore &Store, bool Interactive = true);
	void			Send(ssize_t Which = -1, bool Quiet = false);
	void			Receive(ssize_t Which);
	void			Preview(ssize_t Which);
	void			OnBeforeConnect(ScribeAccount *Account, bool Receive);
	void			OnAfterConnect(ScribeAccount *Account, bool Receive);
    bool			NeedsCapability(const char *Name, const char *Param = NULL) override;
	void			OnInstall(CapsHash *Caps, bool Status) override;
    void			OnCloseInstaller() override;
    bool			HasFolderTasks() { return FolderTasks.Length() > 0; }
    int				GetEventHandle();

	void			Update(int What = 0);
	void			UpdateUnRead(ScribeFolder *Folder, int Delta);
	void			ThingPrint(std::function<void(bool)> Callback, ThingType *m, LPrinter *Info = NULL, LView *Parent = NULL, int MaxPage = -1);
	bool			OpenAMail(ScribeFolder *Folder);
	void			BuildDynMenus();
	LDocView		*CreateTextControl(int Id, const char *MimeType, bool Editor, Mail *m = NULL);
	void			SetLastDrop() { LastDrop = LCurrentTime(); }
	void			SetListPane(LAutoPtr<LView> listPane);
	void			SetLayout(LayoutMode Mode = OptionsLayout);
	bool			IsMyEmail(const char *Email);
	bool			SetItemPreview(LView *v);
	LOptionsFile::PortableType GetPortableType();
	ScribeRemoteContent RemoteContent_GetSenderStatus(const char *Addr);
	void			RemoteContent_ClearCache();
	void			RemoteContent_AddSender(const char *Addr, bool WhiteList);

	void			SetDefaultHandler();
	void			OnSetDefaultHandler(bool Error, bool OldAssert);
	void			SetCurrentIdentity(int i=-1);
	int				GetCurrentIdentity();

	bool			MailReplyTo(Mail *m, bool All = false);
	bool			MailForward(Mail *m);
	bool			MailBounce(Mail *m);
	void			MailMerge(LArray<ListAddr*> &Recip, const char *FileName, Mail *Source);

	void			OnNewMail(List<Mail> *NewMailObjs, bool Add = true);
	void			OnNewMailSound();
	void			OnTrayClick(LMouse &m) override;
	void			OnTrayMenu(LSubMenu &m) override;
	void			OnTrayMenuResult(int MenuId) override;
	void			OnFolderSelect(ScribeFolder *f);

	// This will be called for the first time when the following events have happened:
	// - Startup of the application has completed
	// - The ScribeIpc::OnLoad callback has been called with true
	//
	// And also potentially when a new (temporary) instance of Scribe has sent this 
	// instance a command line.
	//
	// See also OnCommandLineEvent.
	void			OnCommandLine();

	void			AddThingSrc(ScribeFolder *src);
	void			RemoveThingSrc(ScribeFolder *src);
	LArray<ScribeFolder*> GetThingSources(Store3ItemTypes Type);

	bool			GetContacts(List<Contact> &Contacts, ScribeFolder *f = NULL, bool Deep = true);
	List<Contact>	*GetEveryone();
	void			HashContacts(LHashTbl<StrKey<char,false>,Contact*> &Contacts, ScribeFolder *Folder = NULL, bool Deep = true);
	
    // CapabilityInstaller impl
    LString		     GetHttpProxy() override;
    InstallProgress *StartAction(MissingCapsBar *Bar, LCapabilityTarget::CapsHash *Components, const char *Action) override;
    
	class HttpImageThread *GetImageLoader();
	int				GetMaxPages();
	ScribeWnd::LayoutMode GetEffectiveLayoutMode();
	ThingList		*GetMailList() { return MailList; }
	
	// Gets the matching mail store for a given identity
	LMailStore		*GetMailStoreForIdentity
	(
		/// If this is NULL, assume the current identity
		const char *IdEmail = NULL
	);
	
	ScribeFolder	*GetFolder(int Id, LMailStore *s = NULL, bool Quiet = false);
	ScribeFolder	*GetFolder(int Id, LDataI *s);
	ScribeFolder	*GetFolder(const char *Name, LMailStore *s = NULL);
	
	ScribeFolder	*GetCurrentFolder();
	int				GetFolderType(ScribeFolder *f);
	LImageList		*GetIconImgList() { return ImageList; }
	LAutoPtr<LImageList> &GetToolbarImgList() { return ToolbarImgs; }
	LString			GetResourceFile(SribeResourceType Type);
	DoEvery			*GetTicker() { return &Ticker; }
	ScribeAccount	*GetSendAccount();
	ScribeAccount	*GetCurrentAccount();
	ThingFilter		*GetThingFilter();
	List<ScribeAccount> *GetAccounts() { return &Accounts; }
	ScribeAccount	*GetAccountById(int Id);
	ScribeAccount	*GetAccountByEmail(const char *Email);
	LPrinter		*GetPrinter();
	int				GetActiveThreads();
	int				GetToolbarHeight();
	void			GetFilters(List<Filter> &Filters, bool JustIn, bool JustOut, bool JustInternal);
	bool			OnFolderTask(LEventTargetI *Ptr, bool Add);
	LArray<LMailStore> &GetStorageFolders() { return Folders; }
	LMailStore		*GetDefaultMailStore();
	LMailStore		*GetMailStoreForPath(const char *Path);
	bool			OnMailStore(LMailStore **MailStore, bool Add);
	ThingList		*GetItemList() { return MailList; }
	LColour			GetColour(int i);
	LFont			*GetPreviewFont();
	LFont			*GetBoldFont() { return LSysBold; }
	LToolBar		*LoadToolbar(LViewI *Parent, const char *File, LAutoPtr<LImageList> &Img);
	class LVmCallback *GetDebuggerCallback();
	class GpgConnector *GetGpgConnector();
	void			GetUserInput(LView *Parent, LString Msg, bool Password, std::function<void(LString)> Callback);

	int				GetCalendarSources(LArray<CalendarSource*> &Sources);

	Store3Status	GetAccessLevel(LViewI *Parent, ScribePerm Required, const char *ResourceName, std::function<void(bool)> Callback);
	void			GetAccountSettingsAccess(LViewI *Parent, ScribeAccessType AccessType, std::function<void(bool)> Callback);
	const char*		EditCtrlMimeType();
	LAutoString		GetReplyXml(const char *MimeType);
	LAutoString		GetForwardXml(const char *MimeType);
	bool			GetHelpFilesPath(char *Path, int PathSize);
	bool			LaunchHelp(const char *File);
	LAutoString		ProcessSig(Mail *m, char *Xml, const char *MimeType);
	LString			ProcessReplyForwardTemplate(Mail *m, Mail *r, char *Xml, int &Cursor, const char *MimeType);
	bool			LogFilterActivity();
	ScribeFolder *FindContainer(LDataFolderI *f);
	bool			SaveDirtyObjects(int TimeLimitMs = 100);
	class LSpellCheck *CreateSpellObject();
	class LSpellCheck *GetSpellThread(bool OverrideOpt = false);
	bool			SetSpellThreadParams(LSpellCheck *Thread);
	void			OnSpellerSettingChange();
	bool			OnMailTransferEvent(MailTransferEvent *e);
	LViewI			*GetView() { return this; }
	const char		*GetUiTags();

	struct MailStoreUpgradeParams
	{
		LAutoString OldFolders;
		LAutoString NewFolders;
		bool Quiet;
		LStream *Log;
		
		MailStoreUpgradeParams()
		{
		    Quiet = false;
		    Log = 0;
		}
	};

	// Scripting support
	bool GetScriptCallbacks(LScriptCallbackType Type, LArray<LScriptCallback*> &Callbacks);
	LScriptCallback GetCallback(const char *CallbackMethodName);
	int RegisterCallback(LScriptCallbackType Type, LScriptArguments &Args);
	bool RemoveCallback(int Uid);
	LStream *ShowScriptingConsole();
	bool ExecuteScriptCallback(LScriptCallback &c, LScriptArguments &Args, bool ReturnArgs = false);
	LScriptEngine *GetScriptEngine();

	// Options
	LOptionsFile	*GetOptions(bool Create = false) override;
	bool			ScanForOptionsFiles(LArray<OptionsInfo> &Inf, LSystemPath PathType);
	bool			LoadOptions();
	bool			SaveOptions();
	bool			IsSending() { return false; }
	bool			ShowToolbarText();
	bool			IsValid();

	// Data events from storage back ends
	bool GetSystemPath(int Folder, LVariant &Path) override;
	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new) override;
	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items) override;
	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &Items) override;
	void SetContext(const char *file, int line) override;
	bool OnChange(LArray<LDataI*> &items, int FieldHint) override;
	void Post(LDataStoreI *store, void *Param) override { PostEvent(M_STORAGE_EVENT, store->Id, (LMessage::Param)Param); }
	void OnPropChange(LDataStoreI *store, int Prop, LVariantType Type) override;
	bool Match(LDataStoreI *store, LDataPropI *Addr, int Type, LArray<LDom*> &Matches) override;
	ContactGroup *FindGroup(char *Name);

	bool AddStore3EventHandler(LDataEventsI *callback);
	bool RemoveStore3EventHandler(LDataEventsI *callback);

	// ---------------------------------------------------------------------
	// Events
	void			OnDelete();
	int				OnNotify(LViewI *Ctrl, const LNotification &n) override;
	void			OnPaint(LSurface *pDC) override;
    LMessage::Result OnEvent(LMessage *Msg) override;
	int				OnCommand(int Cmd, int Event, OsView Handle) override;
	void			OnPulse() override;
    void            OnPulseSecond();
	bool			OnRequestClose(bool OsShuttingDown) override;
	void			OnSelect(List<Thing> *l = NULL, bool ChangeEvent = false);
	void			OnReceiveFiles(LArray<const char*> &Files) override;
	void			OnUrl(const char *Url) override;
	void			OnZoom(LWindowZoom Action) override;
	void			OnCreate() override;
	void			OnMinute();
	void			OnHour();
	bool			OnIdle();
    void            OnBayesAnalyse(const char *Msg, const char *WhiteListEmail) override;
	void			SetupScriptTimers();
    
    /// \returns true if spam
    bool            OnBayesResult(const char *MailRef, double Rating) override;
    /// \returns true if spam
    bool            OnBayesResult(Mail *m, double Rating);

	void			OnScriptCompileError(const char *Source, Filter *f);
};

////////////////////////////////////////////////////////////////////////////////////
#ifdef SCRIBE_APP
#include "ScribePrivate.h"
#endif
