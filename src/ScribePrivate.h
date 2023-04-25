/*hdr
**      FILE:           ScribePrivate.h
**      AUTHOR:         Matthew Allen
**      DATE:           22/10/97
**      DESCRIPTION:    Scribe email application
**
**      Copyright (C) 1998-2002 Matthew Allen
**              fret@memecode.com
*/

// Includes

#ifndef __SCRIBE_PRIVATE__
#define __SCRIBE_PRIVATE__

#include "lgi/common/XmlTreeUi.h"
#include "lgi/common/Html.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/TabView.h"
#include "lgi/common/RadioGroup.h"

// Defines
#define SCRIBE_TOOLBAR_BORDER_SPACING_PX	3

// Externs
extern const char *AppName;
extern char HelpFile[];
extern const char *DefaultFolderNames[];
extern const char *DefaultRfXml;
extern int DefaultFilterFields[];
extern char MailToStr[];
extern char SubjectStr[];
extern char ContentTypeDefault[];
extern bool OptionSizeInKiB;
extern bool ShowRelativeDates;

// Mime types
extern char sMimeVCard[];
extern char sMimeVCalendar[];
extern char sMimeICalendar[];
extern char sMimeMbox[];
extern char sMimeLgiResource[];
extern char sMimeMessage[];
extern char sMimeXml[];

// Default templates
extern char DefaultTextReplyTemplate[];
extern char DefaultHtmlReplyTemplate[];

////////////////////////////////////////////////////////////////////////////////////////////
// Functions

// Misc
extern int SizeOfFile(char *FileName);
extern char *OsName();
extern LString GetFullAppName(bool Platform = true);
extern void LogMsg(char *str, ...);
extern LStringPipe ScribeInitTraceStore;

// Dnd and clipboard format for an array of "Thing*"
extern char ScribeThingList[];
#define ScribeThingMagic			"thng"
#define ScribeFolderMagic			"fldr"
class ScribeClipboardFmt
{
	char Magic[4];
	OsProcessId ProcessId;
	uint32_t Len;
	union {
		Thing *Things[1]; // 'Len' long...
		ScribeFolder *Folders[1];
	};
	static bool Is(const char *Type, void *Ptr, size_t Len);

public:
	static ScribeClipboardFmt *Alloc(bool ForFolders, size_t Size);
	static ScribeClipboardFmt *Alloc(List<Thing> &Lst);
	static ScribeClipboardFmt *Alloc(LArray<Thing*> &Arr);
	static bool IsThing(void *Ptr, size_t Bytes) { return Is(ScribeThingMagic, Ptr, Bytes); }
	static bool IsFolder(void *Ptr, size_t Bytes) { return Is(ScribeFolderMagic, Ptr, Bytes); }
	size_t Sizeof();
	uint32_t Length() { return Len; }
	
	Thing *ThingAt(size_t Idx, Thing *Set = NULL);
	ScribeFolder *FolderAt(size_t Idx, ScribeFolder *Set = NULL);
};

extern LToolBar *LoadToolbar(LView *Parent, const char *File);
extern LString ScribeGetFileMimeType(const char *File);
// extern void UpgradeRfOption(ObjProperties *Opts, const char *New, const char *Old, const char *Default);
extern ScribeFolder *CastFolder(LDataI *f);
extern int MakeOpenFlags(ScribeAccount *a, bool Send);

extern bool HasEmoji(char *Txt);
extern bool HasEmoji(uint32_t *Txt);
extern LAutoWString TextToEmoji(uint32_t *Txt, bool IsHtml);
extern int FilterCompare(Filter *a, Filter *b, NativeInt Data);

extern bool ExtractHtmlContent(	LString &OutHtml,
								LString &Charset,
								LString &Styles,
								const char *InHtml);
// Scripting
extern bool OnFilterScript(Filter *f, Mail *m, const char *script);
extern bool OnToolScript(ScribeWnd *App, const char *File);

////////////////////////////////////////////////////////////////////////////////////////////
// Classes
class MailTree;
class ScribeWnd;
class Mail;
class Contact;
class MailUi;
class ScribeFolder;
class ContactUi;
class FolderPropertiesDlg;
class ScribeAccount;
class Filter;
class Attachment;
class Calendar;
class CalendarSource;

class ScribeApp : public LApp
{
public:
	int Status;

	ScribeApp(OsAppArguments &AppArgs, LAppArguments *Opts);
};


////////////////////////////////////////////////////////////////////////
extern ItemFieldDef MailFieldDefs[];
extern ItemFieldDef ContactFieldDefs[];
extern ItemFieldDef CalendarFields[];
extern ItemFieldDef FilterFieldDefs[];
extern ItemFieldDef *GetFieldDefByName(char *Name);
extern ItemFieldDef *GetFieldDefById(int Id);

////////////////////////////////////////////////////////////////////////
// extern void StorageCount(StorageItem *Store, Counter &c);
class ContactUi : public ThingUi, public LResourceLoad
{
protected:
	Contact *Item;
	LList *Lst;
	class LContactImage *ImgView;

	bool InitField(int Id, const char *Name);
	bool SaveField(int Id, const char *Name);

public:
	ContactUi(Contact *item);
	~ContactUi();

	void OnLoad();
	void OnSave();
	void OnDestroy();
	int OnNotify(LViewI *Col, LNotification n);
	LMessage::Result OnEvent(LMessage *Msg);
	void OnPosChange();
	void OnPulse();
};

/////////////////////////////////////////////////////////////
#define DefaultMarkColour		(MarkColours32[5])
#define IDM_MARK_MAX			8

#define IDM_UNMARK				30000
#define IDM_MARK_ALL			30001
#define IDM_SELECT_NONE			30002
#define IDM_SELECT_ALL			30003
#define IDM_MARK_BASE			30100
#define IDM_MARK_SELECT_BASE	30200
#define IDM_IDENTITY_BASE		30300

enum MarkedState
{
	MS_None,
	MS_One,
	MS_Multiple
};

extern uint32_t MarkColours32[IDM_MARK_MAX];
extern LSubMenu *BuildMarkMenu(	LSubMenu *MarkMenu,
								MarkedState MarkState,
								uint32_t SelectedMark,
								bool None = false,
								bool All = false,
								bool Select = false);

class AddressList : public LList, public LDragDropTarget
{
	ScribeWnd *App;
	
public:
	AddressList(ScribeWnd *app, int id, int x, int y, int cx, int cy, const char *name = "List");

	const char *GetClass() { return "AddressList"; }

	void OnCreate();
	bool OnKey(LKey &k);
	void OnInit(LDataIt l);
	void OnSave(LDataStoreI *store, LDataIt l);
	void OnItemClick(LListItem *Item, LMouse &m);

	void Copy();
	void Paste();

	// D'n'd support
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);
};

struct BrowseItem
{
	int Score;
	LString First;
	LString Last;
	LString Email;
	
	BrowseItem(const char *first, const char *last, const char *email, int score)
	{
		Score = score;
		First = first;
		Last = last;
		Email = email;
	}
};

class MailUi :
	public ThingUi,
	public MailContainerIter,
	public MailViewOwner
{
	friend class AddressList;
	friend class Mail;
	friend class ScribeTextPipe;

	LViewI *WorkingDlg = NULL;

protected:
	int AddMode = MAIL_ADDR_TO;
	int Sx = 0, Sy = 0;
	int CmdAfterResize = 0;
	bool MetaFieldsDirty = false;
	bool HtmlCtrlDirty = false;
	bool TextCtrlDirty = false;
	bool IgnoreShowImgNotify = false;
	int CurrentEditCtrl = -1;
	MissingCapsBar *MissingCaps = NULL;
	LCapabilityTarget::CapsHash Caps;

	// Commands
	LScriptUi Commands;
		LToolButton *BtnPrev = NULL;
		LToolButton *BtnNext = NULL;
		LToolButton *BtnSend = NULL;
		LToolButton *BtnSave = NULL;
		LToolButton *BtnSaveClose = NULL;
		LToolButton *BtnAttach = NULL;
		LToolButton *BtnReply = NULL;
		LToolButton *BtnReplyAll = NULL;
		LToolButton *BtnForward = NULL;
		LToolButton *BtnBounce = NULL;

	// Gpg
	class MailUiGpg *GpgUi = NULL;

	// To:
	LPanel *ToPanel = NULL;
		LEdit *Entry = NULL;
		class AddressBrowse *Browse = NULL;
		LCombo *SetTo = NULL;
		AddressList *To = NULL;
		LCombo *Remove = NULL;
	
	// From
	LPanel *FromPanel = NULL;
		AddressList *FromList = NULL;
		LCombo *FromCbo = NULL;
		LArray<int> FromAccountId;
		// GDropDown *Drop;

	LPanel *ReplyToPanel = NULL;
		LCheckBox *ReplyToChk = NULL;
		LCombo *ReplyToCbo = NULL;
	
	// Subject
	LPanel *SubjectPanel = NULL;
		LEdit *Subject = NULL;

	// Calendar panel
	LPanel *CalendarPanel = NULL;
		LView *CalPanelStatus = NULL;

	// Tabs
	LTabView *Tab = NULL;
		LTabPage *TabText = NULL;
			bool TextLoaded = NULL;
			LDocView *TextView = NULL;
		LTabPage *TabHtml = NULL;
			bool HtmlLoaded = NULL;
			LDocView *HtmlView = NULL;
		LTabPage *TabAttachments = NULL;
			class AttachmentList *Attachments = NULL;
		LTabPage *TabHeader = NULL;
			LDocView *Header = NULL;

	// Methods
	bool SeekMsg(int Delta);

	bool NeedsCapability(const char *Name, const char *Param = NULL);
	LDocView *GetDoc(const char *MimeType);
	bool SetDoc(LDocView *v, const char *MimeType);
	void OnInstall(LCapabilityTarget::CapsHash *Caps, bool Status);
	void OnCloseInstaller();
	void SetCmdAfterResize(int Cmd);
	void OnChildrenChanged(LViewI *Wnd, bool Attaching);

public:
	MailUi(Mail *item, MailContainer *Container = 0);
	~MailUi();

	const char *GetClass() { return "MailUi"; }
	Mail *GetItem();
	void SetItem(Mail *m);

	bool SetDirty(bool d, bool ui = true);
	void OnLoad();
	AttachmentList *GetAttachments() { return Attachments; }
	void SerializeText(bool FromCtrl);
	bool AddRecipient(Contact *c);
	bool AddRecipient(const char *Email, const char *Name);
	bool AddRecipient(AddressDescriptor *Addr);
	bool AddCalendarEvent(bool AddPopupReminder);

	int OnNotify(LViewI *Col, LNotification n);
	void OnSysKey(int a, int b);
	void OnDirty(bool Dirty);
	void OnDataEntered();
	void OnSave();

	void OnPaint(LSurface *pDC);
	void OnPulse();
	LMessage::Result OnEvent(LMessage *Msg);
	void OnPosChange();
	int OnCommand(int Cmd, int Event, OsView Window);
	int HandleCmd(int Cmd);
	void OnReceiveFiles(LArray<const char*> &Files);
	bool OnViewKey(LView *v, LKey &k);
	void OnAttachmentsChange();
	void OnChange();
	bool IsWorking(int Set = -1);
	bool OnRequestClose(bool OsClose);
	bool CallMethod(const char *Name, LScriptArguments &Arg);
};

class AttachmentList : public LList
{
	MailUi *Ui;

public:
	AttachmentList(int id, int x, int y, int cx, int cy, MailUi *ui);
	~AttachmentList();
	void OnItemClick(LListItem *Item, LMouse &m);
	bool OnKey(LKey &k);
};

// this is the list pane that displays the
// contents of a ScribeFolder
class ThingList :
	public LList,
	public LDragDropTarget
{
	friend class Mail;

	ScribeFolder *Container;
	Mail *CurrentMail;
	int BoldUnread;
	ScribeWnd *App;

public:
	ThingList(ScribeWnd *wnd);
	~ThingList();

	const char *GetClass() { return "ThingList"; }
	LRect &GetClient(bool ClientSpace = true);

	int GetSortCol() { return (Container) ? Container->GetSortCol() : 0; }
	int GetSortField() { return (Container) ? Container->GetSortField() : 0; }
	bool GetSortAscending() { return (Container) ? Container->GetSortAscend() != 0 : 0; }
	void SetSort(int Col, int Ascend);
	void ReSort();
	ScribeFolder *GetContainer() { return Container; }
	void SetContainer(ScribeFolder *c) { Container = c; }

	LFont *GetFont();
	void OnPaint(LSurface *pDC);
	void OnColumnClick(int Col, LMouse &m);
	void OnItemClick(LListItem *Item, LMouse &m);
	void OnItemSelect(LArray<LListItem*> &Item);
	bool OnKey(LKey &k);
	void OnColumnDrag(int Col, LMouse &m);
	bool OnColumnReindex(LItemColumn *Col, int OldIndex, int NewIndex);
	void OnCreate();

	List<Thing> PlaceHolders;
	void DeletePlaceHolders();

	// Dnd
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);
};

// this is the tree view on the left hand side
// it contains all the ThingContainers
class MailTree : public LTree, public LDragDropTarget
{
protected:
	ScribeWnd *App;
	LTreeItem *LastHit;
	int8 LastWasRoot;

	ThingList *Things() { return App->GetMailList(); }
	void OnCreate();
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);

	void OnDragEnter() { LTree::OnDragEnter(); }
	void OnDragExit() { LTree::OnDragExit(); }

public:
	MailTree(ScribeWnd *app);
	~MailTree();

	const char *GetClass() { return "MailTree"; }	
	ssize_t Sizeof();
	bool Serialize(LFile &f, bool Write);

	void OnCreateSubDirectory(ScribeFolder *Item);
	void OnDelete(ScribeFolder *Item, bool Force);
	void OnProperties(ScribeFolder *Item);
	void OnItemClick(LTreeItem *Item, LMouse &m);
	void OnItemSelect(LTreeItem *Item);
	LMessage::Result OnEvent(LMessage *Msg);
};

//////////////////////////////////////////////////////////////
class FilterUi : public ThingUi
{
protected:
	struct FilterUiPriv *d;
	Filter *Item;
	LDocView *Script;
	LScriptUi Commands;
	LTabView *Tab;
	class LFilterView *Conditions;
	LList *Actions;

	void OnLoad();
	void OnSave();
	bool OnViewKey(LView *v, LKey &k);

public:
	FilterUi(Filter *item);
	~FilterUi();

	// Data access
	Filter *GetItem() { return Item; }

	// Events
	int OnNotify(LViewI *Col, LNotification n);
	LMessage::Result OnEvent(LMessage *Msg);
	int OnCommand(int Cmd, int Event, OsView Window);
};

///////////////////////////////////////////////////////////////////////////////
extern void LogStrToFile(char *File, char *Str, ...);

class ILog
{
protected:
	LVariant LogFileName;
	int LogFmt;
	LStreamI *Log;
	bool DeleteLog;
	Progress *Info;

	char *EnumFileName(char *File);
	void WriteLog(const char *s, ssize_t l);

public:
	ILog(LStreamI *log);
	~ILog();

	void LogRead(char *Data, ssize_t Len);
	void LogWrite(const char *Data, ssize_t Len);
	void LogError(int ErrorCode, const char *ErrorDescription);
	void LogInfomation(const char *Str);
	bool LogSetVariant(const char *Name, LVariant &v, const char *Array = NULL);
};

class ILogConnection : public ILog, public LSocket
{
public:
	ILogConnection(LStreamI *log) :
		ILog(log)
	{
	}

	~ILogConnection()
	{
	}

	void OnRead(char *Data, ssize_t Len) override
	{
		LogRead(Data, Len);
	}
	void OnWrite(const char *Data, ssize_t Len) override
	{
		LogWrite(Data, Len);
	}
	void OnError(int ErrorCode, const char *ErrorDescription) override
	{
		LogError(ErrorCode, ErrorDescription);
	}
	void OnInformation(const char *Str) override
	{
		LogInfomation(Str);
	}
	bool SetVariant(const char *Name, LVariant &v, const char *Array = NULL) override
	{
		return LogSetVariant(Name, v, Array);
	}
};

class ILogSocks5Connection : public ILog, public LSocks5Socket
{
public:
	ILogSocks5Connection(	char *proxy,
							int port,
							char *username,
							char *password,
							LStreamI *log) :
		ILog(log)
	{
		SetProxy(proxy, port, username, password);
	}

	void OnRead(char *Data, ssize_t Len) override { if (Socks5Connected) LogRead(Data, Len); }
	void OnWrite(const char *Data, ssize_t Len) override { if (Socks5Connected) LogWrite(Data, Len); }
	void OnError(int ErrorCode, const char *ErrorDescription) override { LogError(ErrorCode, ErrorDescription); }
	void OnInformation(const char *Str) override { LogInfomation(Str); }
	bool SetVariant(const char *Name, LVariant &v, const char *Array = NULL) override
	{
		return LogSetVariant(Name, v, Array);
	}
};

///////////////////////////////////////////////////////////////////////////////
#include "ScribeFolderSelect.h"
#include "NewMailDlg.h"
#include "SearchView.h"

//////////////////////////////////////////////////////////////////////
class ScribePanel : public LPanel
{
protected:
	ScribeWnd *App;

public:
	ScribePanel(ScribeWnd *app, const char *name, int size, bool open = true);
	bool Pour(LRegion &r);
};

//////////////////////////////////////////////////////////////////////
// UI
class CreateSubFolderDlg : public LDialog
{
	LEdit		*FolderName = NULL;
	LRadioGroup	*FolderType = NULL;

public:
	int SubType = -1;
	LString SubName;

	CreateSubFolderDlg(LView *parent, int defaulttype = 0, bool *enable = 0, char *default_name = 0);

	int OnNotify(LViewI *Ctrl, LNotification n);
};

class LanguageDlg : public LDialog
{
	class LanguageDlgPrivate *d = NULL;

public:
	bool Ok;
	LAutoString Lang;

	LanguageDlg(ScribeWnd *app);
	~LanguageDlg();
	
	int OnNotify(LViewI *c, LNotification n);
};

class FolderNameDlg : public LDialog
{
	LEdit *Ed;

public:
	char *Name;

	FolderNameDlg(LView *parent, const char *Old = "");
	~FolderNameDlg();

	void OnCreate();
	int OnNotify(LViewI *Ctrl, LNotification n);
};

////////////////////////////////////////////////////////////////////////////////////////////////
class ScribeAccountItem;

class OptionsDlg : public TabDialog, public LXmlTreeUi
{
	friend class ScribeAccountItem;
	friend class AccountItem;

	class OptionsDlgPrivate *d;

	ScribeWnd *App;
	LFontType EditorFont;
	LFontType HtmlFont;
	List<LLanguage> Langs;
	LCombo *UiLang;
	int SinkHnd;
	ScribeAccountItem *LastRecord;

	void UpdateDefaultSendAccounts();
	void UpdateFontDescription();
	bool PasswordCtrlValue(int CtrlId, char *Option, bool ToWindow);
	void WriteNativeText(LFile &f, char *t);

public:
	OptionsDlg(ScribeWnd *window);
	~OptionsDlg();

	void OnCreate();
	void OnAccountEnable(ScribeAccount *Acc, bool Enable);
	int OnNotify(LViewI *Ctrl, LNotification n);
	LMessage::Result OnEvent(LMessage *m);
};

class ScribeThread : public LThread
{
	ScribeWnd *App;
	int Index;
	
public:
	ScribeThread(ScribeWnd *app, int index);
	int Main();
};

// Security Dialog
class SecurityDlg : public LDialog, public LXmlTreeUi
{
	class SecurityDlgPrivate *d;

public:
	SecurityDlg(ScribeWnd *app);
	~SecurityDlg();

	int OnNotify(LViewI *c, LNotification n);
};

// Bayesian filtering setup
class BayesDlg : public LDialog, public LXmlTreeUi
{
	class BayesDlgPrivate *d;
	
public:
	BayesDlg(ScribeWnd *app);
	~BayesDlg();

	int OnNotify(LViewI *c, LNotification n);
};

////////////////////////////////////////////////////////////////////////////////////
class DynamicHtml : public Html1::LHtml, public LDefaultDocumentEnv
{
	class DynamicHtmlPrivate *d;

public:
	DynamicHtml(ScribeWnd *app, const char *file);
	~DynamicHtml();

	LString OnDynamicContent(LDocView *Parent, const char *Code) override;
	bool OnNavigate(LDocView *Parent, const char *Uri) override;
};

////////////////////////////////////////////////////////////////////////////////////
class ScribeBehaviour
{
public:
	static ScribeBehaviour *New(ScribeWnd *app);

	virtual ~ScribeBehaviour() {}

	// Events/Actions
	virtual bool DoWizard()
	{
		return false;
	}
	
	virtual bool SerializeOptions
	(
		LOptionsFile *Opts,
		char *OptsFile,
		bool Write
	)
	{
		return false;
	}
};

////////////////////////////////////////////////////////////////////////////////////
struct SystemFolderInfo
{
	int Id;
	const char *PathOption;
	const char *HasOption;
};

extern SystemFolderInfo SystemFolders[];

////////////////////////////////////////////////////////////////////////////////////
// Shared memory record format, used to pass argument data to the right running 
// instance of Scribe. Mul also uses this to broker new instances of Scribe.
#include "ScribeSharedMem.h"

////////////////////////////////////////////////////////////////////////////////////
//
ScribeFunc void TraceTime(char *s);
extern void LoadCalendarStringTable();

// Misc
extern void WrapAndQuote(LStream &Out, const char *Quote, int Wrap, const char *Text, const char *Cp = NULL, const char *MimeType = NULL);
extern void RemoveReturns(char *s);
extern bool DecodeUuencodedAttachment(LDataStoreI *Store, LArray<LDataI*> &Files, LStreamI *Out, const char *In);
// extern LDocView *CreateIeControl(int Id);

// Casters...
extern Mail *IsMail(LListItem *Item);
extern Contact *IsContact(LListItem *Item);
extern Thing *CastThing(LDataI *t);

// Folder
extern void Scribe_Repair(ScribeWnd *Parent);

// Scribe windows
extern bool OpenPopView(ScribeWnd *Parent, LArray<ScribeAccount*> &Lst);
extern void OpenFolderProperties(ScribeFolder *Parent, int Tab, std::function<void(bool)> callback);
extern LView *OpenFinder(ScribeWnd *App, ScribeFolder *Folder);

// Import
#include "Imp_Outlook.h"
#include "Imp_Beos.h"
extern void Import_NetscapeContacts(ScribeWnd *Parent);
extern void Import_UnixMBox(ScribeWnd *Parent);
extern void Import_OutlookExpress(ScribeWnd *Parent, bool v5 = true);
extern void Import_EudoraAddressBook(ScribeWnd *App);
extern void Import_MozillaAddressBook(ScribeWnd *App);
extern void Import_MozillaMail(ScribeWnd *App);

extern void Import_OutlookContacts(ScribeWnd *Parent);
extern MailSource *NewOutlookMailSource(ScribeWnd *Parent, ScribeAccount *Account);

// Export
extern void Export_UnixMBox(ScribeWnd *Parent);
extern void ImportCsv(ScribeWnd *App);
extern void ExportCsv(ScribeWnd *App);
extern void ImportEml(ScribeWnd *App);
extern void ExportScribe(ScribeWnd *App, LMailStore *Store);

// DOM stuff
extern void InitStrToDom();

#endif
