/*
**	FILE:			ScribeApp.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			22/10/1998
**	DESCRIPTION:	Scribe email application
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

// Debug defines
// #define PRINT_OUT_STORAGE_TREE
// #define TEST_OBJECT_SIZE

#define USE_SPELLCHECKER			1
#define USE_INTERNAL_BROWSER		1		// for help
#define RUN_STARTUP_SCRIPTS			1
#define PROFILE_ON_PULSE			0
#define TRAY_CONTACT_BASE			1000
#define TRAY_MAIL_BASE				10000

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <memory>

#include "Scribe.h"

#include "lgi/common/StoreConvert1To2.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/OpenSSLSocket.h"
#include "lgi/common/SoftwareUpdate.h"
#include "lgi/common/Html.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/Store3.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Box.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/SpellCheck.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/CssTools.h"
#include "lgi/common/Map.h"
#include "lgi/common/Charset.h"
#include "lgi/common/RefCount.h"
#include "lgi/common/PopupNotification.h"

#include "ScribePrivate.h"
#include "PreviewPanel.h"
#include "ScribeStatusPanel.h"
#include "ScribeFolderDlg.h"
#include "ScribePageSetup.h"
#include "Calendar.h"
#include "CalendarView.h"
#include "ScribeSpellCheck.h"
#include "Store3Common.h"
#include "PrintContext.h"
#include "resource.h"
#include "ManageMailStores.h"
#include "ReplicateDlg.h"
#include "ScribeAccountPreview.h"
#include "Encryption/GnuPG.h"
#include "Store3Webdav/WebdavStore.h"
#include "resdefs.h"
#include "ScribeIpc.h"
#include "DynamicHtml.h"

#define DEBUG_STORE_EVENTS			0
#if DEBUG_STORE_EVENTS
#define LOG_STORE(...)				LgiTrace(__VA_ARGS__)
#else
#define LOG_STORE(...)
#endif

#define IDM_LOAD_MSG				2000
#define RAISED_LOOK					0
#define SUNKEN_LOOK					false
#ifdef MAC
#define SUNKEN_CTRL					false
#else
#define SUNKEN_CTRL					true
#endif

#if LGI_CARBON

#define TRAY_ICON_NONE				-1
#define TRAY_ICON_NORMAL			-1
#define TRAY_ICON_MAIL				0
#define TRAY_ICON_ERROR				1

#elif defined(WIN32)

#define TRAY_ICON_NORMAL			0
#define TRAY_ICON_ERROR				1
#define TRAY_ICON_MAIL				2
#define TRAY_ICON_NONE				3

#else

#define TRAY_ICON_NONE				-1
#define TRAY_ICON_NORMAL			0
#define TRAY_ICON_ERROR				1
#define TRAY_ICON_MAIL				2

#endif

#if LINUX
const char ScribeThingList[] = "application/x-scribe-thing-list";
#else
const char ScribeThingList[] = "com.memecode.ThingList";
#endif

ScribeClipboardFmt *ScribeClipboardFmt::Alloc(bool ForFolders, size_t Size)
{
	ScribeClipboardFmt *obj = (ScribeClipboardFmt*) calloc(sizeof(ScribeClipboardFmt)+((Size-1)*sizeof(Thing*)), 1);
	if (obj)
	{
		memcpy(obj->Magic, ForFolders ? ScribeFolderMagic : ScribeThingMagic, sizeof(obj->Magic));
		obj->ProcessId = LAppInst->GetProcessId();
		obj->Len = (uint32_t)Size;
	}
	return obj;
}

ScribeClipboardFmt *ScribeClipboardFmt::Alloc(List<Thing> &Lst)
{
	ScribeClipboardFmt *Fmt = Alloc(false, Lst.Length());
	for (unsigned i=0; i<Lst.Length() && Fmt; i++)
		Fmt->ThingAt(i, Lst[i]);
	return Fmt;
}

ScribeClipboardFmt *ScribeClipboardFmt::Alloc(LArray<Thing*> &Arr)
{
	ScribeClipboardFmt *Fmt = Alloc(false, Arr.Length());
	for (unsigned i=0; i<Arr.Length() && Fmt; i++)
		Fmt->ThingAt(i, Arr[i]);
	return Fmt;
}

bool ScribeClipboardFmt::Is(const char *Type, void *Ptr, size_t Bytes)
{
	// Do we have the minimum bytes for the structure?
	if (Bytes >= sizeof(ScribeClipboardFmt) &&
		Ptr != NULL)
	{
		ScribeClipboardFmt *This = (ScribeClipboardFmt*)Ptr;

		// Check the magic is the right value
		if (memcmp(This->Magic, Type, 4) != 0)
			return false;

		// Check it's from this process
		if (This->ProcessId != LAppInst->GetProcessId())
			return false;
		
		return true;
	}

	return false;
}

Thing *ScribeClipboardFmt::ThingAt(size_t Idx, Thing *Set)
{
	if (memcmp(Magic, ScribeThingMagic, 4))
		return NULL;
	if (Idx >= Len)
		return NULL;
	if (Set)
		Things[Idx] = Set;
	return Things[Idx];
}

ScribeFolder *ScribeClipboardFmt::FolderAt(size_t Idx, ScribeFolder *Set)
{
	if (memcmp(Magic, ScribeFolderMagic, 4))
		return NULL;
	if (Idx >= Len)
		return NULL;
	if (Set)
		Folders[Idx] = Set;
	return Folders[Idx];
}

size_t ScribeClipboardFmt::Sizeof()
{
	return sizeof(*this) + ((Len - 1) * sizeof(Thing*));
}

bool OptionSizeInKiB = false;
bool ShowRelativeDates = false;
const char *MailAddressDelimiters = "\t\r\n;,";

char16 SpellDelim[] =
{
	' ', '\t', '\r', '\n', ',', ',', '.', ':', ';',
	'{', '}', '[', ']', '!', '@', '#', '$', '%', '^', '&', '*',
	'(', ')', '_', '-', '+', '=', '|', '\\', '/', '?', '\"',
	0
};

const char *DefaultRfXml = 
	"---------- %s ----------\n"
	"%s: <mail.to[0].name> (<mail.to[0].email>)\n"
	"%s: <mail.from.name> (<mail.from.email>)\n"
	"%s: <mail.subject>\n"
	"%s: <mail.datesent>\n"
	"\n"
	"<mail.bodyastext quote=scribe.quote>\n"
	"<cursor>\n"
	"<mail.sig>\n";

uchar DateTimeFormats[] = 
{
	GDTF_DEFAULT,
	
	GDTF_DAY_MONTH_YEAR | GDTF_12HOUR,
	GDTF_MONTH_DAY_YEAR | GDTF_12HOUR,
	GDTF_YEAR_MONTH_DAY | GDTF_12HOUR,

	GDTF_DAY_MONTH_YEAR | GDTF_24HOUR,
	GDTF_MONTH_DAY_YEAR | GDTF_24HOUR,
	GDTF_YEAR_MONTH_DAY | GDTF_24HOUR
};

SystemFolderInfo SystemFolders[] =
{
	{FOLDER_INBOX,		OPT_Inbox,		NULL},
	{FOLDER_OUTBOX,		OPT_Outbox,		NULL},
	{FOLDER_SENT,		OPT_Sent,		NULL},
	{FOLDER_CONTACTS,	OPT_Contacts,	NULL},
	{FOLDER_TRASH,		OPT_Trash,		NULL},
	{FOLDER_CALENDAR,	OPT_Calendar,	OPT_HasCalendar},
	{FOLDER_TEMPLATES,	OPT_Templates,	OPT_HasTemplates},
	{FOLDER_FILTERS,	OPT_Filters,	OPT_HasFilters},
	{FOLDER_GROUPS,		OPT_Groups,		OPT_HasGroups},
	{FOLDER_SPAM,		OPT_SpamFolder,	OPT_HasSpam},
	{-1, 0, 0}
};

ScribeBehaviour *ScribeBehaviour::New(ScribeWnd *app)
{
	return NULL;
}

void ScribeOptionsDefaults(LOptionsFile *f)
{
	if (!f)
		return;

	f->CreateTag("Accounts");
	f->CreateTag("CalendarUI");
	f->CreateTag("CalendarUI.Sources");
	f->CreateTag("MailUI");
	f->CreateTag("ScribeUI");
	f->CreateTag("Plugins");
	f->CreateTag("Print");

	#define DefaultIntOption(opt, def) { LVariant v; if (!f->GetValue(opt, v)) \
											f->SetValue(opt, v = (int)def); }
	#define DefaultStrOption(opt, def) { LVariant v; if (!f->GetValue(opt, v)) \
											f->SetValue(opt, v = def); }
	DefaultIntOption(OPT_DefaultAlternative, 1);
	DefaultIntOption(OPT_BoldUnread, 1);
	DefaultIntOption(OPT_PreviewLines, 1);
	DefaultIntOption(OPT_AutoDeleteExe, 1);
	DefaultIntOption(OPT_DefaultReplyAllSetting, MAIL_ADDR_BCC);
	DefaultIntOption(OPT_BlinkNewMail, 1);
	DefaultIntOption(OPT_MarkReadAfterSeconds, 5);
	DefaultStrOption(OPT_BayesThreshold, "0.9");
	DefaultIntOption(OPT_SoftwareUpdate, 1);
	DefaultIntOption(OPT_ResizeImgAttachments, false);
	DefaultIntOption(OPT_ResizeJpegQual, 80);
	DefaultIntOption(OPT_ResizeMaxPx, 1024);
	DefaultIntOption(OPT_ResizeMaxKb, 200);
	DefaultIntOption(OPT_RegisterWindowsClient, 1);
	DefaultIntOption(OPT_HasTemplates, 0);
	DefaultIntOption(OPT_HasCalendar, 1);
	DefaultIntOption(OPT_HasGroups, 1);
	DefaultIntOption(OPT_HasFilters, 1);
	DefaultIntOption(OPT_HasSpam, 0);

	DefaultStrOption(OPT_Inbox,      LLoadString(IDS_FOLDER_INBOX, "Inbox"));
	DefaultStrOption(OPT_Outbox,     LLoadString(IDS_FOLDER_OUTBOX, "Outbox"));
	DefaultStrOption(OPT_Sent,       LLoadString(IDS_FOLDER_SENT, "Sent"));
	DefaultStrOption(OPT_Trash,      LLoadString(IDS_FOLDER_TRASH, "Trash"));
	DefaultStrOption(OPT_Contacts,   LLoadString(IDS_FOLDER_CONTACTS, "Contacts"));
	DefaultStrOption(OPT_Templates,  LLoadString(IDS_FOLDER_TEMPLATES, "Templates"));
	DefaultStrOption(OPT_Filters,    LLoadString(IDS_FOLDER_FILTERS, "Filters"));
	DefaultStrOption(OPT_Calendar,   LLoadString(IDS_FOLDER_CALENDAR, "Calendar"));
	DefaultStrOption(OPT_Groups,     LLoadString(IDS_FOLDER_GROUPS, "Groups"));
	DefaultStrOption(OPT_SpamFolder, LLoadString(IDS_SPAM, "Spam"));
}


void SetRecipients(ScribeWnd *App, char *Start, LDataIt l, EmailAddressType CC)
{
	while (Start && *Start)
	{
		LString Str;
		auto End = strchr(Start, ',');
		if (End)
		{
			Str.Set(Start, End-Start);
			Start = End + 1;
		}
		else
		{
			Str = Start;
			Start = 0;
		}

		if (Str)
		{
			auto a = new ListAddr(App);
			if (a)
			{
				a->CC = CC;
				
				if (_strnicmp(Str, "mailto:", 7) == 0)
					a->sAddr = Str(7,-1);
				else
					a->sAddr = Str;
				
				l->Insert(a);
			}
		}
	}
}

static const char SoftwareUpdateUri[] = "http://www.memecode.com/update.php";

enum SoftwareStatus
{
	SwError,
	SwCancel,
	SwOutOfDate,
	SwUpToDate
};

static LString ExtractVer(const char *s)
{
	char Buf[256], *Out = Buf;
	for (auto In = s; *In && Out < Buf + sizeof(Buf) - 1; In++)
	{
		if (*In == ' ')
			break;
		if (IsDigit(*In) || *In == '.')
			*Out++ = *In;
	}
	*Out++ = 0;
	return LString(Buf);
}

void IsSoftwareUpToDate(ScribeWnd *Parent,
                        bool WithUI,
                        bool IncBetas,
                        std::function<void(SoftwareStatus, LSoftwareUpdate::UpdateInfo*)> callback)
{
	// LSoftwareUpdate::UpdateInfo Info
	// Software update?
	auto Proxy  = Parent->GetHttpProxy();
	auto Update = new LSoftwareUpdate(AppName, SoftwareUpdateUri, Proxy);

	Update->CheckForUpdate(
		[WithUI, Parent, callback, Update](auto Info, auto errorMsg)
		{
			if (Info)
			{
				auto LocalVer = LString(ScribeVer).SplitDelimit(".");
				LString BuildVer = ExtractVer(Info->Build);
				auto OnlineVer = BuildVer.SplitDelimit(".");
				if (OnlineVer.Length() != LocalVer.Length())
				{
					LgiTrace("%s:%i - Invalid online version number \"%s\"\n", _FL, Info->Version.Get());
					if(callback)
						callback(SwError, Info);
					return;
				}

				unsigned i;
				for(i = 0; i < OnlineVer.Length(); i++)
				{
					auto l = Atoi(LocalVer[i].Get());
					auto o = Atoi(OnlineVer[i].Get());
					if(l < o)
					{
						if(callback)
							callback(SwOutOfDate, Info);
						return;
					}
					if(l > o)
					{
						if(callback)
							callback(SwUpToDate, Info);
						return;
					}
				}

				LDateTime Compile;
				auto Date = LString(__DATE__).SplitDelimit(" ");
				Compile.Month(LDateTime::MonthFromName(Date[0]));
				Compile.Day(atoi(Date[1]));
				Compile.Year(atoi(Date[2]));
				Compile.SetTime(__TIME__);

				bool DateGreaterThenCompile = Info->Date > Compile;
				if (callback)
					callback(DateGreaterThenCompile ? SwOutOfDate : SwUpToDate, Info);
				return;
			}
			else if (WithUI)
			{
				if (callback)
					callback(SwCancel, NULL);
				LgiMsg(Parent, LLoadString(IDS_ERROR_SOFTWARE_UPDATE), AppName, MB_OK, errorMsg);
			}

			if (callback)
				callback(SwError, NULL);
		},
		WithUI ? Parent : NULL,
		IncBetas);
}

void UpgradeSoftware(const LSoftwareUpdate::UpdateInfo *Info,
                     ScribeWnd *Parent,
                     bool WithUI,
                     std::function<void(bool)> Callback)
{
	bool DownloadUpdate = true;

	if (WithUI)
	{
		DownloadUpdate = LgiMsg(Parent,
								LLoadString(IDS_SOFTWARE_UPDATE_DOWNLOAD),
								AppName,
								MB_YESNO,
								Info->Build.Get(),
								Info->Uri.Get(),
                                Info->Date.Get().Get())
							==
								IDYES;
	}

	if (DownloadUpdate)
	{
		auto Proxy = Parent->GetHttpProxy();
		auto Update = new LSoftwareUpdate(AppName, SoftwareUpdateUri, Proxy, ScribeTempPath());
		Update->ApplyUpdate(Info, false, Parent, [Update, Callback](auto Status)
		{
			if (Callback)
				Callback(Status);
			delete Update;
		});
	}
}

void SoftwareUpdate(ScribeWnd *Parent, bool WithUI, bool IncBetas, std::function<void(bool goingToUpdate)> callback)
{
	// Software update?
	IsSoftwareUpToDate(Parent, WithUI, IncBetas, [WithUI, Parent, callback](auto s, auto Info)
	{
		if (s == SwUpToDate)
		{
			if (WithUI)
				LgiMsg(Parent, LLoadString(IDS_SOFTWARE_CURRENT), AppName, MB_OK);
			if (callback)
				callback(false); // we're up to date
		}
		else if (s == SwOutOfDate)
		{
			UpgradeSoftware(Info, Parent, WithUI, callback);
		}
	});
}

const char *AppName = "Scribe";
char HelpFile[] = "index.html";

const char OptionsFileName[] = "ScribeOptions";

const char AuthorEmailAddr[] = "fret@memecode.com";
const char AuthorHomepage[] = "http://www.memecode.com";
const char ApplicationHomepage[] = "http://www.memecode.com/scribe.php";
const char CommercialHomepage[] = "http://www.memecode.com/inscribe.php";
const char FaqHomepage[] = "http://www.memecode.com/scribe/faq.php";

const char *DefaultFolderNames[16];
Store3ItemTypes DefaultFolderTypes[] = {
	MAGIC_MAIL,		// Inbox
	MAGIC_MAIL,		// Outbox
	MAGIC_MAIL,		// Sent
	MAGIC_ANY,		// Trash
	MAGIC_CONTACT,	// Contacts
	MAGIC_MAIL,		// Templates
	MAGIC_FILTER,	// Filters
	MAGIC_CALENDAR,	// Calendar Events
	MAGIC_GROUP,	// Groups
	MAGIC_MAIL,		// Spam
	MAGIC_NONE,
	MAGIC_NONE,
	MAGIC_NONE,
	MAGIC_NONE
};

extern void Log(char *File, char *Str, ...);

//////////////////////////////////////////////////////////////////////////////
void LogMsg(char *str, ...)
{
	#ifdef _DEBUG
    LFile::Path f(LSP_EXE);
    f += "log.txt";
	if (str)
	{
		char buffer[256];
		va_list arg;
		va_start(arg ,str);
		vsprintf_s(buffer, sizeof(buffer), str, arg);
		va_end(arg);

		LFile File;
		while (!File.Open(f.GetFull(), O_WRITE))
		{
			LSleep(5);
		}

		File.Seek(File.GetSize(), SEEK_SET);
		File.Write(buffer, strlen(buffer));
	}
	else
	{
		FileDev->Delete(f.GetFull(), NULL, false);
	}
	#endif
}

LString GetFullAppName(bool Platform)
{
	LString Ret = AppName;
	if (Platform)
	{
		LString s;
		const char *Build = 
			#ifndef _DEBUG
			"Release";
			#else
			"Debug";
			#endif
		
		LArray<int> Ver;
		int Os = LGetOs(&Ver);
		const char *OsName = LGetOsName();
		if (Os == LGI_OS_WIN9X)
		{
			switch (Ver[1])
			{
				case 0:
					OsName = "Win95";
					break;
				case 10:
					OsName = "Win98";
					break;
				case 90:
					OsName = "WinME";
					break;
			}
		}
		else if (Os == LGI_OS_WIN32 ||
				 Os == LGI_OS_WIN64)
		{
			if (Ver[0] < 5)
			{
				OsName = "WinNT";
			}
			else if (Ver[0] == 5)
			{
				if (Ver[1] == 0)
					OsName = "Win2k";
				else
					OsName = "WinXP";
			}
			else if (Ver[0] == 6)
			{
				if (Ver[1] == 0)
					OsName = "Vista";
				else if (Ver[1] == 1)
					OsName = "Win7";
				else if (Ver[1] == 2)
					OsName = "Win8";
				else if (Ver[1] == 3)
					OsName = "Win8.1";
			}
			else if (Ver[0] == 10)
			{
				OsName = "Win10";
			}
			else if (Ver[0] == 11)
			{
				// What's the chances eh?
				OsName = "Win11";
			}
		}

		s.Printf(" v%s (%s v", ScribeVer, OsName);
		Ret += s;
		
		for (unsigned i=0; i<Ver.Length(); i++)
		{
			s.Printf("%s%i", i?".":"", Ver[i]);
			Ret += s;
		}
		
		s.Printf(", %s", Build);
		Ret += s;
	
		#ifdef LINUX
		const char *Wm = NULL;
		switch (LGetWindowManager())
		{
			case WM_Gnome:
				Wm = "Gnome";
				break;
			case WM_Kde:
				Wm = "Kde";
				break;
		}
		if (Wm)
		{
			s.Printf(", %s", Wm);
			Ret += s;
		}
		#endif
		
		LLanguage *CurLang = LGetLanguageId();
		if (CurLang)
		{
			s.Printf(", %s", CurLang->Id);
			Ret += s;
		}
		
		s.Printf(")");
		Ret += s;
	}
	
	return Ret;
}

bool MatchWord(char *Str, char *Word)
{
	bool Status = false;
	
	if (Str && Word)
	{
		#define IsWord(c)	( IsDigit(c) || IsAlpha(c) )
		for (char *s=stristr(Str, Word); s; s=stristr(s+1, Word))
		{
			char *e = s + strlen(Word);
			if (	(s<=Str || !IsWord(s[-1]) )	&&
					(e[0] == 0 || !IsWord(e[0]))	)
			{
				return true;
			}
		}
	}

	return Status;
}



//////////////////////////////////////////////////////////////////////////////
ScribePanel::ScribePanel(ScribeWnd *app, const char *name, int size, bool open) :
	LPanel(name, size, open)
{
	App = app;
}

bool ScribePanel::Pour(LRegion &r)
{
	if (App)
	{
		SetClosedSize(App->GetToolbarHeight());
	}

	return LPanel::Pour(r);
}

//////////////////////////////////////////////////////////////////////////////
#include "ScribeWndPrivate.h"

//////////////////////////////////////////////////////////////////////////////
void UpgradeRfOption(ScribeWnd *App, const char *New, const char *Old, const char *Default)
{
	LVariant v;

	/*
	App->GetOptions()->GetValue(New, v);
	if (v.Str())
	{
		ScribePath *Path = new ScribePath(App, Old);
		if (Path)
		{
			auto Xml = LReadFile(*Path);
			if (Xml)
			{
				App->GetOptions()->SetValue(New, v = Xml);
			}
			App->GetOptions()->DeleteValue(Old);
			DeleteObj(Path);
		}
	}
	*/

	if (Default && !App->GetOptions()->GetValue(New, v))
	{
		App->GetOptions()->SetValue(New, v = Default);
	}
}

////////////////////////////////////////////////////////////////////////////
ScribeWnd::AppState ScribeWnd::ScribeState = ScribeConstructing;


/*
 * This constructor is a little convoluted, but the basic idea is this:
 * 
 * - Do some basic init.
 * - Attempt to load the options (could make portable/desktop mode clear)
 * - If the portable/desktop mode is unclear ask the user.
 * - Call Construct1.
 * - If the UI language is not known, ask the user.
 * - Call Construct2.
 * 
 * Each time a dialog is needed the rest of the code needs to be in a callable function.
 * 
 * It's important to note that the ScribeWnd::OnCreate method needs to be called after
 * the system Handle() is created, and after any dialogs in the ScribeWnd::ScribeWnd
 * constructor have finished.
 */
ScribeWnd::ScribeWnd() :
	BayesianFilter(this),
	CapabilityInstaller("Scribe",
						ScribeVer,
						"http://memecode.com/components/lookup.php",
						ScribeTempPath())
{
	#ifndef HAIKU
	if (_Lock)
		_Lock->SetName("ScribeWnd");
	#endif

	// init some variables
	LApp::ObjInstance()->AppWnd = this;
	LCharsetSystem::Inst()->DetectCharset = ::DetectCharset;
	d = new ScribeWndPrivate(this);	
	Ipc = new ScribeIpc(this);

	#ifndef WIN32
	printf("%s\n", GetFullAppName(true).Get());
	#endif

	auto Type = d->GetInstallMode();
	if (Type == LOptionsFile::UnknownMode)
	{
		 // This may make the mode more clear...
		if (LoadOptions())
			Type = d->GetInstallMode();
	}

	#ifdef HAIKU
	// The event loop for this window won't start till the constructor finishes...
	// And that is needed for the load mail stores state, so start the thread here:
	if (WindowHandle()->Thread() < 0 &&
		WindowHandle()->Lock())
	{
		WindowHandle()->Run();
		WindowHandle()->Unlock();
	}
	PostEvent(M_CONSTRUCT_0, (LMessage::Param)Type);
	#else
	Construct0(Type);
	#endif
}

void ScribeWnd::Construct0(LOptionsFile::PortableType Type)
{
	if (Type == LOptionsFile::UnknownMode)
	{
		d->AskUserForInstallMode([this](auto selectedMode)
		{
			d->SetInstallMode(selectedMode);

			if (!d->Options)
				d->Options.Reset(new LOptionsFile(selectedMode, OptionsFileName));

			Construct1();
		});
	}
	else
	{
		if (!d->Options)
			d->Options.Reset(new LOptionsFile(Type, OptionsFileName));

		Construct1();
	}
}

void ScribeWnd::Construct1()
{
	if (!d->Options)
	{
		LgiTrace("%s:%i - Error: no options object.\n", _FL);
		ScribeState = ScribeExiting;
		return;
	}
	LoadOptions();
	ScribeOptionsDefaults(d->Options);

	#ifdef LINUX
	LSetSystemPath(LSP_TEMP, ScribeTempPath());
	#endif

	LVariant GlyphSub;
	if (GetOptions()->GetValue(OPT_GlyphSub, GlyphSub))
	{
		bool UseGlyphSub = GlyphSub.CastInt32() != 0;
		LSysFont->SubGlyphs(UseGlyphSub);
		LSysBold->SubGlyphs(UseGlyphSub);
		LFontSystem::Inst()->SetDefaultGlyphSub(UseGlyphSub);
	}
	else
	{
		GetOptions()->SetValue(OPT_GlyphSub, GlyphSub = LFontSystem::Inst()->GetDefaultGlyphSub());
	}

	{
		// Limit the size of the 'Scribe.txt' log file
		if (auto p = LTraceGetFilePath())
		{
			int64 Sz = LFileSize(p);
			#define MiB * 1024 * 1024
			if (Sz > (3 MiB))
				FileDev->Delete(p);
		}
	}

	// Process pre-UI options
	LVariant SizeAdj;
	int SzAdj = SizeAdj.CastInt32();
	if (GetOptions()->GetValue(OPT_UiFontSize, SizeAdj) &&
		(SzAdj = SizeAdj.CastInt32()) >= 0 &&
		SzAdj < 5)
	{
		d->FontSizeAdjust = SzAdj - 2;
		if (d->FontSizeAdjust)
		{
			int Pt = LSysFont->PointSize();
		
			LSysFont->PointSize(Pt + d->FontSizeAdjust);
			LSysFont->Create();

			LSysBold->PointSize(Pt + d->FontSizeAdjust);
			LSysBold->Create();
		}
	}
	else
	{
		GetOptions()->SetValue(OPT_UiFontSize, SizeAdj = 2);
	}

	// Resources and languages
	SetLanguage();

	// If no language set...
	LVariant LangId;
	if (!GetOptions()->GetValue(OPT_UiLanguage, LangId))
	{
		// Ask the user...
		auto Dlg = new LanguageDlg(this);
		if (!Dlg->Ok)
		{
			delete Dlg;
			LgiMsg(this, "Failed to create language selection dialog.", "Scribe Error");
			ScribeState = ScribeExiting;
			LCloseApp();
		}
		else
		{
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					// Set the language in the options file
					LVariant v;
					GetOptions()->SetValue(OPT_UiLanguage, v = Dlg->Lang.Get());
		
					// Reload the resource file... to get the new lang.
					LResources *Cur = LgiGetResObj(false);
					DeleteObj(Cur);

					SetLanguage();
					Construct2();
				}
				else // User canceled
				{
					ScribeState = ScribeExiting;
					LCloseApp();
				}
			});
		}
	}
	else Construct2();
}

void ScribeWnd::Construct2()
{
	#if 1
	auto CurRes = LgiGetResObj(false);
	LVariant Theme;
	if (CurRes && GetOptions()->GetValue(OPT_Theme, Theme))
	{
		auto Paths = ScribeThemePaths();
		auto NoTheme = LLoadString(IDS_DEFAULT);
		if (Theme.Str() &&
			Stricmp(NoTheme, Theme.Str()))
		{
			for (auto p: Paths)
			{
				LFile::Path Inst(p);
				Inst += Theme.Str();
				if (Inst.Exists())
				{
					CurRes->SetThemeFolder(Inst);
					d->htmlStatic->Static->OnSystemColourChange();
					break;
				}
			}
		}
	}
	#endif

	LoadCalendarStringTable();

	ZeroObj(DefaultFolderNames);
	DefaultFolderNames[FOLDER_INBOX]     = LLoadString(IDS_FOLDER_INBOX, "Inbox");
	DefaultFolderNames[FOLDER_OUTBOX]    = LLoadString(IDS_FOLDER_OUTBOX, "Outbox");
	DefaultFolderNames[FOLDER_SENT]      = LLoadString(IDS_FOLDER_SENT, "Sent");
	DefaultFolderNames[FOLDER_TRASH]     = LLoadString(IDS_FOLDER_TRASH, "Trash");
	DefaultFolderNames[FOLDER_CONTACTS]  = LLoadString(IDS_FOLDER_CONTACTS, "Contacts");
	DefaultFolderNames[FOLDER_TEMPLATES] = LLoadString(IDS_FOLDER_TEMPLATES, "Templates");
	DefaultFolderNames[FOLDER_FILTERS]   = LLoadString(IDS_FOLDER_FILTERS, "Filters");
	DefaultFolderNames[FOLDER_CALENDAR]  = LLoadString(IDS_FOLDER_CALENDAR, "Calendar");
	DefaultFolderNames[FOLDER_GROUPS]    = LLoadString(IDS_FOLDER_GROUPS, "Groups");
	DefaultFolderNames[FOLDER_SPAM]      = LLoadString(IDS_SPAM, "Spam");

	LStringPipe RfXml;
	RfXml.Print(DefaultRfXml,
				LLoadString(IDS_ORIGINAL_MESSAGE),
				LLoadString(FIELD_TO),
				LLoadString(FIELD_FROM),
				LLoadString(FIELD_SUBJECT),
				LLoadString(IDS_DATE));
	{
		LAutoString Xml(RfXml.NewStr());
		UpgradeRfOption(this, OPT_TextReplyFormat, "ReplyXml", Xml);
		UpgradeRfOption(this, OPT_TextForwardFormat, "ForwardXml", Xml);
	}

	LFontType t;
	if (t.GetSystemFont("small"))
	{
		d->PreviewFont = t.Create();
		if (d->PreviewFont)
		{
			#if defined WIN32
			d->PreviewFont->PointSize(8);
			#endif
		}
	}

	MoveOnScreen();

	// Load global graphics
	LoadImageResources();

	// Load time threads
	// Window name
	Name(AppName);
	SetSnapToEdge(true);
	ClearTempPath();

	#if WINNATIVE
	SetStyle(GetStyle() & ~WS_VISIBLE);
	SetExStyle(GetExStyle() & ~WS_EX_ACCEPTFILES);
	CreateClassW32(AppName, LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_APP)));
	#endif

	#if defined LINUX
	SetIcon("About64px.png", "Network;Email;");
	LFinishXWindowsStartup(this);
	#endif

	ScribeState = ScribeConstructed;
	OnCreate();
}

void ScribeWnd::Construct3()
{
	if (ScribeState == ScribeConstructing)
	{
		// Constructor is still running, probably showing some UI.
		// Don't complete setup at this point.
		return;
	}

	// Load the styles
	LResources::StyleElement(this);

	// Main menu
	Menu = new LMenu(AppName);
	if (Menu)
	{
		if (!Menu->Attach(this))
		{
			LgiTrace("%s:%i - Failed to attach menu.\n", _FL);
		}
		else if (!Menu->Load(this, "ID_MENU", GetUiTags()))
		{
			LgiTrace("%s:%i - Failed to load 'ID_MENU'.\n", _FL);
		}
		else
		{
			if (d->FontSizeAdjust)
			{
				auto m = Menu->GetFont();
				if (m)
				{
					m->PointSize(m->PointSize() + d->FontSizeAdjust);
					m->Create();
				}
			}

			LAssert(ImageList != NULL);
			Menu->SetImageList(ImageList, false);

			auto IdentityItem = Menu->FindItem(IDM_NO_IDENTITIES);
			if (IdentityItem)
			{
				IdentityMenu = IdentityItem->GetParent();
			}

			CmdSend.MenuItem = Menu->FindItem(IDM_SEND_MAIL);
			auto NewMailMenu = Menu->FindItem(IDM_NEW_EMAIL);
			if (NewMailMenu)
			{
				MailMenu = NewMailMenu->GetParent();
			}

			LVariant v;
			WorkOffline = Menu->FindItem(IDM_WORK_OFFLINE);
			if (WorkOffline && GetOptions()->GetValue(OPT_WorkOffline, v))
			{
				WorkOffline->Checked(v.CastInt32() != 0);
			}

			if ((d->DisableUserFilters = Menu->FindItem(IDM_FILTERS_DISABLE)))
			{
				if (GetOptions()->GetValue(OPT_DisableUserFilters, v))
				{
					d->DisableUserFilters->Checked(v.CastInt32() != 0);
				}
			}

			#if RUN_STARTUP_SCRIPTS
			// Run scripts in './Scripts' folder
			LFile::Path s(ScribeResourcePath());
			s = s /
				#ifndef MAC
				".." /
				#endif
				"scripts";
			if (!s.Exists())
			{
				LgiTrace("%s:%i - scripts at '%s' doesn't exist.\n", _FL, s.GetFull().Get());
				s = LFile::Path(LSP_APP_INSTALL) / "scripts";
			}
			if (!s.Exists())
			{
				LgiTrace("%s:%i - Error: the scripts folder '%s' doesn't exist.\n", _FL, s.GetFull().Get());
			}
			else
			{
				bool ErrorDsp = false;
				LDirectory Dir;
				for (int b = Dir.First(s); b; b = Dir.Next())
				{
					if (Dir.IsDir())
						continue;

					auto Ext = LGetExtension(Dir.GetName());
					if (!Ext || _stricmp(Ext, "script") != 0)
						continue;

					LStringPipe Log;
					auto Source = LReadFile(Dir.FullPath());
					if (Source)
					{
						if (auto Cur = new LScript)
						{
							char Msg[256];
							d->CurrentScripts.Add(Cur);
							LScribeScript::Inst->GetLog()->Write(Msg,
								sprintf_s(Msg, sizeof(Msg), "Compiling '%s'...\n", Dir.GetName()));

							LCompiler c;
							if (c.Compile(	Cur->Code,
											d->Engine->GetSystemContext(),
											LScribeScript::Inst,
											s,
											Source,
											NULL))
							{
								if (auto Main = Cur->Code->GetMethod("Main"))
								{
									LVirtualMachine Vm(d);
									
									LScriptArguments Args(&Vm);
									Args.New() = new LVariant((LDom*)this);
									d->Scripts.Add(Cur);
									if (Vm.ExecuteFunction(	Cur->Code,
															Main,
															Args,
															LScribeScript::Inst->GetLog()) &&
										Args.GetReturn()->CastInt32())
									{
										d->CurrentScripts.Delete(Cur, true);
										Cur = NULL;
									}
									else
									{
										LgiTrace("Error: Script's main failed (%s)\n", Cur->Code->GetFileName());
										if (Cur->Callbacks.Length())
											d->DeleteCallbacks(Cur->Callbacks);
										d->Scripts.Delete(Cur);
									}

									Args.DeleteObjects();
								}
							}
							else if (!ErrorDsp)
							{
								ErrorDsp = true;
								OnScriptCompileError(Source, NULL);
							}

							if (Cur)
							{
								d->CurrentScripts.Delete(Cur, true);
								DeleteObj(Cur);
							}
						}
					}
				}

			}
			#endif

			#define EnableItem(id, en) { auto i = Menu->FindItem(id); if (i) i->Enabled(en); }
			#define SetMenuIcon(id, ico) { auto i = Menu->FindItem(id); if (i) i->Icon(ico); }
			
			EnableItem(IDM_IMPORT_OUTLOOK_ITEMS, true);
			
			// SetMenuIcon(IDM_OPEN_FOLDERS, ICON_OPEN_FOLDER);
			SetMenuIcon(IDM_OPTIONS, ICON_OPTIONS);
			SetMenuIcon(IDM_SECURITY, ICON_LOCK);

			SetMenuIcon(IDM_CUT, ICON_CUT);
			SetMenuIcon(IDM_COPY, ICON_COPY);
			SetMenuIcon(IDM_PASTE, ICON_PASTE);

			SetMenuIcon(IDM_LAYOUT1, ICON_LAYOUT1);
			SetMenuIcon(IDM_LAYOUT2, ICON_LAYOUT2);
			SetMenuIcon(IDM_LAYOUT3, ICON_LAYOUT3);
			SetMenuIcon(IDM_LAYOUT4, ICON_LAYOUT4);
			SetMenuIcon(IDM_NEW_EMAIL, ICON_UNSENT_MAIL);
			SetMenuIcon(IDM_SET_READ, ICON_READ_MAIL);
			SetMenuIcon(IDM_SET_UNREAD, ICON_UNREAD_MAIL);
			SetMenuIcon(IDM_NEW_CONTACT, ICON_CONTACT);
			SetMenuIcon(IDM_NEW_GROUP, ICON_CONTACT_GROUP);
			SetMenuIcon(IDM_REPLY, ICON_FLAGS_REPLY);
			SetMenuIcon(IDM_REPLY_ALL, ICON_FLAGS_REPLY);
			SetMenuIcon(IDM_FORWARD, ICON_FLAGS_FORWARD);
			SetMenuIcon(IDM_BOUNCE, ICON_FLAGS_BOUNCE);
			SetMenuIcon(IDM_NEW_FILTER, ICON_FILTER);
			SetMenuIcon(IDM_FILTER_CURRENT_FOLDER, ICON_FOLDER_FILTERS);
			SetMenuIcon(IDM_MEMECODE, ICON_LINK);
			SetMenuIcon(IDM_HOMEPAGE, ICON_LINK);
			SetMenuIcon(IDM_SCRIBE_FAQ, ICON_LINK);
			SetMenuIcon(IDM_INSCRIBE_LINK, ICON_LINK);
			SetMenuIcon(IDM_VERSION_HISTORY, ICON_LINK);
			SetMenuIcon(IDM_DEBUG_INFO, ICON_LINK);
			SetMenuIcon(IDM_TUTORIALS, ICON_LINK);
			SetMenuIcon(IDM_FEEDBACK, ICON_UNREAD_MAIL);
			SetMenuIcon(IDM_HELP, ICON_HELP);

			LMenuItem *mi;
			if
			(
				GetOptions()->GetValue(OPT_EditControl, v) &&
				(mi = Menu->FindItem(IDM_HTML_EDITOR))
			)
				mi->Checked(v.CastInt32() != 0);

			Menu->SetPrefAndAboutItems(IDM_OPTIONS, IDM_ABOUT);
		}
	}

	// Initialize user interface
	SetupUi();

	// Get some of the base submenu pointers. These are needed
	// for SetupAccounts to work correctly, e.g. populate the
	// send/receive/preview submenus. Folders need to be loaded
	// before this for the templates folder
	BuildDynMenus();

	// Load accounts
	SetupAccounts();

	// Recursively load folder tree
	LoadFolders([this](auto status)
	{
		if (ScribeState == ScribeExiting)
			return;

		// Update the templates sub-menu now that the folders are loaded
		BuildDynMenus();

		// Check registry settings
		SetDefaultHandler();

		// Run on load scripts...
		LArray<LScriptCallback*> OnLoadCallbacks;
		if (GetScriptCallbacks(LOnLoad, OnLoadCallbacks))
		{
			for (auto r: OnLoadCallbacks)
			{
				LVirtualMachine Vm;
				LScriptArguments Args(&Vm);
				Args.New() = new LVariant(this);
				ExecuteScriptCallback(*r, Args);
				Args.DeleteObjects();
			}
		}

		OnCommandLineEvent(StartupEvent);
		if (d->FakeIpcEvent)
		{
			// This happens when there is no options file (yet). Ie on first start.
			// Therefor no need for checking the IPC if there is another instance 
			// running.
			Visible(true);
			OnCommandLineEvent(IpcEvent);
		}
	
		#ifdef _DEBUG
		BayesianFilter::UnitTests(this);
		#endif

		ScribeState = ScribeRunning;
	});
}

ScribeWnd::~ScribeWnd()
{
	LAppInst->AppWnd = 0;
	SearchView = NULL;
	ScribeState = ScribeExiting;
	LScribeScript::Inst->ShowScriptingWindow(false);

	// Other cleanup...
	ClearTempPath();
	DeleteObj(Ipc);
	SetPulse();

	// Save anything thats still dirty in the folders...
	// just in case we crash during the shutdown phase.
	ScribeFolder *Cur = GetCurrentFolder();
	if (Cur)
		Cur->SerializeFieldWidths();
	SaveDirtyObjects(5000);

	// Tell the UI not to reference anything in the folders
	if (PreviewPanel)
	{
		PreviewPanel->OnThing(0, false);
	}
	Mail::NewMailLst.Empty();

	// ~AccountStatusItem references the account list... must be before we
	// delete the accounts.
	DeleteObj(StatusPanel);

	// ~Accountlet needs to reference the root container... so
	// it has to go before unloading of folders.
	Accounts.DeleteObjects();

	UnLoadFolders();

	DeleteObj(PreviewPanel);
	SaveOptions();
	DeleteObj(Commands);
	DeleteObj(d->PreviewFont);
	DeleteObj(d->SubSplit);
	DeleteObj(Splitter);
	ListPane.Reset();
	MailList = NULL;

	CmdSend.ToolButton = NULL;
	CmdReceive.ToolButton = NULL;
	CmdPreview.ToolButton = NULL;
	CmdSend.MenuItem = NULL;
	CmdReceive.MenuItem = NULL;
	CmdPreview.MenuItem = NULL;

	// This could be using the OpenSSL library for HTTPS connections. So
	// close it before calling EndSSL.
	DeleteObj(d->ImageLoader);
	
	// This has to be after we close all the accounts... otherwise
	// they might still be using SSL functions, e.g. an IMAP/SSL connect.
	EndSSL();

	DeleteObj(d);
}

void ScribeWnd::SetLanguage()
{
	LVariant LangId;
	if (GetOptions()->GetValue(OPT_UiLanguage, LangId))
	{
		// Set the language to load...
		LAppInst->SetConfig("Language", LangId.Str());
	}
	LResources::SetLoadStyles(true);

	// Load the resources (with the current lang)
	if (!LgiGetResObj(true, "Scribe"))
	{
		LgiMsg(NULL, "The resource file 'Scribe.lr8' is missing.", AppName);
		ScribeState = ScribeExiting;
		LCloseApp();
	}
}

LString ScribeWnd::GetResourceFile(SribeResourceType Type)
{
	THREAD_UNSAFE(LString());
	auto File = d->ResFiles.Find(Type);
	if (!File)
		LgiTrace("%s:%i - No file for resource type %i\n", _FL, Type);
	return File;
}

int GetPxFromFile(LString fn)
{
	auto parts = fn.SplitDelimit("-.");
	for (auto p: parts)
		if (p.IsNumeric())
			return (int)p.Int();
	return 0;
}

void ScribeWnd::LoadImageResources()
{
	THREAD_UNSAFE();
	auto Res = LgiGetResObj();
	LString::Array Folders;
	if (Res)
	{
		auto p = Res->GetThemeFolder();
		if (p)
			Folders.Add(p);
	}
	auto resPath = ScribeResourcePath();
	Folders.Add(resPath);

	for (auto p: Folders)
	{
		LDirectory Dir;

		auto dpi = GetDpiScale();
		auto idealPx = dpi.x < 1.5f ? 16 : 32;

		LgiTrace("%s:%i - Loading resource folder '%s'\n", _FL, p.Get());
		for (auto b = Dir.First(p); b; b = Dir.Next())
		{
			if (Dir.IsDir())
				continue;

			auto Name = Dir.GetName();
			int curPx = GetPxFromFile(Name);
			bool pref = false;

			SribeResourceType type = ResNone;
			if (MatchStr("Toolbar-*.png", Name) ||
				(pref = MatchStr("xgate-icons-*.png", Name)))
				type = ResToolbarFile;
			else if (MatchStr("Icons-*.png", Name))
				type = ResIconsFile;

			if (type != ResNone)
			{
				int prevPx = 0;
				auto prevFile = d->ResFiles.Find(type);
				if (prevFile)
					prevPx = GetPxFromFile(prevFile);

				if (!prevFile || (prevPx != idealPx && curPx == idealPx) || pref)
					d->ResFiles.Add(type, Dir.FullPath());
			}
		}
	}

	ToolbarImgs.Reset(LLoadImageList(GetResourceFile(ResToolbarFile)));
    auto IconsFile = GetResourceFile(ResIconsFile);
	LAssert(IconsFile);
	ImageList.Reset(LLoadImageList(IconsFile));
	if (!ImageList)
		LgiTrace("%s:%i - Failed to load toolbar image ('%s')\n", _FL, IconsFile.Get());
}

int ScribeWnd::GetEventHandle()
{
	THREAD_SAFE();
	return d->AppWndHnd;
}

void ScribeWnd::OnCloseInstaller()
{
	THREAD_UNSAFE();
	d->Bar = NULL;
	if (InThread())
	{
		PourAll();
	}
	else LAssert(0);
}

void ScribeWnd::OnInstall(CapsHash *Caps, bool Status)
{
	THREAD_UNSAFE();
}

bool ScribeWnd::NeedsCapability(const char *Name, const char *Param)
{
	THREAD_SAFE();
	#if DEBUG_CAPABILITIES
	LgiTrace("ScribeWnd::NeedsCapability(%s, %s)\n", Name, Param);
	#endif
	
	if (!InThread())
	{
		#if DEBUG_CAPABILITIES
		LgiTrace("%s:%i - Posting M_NEEDS_CAP\n", _FL);
		#endif
		PostEvent(M_NEEDS_CAP, (LMessage::Param)new LString(Name), (LMessage::Param)(Param?new LString(Param):NULL));
	}
	else
	{
		if (!Name)
			return false;
			
		if (d->MissingCaps.Find(Name))
		{
			#if DEBUG_CAPABILITIES
			LgiTrace("%s:%i - Already in MissingCaps\n", _FL);
			#endif
			return true;
		}

		d->MissingCaps.Add(Name, true);
		LColour cBack;

		LStringPipe MsgBuf(256);
		int i = 0;
		// const char *k;
		// for (bool b=d->MissingCaps.First(&k); b; b=d->MissingCaps.Next(&k), i++)
		for (auto k : d->MissingCaps)
		{
			MsgBuf.Print("%s%s", i?", ":"", k.key);
		}
		
		LVariant Actions;
		if (stristr(Name, "OpenSSL"))
		{
			MsgBuf.Print(LLoadString(IDS_ERROR_SERVER_CONNECT));
			if (Param)
				MsgBuf.Print("\n%s", Param);
			Actions.Add(new LVariant(LLoadString(IDS_INSTALL)));
		}
		else if (stristr(Name, "Registry"))
		{
			MsgBuf.Print(LLoadString(IDS_ERROR_REG_WRITE));
			Actions.Add(new LVariant(LLoadString(IDS_DONT_SHOW_AGAIN)));
		}
		else if (stristr(Name, "SpellingDictionary"))
		{
			MsgBuf.Print(LLoadString(IDS_ERROR_NEED_INSTALL), Param);
			Actions.Add(new LVariant(LLoadString(IDS_DOWNLOAD)));
		}
		else if (stristr(Name, "mkcert"))
		{
			MsgBuf.Print(LLoadString(IDS_ERROR_NEED_INSTALL), Name);
			Actions.Add(new LVariant(LLoadString(IDS_OPEN_WEBSITE)));
		}
		else if (stristr(Name, SslSocket::CAPS_CERT_ERROR))
		{
			LJson j(Param);
			auto msg = j.Get(SslSocket::JSON_MESSAGE);
			d->SslCertHost = j.Get(SslSocket::JSON_HOST);
			d->SslCertRef = j.Get(SslSocket::JSON_REF);
			d->SslCertId = j.Get(SslSocket::JSON_CERT);

			MsgBuf.Print(" - %s: %s", d->SslCertHost.Get(), msg.Get());
			if (d->SslCertRef || d->SslCertId)
				MsgBuf.Print(" (%s, hasCert=%i)", d->SslCertRef.Get(), d->SslCertId ? 1 : 0);

			Actions.Add(new LVariant(LLoadString(IDS_ACCEPT_ONCE)));
			Actions.Add(new LVariant(LLoadString(IDS_ACCEPT_ALWAYS)));
			cBack = LColour::Orange;
		}
		Actions.Add(new LVariant(LLoadString(IDS_OK)));
		
		#if DEBUG_CAPABILITIES
		LgiTrace("%s:%i - Actions.Length()=%i, Bar=%p\n", _FL, Actions.Length(), d->Bar);
		#endif
		if (Actions.Length())
		{
			LAutoString Msg(MsgBuf.NewStr());

			// Check the script hook here...
			bool ShowInstallBar = true;
			LArray<LScriptCallback*> Callbacks;
			if (GetScriptCallbacks(LBeforeInstallBar, Callbacks))
			{
				for (unsigned i=0; i<Callbacks.Length(); i++)
				{
					LScriptCallback &c = *Callbacks[i];
					if (!c.Func)
						continue;

					LVirtualMachine Vm;
					LScriptArguments Args(&Vm);
					LVariant This((LDom*)this);
					LVariant TheMsg(Msg.Get());					
					
					Args.Add(&This);
					Args.Add(&TheMsg);
					Args.Add(&Actions);
					
					if (ExecuteScriptCallback(c, Args, true))
					{
						if (!Args.GetReturn()->CastInt32())
							ShowInstallBar = false;
						else
							Msg.Reset(TheMsg.ReleaseStr());
					}
				}
			}
			
			// Now create the capability install bar...
			if (!d->Bar && ShowInstallBar && Actions.Type == GV_LIST)
			{
				// FYI Capabilities are handled in ScribeWnd::StartAction.
				LArray<const char *> Act;
				for (auto v : *Actions.Value.Lst)
					Act.Add(v->Str());
				
				d->Bar = new MissingCapsBar(this, &d->MissingCaps, Msg, this, Act, cBack ? &cBack : nullptr);
				AddView(d->Bar, 2);
				AttachChildren();
				OnPosChange();
			}
		}
	}

	return true;
}

LAutoString ScribeWnd::GetDataFolder()
{
	THREAD_UNSAFE(LAutoString());
	LVariant v;
	GetOptions()->GetValue(OPT_IsPortableInstall, v);
	
	char p[MAX_PATH_LEN];
	if (LGetSystemPath(v.CastInt32() ? LSP_APP_INSTALL : LSP_APP_ROOT, p, sizeof(p)))
	{
		if (!LDirExists(p))
			FileDev->CreateFolder(p);

		return LAutoString(NewStr(p));
	}
	else LgiTrace("%s:%i - LgiGetSystemPath failed (portable=%i).\n", _FL, v.CastInt32());
	
	return LAutoString();
}

LScriptEngine *ScribeWnd::GetScriptEngine()
{
	THREAD_SAFE();
	return d->Engine;
}

LScriptCallback ScribeWnd::GetCallback(const char *CallbackMethodName)
{
	THREAD_UNSAFE(LScriptCallback());
	LScriptCallback Cb;
	
	auto Cur = d->CurrentScript();
	if (Cur && Cur->Code)
	{
		Cb.Script = Cur;
		Cb.Func = Cur->Code->GetMethod(CallbackMethodName);
	}

	if (!Cb.Func)
	{
		for (auto s: d->Scripts)
		{
			Cb.Script = s;
			if ((Cb.Func = s->Code->GetMethod(CallbackMethodName)))
				break;
		}
	}

	return Cb;
}

int ScribeWnd::RegisterCallback(LScriptCallbackType Type, LScriptArguments &Args)
{
	THREAD_UNSAFE(LScriptCallback::INVALID_CALLBACK);
	if (!d->CurrentScript())
	{
		LgiTrace("%s:%i - No current script.\n", _FL);
		return LScriptCallback::INVALID_CALLBACK;
	}

	auto Fn = Args.StringAt(1);
	auto Cb = GetCallback(Fn);
	if (!Cb.Func)
	{
		LgiTrace("%s:%i - No callback '%s'.\n", _FL, Fn);
		return LScriptCallback::INVALID_CALLBACK;
	}

	switch (Type)
	{
		case LToolsMenu:
		{
			auto Menu = Args.StringAt(0);
			auto Shortcut = Args.StringAt(2);
			auto Cur = d->CurrentScript();
			if (!Menu || !Fn || !Cur)
			{
				LgiTrace("%s:%i - menu=%s, fn=%s.\n", _FL, Menu, Fn);
				return LScriptCallback::INVALID_CALLBACK;
			}

			LScriptCallback &c = Cur->Callbacks.New();
			c = Cb;
			c.Uid = d->NextScriptUid++;
			c.Type = Type;
			c.Param = d->NextToolMenuId;

			LMenuItem *Tools = GetMenu()->FindItem(IDM_TOOLS_MENU);
			auto ToolSub = Tools ? Tools->Sub() : NULL;
			if (ToolSub)
			{
				if (d->NextToolMenuId == IDM_TOOL_SCRIPT_BASE)
				{
					ToolSub->AppendSeparator();
				}

				ToolSub->AppendItem(Menu, c.Param, true, -1, Shortcut);
				d->NextToolMenuId++;
			}
			return c.Uid;
		}
		case LThingContextMenu:
		case LFolderContextMenu:
		case LThingUiToolbar:
		case LMailOnBeforeSend:
		case LMailOnAfterReceive:
		case LApplicationToolbar:
		case LBeforeInstallBar:
		case LInstallComponent:
		case LOnTimer:
		case LRenderMail:
		case LOnLoad:
		{
			auto Cur = d->CurrentScript();
			LAssert(d->Scripts.HasItem(Cur));

			LScriptCallback &c = Cur->Callbacks.New();
			c = Cb;
			c.Type = Type;
			c.Uid = d->NextScriptUid++;
			if (Args.Length() > 2)
				c.Data = *Args[2];

			if (Type == LOnTimer)
				SetupScriptTimers();

			return c.Uid;
		}
		default:
		{
			LAssert(!"Not a known callback type");
			break;
		}
	}

	return LScriptCallback::INVALID_CALLBACK;
}

bool ScribeWnd::RemoveCallback(int Uid)
{
	THREAD_UNSAFE(false);
	for (auto script: d->Scripts)
	{
		for (unsigned i=0; i<script->Callbacks.Length(); i++)
		{
			auto &cb = script->Callbacks[i];
			if (cb.Uid == Uid)
			{
				if (cb.Type == LOnTimer)
				{
					for (auto timer: d->OnSecondTimerCallbacks)
					{
						if (timer->Uid == Uid)
						{
							bool removed = d->OnSecondTimerCallbacks.Delete(timer);
							if (!removed)
							{
								LAssert(!"Remove failed.");
								return false;
							}
						}
					}
				}

				script->Callbacks.DeleteAt(i);
				return true;
			}
		}
	}

	return false;
}

bool ScribeWnd::GetScriptCallbacks(LScriptCallbackType Type, LArray<LScriptCallback*> &Callbacks)
{
	THREAD_UNSAFE(false);
	for (auto s: d->Scripts)
	{
		for (auto &c: s->Callbacks)
		{
			if (c.Type == Type)
				Callbacks.Add(&c);
		}
	}

	return Callbacks.Length() > 0;
}

bool ScribeWnd::ExecuteScriptCallback(LScriptCallback &c, LScriptArguments &Args, bool ReturnArgs)
{
	THREAD_UNSAFE(false);
	if (!c.Func || !c.Script)
		return false;

	// Setup
	LVirtualMachine Vm(d);
	Vm.SetDebuggerEnabled(true);
	d->CurrentScripts.Add(c.Script);
	
	// Call the method
	bool Status = Vm.ExecuteFunction(	c.Script->Code,
										c.Func,
										Args,
										LScribeScript::Inst->GetLog(),
										ReturnArgs ? &Args : NULL) != ScriptError;

	// Cleanup
	d->CurrentScripts.PopLast();
	return Status;
}

LStream *ScribeWnd::ShowScriptingConsole()
{
	THREAD_UNSAFE(NULL);
	auto Item = Menu->FindItem(IDM_SCRIPTING_CONSOLE);
	if (Item)
	{
		Item->Checked(!Item->Checked());
		LScribeScript::Inst->ShowScriptingWindow(Item->Checked());
		
		LVariant v;
		GetOptions()->SetValue(OPT_ShowScriptConsole, v = Item->Checked());
	}
	
	return LScribeScript::Inst->GetLog();
}

LOptionsFile::PortableType ScribeWnd::GetPortableType()
{
	THREAD_SAFE();
	return d->GetInstallMode();
}

void ScribeWnd::RemoteContent_AddSender(const char *Addr, bool WhiteList)
{
	THREAD_UNSAFE();
	if (!Addr)
		return;

	auto Opt = WhiteList ? OPT_RemoteContentWhiteList : OPT_RemoteContentBlackList;
	LVariant v;
	GetOptions()->GetValue(Opt, v); // Not an error if not there...

	auto existing = LString(v.Str()).SplitDelimit(" ,\r\n");
	for (auto p: existing)
	{
		if (MatchStr(p, Addr))
		{
			LgiTrace("%s:%i - '%s' is already in '%s'\n", _FL, Addr, Opt);
			return; // Already in list...
		}
	}

	existing.SetFixedLength(false);
	existing.Add(Addr);
	auto updated = LString("\n").Join(existing);
	GetOptions()->SetValue(Opt, v = updated.Get());
	LgiTrace("%s:%i - Added '%s' to '%s'\n", _FL, Addr, Opt);
	d->RemoteContent_Init = false;
}

ScribeRemoteContent ScribeWnd::RemoteContent_GetSenderStatus(const char *Addr)
{
	THREAD_UNSAFE(RemoteDefault);
	if (!d->RemoteContent_Init)
	{
		LVariant v;
		if (GetOptions()->GetValue(OPT_RemoteContentWhiteList, v))
			d->RemoteWhiteLst = LString(v.Str()).SplitDelimit(" ,\r\n");
		if (GetOptions()->GetValue(OPT_RemoteContentBlackList, v))
			d->RemoteBlackLst = LString(v.Str()).SplitDelimit(" ,\r\n");
		d->RemoteContent_Init = true;
	}

	for (auto p: d->RemoteWhiteLst)
		if (MatchStr(p, Addr))
			return RemoteAlwaysLoad;

	for (auto p: d->RemoteBlackLst)
		if (MatchStr(p, Addr))
			return RemoteNeverLoad;

	return RemoteDefault;
}

void ScribeWnd::RemoteContent_ClearCache()
{
	THREAD_UNSAFE();
	d->RemoteWhiteLst.Empty();
	d->RemoteBlackLst.Empty();
	d->RemoteContent_Init = false;
}

void ScribeWnd::OnSpellerSettingChange()
{
	THREAD_UNSAFE();

	// Kill the current thread
	d->SpellerThread.Reset();
	
	// Setup the new thread
	LSpellCheck *t = GetSpellThread();
	if (t)
	{
		// Trigger an install if needed
		t->Check(d->AppWndHnd, "thisisamispeltword", 0, 18);
	}
}

bool ScribeWnd::SetSpellThreadParams(LSpellCheck *Thread)
{
	THREAD_SAFE();

	if (!Thread)
		return false;
	
	LVariant Lang, Dict;
	GetOptions()->GetValue(OPT_SpellCheckLanguage, Lang);
	GetOptions()->GetValue(OPT_SpellCheckDictionary, Dict);

	LAutoPtr<LSpellCheck::Params> Params(new LSpellCheck::Params);
	if (!Params)
		return false;

	Params->IsPortable = GetPortableType();
	Params->OptionsPath = GetOptions()->GetFile();
	Params->Lang = Lang.Str();
	Params->Dict = Dict.Str();
	Params->CapTarget = this;
	
	Thread->SetParams(Params);
	return true;
}

LSpellCheck *ScribeWnd::CreateSpellObject()
{
	THREAD_SAFE();
	LVariant PrefAspell;
	GetOptions()->GetValue(OPT_PreferAspell, PrefAspell);

	LAutoPtr<LSpellCheck> Obj;

	if (PrefAspell.CastInt32())
		Obj = CreateAspellObject();

	#if defined(MAC)

		if (!Obj)
			Obj = CreateAppleSpellCheck();

	#elif defined(WINDOWS)

		LArray<int> Ver;
		int Os = LGetOs(&Ver);
		if
		(
			!Obj &&
			(Os == LGI_OS_WIN32 || Os == LGI_OS_WIN64)
			&&
			(
				Ver.Length() > 1 &&
				(
					Ver[0] > 6
					||
					(Ver[0] == 6 && Ver[1] > 1)
				)
			)
		)
			Obj = CreateWindowsSpellCheck();
				
	#endif

	if (!Obj)
		Obj = CreateAspellObject();

	SetSpellThreadParams(Obj);
	return Obj.Release();
}

LSpellCheck *ScribeWnd::GetSpellThread(bool OverrideOpt)
{
	THREAD_UNSAFE(NULL);

	LVariant Use;
	if (OverrideOpt)
		Use = true;
	else
		GetOptions()->GetValue(OPT_SpellCheck, Use);

	#if USE_SPELLCHECKER
	if ((Use.CastInt32() != 0) ^ (d->SpellerThread.Get() != 0))
		d->SpellerThread.Reset(Use.CastInt32() ? CreateSpellObject() : NULL);
	#endif

	return d->SpellerThread;
}

LString ScribeWnd::GetHttpProxy()
{
	THREAD_SAFE();
	LString Proxy;
	
	LVariant v;
	if (GetOptions()->GetValue(OPT_HttpProxy, v) && ValidStr(v.Str()))
	{
		Proxy = v.Str();
	}
	else
	{
		LProxyUri p;
		if (p.sHost)
			Proxy = p.ToString();
	}
	
	return Proxy;
}

InstallProgress *ScribeWnd::StartAction(MissingCapsBar *Bar, LCapabilityTarget::CapsHash *Components, const char *ActionParam)
{
	THREAD_UNSAFE(NULL);
	if (!ActionParam)
	{
		LgiTrace("%s:%i - No action supplied.\n", _FL);
		return NULL;
	}

	LArray<LScriptCallback*> Callbacks;
	LVariant Action(ActionParam);
	if (GetScriptCallbacks(LInstallComponent, Callbacks))
	{
		bool StartInstall = true;

		for (unsigned i=0; i<Callbacks.Length(); i++)
		{
			LScriptCallback &c = *Callbacks[i];
			if (!c.Func)
				continue;

			LVirtualMachine Vm;
			LScriptArguments Args(&Vm);
			LVariant This((LDom*)this);
			
			Args.Add(&This);
			Args.Add(&Action);
			
			if (ExecuteScriptCallback(c, Args))
			{
				if (!Args.GetReturn()->CastInt32())
					StartInstall = false;
			}
		}
		
		if (!Action.Str())
		{
			LgiTrace("%s:%i - GInstallComponent removed action name.\n", _FL);
			return NULL;
		}

		if (!StartInstall)
		{
			LgiTrace("%s:%i - GInstallComponent script canceled install of '%s'.\n", _FL, ActionParam);
			return NULL;
		}
	}

	if (!Stricmp(Action.Str(), LLoadString(IDS_OK)))
	{
		// Do nothing
		d->MissingCaps.Empty();
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_DONT_SHOW_AGAIN)))
	{
		// Turn off registering as a client.
		LVariant No(false);
		GetOptions()->SetValue(OPT_RegisterWindowsClient, No);
		GetOptions()->SetValue(OPT_CheckDefaultEmail, No);
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_OPEN_WEBSITE)))
	{
		for (auto c: *Components)
		{
			if (!Stricmp(c.key, "mkcert"))
				LExecute("https://github.com/FiloSottile/mkcert");
		}
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_INSTALL)))
	{
		#ifdef WINDOWS
		bool IsSsl = false;
		for (auto c: *Components)
		{
			if (!_stricmp(c.key, "openssl"))
			{
				IsSsl = true;
				break;
			}
		}
		if (IsSsl)
		{
			LString s;
			s.Printf(LLoadString(IDS_WINDOWS_SSL_INSTALL), LGetOsName());
			
			auto q = new LAlert(this, AppName, s, LLoadString(IDS_OPEN_WEBSITE), LLoadString(IDS_CANCEL));
			q->DoModal([this, q](auto dlg, auto id)
			{
				switch (id)
				{
					case 1:
						LExecute("https://slproweb.com/products/Win32OpenSSL.html");
						break;
					default:
						break;
				}
			});
			return NULL;
		}
		#endif

		return CapabilityInstaller::StartAction(Bar, Components, Action.Str());
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_SHOW_CONSOLE)))
	{
		ShowScriptingConsole();
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_OPEN_SOURCE)))
	{
		if (d->ErrSource)
			LExecute(d->ErrSource);
		else if (d->ErrFilter)
			d->ErrFilter->DoUI();
		
		d->ErrSource.Empty();
		d->ErrFilter = NULL;
	}
	else if (	!Stricmp(Action.Str(), LLoadString(IDS_SHOW_REMOTE_CONTENT)) ||
				!Stricmp(Action.Str(), LLoadString(IDS_ALWAYS_SHOW_REMOTE_CONTENT)))
	{
		auto c = Components->begin();
		LWindow *w = Bar->GetWindow();
		if ((*c).key &&
			!Stricmp((*c).key, "RemoteContent") &&
			w)
		{
			LScriptArguments Args(NULL);
			Args[0] = new LVariant(!Stricmp(Action.Str(), LLoadString(IDS_ALWAYS_SHOW_REMOTE_CONTENT)));
			w->CallMethod(DomToStr(SdShowRemoteContent), Args);
		}
	}
	else if (!_stricmp(Action.Str(), LLoadString(IDS_DOWNLOAD)))
	{
		auto t = GetSpellThread();
		if (t)
			t->InstallDictionary();
		else
			LgiTrace("%s:%i - No spell thread.\n", _FL);
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_ACCEPT_ONCE)))
	{
		d->AllowCert(SslAcceptOnce);
		SaveOptions();
	}
	else if (!Stricmp(Action.Str(), LLoadString(IDS_ACCEPT_ALWAYS)))
	{
		d->AllowCert(SslAcceptAlways);
		SaveOptions();
	}	
	else LAssert(!"Unknown action.");
	
	return nullptr;
}

HttpImageThread *ScribeWnd::GetImageLoader()
{
	if (!d->ImageLoader)
	{
		auto Proxy = GetHttpProxy();
		d->ImageLoader = new HttpImageThread(this, Proxy, 0);
	}

	return d->ImageLoader;
}

const char *ScribeWnd::GetUiTags()
{
	THREAD_UNSAFE(NULL);
	if (!d->UiTags)
	{
		char UiTags[256] =
			#if defined WINDOWS
			"win"
			#elif defined LINUX
			"linux"
			#elif defined MAC
			"mac"
			#elif defined HAIKU
			"haiku"
			#endif
			;
	
		LVariant Tags;
		if (!GetOptions())
		{
			LAssert(!"Where is the options?");
		}
		else if (GetOptions()->GetValue("tags", Tags))
		{
			size_t Len = strlen(UiTags);
			sprintf_s(UiTags+Len, sizeof(UiTags)-Len, " %s", Tags.Str());
		}

		d->UiTags = UiTags;
	}

	return d->UiTags;
}

void ScribeWnd::OnCreate()
{
	if (IsAttached() && ScribeState == ScribeConstructed)
	{
		ScribeState = ScribeInitializing;
		Construct3();
	}
}

ScribeAccount *ScribeWnd::GetAccountByEmail(const char *Email)
{
	THREAD_UNSAFE(NULL);
	if (!Email)
		return NULL;

	for (auto a : *GetAccounts())
	{
		LVariant e = a->Identity.Email();
		if (e.Str() &&
			!_stricmp(e.Str(), Email))
		{
			return a;
		}
	}
	
	return 0;
}

ScribeAccount *ScribeWnd::GetAccountById(int Id)
{
	THREAD_UNSAFE(NULL);
	for (auto a : *GetAccounts())
	{
		if (a->Receive.Id() == Id)
		{
			return a;
		}
	}
	
	return 0;
}

const char *ScribeWnd::EditCtrlMimeType()
{
	THREAD_SAFE();
	LVariant Html;
	GetOptions()->GetValue(OPT_EditControl, Html);
	return Html.CastInt32() ? sTextHtml : sTextPlain;
}

LAutoString ScribeWnd::GetReplyXml(const char *MimeType)
{
	THREAD_SAFE();
	bool IsHtml = MimeType && !_stricmp(MimeType, sTextHtml);

	LVariant s;
	GetOptions()->GetValue(IsHtml ? OPT_HtmlReplyFormat : OPT_TextReplyFormat, s);
	return LAutoString(s.ReleaseStr());
}

LAutoString ScribeWnd::GetForwardXml(const char *MimeType)
{
	THREAD_SAFE();
	bool IsHtml = MimeType && !_stricmp(MimeType, sTextHtml);

	LVariant s;
	GetOptions()->GetValue(IsHtml ? OPT_HtmlForwardFormat : OPT_TextForwardFormat, s);
	return LAutoString(s.ReleaseStr());
}

LVmCallback *ScribeWnd::GetDebuggerCallback()
{
	THREAD_SAFE();
	return d;
}

GpgConnector *ScribeWnd::GetGpgConnector()
{
	THREAD_UNSAFE(NULL);
	if (!d->GpgInst)
	{
		if (!GpgConnector::IsInstalled())
			return NULL;
		
		d->GpgInst.Reset(new GpgConnector());
	}
	
	return d->GpgInst;
}

LFont *ScribeWnd::GetPreviewFont()
{
	THREAD_UNSAFE(NULL);
	return d->PreviewFont;
}

bool ScribeWnd::IsValid()
{
	THREAD_SAFE();
	#if 0
	try
	{
		for (ScribeAccount *a = Accounts.First(); a; a = Accounts.Next())
		{
		}
	}
	catch(...)
	{
		return false;
	}
	#endif

	return true;
}

bool ScribeWnd::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	THREAD_UNSAFE(false);

	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdQuote: // Type: String
		{
			return GetOptions()->GetValue(OPT_QuoteReplyStr, Value);
		}
		case SdName: // Type: String
		{
			Value = AppName;
			break;
		}
		case SdHome: // Type: String
		{
			Value = LGetExePath().Get();
			break;
		}
		case SdNow: // Type: String
		{
			LDateTime Now;
			Now.SetNow();
			char s[64];
			Now.Get(s, sizeof(s));
			Value = s;
			break;
		}
		case SdFolder:	// Type: ScribeFolder[]
						// Pass system folder index or string as array parameter.
		{
			ScribeFolder *f = 0;
			if (!Array)
				return false;

			if (IsDigit(*Array))
			{
				f = GetFolder(atoi(Array));
			}
			else
			{
				f = GetFolder(Array);
			}

			if (!f)
				return false;

			Value = (LDom*)f;
			break;
		}
		case SdAppName: // Type: String
		{
			Value = AppName;
			break;
		}
		case SdCalendarToday: // Type: String
		{
			static uint64_t lastUpdate = 0;
			auto now = LCurrentTime();
			
			if (now - lastUpdate > 60000)
			{			
				lastUpdate = now;
				
				// Due to the asyncronous nature of SummaryOfToday, it maybe waiting for network calendars....
				Calendar::SummaryOfToday(this,
					[this](auto s)
					{
						// This is called sometime after the value is returned to the caller...
						d->CalendarSummary = s;
						
						// But we can refresh the display, with the new value:
						LoadTitleListPane();
					});
			}

			Value = d->CalendarSummary;
			break;
		}
		case SdInboxSummary:
		{
			LStringPipe p;
			
			// Iterate through the mail stores
			for (auto &m: Folders)
			{
				if (!m.Store)
					continue;
				auto Inbox = m.Store->GetObj(FIELD_INBOX);
				if (!Inbox)
					continue;

				auto Unread = Inbox->GetInt(FIELD_UNREAD);
				auto Name = Inbox->GetStr(FIELD_FOLDER_NAME);
				LString Path;
				Path.Printf("/%s/%s", m.Name.Get(), Name);
					
				if (Unread)
					p.Print("%s: <a href='folder://%s'>%i unread</a><br>", m.Name.Get(), Path.Get(), Unread);
				else
					p.Print("%s: 0 unread<br>", m.Name.Get());
			}
			
			// And also the IMAP full folders
			for (auto a: Accounts)
			{
				ScribeProtocol Protocol = a->Receive.ProtocolType();
				if (Protocol == ProtocolImapFull)
				{
					LDataStoreI *Store = a->Receive.GetDataStore();
					if (Store)
					{
						auto Inbox = Store->GetObj(FIELD_INBOX);
						if (Inbox)
						{
							auto Unread = Inbox->GetInt(FIELD_UNREAD);
							auto Name = Inbox->GetStr(FIELD_FOLDER_NAME);
							auto m = a->Receive.Name();
							if (m.Str())
							{
								LString Path;
								Path.Printf("/%s/%s", m.Str(), Name);
								
								if (Unread)
									p.Print("%s: <a href='folder://%s'>%i unread</a><br>", m.Str(), Path.Get(), Unread);
								else
									p.Print("%s: 0 unread<br>", m.Str());
							}
						}
					}
				}
			}
			
			Value = p.NewLStr().Get();
			break;
		}
		case SdExecute: // Type: String
		{
			if (!Array)
				return false;

			const char *s = Array;
			char *Exe = LTokStr(s);
			if (!Exe)
				return false;

			while (*s && *s == ' ') s++;
			
			LStringPipe Out;
			LSubProcess p(Exe, (char*)s);
			if (p.Start())
			{
				p.Communicate(&Out);
				Value = Out.NewLStr().Strip();
			}
			
			DeleteArray(Exe);
			break;
		}
		case SdBuildType: // Type: String
		{
			#ifdef _DEBUG
			Value = "<font color=#ff0000>Debug</font>";
			#else
			Value = "Release";
			#endif
			break;
		}
		case SdPlatform: // Type: String
		{
			LArray<int> Ver;
			LGetOs(&Ver);
			
			LString::Array Va;
			for (auto i: Ver)
				Va.New().Printf("%i", i);
			
			#if defined __GTK_H__
				auto Api = "GTK3";
			#elif LGI_SDL
				auto Api = "SDL";
			#elif LGI_COCOA
				auto Api = "Cocoa";
			#elif LGI_CARBON
				auto Api = "Carbon";
			#elif defined WIN32
				auto Api = "WinApi";
			#elif defined HAIKU
				auto Api = "Haiku";
			#else
				#error "Impl me."
				auto Api = "#err";
			#endif
			
			LString s;
			s.Printf("%s, v%s, %s", LGetOsName(), LString(".").Join(Va).Get(), Api);
			Value = s.Get();
			break;
		}
		case SdVersion: // Type: String
		{
			char Ver[32];
			sprintf_s(Ver, sizeof(Ver), "v%s", ScribeVer);
			Value = Ver;
			break;
		}
		case SdBuild: // Type: String
		{
			char s[128];
			sprintf_s(s, sizeof(s), "%s, %s, %ibit", __DATE__, __TIME__, (int)(sizeof(NativeInt)*8));
			Value = s;
			break;
		}
		case SdLanguage: // Type: String
		{
			LLanguage *l = LGetLanguageId();
			if (!l)
				return false;

			char s[256];
			sprintf_s(s, sizeof(s), "%s \"%s\"", l->Name, l->Id);
			Value = s;
			break;
		}
		case SdString: // Type: String
		{
			if (!Array)
			{
				LAssert(!"Missing string ID");
				return false;
			}

			int Id = atoi(Array);
			Value = LLoadString(Id);
			break;
		}
		case SdCurrentFolder: // Type: ScribeFolder
		{
			Value = (LDom*) GetCurrentFolder();
			break;
		}
		case SdView: // Type: LView
		{
			Value = (LView*)this;
			break;
		}
		case SdNoContact: // Type: Contact
		{
			Value = (NoContactType*)d->NoContact;
			break;
		}
		case SdAccounts:
		{
			if (Array)
			{
				if (IsDigit(*Array))
				{
					auto i = atoi(Array);
					if (i >= 0 && i < (ssize_t)Accounts.Length())
					{
						Value = (LDom*)Accounts[i];
					}
					else return false;
				}
				else
				{
					for (auto a : Accounts)
					{
						LVariant nm = a->Send.Name();
						if (nm.Str() && !_stricmp(nm.Str(), Array))
						{
							Value = (LDom*)a;
							break;
						}
					}
				}
			}
			else
			{
				Value = (int32)Accounts.Length();
			}
			break;
		}
		case SdOptions:
		{
			Value = GetOptions();
			break;
		}
		case SdMailStorePaths:
		{
			if (!Value.SetList())
				return false;
			for (auto &Ms : Folders)
				Value.Add(new LVariant(Ms.Path));
			break;
		}
		case SdRootFolders:
		{
			if (!Value.SetList() || !Tree)
				return false;

			for (auto *i = Tree->GetChild(); i; i = i->GetNext())
			{
				ScribeFolder *c = dynamic_cast<ScribeFolder*>(i);
				if (c)
				{
					auto p = c->GetPath();
					Value.Add(new LVariant(p));
				}
			}
			break;
		}
		case SdCalendarWindow: // Type: CalendarWindow
		{
			if (auto folder = GetFolder(FOLDER_CALENDAR))
			{
				auto wnd = OpenCalender(folder);
				LAssert(wnd);
				auto dom = dynamic_cast<LDom*>(wnd);
				LAssert(dom);
				Value = dynamic_cast<LDom*>(wnd);
				LAssert(!Value.IsNull());
			}
			else return false;
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool ScribeWnd::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	THREAD_UNSAFE(NULL);

	ScribeDomType m = StrToDom(MethodName);
	switch (m)
	{
		case SdGrowlOnMail: // Type: (Mail Obj)
		{
			if (Args.Length() != 1)
			{
				LgiTrace("%s:%i - Wrong arg count: %i.\n", _FL, (int)Args.Length());
				return false;
			}
			
			Mail *m = dynamic_cast<Mail*>(Args[0]->CastDom());
			if (!m)
			{
				LgiTrace("%s:%i - Invalid object.\n", _FL);
				return false;
			}
			
			GrowlOnMail(m);
			break;
		}
		case SdGrowlInfo:
		{
			auto Title = Args.Length() > 0 ? Args[0] : NULL;
			auto Text = Args.Length() > 1 ? Args[1] : NULL;
			GrowlInfo(Title ? Title->Str() : NULL, Text ? Text->Str() : NULL);
			break;
		}
		case SdGetClipboardText: // Type: ()
		{
			LClipBoard c(this);
			*Args.GetReturn() = c.Text();
			break;
		}
		case SdSetClipboardText: // Type: (String Text)
		{
			if (Args.Length() != 1)
			{
				LgiTrace("%s:%i - Wrong arg count: %i.\n", _FL, (int)Args.Length());
				return false;
			}

			char *Str = Args[0]->CastString();
			LClipBoard c(this);

			if (ValidStr(Str))
				*Args.GetReturn() = c.Text(Str);
			else
				*Args.GetReturn() = c.Empty();
			break;
		}
		case SdLookupContactGroup: // Type: (String GroupName)
		{
			if (Args.Length() != 1)
			{
				LgiTrace("%s:%i - Wrong arg count: %i.\n", _FL, (int)Args.Length());
				return false;
			}
			
			ContactGroup *Grp = LookupContactGroup(this, Args[0]->Str());
			*Args.GetReturn() = dynamic_cast<LDom*>(Grp);
			break;
		}
		case SdAskUserString: // Type: (LView ParentView, String Callback, String PromptMessage[, Bool ObsurePassword[, String DefaultValue]])
		{
			auto Vm = dynamic_cast<LVirtualMachine*>(Args.GetVm());
			if (!Vm)
				return false;

			LVirtualMachine::Context Ctx = Vm->SaveContext();
			if (!Ctx || Args.Length() < 3)
			{
				*Args.GetReturn() = false;
				return true;
			}

			LView *View       = Args[0]->CastView();
			auto CallbackName = Args[1]->Str();
			auto Prompt       = Args[2]->CastString();
			bool IsPassword   = Args.Length() > 3 ? Args[3]->CastInt32() != 0 : false;
			auto Default      = Args.Length() > 4 ? Args[4]->Str() : NULL;

			auto i = new LInput(View ? View : this, Default, Prompt, AppName, IsPassword);
			i->DoModal([Ctx, i, CallbackName=LString(CallbackName)](auto dlg, auto id)
			{
				if (id)
				{
					LScriptArguments Args(NULL);
					Args.Add(new LVariant(i->GetStr()));
					Ctx.Call(CallbackName, Args);
					Args.DeleteObjects();
				}
			});

			*Args.GetReturn() = true;
			break;
		}
		case SdCreateAccount: // Type: ()
		{
			ScribeAccount *a = new ScribeAccount(this, (int)Accounts.Length());
			if (a)
			{
				*Args.GetReturn() = (LDom*)a;
				Accounts.Insert(a);
				a->Create();
			}
			else return false;
			break;
		}
		case SdDeleteAccount: // Type: (ScribeAccount AccountToDelete)
		{
			if (Args.Length() != 1)
				return false;
			
			ScribeAccount *a = dynamic_cast<ScribeAccount*>(Args[0]->CastDom());
			if (!a)
			{
				*Args.GetReturn() = false;
			}
			else
			{
				int Idx = (int)Accounts.IndexOf(a);
				if (Idx < 0 || a->IsOnline())
				{
					*Args.GetReturn() = false;
				}
				else
				{
					Accounts.Delete(a);
					a->Delete();
					delete a; // delete actual account object

					// Reindex remaining items so their are no gaps
					int i=0;
					auto it = Accounts.begin();
					for (a = *it; a; a = *++it, i++)
					{
						a->ReIndex(i);
					}
				}
			}
			break;
		}
		case SdShowRemoteContent:
		{
			if (PreviewPanel)
				return PreviewPanel->CallMethod(MethodName, Args);
			else
				return false;
			break;
		}
		case SdSearchHtml: // Type(Html, SearchExp, ResultExp)
		{
			if (Args.Length() != 3)
			{
				LgiTrace("%s:%i - SearchHtml requires 3 arguments.\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}

			auto Html = Args[0]->Str();
			auto SearchExp = Args[1]->Str();
			auto ResultExp = Args[2]->Str();
			if (!Html || !SearchExp || !ResultExp)
			{
				LgiTrace("%s:%i - SearchHtml got non-string argument.\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}

			SearchHtml(Args.GetReturn(), Html, SearchExp, ResultExp);
			return true;
		}
		case SdGetUri: // Type(UriToDownload, CallbackName)
		{
			if (Args.Length() < 2)
			{
				LgiTrace("%s:%i - GetUri requires at least 2 arguments.\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}

			auto Uri = Args[0]->Str();
			auto Callback = Args[1]->Str();
			LVariant *UserData = Args.Length() > 2 ? Args[2] : NULL;
			new ScriptDownloadContentThread(this, Uri, Callback, UserData);
			*Args.GetReturn() = true;
			return true;
		}
		case SdReplicate: // Type(SourceFolders, DestFolders)
		{
			auto Src = Args.StringAt(0);
			auto Dst = Args.StringAt(1);
			if (!Src || !Dst)
			{
				*Args.GetReturn() = "Param error";
				break;
			}

			auto CastDataFolder = [this](const char *name) -> LDataFolderI*
			{
				auto Ms = GetMailStoreForPath(name);
				if (!Ms)
					return NULL;
				if (!Ms->GetRoot())
					return NULL;
				auto obj = Ms->GetRoot()->GetObject();
				return dynamic_cast<LDataFolderI*>(obj);
			};

			auto SrcFolder = CastDataFolder(Src);
			auto DstFolder = CastDataFolder(Dst);
			if (!SrcFolder || !DstFolder)
			{
				*Args.GetReturn() = "Couldn't find both root folders";
				break;
			}
			
			LArray<Store3ItemTypes> Types;
			Types.Add(MAGIC_MAIL);
			auto status = Store3ReplicateFolders(this,
												DstFolder,
												SrcFolder,
												true,
												false,
												&Types);
			*Args.GetReturn() = status >= Store3Delayed;
			break;
		}
		case SdOnNew: // Type: (ScribeFolder, Thing)
		{
			// This is mostly for scripting new email debugging / testing.
			*Args.GetReturn() = false;
			if (Args.Length() != 2)
			{
				LgiTrace("%s:%i - Wrong arg count.\n", _FL);
				return true;
			}

			auto fld = dynamic_cast<ScribeFolder*>(Args[0]->CastDom());
			if (!fld)
			{
				LgiTrace("%s:%i - no folder.\n", _FL);
				return true;
			}

			auto thing = dynamic_cast<Thing*>(Args[1]->CastDom());
			if (!thing)
			{
				LgiTrace("%s:%i - no thing.\n", _FL);
				return true;
			}

			auto dataFolder = dynamic_cast<LDataFolderI*>(fld->GetObject());
			LArray<LDataI*> a = { thing->GetObject() };
			OnNew(dataFolder, a, 0, true, true);
			break;
		}
		default:
		{
			LAssert(!"Unsupported method.");
			return false;
		}
	}
	
	return true;
}

LOptionsFile *ScribeWnd::GetOptions(bool Create)
{
	THREAD_SAFE();

	if (!d->Options && Create)
	{
		LAssert(!"Not here... do it in LoadOptions.");
		return NULL;
	}

	return d->Options;
}

int OptionsFileCmp(OptionsInfo *a, OptionsInfo *b)
{
	int64 Diff = b->Mod - a->Mod;
	return Diff < 0 ? -1 : (Diff > 0 ? 1 : 0);
}

OptionsInfo::OptionsInfo()
{
}
	
OptionsInfo &OptionsInfo::operator =(char *p)
{
	File = p;
	Leaf = LGetLeaf(File);
		
	if (Leaf)
	{
		char n[64];
		sprintf_s(n, sizeof(n), "%s.xml", OptionsFileName);
		Usual = !_stricmp(n, Leaf);
	}
		
	return *this;
}
	
LAutoPtr<LOptionsFile> OptionsInfo::Load()
{
	// Read the file...
	size_t Count = 0;
	LXmlTag *Stores = NULL;
	LXmlTag *Acc = NULL;
	LAutoPtr<LOptionsFile> Of(new LOptionsFile(File));
	if (!Of)
		return Of;
	if (!Of->SerializeFile(false))
		goto OnError;
		
	// Sanity check the options...
	Acc = Of->LockTag(OPT_Accounts, _FL);
	if (!Acc)
		goto OnError;
	Of->Unlock();
		
	Stores = Of->LockTag(OPT_MailStores, _FL);
	if (!Stores)
		goto OnError;

	Count = Stores->Children.Length();
	Of->Unlock();
	if (Count == 0)
		goto OnError;
		
	return Of;

OnError:
	Of.Reset();
	return Of;
}

#define DEBUG_OPTS_SCAN		0
bool ScribeWnd::ScanForOptionsFiles(LArray<OptionsInfo> &Files, LSystemPath PathType)
{
	LString Root = LGetSystemPath(PathType);
	LDirectory Dir;
	char p[MAX_PATH_LEN];

	if (IsUnitTest)
		Root += ".unittest";

	#if DEBUG_OPTS_SCAN
	LgiTrace("%s:%i - Root='%s'\n", _FL, Root.Get());
	#endif

	for (int b = Dir.First(Root); b; b = Dir.Next())
	{
		if
		(
			!Dir.IsDir()
			&&
			Dir.Path(p, sizeof(p))
		)
		{
			LResolveShortcut(p, p, sizeof(p));
			auto Ext = LGetExtension(Dir.GetName());
			if (stristr(Dir.GetName(), OptionsFileName) != NULL &&
				Ext &&				
				(!_stricmp(Ext, "xml") || !_stricmp(Ext, "bak")))
			{
				OptionsInfo &i = Files.New();
				i = p;
				i.Mod = Dir.GetLastWriteTime();

				#if DEBUG_OPTS_SCAN
				LgiTrace("%s:%i - File='%s'\n", _FL, p);
				#endif
			}
		}
	}
	
	Files.Sort(OptionsFileCmp);
	
	// Scan through the results and pick out the normal file
	for (unsigned i=0; i<Files.Length() && !d->Options; i++)
	{
		if (Files[i].Usual)
		{
			d->Options = Files[i].Load();

			#if DEBUG_OPTS_SCAN
			LgiTrace("%s:%i - Attempt '%s' = %p\n", _FL, Files[i].File.Get(), d->Options);
			#endif
		}
	}

	if (!d->Options)
	{
		// Scan through the alternative files and look for something 
		// we can use.
		#if DEBUG_OPTS_SCAN
		LgiTrace("%s:%i - Scanning backups\n", _FL);
		#endif
		for (unsigned i=0; i<Files.Length() && !d->Options; i++)
		{
			if (!Files[i].Usual)
			{
				d->Options = Files[i].Load();
				if (d->Options)
				{
					// Lets rename this baby back to the real filename
					LString Xml = OptionsFileName;
					Xml += ".xml";
					LFile::Path Normal(Root, Xml);
					if (LFileExists(Normal))
						FileDev->Delete(Normal);
					
					d->Options->SetFile(Normal);
					d->Options->SerializeFile(true); // sets to clean after changing filename.
				}
			}
		}
	}

	if (d->Options)
	{
		// Load OK: Clear out any old options files...
		#if DEBUG_OPTS_SCAN
		LgiTrace("%s:%i - Files.len=" LPrintfSizeT "\n", _FL, Files.Length());
		#endif
		while (Files.Length() > 6)
		{
			auto Idx = Files.Length() - 1;
			auto &f = Files[Idx];
			#if DEBUG_OPTS_SCAN
			LgiTrace("%s:%i - Delete '%s'\n", _FL, f.File.Get());
			#endif
			FileDev->Delete(f.File);
			Files.DeleteAt(Idx);
		}
	}

	return d->Options != NULL;
}

bool ScribeWnd::IsUnitTest = false;

bool ScribeWnd::LoadOptions()
{
	bool Load = false;
	
	THREAD_UNSAFE(false);

	// Check if we are running unit tests...
	if ((IsUnitTest = LAppInst->GetOption("unittest")))
	{
		d->UnitTestServer.Reset(new LUnitTestServer(this));
	}

	// Now look in the application install folder
	LArray<OptionsInfo> Files;
	if (!d->Options &&
		ScanForOptionsFiles(Files, LSP_APP_INSTALL))
	{
		// File is in the install folder...
		d->SetInstallMode(LOptionsFile::PortableMode);
		LgiTrace("Selecting portable mode based on options file path.\n");
	}
	
	// Look in the app root
	if
	(
		!d->Options &&
		ScanForOptionsFiles(Files, LSP_APP_ROOT)
	)
	{
		// Desktop mode
		d->SetInstallMode(LOptionsFile::DesktopMode);
		LgiTrace("Selecting desktop mode based on options file path.\n");
	}

	// Do multi-instance stuff
	if (Ipc &&
		d->Options &&
		d->Options->GetFile())
	{
		// printf("%s:%i - Calling Ipc->OnLoad...\n", _FL);
		Ipc->OnLoad(d->Options->GetFile(),
					&d->MulPassword,
					[this](auto status)
					{
						LgiTrace("%s:%i - Ipc.OnLoad=%i\n", _FL, status);
						if (status)
						{
							Visible(true);
							OnCommandLineEvent(IpcEvent);
						}
						else
						{
							// auto s = ScribeState;
							ScribeState = ScribeExiting;
							LCloseApp();
							// Args have been passed on to running instance.
						}
					});
	}
	else
	{
		// printf("%s:%i - Not calling IPC? %p, %p, %s\n", _FL, Ipc, d->Options.Get(), d->Options?d->Options->GetFile():NULL);
		d->FakeIpcEvent = true;
	}

	if (ScribeState == ScribeExiting)
		return false;

	// Open file and load..
	if (!Load &&
		d->Options &&
		LFileExists(d->Options->GetFile()))
	{
		auto Opts = GetOptions();
		Load = Opts->SerializeFile(false);
		if (Load)
		{
			LVariant v = d->GetInstallMode() == LOptionsFile::PortableMode;
			GetOptions()->SetValue(OPT_IsPortableInstall, v);
			LgiTrace("LoadOptions(%s)\n", d->Options->GetFile());
		}
		else
		{
            auto err = GetOptions()->GetError();
			LgiMsg(	this,
					"Error: loading options: %s",
					AppName,
					MB_OK,
					err);
		}
	}

	if (!d->Options)
	{
		// d->Options = new LOptionsFile(d->GetInstallMode(), OptionsFileName);
		return false;
	}

	if (d->Options)
	{
		LVariant v;

		if (!d->Options->GetValue(OPT_IsPortableInstall, v) &&
			d->GetInstallMode() != LOptionsFile::UnknownMode)
		{
			v = d->GetInstallMode() == LOptionsFile::PortableMode;
			d->Options->SetValue(OPT_IsPortableInstall, v);
		}

		ScribeOptionsDefaults(d->Options);

		if (Load)
		{
			if (GetOptions()->GetValue(OPT_PrintSettings, v))
			{
				auto *p = GetPrinter();
				if (p)
				{
					LString s = v.Str();
					p->Serialize(s, false);
				}
			}
		}

		if (d->Options->GetValue(OPT_PreviewLines, v))
		{
			Mail::PreviewLines = v.CastInt32() != 0;
		}

		// upgrade smtp password
		const char *Pw = "SmtpPsw";
		if (!GetOptions()->GetValue(OPT_EncryptedSmtpPassword, v))
		{
			// no encrypted password, look for unencrypted password
			if (GetOptions()->GetValue(Pw, v))
			{
				LPassword p;
				p.Set(v.Str());
				p.Serialize(GetOptions(), OPT_EncryptedSmtpPassword, true);
			}
		}

		// if old un-encrypted password exists...
		// delete the key, we are now storing an encrypted 
		// password
		if (GetOptions()->GetValue(Pw, v))
			GetOptions()->DeleteValue(Pw);
		
		if (GetOptions()->GetValue(OPT_AdjustDateTz, v))
			Mail::AdjustDateTz = !v.CastInt32();

		if (!GetOptions()->GetValue(OPT_ConfirmDelete, v))
			GetOptions()->SetValue(OPT_ConfirmDelete, v = true);

		if (!GetOptions()->GetValue(OPT_DelDirection, v))
			GetOptions()->SetValue(OPT_DelDirection, v = DeleteActionPrev);

		if (GetOptions()->GetValue(OPT_SizeInKiB, v))
			OptionSizeInKiB = v.CastInt32() != 0;	
		if (GetOptions()->GetValue(OPT_RelativeDates, v))
			ShowRelativeDates = v.CastInt32() != 0;	

		// date format
		if (GetOptions()->GetValue(OPT_DateFormat, v))
		{
			int Idx = v.CastInt32();
			if (Idx >= 0 && Idx < CountOf(DateTimeFormats))
				LDateTime::SetDefaultFormat(DateTimeFormats[Idx]);
		}
		
		// SSL debug logging
		//if (GetOptions()->GetValue(OPT_DebugSSL, v))
		// 	SslSocket::DebugLogging = v.CastInt32() != 0;

		// Growl
		if (GetOptions()->GetValue(OPT_GrowlEnabled, v) &&
			v.CastInt32())
		{
			LVariant Ver, Bld;
			GetVariant(DomToStr(SdVersion), Ver);
			GetVariant("Build", Bld);
			LString n;
			n.Printf("%s\n%s", Ver.Str(), Bld.Str());
			GrowlInfo("Scribe has started up...", n);
		}
	}

	#if LGI_EXCEPTIONS
	try
	{
	#endif
		// Default the font settings to the system font
		// if they don't already exist
		const char *OptFont[] = { OPT_EditorFont, OPT_PrintFont, OPT_HtmlFont, 0 };
		int Index = 0;
		for (const char **Opt=OptFont; *Opt; Opt++, Index++)
		{
			LVariant v;
			if (!GetOptions()->GetValue(*Opt, v))
			{
				LFontType Type;
				if (Type.GetSystemFont("System"))
				{
					if (Index == 2)
					{
						int Pt = Type.GetPointSize();
						Type.SetPointSize(Pt+3);
					}
					Type.Serialize(GetOptions(), *Opt, true);
				}
			}
		}
	#if LGI_EXCEPTIONS
	}
	catch (...)
	{
		LgiMsg(	this,
				LLoadString(IDS_ERROR_FONT_SETTINGS),
				AppName,
				MB_OK);
	}
	#endif

	return true;
}

bool ScribeWnd::SaveOptions()
{
	THREAD_UNSAFE(false);

	LStringPipe Log(256);
	bool Status = false;
	bool WriteFailed = false;
	bool WndStateSet = false;
	
	RestartSave:
	
	if (!d->Options)
	{
		Log.Print("No options object to save.\n");
	}	
	else if (!d->Options->GetFile())
	{
		bool PortableOk = true;
		char Path[MAX_PATH_LEN];
		char Leaf[32];

		sprintf_s(Leaf, sizeof(Leaf), "%s.xml", OptionsFileName);
		Log.Print("No current path for '%s', creating...\n", Leaf);

		LVariant v;
		GetOptions()->GetValue(OPT_IsPortableInstall, v);
		if (v.CastInt32())
		{
			if (!LGetSystemPath(LSP_APP_INSTALL, Path, sizeof(Path)))
			{
				PortableOk = false;
				Log.Print("Error: LgiGetSystemPath(LSP_APP_INSTALL) failed.\n");
			}
			else
			{
				LMakePath(Path, sizeof(Path), Path, Leaf);
				
				// Do write test to confirm we are good to go
				LFile f;
				if (f.Open(Path, O_WRITE))
				{
					f.Close();
					FileDev->Delete(Path, NULL, false);
					d->Options->SetFile(Path);
				}
				else
				{
					PortableOk = false;
					Log.Print("Warning: '%s' is not writable.\n", Path);
				}
			}
		}
		
		if (!v.CastInt32() || !PortableOk)
		{
			// Desktop mode then.
			if (v.CastInt32())
			{
				const char *Msg = "Switching to desktop mode because the install folder is not writable.";
				Log.Print("%s\n", Msg);
				LgiMsg(this, Msg, AppName, MB_OK);
				GetOptions()->SetValue(OPT_IsPortableInstall, v = false);
			}

			if (!LGetSystemPath(LSP_APP_ROOT, Path, sizeof(Path)))
			{
				Log.Print("Error: LgiGetSystemPath(LSP_APP_ROOT) failed.\n");
			}
			else
			{
				LMakePath(Path, sizeof(Path), Path, LAppInst->LBase::Name());
				if (!LDirExists(Path))
				{
					if (!FileDev->CreateFolder(Path))
					{
						Log.Print("Error: CreateFolder('%s') failed.\n", Path);
					}
				}

				LMakePath(Path, sizeof(Path), Path, Leaf);
				
				// Do write test to confirm we are good to go
				LFile f;
				if (f.Open(Path, O_WRITE))
				{
					f.Close();
					FileDev->Delete(Path, NULL, false);
					d->Options->SetFile(Path);
				}
				else
				{
					Log.Print("Error: '%s' is not writable.\n", Path);
				}
			}
		}
	}
	else Log.Print("%s:%i - %p %s\n", _FL, d->Options.Get(), d->Options?d->Options->GetFile():"#NoOptions");

	if (d->Options && d->Options->GetFile() && d->Options->IsValid())
	{
		// Backup options file
		char Backup[MAX_PATH_LEN];
		strcpy_s(Backup, sizeof(Backup), d->Options->GetFile());
		auto Ext = (char*)LGetExtension(Backup);
		if (Ext)
		{
			*--Ext = 0;

			LString s;
			for (int i=1; i<100; i++)
			{
				s.Printf("%s_%i.bak", Backup, i);
				if (!LFileExists(s))
					break;
			}

			if (!LFileExists(s))
				FileDev->Move(d->Options->GetFile(), s);
		}

		// Update some settings...
		#if LGI_VIEW_HANDLE
		if (Handle())
		#endif
			WndStateSet = SerializeState(GetOptions(), OPT_ScribeWndPos, false);

		LVariant v;
		if (Splitter)
			GetOptions()->SetValue(OPT_SplitterPos, v = (int)Splitter->Value());
		if (d->SubSplit)
		{
			auto First = d->SubSplit->GetViewAt(0);
			if (First == (LViewI*)SearchView)
			{
				auto Lst = (SearchView) ? d->SubSplit->GetViewAt(1) : NULL;
				if (Lst)
					GetOptions()->SetValue(OPT_SubSplitPos, v = (int)Lst->GetPos().Y());
			}
			else
				GetOptions()->SetValue(OPT_SubSplitPos, v = (int)d->SubSplit->Value());
		}

		// Write them...
		if (GetOptions()->SerializeFile(true))
		{
			Status = true;
		}
		else
		{
			// We probably don't have write permissions to the install folder...
			Log.Print("Error: Options.Serialize failed.\n");
			
			if (!WriteFailed)
			{
				// This blocks any possibility of an infinite loop
				WriteFailed = true;
				d->Options->SetFile(NULL);
				
				// Set desktop mode explicitly
				LVariant v;
				GetOptions()->GetValue(OPT_IsPortableInstall, v = false);
				
				Log.Print("Restarting save after setting desktop mode...\n");
				goto RestartSave;
			}
		}
	}
	#ifdef _DEBUG
	else Log.Print("%s:%i - %p %s %i\n", _FL,
		d->Options.Get(),
		d->Options ? d->Options->GetFile() : NULL,
		d->Options ? d->Options->IsValid() : false);
	#endif

	if (!Status)
	{
		LString a = Log.NewLStr();
		LgiMsg(this, "Saving options failed:\n%s", AppName, MB_OK, a.Get());
	}
	
	if (!WndStateSet)
	{
		LRect r(10, 10, 790, 590);
		SetPos(r);
		MoveToCenter();
	}
	
	return Status;
}

void ScribeWnd::OnCommandLineEvent(CmdLineEvent event)
{
	THREAD_UNSAFE();

	int flag = 1 << event;
	if ((d->CmdLineEvents & flag) == 0)
	{
		auto AllFlags = (1 << IpcEvent) |
						(1 << StartupEvent);

		d->CmdLineEvents |= flag;
		
		if (d->CmdLineEvents == AllFlags)
		{
			LgiTrace("%s:%i - OnCommandLineEvent(%i) has all flags: calling OnCommandLine.\n", _FL, (int)event);
			OnCommandLine();
		}
		else LgiTrace("%s:%i - OnCommandLineEvent(%i) hasn't got all flags yet.\n", _FL, (int)event);
	}
	else LgiTrace("%s:%i - OnCommandLineEvent(%i): flag %x already set?\n", _FL, (int)event, flag);
}

//
// Command Line Options:
//
//	-m, -t				: To recipient(s)
//	-f					: The filename of the attachment
//	-b					: Attach as a binary
//	-c					: CC'd recipient(s)
//	-s					: Subject for the email
//	-n					: Send now... else UI is shown
//	-p					: Print the file
//  -upgrade_folders	: trigger a folder upgrade
//  -o                  : Load the following options file
//  -u                  : Load the following URL/file
//
void ScribeWnd::OnCommandLine()
{
	THREAD_UNSAFE();

	//LgiTrace("CmdLine: %S\n", GetCommandLineW());

	// Check command line args
	LString Str, File;

	Visible(true);
	
	bool CreateMail = false;
	CreateMail = LAppInst->GetOption("m", Str);
	if (!CreateMail)
		CreateMail = LAppInst->GetOption("t", Str);
	// LgiTrace("%s:%i - CreateMail=%i Str=%s\n", _FL, CreateMail, Str.Get());
	
	bool HasFile = LAppInst->GetOption("f", File);
	if (!CreateMail)
		CreateMail = HasFile;

	LString OpenArg;
	if (LAppInst->GetOption("u", OpenArg) &&
		OpenArg)
	{
		LUri u(OpenArg);
		if (u.sProtocol)
		{
			OnUrl(OpenArg);
		}
		else if (LFileExists(OpenArg))
		{
			LArray<const char*> Files;
			Files.Add(OpenArg);
			OnReceiveFiles(Files);
		}
	}

	Mail *NewEmail = 0;
	if (CreateMail && Str)
	{
		// strip off quotes if needed
		char *In = Str, *Out = Str;
		for (; In && *In; In++)
		{
			if (!strchr("\'\"", *In))
			{
				*Out++ = *In;
			}
		}
		*Out++ = 0;

		// create object
		NewEmail = dynamic_cast<Mail*>(CreateItem(MAGIC_MAIL, NULL, false));
		if (NewEmail)
		{
			Mailto mt(this, Str);
			mt.Apply(NewEmail);

			// cc's?
			if (LAppInst->GetOption("c", Str))
			{
				SetRecipients(this, Str, NewEmail->GetObject()->GetList(FIELD_TO), MAIL_ADDR_CC);
			}

			// attach a file?
			if (File)
			{
				if (LAppInst->GetOption("b"))
				{
					// attach as a binary file
					NewEmail->AttachFile(this, &File[0]);
				}
				else
				{
					// insert as the body
					auto b = LReadFile(&File[0]);
					if (b)
						NewEmail->SetBody(b);
				}
			}

			// subject?
			if (LAppInst->GetOption("s", Str))
			{
				NewEmail->SetSubject(Str);
			}

			// Send now or later?
			if (LAppInst->GetOption("n"))
			{
				// Check for exit after send option
				d->ExitAfterSend = LAppInst->GetOption("exit");
				
				// now
				NewEmail->SetFlags(MAIL_CREATED | MAIL_READY_TO_SEND | NewEmail->GetFlags());
				NewEmail->Save();
				OnCommand(IDM_SEND_MAIL, 0,
					#if LGI_VIEW_HANDLE
					Handle()
					#else
					NULL
					#endif
					);
			}
			else
			{
				// later
				NewEmail->DoUI();
			}
		}
	} 

	// Pop3 on startup option
	LVariant n;
	if (GetOptions()->GetValue(OPT_Pop3OnStart, n) && n.CastInt32())
	{
		OnCommand(IDM_RECEIVE_MAIL, 0, NULL);
	}
}

void ScribeWnd::SetCurrentIdentity(int i)
{
	THREAD_UNSAFE();

	LVariant v = i;
	GetOptions()->SetValue(OPT_CurrentIdentity, v);
	
	if (DefaultIdentityItem) DefaultIdentityItem->Checked(i < 0);
	for (auto a: Accounts)
	{
		a->SetCheck(i == a->GetIndex());
	}
}

ScribeAccount *ScribeWnd::GetCurrentAccount()
{
	THREAD_UNSAFE(NULL);

	auto Idx = GetCurrentIdentity();
	ScribeAccount *a = (Idx >= 0 && Idx < (ssize_t)Accounts.Length()) ? Accounts.ItemAt(Idx) : NULL;
	bool ValidId = a && a->IsValid();
	if (!ValidId)
	{
		LgiTrace("%s:%i - No current identity: accounts.len=%i\n", _FL, (int)Accounts.Length());
		
		// Find a valid account to be the identity...
		for (auto a: Accounts)
		{
			if (!a->Send.Disabled() &&
				a->Identity.IsValid())
			{
				break;
			}
		}
	}
	return a;
}

int ScribeWnd::GetCurrentIdentity()
{
	THREAD_UNSAFE(-1);

	LVariant i;
	if (GetOptions()->GetValue(OPT_CurrentIdentity, i))
		return i.CastInt32();
	else if (ScribeState != ScribeInitializing)
		LgiTrace("%s:%i - No OPT_CurrentIdentity set.\n", _FL);

	return -1;
}

void ScribeWnd::SetupAccounts()
{
	THREAD_UNSAFE();
	int i, CurrentIdentity = GetCurrentIdentity();

	if (StatusPanel)
	{
		StatusPanel->Empty();
	}

	#if !defined(COCOA) // FIXME
	LAssert(ReceiveMenu && PreviewMenu);
	#endif

	if (SendMenu)
		SendMenu->Empty();
	if (ReceiveMenu)
		ReceiveMenu->Empty();
	if (PreviewMenu)
		PreviewMenu->Empty();
	if (IdentityMenu)
		IdentityMenu->Empty();

	static bool Startup = true;

	bool ResetDefault = false;
	LArray<ScribeAccount*> Enabled;
	for (i=0; true; i++)
	{
		auto a = Startup ? new ScribeAccount(this, i) : Accounts[i];
		if (a)
		{
			if (i == 0)
				a->Create();
			
			a->Register(this);

			LVariant ReceiveName = a->Receive.Name();
			LVariant ReceiveServer = a->Receive.Server();
			LVariant SendServer = a->Send.Server();

			if (i == 0 ||
				ValidStr(ReceiveName.Str()) ||
				ValidStr(ReceiveServer.Str()) ||
				ValidStr(SendServer.Str()) )
			{
				a->Send.SendItem = SendItem;
				a->Receive.ReceiveItem = ReceiveItem;
				a->Receive.PreviewItem = PreviewItem;

				if (!Accounts.HasItem(a))
				{
					Accounts.Insert(a);
				}
				
				if (i) a->Create();				
				a->InitMenus();

				// Identity Menu Item
				auto AccountName = a->Send.Name();
				auto IdEmail = a->Identity.Email();
				auto IdName = a->Identity.Name();
				if (IdentityMenu &&
					ValidStr(IdEmail.Str()))
				{
					LString s;
					if (AccountName.Str())
						s = LString::Fmt("%s: ", AccountName.Str());
					if (IdName.Str())
						s += LString::Fmt("%s <%s>", IdName.Str(), IdEmail.Str());
					else
						s += LString::Fmt("<%s>", IdEmail.Str());

					a->SetMenuItem(IdentityMenu->AppendItem(s, IDM_IDENTITY_BASE+i+1, !a->Send.Disabled()));
					if (a->Send.Disabled())
					{
						a->SetCheck(false);
						if (i == CurrentIdentity)
							ResetDefault = true;
					}
					else
					{
						a->SetCheck(i == CurrentIdentity);
						Enabled[i] = a;
					}
				}
			}
			else
			{
				Accounts.Delete(a);
				DeleteObj(a);
			}
		}

		if (!a)
			break;
	}

	if ((ResetDefault || CurrentIdentity < 0) && Enabled.Length())
	{
		for (unsigned i=0; i<Enabled.Length(); i++)
		{
			if (Enabled[i])
			{
				Enabled[i]->SetCheck(true);
				
				LVariant v;
				GetOptions()->SetValue(OPT_CurrentIdentity, v = (int)i);
				break;					
			}
		}
	}

	Startup = false;

	if (ReceiveMenu &&
		i == 0)
	{
		ReceiveMenu->AppendItem(LLoadString(IDS_NO_ITEMS), 0, false);
	}

	if (StatusPanel)
	{
		StatusPanel->OnAccountListChange();
	}

	SetPulse(100);
}

//////////////////////////////////////////////////////////////////////////////
class LShutdown : public LDialog
{
	LTableLayout *Tbl = NULL;
	LTextLabel *Msg = NULL;
	LButton *ActionBtn = NULL;
	LButton *CancelBtn = NULL;
	bool Disconnected = false;
	uint64_t WaitTs = 0;
	
public:
	ScribeAccount *Waiting = NULL;
	LArray<ScribeAccount*> Accounts;

	LShutdown(LArray<ScribeAccount*> accounts)
	{
		Accounts = accounts;
		LRect r(	0,
					0,
					320 + LAppInst->GetMetric(LGI_MET_DECOR_X),
					70 + LAppInst->GetMetric(LGI_MET_DECOR_Y));
		SetPos(r);
		MoveToCenter();

		LString Str;
		Str.Printf("%s %s", AppName, LLoadString(IDS_EXITING));
		LView::Name(Str);

		AddView(Tbl = new LTableLayout(IDC_TABLE));
		
		auto c = Tbl->GetCell(0, 0);
		c->Add(Msg = new LTextLabel(-1, 10, 10, 300, -1, LLoadString(IDS_NONE)));

		c = Tbl->GetCell(0, 1);
		c->TextAlign(LCss::AlignCenter);
		c->Width("100%");
		c->Add(ActionBtn = new LButton(IDC_KILL, 0, 0, -1, -1, LLoadString(IDS_DISCONNECT)));
		c->Add(CancelBtn = new LButton(IDCANCEL, 0, 0, -1, -1, LLoadString(IDS_CANCEL)));

		if (ActionBtn)
			ActionBtn->Enabled(false);
	}

	~LShutdown()
	{
	}

	void OnCreate() override
	{
		SetPulse(100);
	}

	void OnPulse() override
	{
		if (Accounts.Length())
		{
			LArray<ScribeAccount*> Remove;
			for (auto a: Accounts)
			{
				if (!a->IsOnline())
				{
					Remove.Add(a);
					if (a == Waiting)
						Waiting = NULL;
				}
				else if (!Waiting)
				{
					Waiting = a;
					
					LString s;
					LVariant v = a->Receive.Name();
					s.Printf(LLoadString(IDS_WAITING_FOR), v.Str() ? v.Str() : LLoadString(IDS_NONE));
					Msg->Name(s);

					WaitTs = LCurrentTime();
					
					Disconnected = false;
					ActionBtn->Name(LLoadString(IDS_DISCONNECT));
					ActionBtn->Enabled(true);
				}
			}

			for (auto r: Remove)
				Accounts.Delete(r);
		}
		else
		{
			SetPulse();
			EndModal(false);
		}
	}

	int OnNotify(LViewI *Ctrl, const LNotification &n) override
	{
		switch (Ctrl->GetId())
		{
			case IDC_KILL:
			{
				if (Waiting)
				{
					WaitTs = LCurrentTime();
					if (!Disconnected)
					{
						Disconnected = true;
						ActionBtn->Name(LLoadString(IDS_KILL));
						Waiting->Disconnect();
					}
					else
					{
						Waiting->Kill();
						ActionBtn->Enabled(false);
					}
				}
				else
				{
					ActionBtn->Enabled(false);
				}
				break;
			}
			case IDCANCEL:
			{
				EndModal(false);
				break;
			}
		}

		return 0;
	}
};

bool ScribeWnd::OnRequestClose(bool OsShuttingDown)
{
	THREAD_UNSAFE(false);

	if (FolderTasks.Length() > 0)
	{
		LgiTrace("%s:%i - %i folder tasks still busy...\n", _FL, FolderTasks.Length());
		return false;
	}

	LString OnClose = LAppInst->GetConfig("Scribe.OnClose");
	if (!d->IngoreOnClose &&
		!OsShuttingDown &&
		OnClose.Equals("minimize"))
	{
		SetZoom(LZoomMin);
		return false;
	}

	Visible(false);

	if (ScribeState != ScribeRunning)
	{
		// Inside a folder load/unload or initialization
		// Tell the loader to quit out...
		ScribeState = ScribeExiting;

		// Leave now, we can exit when we're ready
		return false;
	}
	else if (IsSending() ||
			 GetActiveThreads() > 0)
	{
		// whack up a shutdown window
		LArray<ScribeAccount*> Online;
		for (auto acc: Accounts)
		{
			acc->OnEndSession();
			if (acc->IsOnline())
				Online.Add(acc);
		}

		if (Online.Length() > 0)
		{
			auto Dlg = new LShutdown(Online);
			Dlg->DoModal([this](auto dlg, auto id)
			{
				if (id)
				{
					ScribeState = ScribeExiting;
					LCloseApp();
				}
				else
				{
					ScribeState = ScribeRunning;
					Visible(true);
				}
			});
			return false; // At the very minimum the app has to wait for the user to respond.
		}
	}
	else
	{
		// End all sessions if any...
		for (auto i: Accounts)
		{
			i->OnEndSession();
		}
	}

	// close all the other top level windows
	while (ThingUi::All.Length() > 0)
	{
		ThingUi *Ui = ThingUi::All.First();
		if (!Ui->OnRequestClose(OsShuttingDown))
		{
			ScribeState = ScribeRunning;
			Visible(true);
			return false;
		}

		size_t Start = ThingUi::All.Length();
		Ui->Quit();
		if (ThingUi::All.Length() >= Start)
		{
			LAssert(0);
			break;
		}
	}

	SerializeState(GetOptions(), OPT_ScribeWndPos, false);
	LCloseApp();

	return LWindow::OnRequestClose(OsShuttingDown);
}

void ScribeWnd::DoOnTimer(LScriptCallback *c)
{
	THREAD_UNSAFE();

	if (!c)
		return;

	auto Now = LCurrentTime();
	if (c->PrevTs)
	{
		auto Since = Now - c->PrevTs; 
		double Sec = (double)Since / 1000.0;
		if (Sec >= c->fParam)
		{
			// Call the function
			c->PrevTs = Now;

			LVirtualMachine Vm;
			LScriptArguments Args(&Vm);
			Args.Add(new LVariant((LDom*)this));
			ExecuteScriptCallback(*c, Args);
		}
	}
	else
	{
		c->PrevTs = Now;
	}
}

void ScribeWnd::SetupScriptTimers()
{
	LArray<LScriptCallback*> Cb;
	if (!GetScriptCallbacks(LOnTimer, Cb))
		return;

	for (auto c: Cb)
	{
		if (!c->Func)
			continue;

		if (c->fParam == 0.0)
		{
			// Work out the period from 'Data'
			char *s = c->Data.Str();
			while (*s && IsWhite(*s)) s++;
			char *u = s;
			while (*u && !IsAlpha(*u)) u++;
			double v = atof(s);

			switch (*u)
			{
				case 's': case 'S': // seconds
					c->fParam = v;
					break;
				case 'm': case 'M': // mins
					c->fParam = v * LDateTime::MinuteLength;
					break;
				case 'h': case 'H': // hours
					c->fParam = v * LDateTime::HourLength;
					break;
				case 'd': case 'D': // days
					c->fParam = v * LDateTime::DayLength;
					break;
				default:
				{
					LgiTrace("%s:%i - Couldn't understand period '%s'\n", _FL, c->Data.Str());
					c->Data.Empty();
					break;
				}
			}

			if ((c->OnSecond = c->fParam < 60.0))
			{
				d->OnSecondTimerCallbacks.Add(c);
			}
		}
	}
}

void ScribeWnd::OnMinute()
{
	THREAD_UNSAFE();

	if (Folders.Length() == 0)
		return;

	// Check for calendar event alarms...
	Calendar::CheckReminders();

	// Check for any outgoing email that should be re-attempted...
	if (auto Outbox = GetFolder(FOLDER_OUTBOX))
	{
		bool Resend = false;

		for (auto t : Outbox->Items)
		{
			Mail *m = t->IsMail();
			if (m &&
				!TestFlag(m->GetFlags(), MAIL_SENT) &&
				TestFlag(m->GetFlags(), MAIL_READY_TO_SEND) &&
				m->SendAttempts > 0)
			{
				Resend = true;
				break;
			}			
		}

		if (Resend)
			Send();
	}

	LArray<LScriptCallback*> Cb;
	if (GetScriptCallbacks(LOnTimer, Cb))
	{
		for (auto c: Cb)
		{
			if (c->Func &&
				c->fParam >= 0.001 &&
				!c->OnSecond)
			{
				DoOnTimer(c);
			}
		}
	}
}

void ScribeWnd::OnHour()
{
	THREAD_UNSAFE();

	// Force time zone update in case of daylight savings change.
	LDateTime::SystemTimeZone(true);

	// Check if we need should be doing a software update check
	static bool InSoftwareCheck = false;
	if (!InSoftwareCheck)
	{
		char s[64];

		InSoftwareCheck = true;
		LVariant v;
		if (GetOptions()->GetValue(OPT_SoftwareUpdate, v) &&
			v.CastInt32())
		{
			LDateTime Now, Last;

			Now.SetFormat(GDTF_YEAR_MONTH_DAY);
			Last.SetFormat(Now.GetFormat());

			Now.SetNow();
					
			if (!GetOptions()->GetValue(OPT_SoftwareUpdateLast, v) ||
				!Last.Set(v.Str()))
			{
				// Record now as the last check point
				Now.Get(s, sizeof(s));
				GetOptions()->SetValue(OPT_SoftwareUpdateLast, v = s);
			}
			else if (GetOptions()->GetValue(OPT_SoftwareUpdateTime, v))
			{
				// Valid last check date/time.
				switch (v.CastInt32())
				{
					case 0: // Week
						Last.AddDays(7);
						break;
					case 1: // Month
						Last.AddMonths(1);
						break;
					case 2: // Year
						Last.AddMonths(12);
						break;
					default:
						LgiTrace("%s:%i - The option '%s' is not valid\n", _FL, OPT_SoftwareUpdateTime);
						return;
				}

				if (Last < Now)
				{
					// Save the last date for next time...
					Now.Get(s, sizeof(s));
					GetOptions()->SetValue(OPT_SoftwareUpdateLast, v = s);

					// Check for update now...
					GetOptions()->GetValue(OPT_SoftwareUpdateIncBeta, v);
					IsSoftwareUpToDate(	this,
										false,
										v.CastInt32() != 0,
										[this](auto s, auto Info)
										{
											if (s == SwOutOfDate)
												UpgradeSoftware(Info, this, true, [](auto status)
												{
													if (status)
														LCloseApp();
												});
										});
				}
			}
		}
		InSoftwareCheck = false;
	}
}

bool ScribeWnd::SaveDirtyObjects(int TimeLimitMs)
{
	THREAD_UNSAFE(false);

	bool Status = false;
	if (Thing::DirtyThings.Length() > 0)
	{
		static bool SavingObjects = false;

		if (!SavingObjects)
		{
			SavingObjects = true;

			LArray<int> WriteTimes;
			// ssize_t StartDirty = Thing::DirtyThings.Length();
			uint64 Start = LCurrentTime();
			for (unsigned i=0;
				i<ThingType::DirtyThings.Length() && LCurrentTime() - Start < TimeLimitMs;
				i++)
			{
				ThingType *t = ThingType::DirtyThings[i];
				if (t)
				{
					static int FailedWrites = 0;

					uint64 WriteStart = LCurrentTime();
					if (t->Save(NULL))
					{
						WriteTimes.Add((int) (LCurrentTime() - WriteStart));
						
						LAssert(!ThingType::DirtyThings.HasItem(t));
						Status = true;
					}
					else
					{
						LgiTrace("Failed to save thing type 0x%x\n", t->Type());

						FailedWrites++;
						if (FailedWrites > 2)
						{
							while (ThingType::DirtyThings.Length())
								ThingType::DirtyThings[0]->SetDirty(false);
							FailedWrites = 0;
						}
					}
				}
			}

			SavingObjects = false;
			
			/*
			if (Status)
			{
				LStringPipe p;
				p.Print("WriteTimes: ");
				for (unsigned i=0; i<WriteTimes.Length(); i++)
					p.Print("%i, ", WriteTimes[i]);
				LAutoString a(p.NewStr());
				#ifdef WINDOWS
				OutputDebugStringA(a);
				#else
				printf("%s", a.Get());
				#endif
			}
			*/
		}
	}

	return Status;
}

void ScribeWnd::OnPulse()
{
	THREAD_UNSAFE();

	if (ScribeState == ScribeRunning)
	{
		OnIdle();
		
		uint64 Now = LCurrentTime();
		if (Now - d->LastTs >= 1000)
		{
			d->LastTs = Now;
			OnPulseSecond();
		}
	}
}

void ScribeWnd::OnPulseSecond()
{
	THREAD_UNSAFE();

	#if PROFILE_ON_PULSE
	LProfile Prof("NewMailLst handling");
	Prof.HideResultsIfBelow(50);
	#endif
	
	if (Mail::NewMailLst.Length() > 0)
	{
		LVariant Blink;
		if (GetOptions()->GetValue(OPT_BlinkNewMail, Blink) && Blink.CastInt32())
		{
			d->TrayIcon->Value((d->TrayIcon->Value() == TRAY_ICON_MAIL) ? TRAY_ICON_NONE : TRAY_ICON_MAIL);
		}
	}
	else
	{
		bool Err = false;
		for (auto a: Accounts)
		{
			if (!a->Receive.GetStatus() ||
				!a->Send.GetStatus())
			{
				Err = true;
			}
		}
		
		d->TrayIcon->Value(Err ? TRAY_ICON_ERROR : TRAY_ICON_NORMAL);
	}
	
	#if PROFILE_ON_PULSE
	Prof.Add("StatusPanel handling");
	#endif

	if (StatusPanel)
	{
		StatusPanel->OnPulse();
	}

	#if PROFILE_ON_PULSE
	Prof.Add("OnXXXX handling");
	#endif
	
	LDateTime Now;
	Now.SetNow();
	if (d->LastMinute != Now.Minutes()) // Check every minute...
	{
		d->LastMinute = Now.Minutes();
		OnMinute();
	}
	if (d->LastHour != Now.Hours()) // Check every hour...
	{
		d->LastHour = Now.Hours();
		OnHour();
	}
	{
		// These timers need to be checked every second...
		for (auto c: d->OnSecondTimerCallbacks)
			DoOnTimer(c);
	}

	#if PROFILE_ON_PULSE
	Prof.Add("Instance handling");
	#endif

	if (Ipc && Ipc->OnPulse())
	{
		OnCommandLine();
		if (GetZoom() == LZoomMin)
			SetZoom(LZoomNormal);
		Visible(true);
	}

	#if PROFILE_ON_PULSE
	Prof.Add("PreviewPanel handling");
	#endif

	if (PreviewPanel)
	{
		PreviewPanel->OnPulse();
	}
}

void ScribeWnd::AddFolderToMru(char *FileName)
{
	THREAD_UNSAFE();

	if (FileName)
	{
		// read MRU
		List<char> Files;
		int i;
		for (i=0; i<10; i++)
		{
			char Key[32];
			LVariant f;
			sprintf_s(Key, sizeof(Key), "FolderMru.%i", i);
			if (GetOptions()->GetValue(Key, f))
			{
				Files.Insert(NewStr(f.Str()));
				GetOptions()->DeleteValue(Key);
			}
		}

		// remove FileName if present
		for (auto f: Files)
		{
			if (_stricmp(f, FileName) == 0)
			{
				Files.Delete(f);
				DeleteArray(f);
				break;
			}
		}

		// insert FileName at the start of the list
		Files.Insert(NewStr(FileName));

		// write MRU
		for (i=0; i<10; i++)
		{
			char *n = Files.ItemAt(i);
			if (n)
			{
				char Key[32];
				sprintf_s(Key, sizeof(Key), "FolderMru.%i", i);
				LVariant f;
				GetOptions()->SetValue(Key, f = n);
			}
			else break;
		}

		// Clean up
		Files.DeleteArrays();
	}
}

bool ScribeWnd::CleanFolders(ScribeFolder *f)
{
	THREAD_UNSAFE(false);

	if (!f)
		return false;

	if (f->Select())
	{
		f->SerializeFieldWidths();
	}

	for (ScribeFolder *c = f->GetChildFolder(); c; c = c->GetNextFolder())
	{
		CleanFolders(c);
	}

	return true;
}

void ScribeWnd::OnFolderChanged(LDataFolderI *folder)
{
	THREAD_SAFE();
}

bool ScribeWnd::OnFolderTask(LEventTargetI *Ptr, bool Add)
{
	THREAD_UNSAFE(false);

	if (Add)
	{
		if (FolderTasks.HasItem(Ptr))
		{
			LAssert(!"Can't add task twice.");
			return false;
		}
		FolderTasks.Add(Ptr);
		return true;
	}
	else
	{
		if (!FolderTasks.HasItem(Ptr))
		{
			LAssert(!"Item not part of task list.");
			return false;
		}
		FolderTasks.Delete(Ptr);
		return true;
	}
}

LMailStore *ScribeWnd::GetDefaultMailStore()
{
	THREAD_UNSAFE(NULL);
	LMailStore *Def = 0;

	for (unsigned i=0; i<Folders.Length(); i++)
	{
		if (Folders[i].IsOk())
		{
			if (Folders[i].Default || !Def)
			{
				Def = &Folders[i];
			}
			else if (Def)
			{
				if (Folders[i].Priority() > Def->Priority())
				{
					Def = &Folders[i];
				}
			}
		}
	}

	return Def;
}

bool HasMailStore(LXmlTag *MailStores, char *Name)
{
	for (auto t : MailStores->Children)
	{
		char *StoreName = t->GetAttr(OPT_MailStoreName);
		if (StoreName && Name && !_stricmp(StoreName, Name))
			return true;
	}
	return false;
}

LDataStoreI *ScribeWnd::CreateDataStore(const char *_Full, bool CreateIfMissing)
{
	THREAD_UNSAFE(NULL);

	LString Full(_Full);
	auto Ext = LGetExtension(Full);
	if (Ext)
	{
		if (!_stricmp(Ext, "mail2"))
		{
			LgiMsg(this, LLoadString(IDS_MAIL2_DEPRECATED), AppName, MB_OK, Full.Get());
		}
		else if (!_stricmp(Ext, "mail3"))
		{
			return OpenMail3(Full, this, CreateIfMissing);
		}
		else if (!_stricmp(Ext, "sqlite"))
		{
			LTrimDir(Full);
			return OpenMail3(Full, this, CreateIfMissing);
		}
		else
		{
			LgiTrace("%s:%i - Not a valid mail store extension: %s\n", _FL, Full.Get());
			LAssert(!"Not a valid mail store extension.");
		}
	}
	else LgiTrace("%s:%i - No extension for CreateDataStore: %s\n", _FL, Full.Get());

	return NULL;
}

bool ScribeWnd::ProcessFolder(LDataStoreI *Store, int StoreIdx, char *StoreName)
{
	THREAD_UNSAFE(false);

	if (Store->GetInt(FIELD_VERSION) == 0)
	{
		// version error
		LgiMsg(this, LLoadString(IDS_ERROR_FOLDERS_VERSION), AppName, MB_OK, 0, Store->GetInt(FIELD_VERSION));
		return false;
	}

	if (Store->GetInt(FIELD_READONLY))
	{
		LgiMsg(this, LLoadString(IDS_ERROR_READONLY_FOLDERS), AppName);
	}

	// get root item
	auto Root = Store->GetRoot();
	if (!Root)
		return false;

	auto &MailStore = Folders[StoreIdx];
	auto Folder = new ScribeFolder;
	if (Folder)
	{
		MailStore.SetRoot(Folder);
		Folder->App = this;
		Folder->SetObject(Root, false, _FL);

		Root->SetStr(FIELD_FOLDER_NAME, StoreName);
		Root->SetInt(FIELD_FOLDER_TYPE, MAGIC_NONE);
	}

	#ifdef TEST_OBJECT_SIZE
	// debug/repair code
	if (Root->StoreSize != Root->Sizeof())
	{
		SizeErrors[0]++;
		Root->StoreSize = Root->Sizeof();
		if (Root->Object)
		{
			Root->Object->StoreDirty = true;
		}
	}
	#endif

	// Insert the root object and then...
	Tree->Insert(Folder);

	// Recursively load the rest of the tree
	{
		// LProfile p("Loadfolders");
		Folder->LoadFolders();
	}
				
	// This forces a re-pour to re-order the folders according to their
	// sort settings.
	Tree->UpdateAllItems();

	if (ScribeState != ScribeExiting)
	{
		// Show the tree
		Folder->Expanded(Folders[StoreIdx].Expanded);

		// Checks the folders for a number of required objects
		// and creates them if required
		auto StoreType = Store->GetInt(FIELD_STORE_TYPE);
		if (StoreType == Store3Sqlite)
			Validate(&Folders[StoreIdx]);
		else if (StoreType < 0)
			LAssert(!"Make sure you impl the FIELD_STORE_TYPE field in the store.");

					
		// FIXME
		// AddFolderToMru(Full);
	}

	return true;
}

#include "LoadMailStoreState.h"

void ScribeWnd::LoadMailStores(std::function<void(bool)> Callback)
{
	THREAD_UNSAFE();

	if (auto s = new LoadMailStoreState(this, Callback))
		s->Start();
}

void ScribeWnd::LoadFolders(std::function<void(bool)> Callback)
{
	THREAD_UNSAFE();

	AppState PrevState = ScribeState;
	ScribeState = ScribeLoadingFolders;

	// Setup Mailstores tag
	{
		LXmlTag *MailStores = GetOptions()->LockTag(OPT_MailStores, _FL);
		if (!MailStores)
		{
			// Check if we can upgrade the old folder tag
			char n[32];
			sprintf_s(n, sizeof(n), "%s-Folders", LGetOsName());
			LVariant OldFolders;
			GetOptions()->GetValue(n, OldFolders);

			// Create mail store element..
			GetOptions()->CreateTag(OPT_MailStores);
			if ((MailStores = GetOptions()->LockTag(OPT_MailStores, _FL)))
			{
				if (OldFolders.Str())
				{
					LXmlTag *Store = MailStores->CreateTag(OPT_MailStore);
					if (Store)
					{
						char Opts[MAX_PATH_LEN];
						LMakePath(Opts, sizeof(Opts), GetOptions()->GetFile(), "..");
						auto Rel = LMakeRelativePath(Opts, OldFolders.Str());

						Store->SetAttr(OPT_MailStoreLocation, Rel ? Rel.Get() : OldFolders.Str());

						// No need to ask the user for a store name, it'll be
						// asked later in this method anyway...

						// Leave the old folder tag in the xml in case the user
						// downgrades to v1.xx
					}
				}
			}
		}

		GetOptions()->Unlock();
		if (!MailStores)
		{
			if (Callback)
				Callback(false);
			return;
		}
	}

	// Set loading flags
	CmdSend.Enabled(false);
	CmdReceive.Enabled(false);
	CmdPreview.Enabled(false);

	LoadMailStores([this, PrevState, Callback](auto Status)
	{
		if (Tree)
		{
			for (auto a: Accounts)
			{
				if (!a->Receive.Disabled() && a->Receive.IsPersistant())
					a->Receive.Connect(0, false);
			}
		}

		using BoolFn = std::function<void(bool)>;
		auto FinishLoad = new BoolFn
		(
			[this, PrevState, Callback](bool Status)
			{
				if (ScribeState == ScribeExiting)
				{
					LCloseApp();
				}
				else
				{
					d->FoldersLoaded = true;
					PostEvent(M_SCRIBE_LOADED);
				}

				if (ScribeState == ScribeExiting)
					LCloseApp();
				ScribeState = PrevState;

				if (Callback)
					Callback(Status);
			}
		);

		if (Folders.Length() == 0)
		{
			auto Dlg = new ScribeFolderDlg(this);
			Dlg->DoModal([this, Dlg, FinishLoad, Callback, &Status](auto dlg, auto id)
			{
				if (id == IDOK)
				{
					bool CreateMailStore = false;

					if (Dlg->Create)
					{
						// create folders
						if (LFileExists(Dlg->FolderFile))
						{
							if (LgiMsg(this, LLoadString(IDS_ERROR_FOLDERS_ALREADY_EXIST), AppName, MB_YESNO) == IDYES)
								CreateMailStore = true;
							else
								LgiMsg(this, LLoadString(IDS_ERROR_WONT_OVERWRITE_FOLDERS), AppName);
						}
						else if ((Status = CreateFolders(Dlg->FolderFile)))
							CreateMailStore = true;
					}
					else
						CreateMailStore = true;

					if (CreateMailStore)
					{
						LXmlTag *MailStores = GetOptions()->LockTag(OPT_MailStores, _FL);
						if (MailStores)
						{
							LXmlTag *Store = MailStores->CreateTag(OPT_MailStore);
							if (Store)
							{
								char p[MAX_PATH_LEN];
								LMakePath(p, sizeof(p), GetOptions()->GetFile(), "..");
								auto RelPath = LMakeRelativePath(p, Dlg->FolderFile);
								Store->SetAttr(OPT_MailStoreLocation, RelPath ? RelPath.Get() : Dlg->FolderFile.Get());
							}
							GetOptions()->Unlock();

							LoadMailStores(NULL);
						}
					}
				}

				if (id)
					(*FinishLoad)(Status);
				else if (Callback)
					Callback(false);
				delete FinishLoad;
			});
		}
		else
		{
			(*FinishLoad)(Status);
			delete FinishLoad;
		}
	});
}

bool ScribeWnd::UnLoadFolders()
{
	THREAD_UNSAFE(false);

	if (FolderTasks.Length() > 0 ||
		ScribeState == ScribeLoadingFolders)
	{
		// Um, we can't unload folders right now 
		// something is already happening...
		return false;
	}

	AppState PrevState = ScribeState;
	ScribeState = ScribeUnloadingFolders;

	OnSelect();

	if (MailList)
	{
		if (auto Container = MailList->GetContainer())
		{
			// save folder settings
			Container->SerializeFieldWidths();
		}

		MailList->SetContainer(NULL);
		MailList->RemoveAll();
	}

	int Error = 0;
	while (Thing::DirtyThings.Length() > 0)
	{
		if (!SaveDirtyObjects())
		{	
			Error++;
			LgiTrace("%s:%i - SaveDirtyObjects failed.\n", _FL);

			if (Error > 100)
			{
				// I think we're stuck...
				return false;
			}
		}
	}

	// Unload IMAP folders...
	for (auto a: Accounts)
	{
		if (!a->Receive.Disabled() &&
			a->Receive.IsPersistant())
		{
			a->Receive.Disconnect();
		}
	}

	if (GetOptions())
	{
		// Unload local folders...
		auto MailStores = GetOptions()->LockTag(OPT_MailStores, _FL);

		for (size_t i=0; i<Folders.Length(); i++)
		{
			// Save the expanded state...
			if (Folders[i].GetRoot())
			{
				bool Expanded = Folders[i].GetRoot()->Expanded();

				for (auto ms: MailStores->Children)
				{
					auto StoreName = ms->GetAttr(OPT_MailStoreName);
					if (Folders[i].Name.Equals(StoreName))
					{
						ms->SetAttr(OPT_MailStoreExpanded, Expanded);
						break;
					}
				}
			}

			Folders[i].DeleteRoot();
			Folders[i].Store.Reset();
		}

		if (MailStores)
		{
			GetOptions()->Unlock();
			MailStores = NULL;
		}
	}

	Folders.Length(0);
	d->FoldersLoaded = false;

	if (ScribeState == ScribeExiting)
		LCloseApp();
	ScribeState = PrevState;

	return true;
}

void ScribeWnd::BuildDynMenus()
{
	THREAD_UNSAFE();

	if (MailMenu)
	{
		LString SendMail = LLoadString(IDS_SEND_MAIL);
		LString ReceiveMail = LLoadString(IDS_RECEIVE_MAIL);
		LString PreviewMail = LLoadString(IDS_PREVIEW_ON_SERVER);
		auto ReceiveAll = LLoadString(IDS_RECEIVE_ALL_ACCOUNTS);

		if (!CmdReceive.MenuItem && ReceiveAll)
			CmdReceive.MenuItem = MailMenu->AppendItem(ReceiveAll, IDM_RECEIVE_ALL, true);

		if (!SendMenu && SendMail)
		{
			auto s = SendMail.SplitDelimit("\t");
			SendMenu = MailMenu->AppendSub(s[0]);
		}

		if (!ReceiveMenu && ReceiveMail)
		{
			auto s = ReceiveMail.SplitDelimit("\t");
			ReceiveMenu = MailMenu->AppendSub(s[0]);
		}

		if (!PreviewMenu && PreviewMail)
		{
			auto s = PreviewMail.SplitDelimit("\t");
			PreviewMenu = MailMenu->AppendSub(s[0]);
		}
	}

	if (!NewTemplateMenu)
	{
		auto i = Menu->FindItem(IDM_NO_TEMPLATES);
		if (i)
		{
			NewTemplateMenu = i->GetParent();
		}
	}
	if (NewTemplateMenu)
	{
		NewTemplateMenu->Empty();

		int d = 0;
		ScribeFolder *Templates = GetFolder(FOLDER_TEMPLATES, NULL, true);
		if (Templates)
		{
			Templates->LoadThings();

			for (auto i: Templates->Items)
			{
				Mail *m = i->IsMail();
				if (m)
				{
					NewTemplateMenu->AppendItem(m->GetSubject()
													?
													m->GetSubject()
													:
													"(no subject)",
												IDM_NEW_FROM_TEMPLATE + d,
												true);
					d++;
				}
			}

			if (d == 0)
			{
				NewTemplateMenu->AppendItem(LLoadString(IDS_NO_ITEMS_IN_FOLDER), -1, false);
			}
		}
		else
		{
			NewTemplateMenu->AppendItem(LLoadString(IDS_NO_TEMPLATES), -1, false);
		}
	}
}

int ScribeWnd::GetToolbarHeight()
{
	return (Commands) ? MAX(Commands->Y()-1, 20) : 20;
}

LToolBar *ScribeWnd::LoadToolbar(LViewI *Parent, const char *File, LAutoPtr<LImageList> &Img)
{
	THREAD_UNSAFE(NULL);

	if (!Img)
		Img.Reset(LLoadImageList(File));
	if (!Img)
	{
		LAssert(!"Missing image resource.");
		return NULL;
	}

	LToolBar *Tools = NULL;
	if (Img)
	{
		Tools = new LToolBar;
		if (Tools)
			Tools->SetImageList(Img, Img->TileX(), Img->TileY(), false);
	}
	else
	{
		Tools = LgiLoadToolbar(Parent, File);
	}

	if (Tools)
	{
		LVariant SizeAdj;
		LFont *f = Tools->GetFont();
		if (f)
		{
			if (GetOptions()->GetValue(OPT_UiFontSize, SizeAdj))
			{
				SizeAdj.Cast(GV_INT32);
				SizeAdj.Value.Int -= 2;
				f->PointSize(f->PointSize()+SizeAdj.Value.Int);
			}
		}

		Tools->GetCss(true)->BorderSpacing(LCss::Len(LCss::LenPx, SCRIBE_TOOLBAR_BORDER_SPACING_PX));
		Tools->TextLabels(ShowToolbarText());
	}

	return Tools;
}

void ScribeWnd::LoadTitleListPane()
{
	LAutoPtr<LView> v(new DynamicHtml(this, "title.html"));
	SetListPane(v);
}

void ScribeWnd::SetListPane(LAutoPtr<LView> listPane)
{
	THREAD_UNSAFE();

	ListPane = listPane;

	auto ThingLst = dynamic_cast<ThingList*>(ListPane.Get());
	auto Html     = dynamic_cast<DynamicHtml*>(ListPane.Get());
	if (!ThingLst)
	{
		DeleteObj(SearchView);
		if (MailList)
			MailList->RemoveAll();
	}

	ListPane->Sunken(SUNKEN_CTRL);

	// Set either 'MailList' or 'TitlePage'
	if (ThingLst)
	{
		MailList = ThingLst;
		DeleteObj(TitlePage);
		if (GetCtrlValue(IDM_ITEM_FILTER))
			OnCommand(IDM_ITEM_FILTER, 0, NULL);
	}
	else
	{
		DeleteObj(MailList);
		TitlePage = Html;
	}
	
	SetLayout();
}

bool ScribeWnd::SetItemPreview(LView *v)
{
	THREAD_UNSAFE(false);

	v->Sunken(SUNKEN_CTRL);
	if (d->SubSplit->IsAttached())
	{
		if (d->LastLayout == 2)
		{
			Splitter->SetViewAt(1, v);
		}
		else
		{
			d->SubSplit->SetViewAt(1, v);
		}

		return true;
	}

	return false;
}

ScribeWnd::LayoutMode ScribeWnd::GetEffectiveLayoutMode()
{
	THREAD_UNSAFE(OptionsLayout);

	LVariant Mode;
	GetOptions()->GetValue(OPT_LayoutMode, Mode);
	ScribeFolder *Cur = GetCurrentFolder();
	
	if (Cur &&
		Cur->IsItem() &&
		Cur->IsItem()->IsRoot())
	{
		Mode = FoldersAndList;
	}
	else if (Mode.CastInt32() == 0)
	{
		Mode = FoldersListAndPreview;
	}	
	
	return (LayoutMode) Mode.CastInt32();
}

void ScribeWnd::SetLayout(LayoutMode Mode)
{
	THREAD_UNSAFE();

	if (Mode > 0)
	{
		LVariant v;
		GetOptions()->SetValue(OPT_LayoutMode, v = (int)Mode);
	}

	Mode = GetEffectiveLayoutMode();

	if (!Splitter)
		return;

	bool JustPreviewPane =	(Mode == FoldersAndList && d->LastLayout == FoldersListAndPreview) ||
							(Mode == FoldersListAndPreview && d->LastLayout == FoldersAndList);

	// If 'Content' gets attached to a view, then 'ListPane' should be released.
	LView *Content = NULL;
	if (TitlePage)
		Content = TitlePage;
	else if (MailList)
		Content = MailList;

	if (JustPreviewPane)
	{
		// Optimized path for hide/show the preview pane that doesn't destroy the tree
		// control and cause it to lose focus... otherwise we can't set it's focus due
		// to some weird windows interaction.
		switch (Mode)
		{
			default:
			case FoldersListAndPreview:
			{
				if (Content)
					Content->Sunken(SUNKEN_CTRL);
				if (PreviewPanel)
					PreviewPanel->Sunken(SUNKEN_CTRL);

				Splitter->SetViewAt(1, d->SubSplit);
				d->SubSplit->SetVertical(true);
				
				int Idx = 0;
				if (SearchView)
					d->SubSplit->SetViewAt(Idx++, SearchView);				
				if (d->SubSplit->SetViewAt(Idx++, Content))
					ListPane.Release(); // Something else now owns the list pane...
				d->SubSplit->SetViewAt(Idx++, PreviewPanel);
				break;
			}
			case FoldersAndList:
			{
				if (Content)
					Content->Sunken(SUNKEN_CTRL);
				
				if (SearchView)
				{
					#if LGI_VIEW_HANDLE
					if (!d->SubSplit->Handle())
						Splitter->SetViewAt(1, d->SubSplit);
					#endif
					d->SubSplit->SetVertical(true);
					Splitter->SetViewAt(1, d->SubSplit);

					int Idx = 0;
					if (SearchView)
						d->SubSplit->SetViewAt(Idx++, SearchView);				
					if (d->SubSplit->SetViewAt(Idx++, Content))
						ListPane.Release(); // Something else now owns the list pane...
				}
				else
				{
					d->SubSplit->Detach();
					if (Splitter->SetViewAt(1, Content))
						ListPane.Release(); // Something else now owns the list pane...
				}
				break;
			}
		}
	}
	else
	{		
		if (Tree)
			Tree->Sunken(SUNKEN_CTRL);
		if (Content)
			Content->Sunken(SUNKEN_CTRL);
		if (PreviewPanel)
			PreviewPanel->Sunken(SUNKEN_CTRL);
		switch (Mode)
		{
			default:
			case FoldersListAndPreview:
			{
				Splitter->SetVertical(false);
				d->SubSplit->SetVertical(true);
				Splitter->SetViewAt(0, Tree);
				Splitter->SetViewAt(1, d->SubSplit);

				int Idx = 0;
				if (SearchView)
					d->SubSplit->SetViewAt(Idx++, SearchView);
				if (d->SubSplit->SetViewAt(Idx++, Content))
					ListPane.Release(); // Something else now owns the list pane...
				d->SubSplit->SetViewAt(Idx++, PreviewPanel);

				DeleteObj(d->SearchSplit);
				break;
			}
			case PreviewOnBottom:
			{
				Splitter->SetVertical(true);
				d->SubSplit->SetVertical(false);
				Splitter->SetViewAt(0, d->SubSplit);
				Splitter->SetViewAt(1, PreviewPanel);

				d->SubSplit->SetViewAt(0, Tree);
				if (SearchView)
				{
					if (!d->SearchSplit)
						d->SearchSplit = new LBox;
					d->SubSplit->SetViewAt(1, d->SearchSplit);
					d->SearchSplit->SetVertical(true);
					d->SearchSplit->SetViewAt(0, SearchView);
					if (d->SearchSplit->SetViewAt(1, Content))
						ListPane.Release(); // Something else now owns the list pane...
				}
				else
				{
					if (d->SubSplit->SetViewAt(1, Content))
						ListPane.Release(); // Something else now owns the list pane...
					DeleteObj(d->SearchSplit);
				}
				break;
			}
			case FoldersAndList:
			{
				Splitter->SetVertical(false);
				Splitter->SetViewAt(0, Tree);
				
				if (SearchView)
				{
					d->SubSplit->SetVertical(true);
					Splitter->SetViewAt(1, d->SubSplit);
					d->SubSplit->SetViewAt(0, SearchView);
					if (d->SubSplit->SetViewAt(1, Content))
						ListPane.Release(); // Something else now owns the list pane...
				}
				else
				{
					d->SubSplit->Detach();
					if (Splitter->SetViewAt(1, Content))
						ListPane.Release(); // Something else now owns the list pane...
				}
				DeleteObj(d->SearchSplit);
				break;
			}
			case ThreeColumn:
			{
				Splitter->SetVertical(false);
				Splitter->SetViewAt(0, Tree);
				
				if (SearchView)
				{
					d->SubSplit->SetVertical(true);
					Splitter->SetViewAt(1, d->SubSplit);
					d->SubSplit->SetViewAt(0, SearchView);
					d->SubSplit->SetViewAt(1, Content);
				}
				else
				{
					d->SubSplit->Detach();
					Splitter->SetViewAt(1, Content);
				}
				
				Splitter->SetViewAt(2, PreviewPanel);
				DeleteObj(d->SearchSplit);
				break;
			}
		}
	}

	if (!SearchView)
	{
		LVariant Pos = 200;
		GetOptions()->GetValue(OPT_SplitterPos, Pos);
		if (Pos.CastInt32() < 10)
			Pos = 200;
		Splitter->Value(Pos.CastInt32());

		LRect r = Splitter->GetPos();
		r.x2++;
		Splitter->SetPos(r);

		if (d->SubSplit->IsAttached())
		{
			Pos = 200;
			GetOptions()->GetValue(OPT_SubSplitPos, Pos);
			if (Pos.CastInt32() < 10)
				Pos = 200;
			d->SubSplit->Value(Pos.CastInt32());
		}
	}
	
	PourAll();
	d->LastLayout = Mode;
}

void ScribeWnd::SetupUi()
{
	THREAD_UNSAFE();

	// Show the window
	if (!SerializeState(GetOptions(), OPT_ScribeWndPos, true))
	{
		LRect r(0, 0, 1023, 767);
		SetPos(r);
		MoveToCenter();
	}

	// Main toolbar
	Commands = LoadToolbar(this, GetResourceFile(ResToolbarFile), ToolbarImgs);
	if (Commands)
	{
		Commands->Attach(this);
		#ifdef MAC
		Commands->Raised(false);
		#else
		Commands->Raised(RAISED_LOOK);
		#endif

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_NEW_EMAIL)), IDM_NEW_EMAIL, TBT_PUSH, true, IMG_NEW_MAIL);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_NEW_CONTACT)), IDM_NEW_CONTACT, TBT_PUSH, true, IMG_NEW_CONTACT);
		Commands->AppendSeparator();

		CmdSend.ToolButton = Commands->AppendButton(RemoveAmp(LLoadString(IDS_SEND)), IDM_SEND_MAIL, TBT_PUSH, true, IMG_SEND);
		Commands->AppendButton("+", IDM_RECEIVE_AND_SEND, TBT_PUSH, true, -2);
		CmdReceive.ToolButton = Commands->AppendButton(RemoveAmp(LLoadString(IDS_RECEIVE)), IDM_RECEIVE_MAIL, TBT_PUSH, true, IMG_RECEIVE);
		CmdPreview.ToolButton = Commands->AppendButton(RemoveAmp(LLoadString(IDS_PREVIEW)), IDM_PREVIEW_POP3, TBT_PUSH, true, IMG_PREVIEW);
		Commands->AppendSeparator();

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_DELETE)), IDM_DELETE, TBT_PUSH, true, IMG_TRASH);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_SPAM)), IDM_DELETE_AS_SPAM, TBT_PUSH, true, IMG_DELETE_SPAM);
		Commands->AppendSeparator();

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_REPLY)), IDM_REPLY, TBT_PUSH, true, IMG_REPLY);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_REPLYALL)), IDM_REPLY_ALL, TBT_PUSH, true, IMG_REPLY_ALL);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_FORWARD)), IDM_FORWARD, TBT_PUSH, true, IMG_FORWARD);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_BOUNCE)), IDM_BOUNCE, TBT_PUSH, true, IMG_BOUNCE);
		Commands->AppendSeparator();

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_PRINT)), IDM_PRINT, TBT_PUSH, true, IMG_PRINT);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_CALENDAR)), IDM_CALENDAR, TBT_PUSH, true, IMG_CALENDAR);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_ITEM_FILTER)), IDM_ITEM_FILTER, TBT_TOGGLE, true, IMG_SEARCH);
		Commands->AppendButton(RemoveAmp(LLoadString(IDS_THREAD)), IDM_THREAD, TBT_TOGGLE, true, IMG_THREADS);
		d->ShowConsoleBtn = Commands->AppendButton(RemoveAmp(LLoadString(IDS_SHOW_CONSOLE)), IDM_SHOW_CONSOLE, TBT_PUSH, true, IMG_CONSOLE_NOMSG);
		Commands->AppendSeparator();

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_HELP)), IDM_HELP, TBT_PUSH, true, IMG_HELP);

		Commands->Customizable(GetOptions(), OPT_ScribeWndToolbar);
		if (d->ScriptToolbar.Reset(new LScriptUi(Commands)))
			d->ScriptToolbar->SetupCallbacks(this, 0, 0, LApplicationToolbar);
		PourAll();
	}

	CmdSend.Enabled(false);
	CmdReceive.Enabled(false);

	// Preview and status windows
	PreviewPanel = new LPreviewPanel(this);
	StatusPanel = new AccountStatusPanel(this, ImageList);
	if (PreviewPanel &&
		StatusPanel)
	{
		#ifdef MAC
		StatusPanel->Raised(false);
		#else
		StatusPanel->Raised(RAISED_LOOK);
		#endif
		StatusPanel->Attach(this);

		PourAll();
	}

	// Splitter window, for folders and item list
	Tree = new MailTree(this);
	if (ImageList)
	{
		Tree->AskImage(true);
		Tree->SetImageList(ImageList, false);
	}

	d->SubSplit = new LBox;
	
	Splitter = new LBox;
	if (Splitter)
	{
		Splitter->Attach(this);
		#ifdef MAC
		Splitter->Raised(false);
		#else
		Splitter->Raised(RAISED_LOOK);
		#endif

		SetLayout();
	}


	#if WINNATIVE
	d->TrayIcon->Load(MAKEINTRESOURCE(IDI_SMALL));
	d->TrayIcon->Load(MAKEINTRESOURCE(IDI_ERR));
	d->TrayIcon->Load(MAKEINTRESOURCE(IDI_MAIL));
	d->TrayIcon->Load(MAKEINTRESOURCE(IDI_BLANK));
	#else
	d->TrayIcon->Load(_T("tray_small.png"));
	d->TrayIcon->Load(_T("tray_error.png"));
	d->TrayIcon->Load(_T("tray_mail.png"));
	d->TrayIcon->Load(_T("tray_empty.png"));
	#endif
	
	LStringPipe s(256);
	LVariant UserName;
	
	s.Print("%s", AppName);
	if (GetOptions()->GetValue(OPT_UserName, UserName))
	{
		s.Print(" [%s]", UserName.Str());
	}
	auto AppTitle = s.NewLStr();
	d->TrayIcon->Name(AppTitle);
	Name(AppTitle);

	d->TrayIcon->Value(TRAY_ICON_NORMAL);
	d->TrayIcon->Visible(true);

	auto Item = Menu->FindItem(IDM_SCRIPTING_CONSOLE);
	if (Item)
	{
		LVariant v;
		if (GetOptions()->GetValue(OPT_ShowScriptConsole, v))
		{
			Item->Checked(v.CastInt32() != 0);
			LScribeScript::Inst->ShowScriptingWindow(Item->Checked());
		}
	}

	if (Tree)
	{
		Tree->Focus(true);
	}
}

Thing *ScribeWnd::CreateItem(int Type, ScribeFolder *Folder, bool Ui)
{
	THREAD_UNSAFE(NULL);

	auto FolderStore = Folder && Folder->GetObject() ? Folder->GetObject()->GetStore() : NULL;
	auto DefaultStore = GetDefaultMailStore();
	LDataStoreI *Store = FolderStore ? FolderStore : (DefaultStore ? DefaultStore->Store.Get() : NULL);
	if (!Store)
	{
		LAssert(!"no store");
		LgiTrace("%s:%i - No store for creating calendar object.\n", _FL);
		return NULL;
	}

	auto Obj = Store->Create(Type);
	if (!Obj)
	{
		LAssert(!"create failed");
		LgiTrace("%s:%i - store failed to create object.\n", _FL);
		return NULL;
	}

	#define HANDLE_CREATE_ITEM(Magic, Type)					\
		case Magic:											\
		{													\
			auto o = new Type(this, Obj);					\
			if (!o)											\
			{												\
				LgiTrace("%s:%i - Alloc failed.\n", _FL);	\
				break;										\
			}												\
			if (Folder) o->SetParentFolder(Folder);			\
			if (Ui) o->DoUI();								\
			return o;										\
		}


	switch ((uint32_t)Type)
	{
		case MAGIC_MAIL:
		{
			// create a new mail message
			auto m = new Mail(this, Obj);
			if (!m)
			{
				LgiTrace("%s:%i - Alloc failed.\n", _FL);
				break;
			}

			if (!m->GetObject())
			{
				m->DecRef();
				return 0;
			}

			m->OnCreate();

			if (Folder)
				m->SetParentFolder(Folder);

			if (Ui)
				m->DoUI();

			return m;
		}
		HANDLE_CREATE_ITEM(MAGIC_CONTACT, Contact)
		HANDLE_CREATE_ITEM(MAGIC_CALENDAR, Calendar)
		HANDLE_CREATE_ITEM(MAGIC_FILTER, Filter)
		HANDLE_CREATE_ITEM(MAGIC_GROUP, ContactGroup)
		default:
			LAssert(!"Unhandled object type.");
			break;
	}

	return NULL;
}

void ScribeWnd::OnPaint(LSurface *pDC)
{
	THREAD_UNSAFE();

	LCssTools Tools(this);
	auto c = GetClient();
	// c.Offset(0, -26);
	Tools.PaintContent(pDC, c);
}

int CompareContacts(Contact **a, Contact **b)
{
	if (a && b)
	{
		auto A = (*a)->GetFirst();
		auto B = (*b)->GetFirst();
		if (A && B)
			return _stricmp(A, B);
	}

	return 0;
}

bool ScribeWnd::OpenAMail(ScribeFolder *Folder)
{
	THREAD_UNSAFE(false);

	if (Folder &&
		Tree)
	{
		Folder->LoadThings();

		for (auto i: Folder->Items)
		{
			Mail *m = i->IsMail();
			if (m && !(m->GetFlags() & MAIL_READ))
			{
				Tree->Select(Folder);
				m->DoUI();
				return true;
			}
		}

		for (auto *t=Folder->GetChild(); t; t=t->GetNext())
		{
			ScribeFolder *f = dynamic_cast<ScribeFolder*>(t);
			if (OpenAMail(f))
			{
				return true;
			}
		}
	}

	return false;
}

void AddContactToMenu(LSubMenu *Menu, Contact *c, ssize_t Index)
{
	if (!c || Index < 0)
		return;

	auto Email = c->GetEmail();
	if (!Email)
		return;

	// has an email, list it
	auto First = c->GetFirst();
	auto Last = c->GetLast();
	if (First || Last)
	{
		char Buf[256];
		sprintf_s(Buf, sizeof(Buf), "%s %s", (First)?First:"", (Last)?Last:"");
		auto Item = Menu->AppendItem(Buf, TRAY_CONTACT_BASE + (int)Index, true);
		if (Item) Item->Icon(ICON_CONTACT);
	}
}

void ScribeWnd::AddContactsToMenu(LSubMenu *Menu)
{
	THREAD_UNSAFE();

	if (!Menu)
		return;

	d->TrayMenuContacts.Sort(CompareContacts);
	if (((ssize_t)d->TrayMenuContacts.Length() << 4) > GdcD->Y() - 200)
	{
		// Group contacts by starting letter
		LArray<Contact*> Alpha[26];
		LArray<Contact*> Other;
		for (auto c: d->TrayMenuContacts)
		{
			auto First = c->GetFirst();
			auto Last = c->GetLast();
			auto Email = c->GetEmail();
			if (Email)
			{
				// has an email, list it
				if (First || Last)
				{
					auto Name = First?First:Last;
					if
					(
						(*Name >= 'a' && *Name <= 'z')
						||
						(*Name >= 'A' && *Name <= 'Z')
					)
					{
						int Ind = tolower(*Name) - 'a';
						if (Ind >= 0 && Ind < CountOf(Alpha))
							Alpha[Ind].Add(c);
						else
							Other.Add(c);
					}
					else
						Other.Add(c);
				}
			}
		}

		for (int i=0; i<CountOf(Alpha); i++)
		{
			if (Alpha[i].Length() > 0)
			{
				char Group[64];
				sprintf_s(Group, sizeof(Group), "%c...", 'a' + i);
				auto Sub = Menu->AppendSub(Group);
				if (Sub)
				{
					for (auto c: Alpha[i])
						AddContactToMenu(Sub, c, d->TrayMenuContacts.IndexOf(c));
				}
			}
		}

		if (Other.Length())
		{
			auto Sub = Menu->AppendSub("Other...");
			if (Sub)
			{
				for (auto c: Other)
					AddContactToMenu(Sub, c, d->TrayMenuContacts.IndexOf(c));
			}
		}
	}
	else
	{
		// Display all...
		for (size_t i=0; i<d->TrayMenuContacts.Length(); i++)
		{
			AddContactToMenu(Menu, d->TrayMenuContacts[i], i);
		}
	}
}

void ScribeWnd::OnUrl(const char *Url)
{
	THREAD_UNSAFE();

	LUri u(Url);
	if (u.IsProtocol("mailto"))
		CreateMail(0, Url, 0);
}

void ScribeWnd::OnReceiveFiles(LArray<const char*> &Files)
{
	THREAD_UNSAFE();

	int UnknownFormats = 0;
	int64 Period = LCurrentTime() - LastDrop;

	if (Period > 500) // Lock out drops within 500ms of an LGI drop
	{
		LString sSend, sPages;
		bool HasSend = LAppInst->GetOption("send", sSend);
		bool HasPrint = LAppInst->GetOption("p", sPages);
		LArray<Mail*> NewMail;

		for (unsigned i=0; i<Files.Length(); i++)
		{
			auto f = Files[i];
			LString MimeType = ScribeGetFileMimeType(f);

			if (MimeType)
			{
				Thing *t = NULL;
				
				if (!_stricmp(MimeType, sMimeVCard))
				{
					t = CreateItem(MAGIC_CONTACT, NULL, false);
				}
				else if (!_stricmp(MimeType, sMimeVCalendar))
				{
					t = CreateItem(MAGIC_CALENDAR, NULL, false);
				}
				else if (!_stricmp(MimeType, sMimeMessage))
				{
					t = CreateItem(MAGIC_MAIL, NULL, false);
				}
				else if (!_stricmp(MimeType, sMimeMbox))
				{
					// What should I do here?
				}
				else UnknownFormats++;

				if (t)
				{
					LAutoPtr<LFile> str(new LFile);
					if (str->Open(f, O_READ))
					{
						if (t->Import(t->AutoCast(str), MimeType))
						{
							if (HasSend)
							{
								Mail *m = t->IsMail();
								if (m)
								{
									NewMail.Add(m);
									m->SetFlags(m->GetFlags() | MAIL_CREATED | MAIL_READ);
								}
							}
							else
							{
								t->DoUI();
							}
						}
					}
				}
			}
			else UnknownFormats++;
		}

		bool SendNow = sSend ? atoi(sSend) != 0 : false;
		for (unsigned i=0; i<NewMail.Length(); i++)
		{
			Mail *m = NewMail[i];
			if (SendNow)
			{
				m->SetFlags(m->GetFlags() | MAIL_READY_TO_SEND);
				m->Save();
			}
			else if (HasPrint)
			{
				ThingPrint(NULL, m, GetPrinter(), 0, sPages ? atoi(sPages) : 0);
			}
			else
			{
				m->DoUI();
			}
		}
		if (SendNow)
			Send();
	}
}

void ScribeWnd::OnTrayClick(LMouse &m)
{
	THREAD_UNSAFE();

	if (m.Down())
	{
		#ifndef MAC
		// No support for different mouse button info so the default
		// action is to show the sub-menu of contacts and actions.
		if (m.IsContextMenu())
		#endif
		{
			LWindow::OnTrayClick(m);
		}
		#ifndef MAC
		else if (m.Left())
		{
			if (m.Double())
			{
				if (Mail::NewMailLst.Length() > 0)
				{
					ScribeFolder *InBox = GetFolder(FOLDER_INBOX);
					OpenAMail(InBox);
				}
			}
			else
			{
				if (GetZoom() == LZoomMin)
				{
					SetZoom(LZoomNormal);
				}
				else
				{
					if (Obscured())
						SetZoom(LZoomNormal);  // Bounce in front, first.
					else
						SetZoom(LZoomMin);
				}
				Visible(true);

				MoveOnScreen();
				Raise();

				if (MailList)
				{
					MailList->Focus(true);
				}
			}
		}
		else if (m.Middle())
		{
			Mail::NewMailLst.Empty();
		}
		#endif
	}
}

void ScribeWnd::OnTrayMenu(LSubMenu &m)
{
	THREAD_UNSAFE();

	m.SetImageList(ImageList, false);
	d->TrayMenuContacts.Length(0);

	#if MAC || LINUX
	m.AppendItem(LLoadString(IDS_OPEN), IDM_OPEN);
	m.AppendSeparator();
	#endif

	LHashTbl<ConstStrKey<char,false>,Contact*> Added;
	LArray<ScribeFolder*> Srcs = GetThingSources(MAGIC_CONTACT);
	for (auto c: Srcs)
	{
		c->LoadThings();
		for (auto i: c->Items)
		{
			Contact *c = i->IsContact();
			if (!c) continue;

			bool IsAdded = false;
			auto Emails = c->GetEmails();
			for (auto e: Emails)
			{
				if (Added.Find(e))
					IsAdded = true;
			}

			if (Emails.Length() && !IsAdded)
			{
				for (auto e: Emails)
					Added.Add(e, c);
				d->TrayMenuContacts.Add(c);
			}
		}
	}
	AddContactsToMenu(&m);

	m.AppendSeparator();

	if (Mail::NewMailLst.Length() > 0)
	{
		int i=0;
		for (auto ml: Mail::NewMailLst)
		{
			LStringPipe p;
			
			// This code figures out how many UTF characters to print. We
			// can't split a UTF character because downstream conversions
			// will fail.
			const char *Subj = ml->GetSubject();
			if (!Subj)
				Subj = "(No Subject)";
			LUtf8Ptr u((uint8_t*)Subj);
			while ((int32)u && u.GetPtr() - (uchar*)Subj < 64)
				u++;
			ssize_t Bytes = u.GetPtr() - (uchar*)Subj;
			
			p.Print("%.*s, %s <%s>",
					Bytes,
					Subj?Subj:(char*)"(No Subject)",
					ml->GetFromStr(FIELD_NAME),
					ml->GetFromStr(FIELD_EMAIL));
			LAutoString a(p.NewStr());
			
			LAssert(LIsUtf8(a));
			
			auto Item = m.AppendItem(a, TRAY_MAIL_BASE+i++, true);
			if (Item)
			{
				Item->Icon(ICON_UNREAD_MAIL);
			}
		}
		m.AppendSeparator();
	}

	if (GetZoom() == LZoomMin)
	{
		m.AppendItem(LLoadString(IDS_OPEN), IDM_OPEN, true);
	}

	auto NewMail = m.AppendItem(LLoadString(IDS_NEW_EMAIL), IDM_NEW_EMAIL);
	if (NewMail) NewMail->Icon(ICON_UNSENT_MAIL);
	m.AppendItem(LLoadString(IDS_EXIT), IDM_EXIT);
}

void ScribeWnd::OnTrayMenuResult(int MenuId)
{
	THREAD_UNSAFE();

	switch (MenuId)
	{
		case IDM_OPEN:
		{
			if (GetZoom() == LZoomMin)
			{
				SetZoom(LZoomNormal);
			}

			Visible(true);
			Raise();
			
			if (MailList)
			{
				MailList->Focus(true);
			}
			break;
		}
		case IDM_NEW_EMAIL:
		{
			CreateMail();
			break;
		}
		case IDM_EXIT:
		{
			d->IngoreOnClose = true;
			PostEvent(M_CLOSE);
			break;
		}
		default:
		{
			auto i = MenuId - TRAY_CONTACT_BASE;
			Contact *c = d->TrayMenuContacts.IdxCheck(i) ? d->TrayMenuContacts[i] : NULL;
			if (c)
			{
				CreateMail(c);
			}

			Mail *m = Mail::NewMailLst[MenuId - TRAY_MAIL_BASE];
			if (m)
			{
				Mail::NewMailLst.Delete(m);
				m->DoUI();
			}
			break;
		}
	}
}

void ScribeWnd::OnZoom(LWindowZoom Action)
{
	THREAD_UNSAFE();

	if (Action == LZoomMin)
	{
		LVariant i;
		if (GetOptions()->GetValue(OPT_MinimizeToTray, i) && i.CastInt32())
		{
			Visible(false);
		}
	}
}

void ScribeWnd::GetUserInput(LView *Parent, LString Msg, bool Password, std::function<void(LString)> Callback)
{
	THREAD_UNSAFE();

	if (!InThread())
	{
		// Run the function on the Window's thread:
		RunCallback([this, Parent, Msg, Password, Callback]() -> auto
			{
				GetUserInput(Parent, Msg, Password, Callback);
				return 0;
			},
			_FL);
		return;
	}

	auto Inp = new LInput(Parent ? Parent : this, "", Msg, AppName, Password);
	Inp->DoModal([this, Inp, Callback](auto dlg, auto id)
	{
		if (Callback)
			Callback(id ? Inp->GetStr() : LString());
	});
}

LMessage::Result ScribeWnd::OnEvent(LMessage *Msg)
{
	d->TrayIcon->OnEvent(Msg);
	BayesianFilter::OnEvent(Msg);
	
	switch (Msg->Msg())
	{
		case M_CONSTRUCT_0:
		{
			// Haiku: This allows Construct0 to run in the window's thread.
			// This solves a bunch of locking issues.
			Construct0((LOptionsFile::PortableType)Msg->A());
			break;
		}
		case M_UNIT_TEST:
		{
			auto j = Msg->AutoA<LJson>();
			if (!j)
				break;

			auto cmd = j->Get("cmd");
			if (!cmd)
				break;

			LAssert(!"Impl me.");
			if (cmd.Equals("somecmd"))
			{
				
			}
			break;
		}
		case M_CALENDAR_SOURCE_EVENT:
		{
			auto cs = (CalendarSource*)Msg->A();
			auto m = Msg->AutoB<LMessage>();
			if (cs && m)
				cs->OnEvent(m);
			break;
		}
		case M_SET_HTML:
		{
			auto Html = Msg->AutoA<LString>();
			if (PreviewPanel && Html)
			{
				LScriptArguments Arg(NULL);
				Arg.Add(new LVariant(Html.Get()));
				PreviewPanel->CallMethod(DomToStr(SdSetHtml), Arg);
			}
			break;
		}
		case M_NEW_CONSOLE_MSG:
		{
			if (d->ShowConsoleBtn)
				d->ShowConsoleBtn->Image(IDM_CONSOLE_MSGS);
			break;
		}
		case M_NEEDS_CAP:
		{
			auto cap = Msg->AutoA<LString>();
			auto param = Msg->AutoB<LString>();
			NeedsCapability(cap?cap->Get():nullptr, param?param->Get():nullptr);
			return 0;
			break;
		}
		case M_STORAGE_EVENT:
		{
			auto Store = LDataStoreI::Map.Find((int)Msg->A());
			if (Store)
				Store->OnEvent((void*)Msg->B());
			break;
		}
		case M_SCRIBE_SET_MSG_FLAG:
		{
			LAutoString p((char*)Msg->A());
			if (!p)
				break;

			auto d = strrchr(p, '/');
			if (!d)
				break;

			*d++ = 0;

			if (auto f = GetFolder(p))
			{
				LUri u;
				LString a = u.DecodeStr(d);
				f->GetMessageById(a,
					[this, NewFlag=(int)Msg->B()](auto r)
					{
						if (r)
						{
							int ExistingFlags = r->GetFlags();
							r->SetFlags(ExistingFlags | NewFlag);
						}
					});
			}
			break;
		}
		case M_SCRIBE_DEL_THING:
		{
			auto t = (Thing*)Msg->A();
			DeleteObj(t);
			break;
		}
		case M_SCRIBE_LOADED:
		{
			if (d->FoldersLoaded)
			{
				// Ok let the user in
				CmdSend.Enabled(true);
				CmdReceive.Enabled(true);
				CmdPreview.Enabled(true);
			}
			
			if (d->TrayIcon)
				d->TrayIcon->UpdateMenu();
			break;
		}
		case M_SCRIBE_THREAD_DONE:
		{
			// Finialize connection
			auto Thread = (AccountThread*) Msg->A();
			auto Acc = (Accountlet*) Msg->B();
			if (Thread && Acc)
			{
				OnAfterConnect(Acc->GetAccount(), Acc->IsReceive());

				d->NewMailTimeout = 2;
				Acc->OnThreadDone();
			}
			else
			{
				LAssert(0);
			}
			break;
		}
		case M_SCRIBE_MSG:
		{
			LAutoString m((char*)Msg->A());
			if (!m)
				break;

			if (Msg->B())
			{
				if (LgiMsg(this, m, AppName, MB_YESNO) == IDYES)
				{
					PostEvent(M_COMMAND, IDM_OPTIONS, 0);						
				}
			}
			else
			{
				LgiMsg(this, "%s", AppName, MB_OK, m.Get());
			}
			break;
		}
		case M_SCRIBE_NEW_MAIL:
		{
			if (Lock(_FL))
			{
				if (NewMailDlg)
				{
					NewMailDlg->AddThings(&Mail::NewMailLst);
				}
				Unlock();
			}
			break;
		}
		case M_SCRIBE_OPEN_THING:
		{
			auto t = (Thing*) Msg->A();
			if (t)
			{
				t->DoUI();
			}
			break;
		}
		case M_SCRIBE_ITEM_SELECT:
		{
			if (!MailList || !IsAttached())
				break;

			List<Thing> Things;
			MailList->GetSelection(Things);
			OnSelect(&Things);
			break;
		}
		case M_URL:
		{
			auto Url = Msg->AutoA<LString>();
			if (!Url)
				break;

			LUri u(*Url);
			if (u.sProtocol && !_stricmp(u.sProtocol, "mailto"))
			{
				Mailto mt(this, *Url);
				if (mt.To.Length() > 0)
				{
					Thing *t = CreateItem(MAGIC_MAIL, NULL, false);
					if (t)
					{
						Mail *m = t->IsMail();
						if (m)
						{
							mt.Apply(m);
							m->DoUI();
						}
						else DeleteObj(t);
					}
				}
			}
			break;
		}
	}

	return LWindow::OnEvent(Msg);
}

bool ScribeWnd::IsMyEmail(const char *Email)
{
	THREAD_UNSAFE(false);

	if (Email)
	{
		LVariant e;
		for (auto a : *GetAccounts())
		{
			LVariant e = a->Identity.Email();
			if (e.Str() && _stricmp(Email, e.Str()) == 0)
			{
				return true;
			}
		}
	}

	return false;
}

int ScribeWnd::GetMaxPages()
{
	THREAD_SAFE();
	return d->PrintMaxPages;
}

void ScribeWnd::ThingPrint(std::function<void(bool)> Callback, ThingType *m, LPrinter *Printer, LView *Parent, int MaxPages)
{
	THREAD_UNSAFE();
	d->PrintMaxPages = MaxPages;
	
	if (!Printer)
		Printer = GetPrinter();
	if (!Printer)
	{
		if (Callback) Callback(false);
		return;
	}
	Thing *t = dynamic_cast<Thing*>(m);
	if (!t)
	{
		if (Callback) Callback(false);
		return;
	}
		
	auto Events = new ScribePrintContext(this, t);
	Printer->Print(	Events,
					[this, Events, Parent, Printer, Callback](auto pages)
					{
						if (pages == Events->OnBeginPrintError)
						{
							LgiMsg(Parent, "Printing failed: %s", AppName, MB_OK, Printer->GetErrorMsg().Get());
							if (Callback)
								Callback(false);
						}
						else if (Callback)
						{
							Callback(true);
						}

						delete Events;
					},
					AppName,
					-1,
					Parent ? Parent : this);
}

bool ScribeWnd::MailReplyTo(Mail *m, bool All)
{
	THREAD_UNSAFE(false);
	bool Status = false;

	if (m)
	{
		LDataStoreI *Store = m->GetObject() ? m->GetObject()->GetStore() : NULL;
		LDataI *NewMailObj = Store && Store->GetInt(FIELD_STORE_TYPE) == Store3Sqlite ? Store->Create(MAGIC_MAIL) : NULL;
		
		Mail *NewMail = new Mail(this, NewMailObj);
		if (NewMail)
		{
			if (NewMail->GetObject())
			{
				NewMail->OnReply(m, All, true);

				LView *w = NewMail->DoUI();
				if (w)
				{
					LViewI *t = w->FindControl(IDC_TEXT);
					if (t)
					{
						t->Focus(true);
					}
				}

				Status = true;
			}
			else DeleteObj(NewMail);
		}
	}

	return Status;
}

bool ScribeWnd::MailForward(Mail *m)
{
	THREAD_UNSAFE(false);
	bool Status = false;

	if (m)
	{
		Mail *NewMail = new Mail(this);
		if (NewMail)
		{
			if (NewMail->OnForward(m, true))
			{
				NewMail->DoUI();
				Status = true;
			}
			else
			{
				NewMail->DecRef();
			}
		}
	}

	return Status;
}

bool ScribeWnd::MailBounce(Mail *m)
{
	THREAD_UNSAFE(false);
	bool Status = false;

	if (!m)
		return false;

	if (auto NewMail = new Mail(this))
	{
		if (NewMail->OnBounce(m, true))
		{
			NewMail->DoUI();
			Status = true;
		}
		else
		{
			DeleteObj(NewMail);
		}
	}

	return Status;
}

Mail *ScribeWnd::CreateMail(Contact *c, const char *Email, const char *Name)
{
	THREAD_UNSAFE(NULL);
	
	auto thing = CreateItem(MAGIC_MAIL, NULL, false);
	if (!thing)
	{
		LgiTrace("%s:%i - CreateItem failed.\n", _FL);
		return NULL;
	}

	auto m = thing->IsMail();
	if (m)
	{
		bool IsMailTo = false;
		if (Email)
		{
			IsMailTo = !Strnicmp(Email, "mailto:", 7);
			if (IsMailTo)
			{
				Mailto mt(this, Email);
				mt.Apply(m);
			}
		}

		if (auto UI = dynamic_cast<MailUi*>(m->DoUI()))
		{
			if (c)
				UI->AddRecipient(c);

			if (Email && !IsMailTo)
				UI->AddRecipient(Email, Name);
		}
	}

	return m;
}

Mail *ScribeWnd::LookupMailRef(const char *MsgRef, bool TraceAllUids)
{
	THREAD_UNSAFE(NULL);
	
	if (!MsgRef)
		return NULL;
	
	LString ref(MsgRef);
	if (auto RawUid = strrchr(ref, '/'))
	{
		*RawUid++ = 0;
		
		LUri u;		
		auto Uid = u.DecodeStr(RawUid);
		
		// Try the mail message map first...
		auto m = Mail::GetMailFromId(Uid);
		if (m)
			return m;

		// Ok, not found, so look in last known folder...
		if (auto f = GetFolder(ref))
		{
			for (auto t: f->Items)
			{
				if (auto m = t->IsMail())
				{
					auto s = m->GetMessageId();
					if (!Strcmp(s, Uid.Get()))
						return m;
					
					if (TraceAllUids)
						LgiTrace("\t%s\n", s);
				}
			}
		}
	}

	return NULL;
}

void ScribeWnd::OnBayesAnalyse(const char *Msg, const char *WhiteListEmail)
{
	THREAD_UNSAFE();
	LString s, q;
	s.Printf("<html><body style='background:L_MED;'><pre>%s</pre>", Msg);
	if (WhiteListEmail)
	{
		q.Printf(LLoadString(IDS_REMOVE_WHITELIST), WhiteListEmail);
		s += LString("<br>") + q;
	}
	s += "</body></html>";
	LHtmlMsg([this, WhiteListEmail=LString(WhiteListEmail)](auto result)
			{
				if (result == IDYES)
					RemoveFromWhitelist(WhiteListEmail);
			},
			this,
			s,
			AppName,
			WhiteListEmail ? MB_YESNO : MB_OK);
}

bool ScribeWnd::OnBayesResult(const char *MailRef, double Rating)
{
	THREAD_UNSAFE(false);

	if (auto m = LookupMailRef(MailRef))
		return OnBayesResult(m, Rating);
	#ifdef _DEBUG
	else
	{
		LgiTrace("%s:%i - error finding mail ref: %s\n", _FL, MailRef);
		LookupMailRef(MailRef, true);
		LAssert(!"We should always be able to resolve the reference, unless m is completely deleted");
	}
	#endif
	
	return false;
}

bool ScribeWnd::OnBayesResult(Mail *m, double Rating)
{
	THREAD_UNSAFE(false);
	if (!m)
		return false;

	LVariant v;
	GetOptions()->GetValue(OPT_BayesThreshold, v);
	double BayesThresh = v.CastDouble();
	if (BayesThresh < 0.1) BayesThresh = 0.1;
	if (BayesThresh > 1.0) BayesThresh = 1.0;

	if (Rating < BayesThresh)
	{
		// Not spam, so we continue new mail processing
		if (m->NewEmail == Mail::NewEmailBayes)
		{
			List<Mail> Nm;
			Nm.Insert(m);
			m->NewEmail = Mail::NewEmailGrowl;
			OnNewMail(Nm, true);
		}
		return false;
	}

	// Spam is pink!
	m->SetMarkColour(Rgb32(255, 0, 0));
	m->SetFlags(m->GetFlags() | MAIL_BAYES_SPAM);

	ScribeBayesianFilterMode FilterMode = BayesOff;
	if (GetOptions()->GetValue(OPT_BayesFilterMode, v))
		FilterMode = (ScribeBayesianFilterMode)v.CastInt32();
		
	if (FilterMode == BayesTrain)
	{
		// Move to folder
		LVariant MoveToPath;
		if (!GetOptions()->GetValue(OPT_BayesMoveTo, MoveToPath))
		{
			MoveToPath = "/Spam/Probably";
		}
		
		if (auto f = GetFolder(MoveToPath.Str()))
		{
			LArray<Thing*> Items;
			Items.Add(m);
			f->MoveTo(Items, false, [this, m](auto result, auto status)
			{			
				List<Mail> obj;
				obj.Insert(m);
				OnNewMail(obj, false);
			});
		}
	}
	else
	{
		m->DeleteAsSpam(this);
	}

	return true;
}

#if WINNATIVE
struct DefaultClient
{
	char DefIcon[MAX_PATH_LEN];
	char CmdLine[MAX_PATH_LEN];
	char DllPath[MAX_PATH_LEN];

	static constexpr const char *sCurrentMailClient = "HKCU\\SOFTWARE\\Clients\\Mail";
	static constexpr const char *sSystemMailClient  = "HKLM\\SOFTWARE\\Clients\\Mail";

	DefaultClient()
	{
		auto Exe = LGetExeFile();
		sprintf_s(DefIcon, sizeof(DefIcon), "%s,1", Exe.Get());
		sprintf_s(CmdLine, sizeof(CmdLine), "\"%s\" /m \"%%1\"", Exe.Get());
		LMakePath(DllPath, sizeof(DllPath), Exe, "../ScribeMapi.dll");
	}

	bool IsWindowsXp()
	{
		LArray<int> Ver;
		int Os = LGetOs(&Ver);
		if
			(
				(
					Os == LGI_OS_WIN32
					||
					Os == LGI_OS_WIN64
					)
				&&
				Ver.Length() > 1
				&&
				Ver[0] == 5
				&&
				Ver[1] == 1
				)
			return true;

		return false;
	}

	bool InstallMailto(bool Write)
	{
		LAutoPtr<LRegKey> mailto = CheckKey(Write, "HKCR\\mailto");
		if (!mailto)
			return false;
		if (!CheckString(Write, mailto, NULL, "URL:MailTo Protocol"))
			return false;

		LAutoPtr<LRegKey> deficon = CheckKey(Write, "HKCR\\mailto\\DefaultIcon");
		if (!deficon)
			return false;
		if (!CheckString(Write, deficon, NULL, DefIcon))
			return false;

		LAutoPtr<LRegKey> shell = CheckKey(Write, "HKCR\\mailto\\shell");
		if (!shell)
			return false;
		if (!CheckString(Write, shell, NULL, "open"))
			return false;

		LAutoPtr<LRegKey> cmd = CheckKey(Write, "HKCR\\mailto\\shell\\open\\command");
		if (!cmd)
			return false;
		if (!CheckString(Write, cmd, NULL, CmdLine))
			return false;


		return true;
	}

	LAutoPtr<LRegKey> CheckKey(bool Write, const char *Key, ...) const
	{
		char Buffer[512];
		va_list Arg;
		va_start(Arg, Key);
		vsprintf_s(Buffer, sizeof(Buffer), Key, Arg);
		va_end(Arg);

		LAutoPtr<LRegKey> k(new LRegKey(Write, Buffer));
		if (k && Write && !k->IsOk())
		{
			if (!k->Create())
			{
				k.Reset();
				LgiTrace("%s:%i - Failed to create '%s'\n", _FL, Buffer);
			}
		}

		return k;
	}

	bool CheckInt(bool Write, LRegKey *k, const char *Name, uint32_t Value)
	{
		if (!k)
		{
			LgiTrace("%s:%i - No key: '%s'\n", _FL, Name);
			return false;
		}

		uint32_t Cur;
		if (!k->GetInt(Name, Cur))
			Cur = Value + 1;

		if (Cur == Value)
			return true;

		if (Write)
		{
			bool Status = k->SetInt(Name, Value);
			if (!Status)
				LgiTrace("%s:%i - Failed to set key '%s': '%s' to %i\n", _FL, k->Name(), Name, Value);
			return Status;
		}

		return false;
	}

	bool CheckString(bool Write, LRegKey *k, const char *StrName, const char *StrValue)
	{
		if (!k)
		{
			LgiTrace("%s:%i - No key: '%s' to '%s'\n", _FL, StrName, StrValue);
			return false;
		}

		LString v;
		if (k->GetStr(StrName, v))
		{
			bool Same = Stricmp(v.Get(), StrValue) == 0;
			if (Write && !Same)
			{
				bool Status = k->SetStr(StrName, StrValue);
				if (!Status)
					LgiTrace("%s:%i - Failed to set key '%s': '%s' to '%s'\n", _FL, k->Name(), StrName, StrValue);
				return Status;
			}

			return Same;
		}
		else if (Write)
		{
			bool Status = k->SetStr(StrName, StrValue);
			if (!Status)
				LgiTrace("%s:%i - Failed to set key '%s': '%s' to '%s'\n", _FL, k->Name(), StrName, StrValue);
			return Status;
		}

		return false;
	}

	bool IsDefault()
	{
		LAutoPtr<LRegKey> mail = CheckKey(false, sCurrentMailClient);
		if (!mail)
			return false;

		LString v;
		if (!mail->GetStr(NULL, v))
			return false;

		return !Stricmp(v.Get(), AppName);
	}

	bool SetDefault() const
	{
		LAutoPtr<LRegKey> mail = CheckKey(true, sCurrentMailClient);
		if (!mail)
			return false;

		// Set the default client in the current user tree.
		mail->SetStr(NULL, "Scribe");

		// Configure the mailto handler
		const char *Base = "HKEY_ROOT";
		bool Error = false;
		LRegKey Mt(true, "%s\\mailto", Base);
		if (Mt.IsOk() || Mt.Create())
		{
			if (!Mt.SetStr(0, "URL:MailTo Protocol") ||
				!Mt.SetStr("URL Protocol", ""))
				Error = true;
		}
		else
		{
			LgiTrace("%s:%i - Couldn't open/create registry key (err=%i).\n", _FL, GetLastError());
			Error = true;
		}

		LRegKey Di(true, "%s\\mailto\\DefaultIcon", Base);
		if (Di.IsOk() || Di.Create())
		{
			if (!Di.SetStr(0, DefIcon))
				Error = true;
		}
		else
		{
			LgiTrace("%s:%i - Couldn't open/create registry key (err=%i).\n", _FL, GetLastError());
			Error = true;
		}

		LRegKey c(true, "%s\\mailto\\shell\\open\\command", Base);
		if (c.IsOk() || c.Create())
		{
			if (!c.SetStr(NULL, CmdLine))
				Error = true;
		}
		else
		{
			LgiTrace("%s:%i - Couldn't open/create registry key (err=%i).\n", _FL, GetLastError());
			Error = true;
		}

		return Error;
	}

	bool InstallAsClient(char *Base, bool Write)
	{
		// Create software client entry, to put Scribe in the Internet Options for mail clients.
		LAutoPtr<LRegKey> mail = CheckKey(Write, "%s\\Software\\Clients\\Mail", Base);
		if (!mail)
			return false;

		LAutoPtr<LRegKey> app = CheckKey(Write, "%s\\Software\\Clients\\Mail\\Scribe", Base);
		if (!app)
			return false;
		if (!CheckString(Write, app, NULL, AppName))
			return false;
		if (!CheckString(Write, app, "DllPath", DllPath))
			return false;

		LAutoPtr<LRegKey> shell = CheckKey(Write, "%s\\Software\\Clients\\Mail\\Scribe\\shell\\open\\command", Base);
		if (!shell)
			return false;
		if (!CheckString(Write, shell, NULL, CmdLine))
			return false;

		LAutoPtr<LRegKey> icon = CheckKey(Write, "%s\\Software\\Clients\\Mail\\Scribe\\DefaultIcon", Base);
		if (!icon)
			return false;
		if (!CheckString(Write, icon, NULL, DefIcon))
			return false;

		LAutoPtr<LRegKey> proto = CheckKey(Write, "%s\\Software\\Classes\\Protocol\\mailto", Base);
		if (!proto)
			return false;
		if (!CheckString(Write, proto, NULL, "URL:MailTo Protocol"))
			return false;
		if (!CheckString(Write, proto, "URL Protocol", ""))
			return false;
		if (!CheckInt(Write, proto, "EditFlags", 0x2))
			return false;

		LAutoPtr<LRegKey> proto_cmd = CheckKey(Write, "%s\\Software\\Classes\\Protocol\\mailto\\shell\\open\\command", Base);
		if (!proto_cmd)
			return false;
		if (!CheckString(Write, proto_cmd, NULL, CmdLine))
			return false;

		return true;
	}

	struct FileType
	{
		const char *Name;
		const char *Desc;
		int Icon;
	};

	static FileType FileTypes[];

	bool WinInstall(bool Write)
	{
		// http://msdn.microsoft.com/en-us/library/windows/desktop/cc144154%28v=vs.85%29.aspx
		LArray<int> Ver;
		int Os = LGetOs(&Ver);
		if
		(
			(
				Os == LGI_OS_WIN32
				||
				Os == LGI_OS_WIN64
			)
			&&
			Ver[0] >= 6)
		{
			char Path[MAX_PATH_LEN];
			auto Exe = LGetExeFile();

			for (int i=0; FileTypes[i].Name; i++)
			{
				LAutoPtr<LRegKey> base = CheckKey(Write, "HKEY_CLASSES_ROOT\\%s", FileTypes[i].Name);
				if (!base)
					return false;
				if (!CheckString(Write, base, NULL, FileTypes[i].Desc))
					return false;

				LAutoPtr<LRegKey> r = CheckKey(Write, "HKEY_CLASSES_ROOT\\%s\\shell\\Open\\command", FileTypes[i].Name);
				if (!r)
					return false;
				sprintf_s(Path, sizeof(Path), "\"%s\" -u \"%%1\"", Exe.Get());
				if (!CheckString(Write, r, NULL, Path))
					return false;

				LAutoPtr<LRegKey> ico = CheckKey(Write, "HKEY_CLASSES_ROOT\\%s\\DefaultIcon", FileTypes[i].Name);
				if (!ico)
					return false;
				sprintf_s(Path, sizeof(Path), "%s,%i", Exe.Get(), FileTypes[i].Icon);
				if (!CheckString(Write, ico, NULL, Path))
					return false;
			}

			LAutoPtr<LRegKey> r = CheckKey(Write, "HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\Mail\\Scribe\\Capabilities");
			if (!r)
				return false;
			if (!CheckString(Write, r, "ApplicationDescription", "Scribe is a small lightweight email client.") &&
				!CheckString(Write, r, "ApplicationName", "Scribe") &&
				!CheckString(Write, r, "ApplicationIcon", DefIcon))
				return false;

			LAutoPtr<LRegKey> as = CheckKey(Write, "HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\Mail\\Scribe\\Capabilities\\FileAssociations");
			if (!as)
				return false;
			if (!CheckString(Write, as, ".eml",   "Scribe.Email")    &&
				!CheckString(Write, as, ".msg",   "Scribe.Email")    &&
				!CheckString(Write, as, ".mbox",  "Scribe.Folder")   &&
				!CheckString(Write, as, ".mbx",   "Scribe.Folder")   &&
				!CheckString(Write, as, ".ics",   "Scribe.Calendar") &&
				!CheckString(Write, as, ".vcs",   "Scribe.Calendar") &&
				!CheckString(Write, as, ".vcf",   "Scribe.Contact")  &&
				!CheckString(Write, as, ".mail3", "Scribe.MailStore"))
				return false;

			LAutoPtr<LRegKey> ua = CheckKey(Write, "HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\Mail\\Scribe\\Capabilities\\UrlAssociations");
			if (!ua)
				return false;
			if (!CheckString(Write, ua, "mailto", "Scribe.Mailto"))
				return false;

			LAutoPtr<LRegKey> a = CheckKey(Write, "HKEY_LOCAL_MACHINE\\SOFTWARE\\RegisteredApplications");
			if (!a)
				return false;
			if (!CheckString(Write, a, "Scribe", "SOFTWARE\\Clients\\Mail\\Scribe\\Capabilities"))
				return false;
		}

		return true;
	}

	void WinUninstall()
	{
		for (int i=0; FileTypes[i].Name; i++)
		{
			LRegKey base(true, "HKEY_CLASSES_ROOT\\%s", FileTypes[i].Name);
			base.DeleteKey();
		}
	}

	LString WrapResult(LRegKey &k)
	{
		auto code = k.GetErrorCode();
		auto msg = k.GetErrorName();
		LString s;
		auto ok = code == 0 || code == 2;
		auto cls = ok ? "ok" : "err";
		s.Printf("<span class='%s'>%s (%i)</span>", cls, !msg && ok ? "ok" : msg.Get(), code);
		return s;
	}

	void CleanRegistry(ScribeWnd *parent)
	{
		enum Action {
			noAction,
			checkDef,
			removeKey,
		};
		struct Pair {
			const char *key;
			Action action;
		};

		const char *bases[] = {
			"HKCU",
			"HKLM"
		};

		Pair keys[] = {
			{"mailto", checkDef},
			{"mailto\\DefaultIcon", removeKey},
			{"mailto\\shell\\open\\command", removeKey},
			{"Software\\Clients\\Mail", checkDef},
			{"Software\\Clients\\Mail\\Scribe\\shell\\open\\command", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\shell\\open", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\shell", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\DefaultIcon", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\Capabilities\\FileAssociations", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\Capabilities\\UrlAssociations", removeKey},
			{"Software\\Clients\\Mail\\Scribe\\Capabilities", removeKey},
			{"Software\\Clients\\Mail\\Scribe", removeKey},
			{"Software\\Classes\\Protocol\\mailto", checkDef},
			{"Software\\Classes\\Protocol\\mailto\\shell\\open\\command", checkDef},
		};

		int deleted = 0;
		LStringPipe log;

		bool previous = LRegKey::AssertOnError;
		LRegKey::AssertOnError = false;

		for (int base=0; base<CountOf(bases); base++)
		{
			auto baseName = bases[base];
			log.Print("<h2>%s</h2><table>\n", baseName);

			for (int key=0; key<CountOf(keys); key++)
			{
				auto &cur = keys[key];
				LString keyName;
				keyName.Printf("%s\\%s", baseName, cur.key);
				log.Print("<tr><td><span class=key>%s</span>", keyName.Get());
					
				switch (cur.action)
				{
					case checkDef:
					{
						LRegKey ro(false, keyName);
						if (!ro.IsOk())
						{
							log.Print("<td>%s\n", WrapResult(ro).Get());
							break;
						}
						
						auto cur = ro.GetStr();
						if (Stristr(cur, AppName) == NULL)
						{
							log.Print("<td><span class='ok'>not scribe, is '%s'</span>\n", cur);
							continue;
						}

						LRegKey rw(true, keyName);
						if (rw.IsOk())
							rw.SetStr(NULL, NULL);
						log.Print("<td>%s\n", WrapResult(rw).Get());
						break;
					}
					case removeKey:
					{
						LRegKey k(true, keyName);
						if (k.DeleteKey())
							deleted++;
						log.Print("<td>%s\n", WrapResult(k).Get());
						break;
					}
				}
			}

			log.Print("</table><br>\n");
		}

		LRegKey::AssertOnError = previous;
		LHtmlMsg(NULL, parent,
			"<html>\n"
			"<head><style>\n"
			".key { color: Teal; }\n"
			".err { color: FireBrick; }\n"
			".ok { color: ForestGreen; }\n"
			"</style></head>\n"
			"<body>\n"
			"%s\n"
			"</body></html>\n", AppName, MB_OK, log.NewLStr().Get());
	}
};

DefaultClient::FileType DefaultClient::FileTypes[] =
{
	{ "Scribe.Email",		"Email",			2 },
	{ "Scribe.Folder",		"Mailbox",			0 },
	{ "Scribe.Calendar",	"Calendar Event",	6 },
	{ "Scribe.Contact",		"Contact",			4 },
	{ "Scribe.MailStore",	"Mail Store",		0 },
	{ "Scribe.Mailto",		"Mailto Protocol",	0 },
	{ NULL,					NULL,				0 }
};
#endif

class ScribePasteState : public LProgressDlg
{
	ScribeWnd *App = NULL;
	ScribeFolder *Folder = NULL;
	LString Data;
	LDataStoreI::StoreTrans Trans;
	LProgressPane *LoadPane = NULL, *SavePane = NULL;
	ScribeClipboardFmt *tl = NULL;
	uint32_t Errors = 0;
	ssize_t Idx = 0;

	enum PasteState
	{
		LoadingThings,
		SavingThings,
	}	State = LoadingThings;

public:
	ScribePasteState(ScribeWnd *app, ScribeFolder *folder, LString data) :
		LProgressDlg(app),
		App(app),
		Folder(folder),
		Data(data)
	{
		// Paste 'ScribeThingList'
		tl = (ScribeClipboardFmt*)Data.Get();

		Trans = Folder->GetObject()->GetStore()->StartTransaction();

		LoadPane = ItemAt(0);
		LoadPane->SetDescription("Loading objects...");
		LoadPane->SetRange(tl->Length());

		SavePane = Push();
		SavePane->SetRange(tl->Length());
		SavePane->SetDescription("Saving: No errors...");

		// LProgressDlg will do a SetPulse in it's OnCreate
	}

	void OnPulse()
	{
		auto Start = LCurrentTime();
		static int TimeSlice = 300; //ms

		// LgiTrace("OnPulse state=%i cancelled=%i\n", State, IsCancelled());
		if (State == LoadingThings)
		{
			while (	Idx < tl->Length() &&
					!IsCancelled() &&
					LCurrentTime() - Start < TimeSlice)
			{
				auto t = tl->ThingAt(Idx++);
				if (!t)
				{
					LgiTrace("%s:%i - not a thing\n", _FL);
					continue;
				}

				auto Obj = t->GetObject();
				if (Obj->GetInt(FIELD_LOADED) < Store3Loaded)
					Obj->SetInt(FIELD_LOADED, Store3Loaded);
			}

			Value(Idx);
			// LgiTrace("%s:%i %i >= %i\n", _FL, (int)Idx, (int)tl->Length());
			if (Idx >= tl->Length())
			{
				State = SavingThings;
				Idx = 0;
			}
		}
		else if (State == SavingThings)
		{
			// Having this check at the top means the UI gets a chance to update to it's complete state..
			if (Idx >= tl->Length())
			{
				if (Errors > 0)
					LgiMsg(this, "Failed to save %i of %i objects.", AppName, MB_OK, Errors, tl->Length());
				Quit();
				return;
			}

			while (	Idx < tl->Length() &&
					!IsCancelled() &&
					LCurrentTime() - Start < TimeSlice)
			{
				auto t = tl->ThingAt(Idx++);
				if (!t)
					continue;

				auto Obj = t->GetObject();
				LAssert(Obj->GetInt(FIELD_LOADED) == Store3Loaded); // Load loop should have done this already

				auto Dst = App->CreateItem(Obj->Type(), Folder, false);
				if (Dst)
				{
					*Dst = *t;
					Dst->Update();
					if (!Dst->Save(Folder))
					{
						LString s;
						s.Printf("Saving: " LPrintfSSizeT " error(s)", ++Errors);
						SetDescription(s);
					}
				}
				else Errors++;
			}

			SavePane->Value(Idx);
		}

		LProgressDlg::OnPulse();
	}
};

int ScribeWnd::OnCommand(int Cmd, int Event, OsView WndHandle)
{
	// Send mail multi-menu
	if (Cmd >= IDM_SEND_FROM &&
		Cmd <= IDM_SEND_FROM + (ssize_t)Accounts.Length())
	{
		Send(Cmd - IDM_SEND_FROM);
		return 0;
	}

	// Receive mail multi-menu
	if (Cmd >= IDM_RECEIVE_FROM &&
		Cmd < IDM_RECEIVE_FROM + (ssize_t)Accounts.Length())
	{
		Receive(Cmd - IDM_RECEIVE_FROM);
		return 0;
	}

	// Preview mail multi-menu
	if (Cmd >= IDM_PREVIEW_FROM &&
		Cmd < IDM_PREVIEW_FROM + (ssize_t)Accounts.Length())
	{
		Preview(Cmd - IDM_PREVIEW_FROM);
		return 0;
	}

	// Identity multi-menu
	if (Cmd >= IDM_IDENTITY_BASE &&
		Cmd <= IDM_IDENTITY_BASE + (ssize_t)Accounts.Length())
	{
		SetCurrentIdentity(Cmd - IDM_IDENTITY_BASE - 1);
		return 0;
	}

	// Is this a script tool?
	if (LScribeScript::Inst &&
		Cmd >= IDM_TOOL_SCRIPT_BASE &&
		Cmd < IDM_TOOL_SCRIPT_BASE + (int)d->Scripts.Length())
	{
		// Do tools menu callback... find the right callback....
		LArray<LScriptCallback*> c;
		if (GetScriptCallbacks(LToolsMenu, c))
		{
			for (unsigned i=0; i<c.Length(); i++)
			{
				if (c[i]->Func &&
					c[i]->Param == Cmd)
				{
					// Call the callback
					char Msg[MAX_PATH_LEN];
					LScribeScript::Inst->GetLog()->Write
						(
							Msg,
							sprintf_s(Msg, sizeof(Msg), "\n\nRunning tool script '%s'...\n", c[i]->Script->Code->GetFileName())
						);

					// Setup the arguments...
					LScriptArguments Args(NULL);
					Args.New() = new LVariant((LDom*)this);
					Args.New() = new LVariant(Cmd);

					// Call the method
					ExecuteScriptCallback(*c[i], Args);

					// Cleanup
					Args.DeleteObjects();
					break;
				}
			}
		}
		return 0;
	}

	// New from template multi-menu
	if (Cmd >= IDM_NEW_FROM_TEMPLATE &&
		Cmd < IDM_NEW_FROM_TEMPLATE + 100)
	{
		int Index = Cmd - IDM_NEW_FROM_TEMPLATE;
		ScribeFolder *Templates = GetFolder(FOLDER_TEMPLATES);
		if (Templates)
		{
			Templates->LoadThings();

			for (auto i: Templates->Items)
			{
				Mail *m = i->IsMail();
				if (m)
				{
					if (Index == 0)
					{
						Thing *t = CreateItem(MAGIC_MAIL, 0, false); // GetFolder(FOLDER_OUTBOX)
						Mail *NewMail = IsMail(t);
						if (NewMail)
						{
							*NewMail = (Thing&)*m;
							NewMail->DoUI();
							break;
						}
					}
					Index--;
				}
			}
		}
		return 0;
	}

	switch (Cmd)
	{
		// File menu
		case IDM_MANAGE_MAIL_STORES:
		{
			auto Dlg = new ManageMailStores(this);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					SaveOptions();

					if (!UnLoadFolders())
						return;

					LXmlTag *Ms = GetOptions()->LockTag(OPT_MailStores, _FL);
					if (Ms)
					{
						while (Ms->Children.Length())
							delete Ms->Children[0];
					
						LXmlTag *t = Dlg->Options.GetChildTag(OPT_MailStores);
						if (t)
						{
							for (auto c: t->Children)
							{
								LXmlTag *n = new LXmlTag;
								n->Copy(*c, true);
								Ms->InsertTag(n);
							}
						}

						GetOptions()->Unlock();
					}

					LVariant v;
					GetOptions()->SetValue(OPT_CreateFoldersIfMissing, v = true);

					if (!Dlg->Options.GetValue(OPT_StartInFolder, v))
						v.Empty();
					GetOptions()->SetValue(OPT_StartInFolder, v);

					LoadFolders(NULL);
				}
			});
			break;
		}
		case IDM_REPLICATE:
		{
			auto Dlg = new ReplicateDlg(this);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					UnLoadFolders();
					Dlg->StartProcess();
				}
			});
			break;
		}
		case IDM_SECURITY:
		{
			// Check for user perm password...
			// No point allow any old one to edit the security settings.
			auto ShowDialog = [this]()
			{
				auto Dlg = new SecurityDlg(this);
				Dlg->DoModal(NULL);
			};

			LPassword p;
			if (p.Serialize(GetOptions(), OPT_UserPermPassword, false))
			{
				GetAccessLevel(
					this,
					PermRequireUser,
					"Security Settings",
					[ShowDialog](bool Allow)
					{
						if (Allow)
							ShowDialog();
					});
			}
			else
			{
				ShowDialog();
			}
			break;
		}
		case IDM_OPTIONS:
		{
			LVariant ShowTotals;
			GetOptions()->GetValue(OPT_ShowFolderTotals, ShowTotals);

			// do the dialog
			auto Dlg = new OptionsDlg(this);
			Dlg->DoModal([this, Dlg, ShowTotals](auto dlg, auto id)
			{
				if (id)
				{
					// set up the POP3 accounts
					SetupAccounts();
					SaveOptions();

					// close any IMAP accounts that are now disabled.
					for (auto a : Accounts)
					{
						if (a->Receive.IsConfigured() &&
							a->Receive.IsPersistant())
						{
							if (a->Receive.Disabled())
								a->Receive.Disconnect();
							else
								Receive((int)a->GetIndex());
						}
					}

					// List/Tree view options update
					LVariant i;
					if (GetOptions()->GetValue(OPT_ShowFolderTotals, i) &&
						i.CastInt32() != ShowTotals.CastInt32())
					{
						Tree->UpdateAllItems();
					}
					if (GetOptions()->GetValue(OPT_PreviewLines, i))
					{
						Mail::PreviewLines = i.CastInt32() != 0;
					}
					if (MailList)
					{
						if (GetOptions()->GetValue(OPT_GridLines, i))
						{
							MailList->DrawGridLines(i.CastInt32() != 0);
						}
						MailList->Invalidate();
					}

					// date formats
					if (GetOptions()->GetValue(OPT_DateFormat, i))
					{
						int Idx = i.CastInt32();
						if (Idx >= 0 && Idx < CountOf(DateTimeFormats))
						{
							LDateTime::SetDefaultFormat(DateTimeFormats[Idx]);
						}
					}
					if (GetOptions()->GetValue(OPT_AdjustDateTz, i))
						Mail::AdjustDateTz = i.CastInt32() == 0;

					/*
					// SSL debug logging
					if (GetOptions()->GetValue(OPT_DebugSSL, i))
						SslSocket::DebugLogging = i.CastInt32() != 0;
					*/

					// Html edit menu
					if (GetOptions()->GetValue(OPT_EditControl, i))
					{
						auto mi = Menu->FindItem(IDM_HTML_EDITOR);
						if (mi) mi->Checked(i.CastInt32() != 0);
					}
				}
			});
			break;
		}
		case IDM_WORK_OFFLINE:
		{
			if (WorkOffline)
			{
				WorkOffline->Checked(!WorkOffline->Checked());
				LVariant v;
				GetOptions()->SetValue(OPT_WorkOffline, v = WorkOffline->Checked());

				if (!WorkOffline->Checked())
				{
					// Offline -> Online transition.

					// Check if any pending messages are in the Outbox
					ScribeFolder *Outbox = GetFolder(FOLDER_OUTBOX);
					if (Outbox)
					{
						bool HasMailToSend = false;

						for (auto t: Outbox->Items)
						{
							Mail *m = t->IsMail();
							if (m)
							{
								if (TestFlag(m->GetFlags(), MAIL_READY_TO_SEND))
								{
									HasMailToSend = true;
									break;
								}
							}
						}

						if (HasMailToSend)
						{
							PostEvent(M_COMMAND, IDM_SEND_MAIL,
								#ifndef __GTK_H__
								(LMessage::Param)Handle()
								#else
								0
								#endif
								);
						}
					}
				}
			}
			break;
		}
		case IDM_ITEM_FILTER:
		{
			if (GetCtrlValue(IDM_ITEM_FILTER))
			{
				if ((SearchView = new LSearchView(this)))
				{
					SearchView->Focus(true);
					SetLayout();
				}
			}
			else
			{
				DeleteObj(SearchView);
			}

			if (auto Folder = GetCurrentFolder())
				Folder->Populate(MailList, nullptr);
			break;
		}
		case IDM_PRINT:
		{
			if (MailList)
			{
				List<LListItem> Sel;
				if (MailList->LList::GetSelection(Sel))
				{
					for (auto i: Sel)
					{
						ThingType *t = dynamic_cast<ThingType*>(i);
						ThingPrint(NULL, t);
					}
				}
			}
			break;
		}
		case IDM_PRINTSETUP:
		{
			auto *p = GetPrinter();
			if (p && p->Browse(this, LPrinter::PoDefault))
			{
				LString Str;
				if (p->Serialize(Str, true))
				{
					LVariant v;
					GetOptions()->SetValue(OPT_PrintSettings, v = Str);
				}
			}
			break;
		}
		case IDM_PAGE_SETUP:
		{
			auto Dlg = new ScribePageSetup(this, GetOptions());
			Dlg->DoModal(NULL);
			break;
		}
		case IDM_EXIT:
		{
			LMouse m;
			GetMouse(m);
			d->IngoreOnClose = m.Ctrl();
			LCloseApp();
			break;
		}

		// Edit menu
		case IDM_FIND:
		{
			auto v = GetFocus();
			LDocView *doc = dynamic_cast<LDocView*>(v);
			if (doc)
			{
				doc->DoFind(NULL);
			}
			else
			{
				ScribeFolder *Folder = GetCurrentFolder();
				OpenFinder(this, Folder);
			}
			break;
		}
		case IDM_COPY:
		{
			if (MailList && MailList->Focus())
			{
				List<Thing> Lst;
				if (!MailList->GetSelection(Lst))
					break;

				// Copy 'ScribeThingList'
				ScribeClipboardFmt *tl = ScribeClipboardFmt::Alloc(Lst);
				if (!tl)
					break;

				LClipBoard Clip(this);
				if (Clip.IsOpen())
				{
					if (!Clip.Binary(d->ClipboardFormat, (uchar*)tl, tl->Sizeof(), true))
					{
						LgiMsg(this, "Couldn't set the clipboard data.", AppName, MB_OK);
					}
				}
				else
				{
					LgiMsg(this, "Couldn't open the clipboard.", AppName, MB_OK);
				}

				free(tl);
			}
			else
			{
				LViewI *v = LAppInst->GetFocus();
				if (v)
					v->PostEvent(M_COPY);
			}
			break;
		}
		case IDM_PASTE:
		{
			LViewI *v = LAppInst->GetFocus();
			if (v && v->GetWindow() != (LWindow*)this)
			{
				v->PostEvent(M_PASTE);
				break;
			}
			
			if (!MailList->Focus() && !Tree->Focus())
			{
				LgiTrace("%s:%i - List/Tree doesn't have focus.\n");
				break;
			}

			auto Folder = dynamic_cast<ScribeFolder*>(Tree->Selection());
			if (!Folder || !Folder->GetObject())
			{
				LgiMsg(this, "No current folder.", AppName, MB_OK);
				break;
			}

			LClipBoard Clip(this);
			if (!Clip.IsOpen())
			{
				LgiMsg(this, "Couldn't open the clipboard.", AppName, MB_OK);
				break;
			}

			Clip.Binary(d->ClipboardFormat,
				[this, Folder](auto Data, auto Err)
				{
					if (Data)
					{
						if (ScribeClipboardFmt::IsThing(Data.Get(), Data.Length()))
						{
							new ScribePasteState(this, Folder, Data);
						}
					}
					else
					{
						LgiMsg(this, "Couldn't get the clipboard data: %s", AppName, MB_OK, Err.Get());
					}
				});
			break;
		}
		case IDM_DELETE:
		{
			LViewI *f = LAppInst->GetFocus();
			LEdit *e = dynamic_cast<LEdit*>(f);
			if (e)
			{
				// This handles the case where on a mac the menu eats the delete key, even
				// when the edit control needs it
				LKey k(LK_DELETE, 0);
				k.Down(true);
				f->OnKey(k);
				k.Down(false);
				f->OnKey(k);
			}
			else
			{
				OnDelete();
			}
			break;
		}
		case IDM_DELETE_AS_SPAM:
		{
			if (!MailList)
			{
				LAssert(0);
				break;
			}

			List<LListItem> Sel;
			MailList->GetSelection(Sel);
			int Index = -1;

			for (auto i: Sel)
			{
				if (auto m = IsMail(i))
				{
					if (Index < 0)
						Index = MailList->IndexOf(i);
					m->DeleteAsSpam(this);
				}
				else LgiTrace("%s:%i - can't mark as spam things that aren't mail.\n", _FL);
			}

			if (Index >= 0)
			{
				auto i = MailList->ItemAt(Index);
				if (!i)
					i = MailList->ItemAt(MailList->Length()-1);
				if (i)
					i->Select(true);
			}
			break;
		}
		case IDM_REFRESH:
		{
			auto f = GetCurrentFolder();
			if (!f)
				break;
			
			auto s = DomToStr(SdRefresh);
			if (auto obj = f->GetFldObj())
				obj->OnCommand(s);
			break;
		}

		// Mail menu
		case IDM_NEW_EMAIL:
		{
			CreateMail();
			break;
		}
		case IDM_SET_READ:
		case IDM_SET_UNREAD:
		{
			ScribeFolder *f = GetCurrentFolder();
			if (!f)
				break;

			bool SetRead = Cmd == IDM_SET_READ;
			f->LoadThings();

			LArray<LDataI*> Change;
			for (auto t: f->Items)
			{
				Mail *m = t->IsMail();
				if (m && m->Select())
					Change.Add(m->GetObject());
			}

			LVariant v = MAIL_READ;
			LDataStoreI *Store = f->GetObject()->GetStore();
			if (Store->Change(Change, FIELD_FLAGS, v, SetRead ? OpPlusEquals : OpMinusEquals) == Store3Error)
			{
				for (auto t : f->Items)
				{
					Mail *m = t->IsMail();
					if (!m)
						continue;

					if (!m->Select())
						continue;

					if (SetRead)
						m->SetFlags(m->GetFlags() | MAIL_READ);
					else
						m->SetFlags(m->GetFlags() & ~MAIL_READ);
				}
			}
			break;
		}
		case IDM_REPLY:
		case IDM_REPLY_ALL:
		{
			if (MailList)
				MailReplyTo(IsMail(MailList->GetSelected()), (Cmd == IDM_REPLY_ALL));
			break;
		}
		case IDM_FORWARD:
		{
			if (MailList)
				MailForward(IsMail(MailList->GetSelected()));
			break;
		}
		case IDM_BOUNCE:
		{
			if (MailList)
				MailBounce(IsMail(MailList->GetSelected()));
			break;
		}
		case IDM_SEND_MAIL:
		{
			Send();
			break;
		}
		case IDM_RECEIVE_AND_SEND:
		{
			d->SendAfterReceive = true;
			PostEvent(M_COMMAND, IDM_RECEIVE_MAIL, (LMessage::Param)FindControl(IDM_RECEIVE_MAIL));
			break;
		}
		case IDM_THREAD:
		{
			if (!MailList)
				break;

			if (auto f = GetCurrentFolder())
			{
				f->SetThreaded(!f->GetThreaded());
				f->Populate(MailList, nullptr);
			}
			break;
		}
		case IDM_RECEIVE_ALL:
		{
			#define LOG_RECEIVE_ALL		0
			int i = 0;
			
			Accounts.Sort([](auto a, auto b){ return a->Compare(b); });
			
			for (auto a : Accounts)
			{
				#if LOG_RECEIVE_ALL
				auto name = a->Identity.Name();
				auto email = a->Identity.Email();
				LString desc;
				desc.Printf("%s/%s", name.Str(), email.Str());
				#endif
				
				if (!a->Receive.IsConfigured())
				{
					#if LOG_RECEIVE_ALL
					LgiTrace("%s:%i - %i/%s not configured.\n", _FL, a->GetIndex(), desc.Get());
					#endif
				}
				else if (a->Receive.Disabled() > 0)
				{
					#if LOG_RECEIVE_ALL
					LgiTrace("%s:%i - %i/%s is disabled.\n", _FL, a->GetIndex(), desc.Get());
					#endif
				}
				else
				{
					#if LOG_RECEIVE_ALL				
					LgiTrace("%s:%i - %i/%s will connect.\n", _FL, a->GetIndex(), desc.Get());
					#endif
					Receive((int) a->GetIndex());
				}
				i++;
			}
			break;
		}
		case IDM_RECEIVE_MAIL:
		{
			LVariant Def;
			if (GetOptions()->GetValue(OPT_Pop3DefAction, Def) && Def.CastInt32() == 0)
				return OnCommand(IDM_RECEIVE_ALL, 0, NULL);

			Receive(0);
			break;
		}
		case IDM_PREVIEW_POP3:
		{
			LArray<ScribeAccount*> Account;

			Accounts.Sort([](auto a, auto b){ return a->Compare(b); });

			for (auto a: Accounts)
			{
				if (!a->Receive.IsConfigured() ||
					a->Receive.Disabled())
					continue;
				
				auto Protocol = ProtocolToEnum(a->Receive.Protocol().Str());
				if (Protocol == ProtocolPop3)
				{
					Account.Add(a);
					break;
				}
			}
			
			if (Account.Length() > 0)
				OpenPopView(this, Account);
			else
				LPopupNotification::Message(this, "No suitable accounts to preview.");

			break;
		}

		case IDM_CALENDAR:
		{
			if (auto folder = GetFolder(FOLDER_CALENDAR))
				OpenCalender(folder);
			break;
		}

		// Contact menu
		case IDM_NEW_CONTACT:
		{
			CreateItem(MAGIC_CONTACT, NULL);
			break;
		}
		case IDM_NEW_GROUP:
		{
			CreateItem(MAGIC_GROUP, NULL);
			break;
		}

		// Filter menu
		case IDM_NEW_FILTER:
		{
			Thing *t = CreateItem(MAGIC_FILTER, NULL, false);
			if (t)
			{
				t->IsFilter()->SetIncoming(true);
				t->DoUI();
			}
			break;
		}
		case IDM_FILTER_CURRENT_FOLDER:
		{
			ScribeFolder *Folder = GetCurrentFolder();
			if (Folder)
			{
				List<Filter> Filters;
				GetFilters(Filters, false, false, true);

				List<Mail> Src;
				for (auto i: Folder->Items)
				{
					if (i->IsMail())
					{
						Src.Insert(i->IsMail());
					}
				}

				if (!Src[0])
				{
					LgiMsg(this, LLoadString(IDS_NO_MAIL_TO_FILTER), AppName);
				}
				else
				{
					Filter::ApplyFilters(this, Filters, Src);
				}
			}
			break;
		}
		case IDM_FILTER_SELECTION:
		{
			ScribeFolder *Folder = GetCurrentFolder();
			if (Folder)
			{
				List<Filter> Filters;
				GetFilters(Filters, false, false, true);

				List<Mail> Src;
				for (auto i: Folder->Items)
				{
					if (i->IsMail() && i->Select())
					{
						Src.Insert(i->IsMail());
					}
				}

				if (Src.Length())
				{
					Filter::ApplyFilters(this, Filters, Src);
				}
			}
			break;
		}
		case IDM_DEBUG_FILTERS:
		{
			auto i = Menu->FindItem(IDM_DEBUG_FILTERS);
			if (i)
			{
				i->Checked(!i->Checked());
			}
			break;
		}
		case IDM_HTML_EDITOR:
		{
			auto i = Menu->FindItem(IDM_HTML_EDITOR);
			if (i)
			{
				i->Checked(!i->Checked());
				
				LVariant v;
				GetOptions()->SetValue(OPT_EditControl, v = i->Checked() ? 1 : 0);
			}
			break;
		}
		case IDM_FILTERS_DISABLE:
		{
			if (d->DisableUserFilters)
			{
				d->DisableUserFilters->Checked(!d->DisableUserFilters->Checked());

				LVariant v;
				GetOptions()->SetValue(OPT_DisableUserFilters, v = d->DisableUserFilters->Checked());
			}			
			break;
		}
		case IDM_BUILD_BAYES_DB:
		{
			BuildSpamDb();
			break;
		}
		case IDM_BAYES_STATS:
		{
			BuildStats();
			break;
		}
		case IDM_BAYES_SETTINGS:
		{
			auto Dlg = new BayesDlg(this);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (!id)
					return;

				OnSettingsChange();

				LVariant i;
				if (!GetOptions()->GetValue(OPT_BayesFilterMode, i))
					return;

				ScribeBayesianFilterMode m = ((ScribeBayesianFilterMode)i.CastInt32());
				if (m != BayesOff)
				{
					LVariant SpamPath, ProbablyPath;
					GetOptions()->GetValue(OPT_SpamFolder, SpamPath);
					GetOptions()->GetValue(OPT_BayesMoveTo, ProbablyPath);
											
					if (m == BayesFilter)
					{
						auto Spam = GetFolder(SpamPath.Str());
						if (!Spam)
						{							
							if (auto RelevantStore = GetMailStoreForPath(SpamPath.Str()))
							{
								LString p = SpamPath.Str();
								auto a = p.SplitDelimit("/");
									
								Spam = RelevantStore->GetRoot();
								for (unsigned i=1; i<a.Length(); i++)
								{
									auto c = Spam->GetSubFolder(a[i]);
									if (!c)
										c = Spam->CreateSubFolder(a[i], MAGIC_MAIL);
									Spam = c;
								}
							}
						}
							
						if (Spam)
						{
							LVariant v;
							GetOptions()->SetValue(OPT_HasSpam, v = 1);
						}
					}
					else if (m == BayesTrain)
					{
						auto Probably = GetFolder(ProbablyPath.Str());
						if (!Probably)
						{
							LgiMsg(this, "Couldn't find the folder '%s'", AppName, MB_OK, ProbablyPath.Str());
						}
					}
				}
			});
			break;
		}
		case IDM_BAYES_CHECK:
		{
			List<LListItem> Sel;
			if (MailList) MailList->GetSelection(Sel);

			for (auto i: Sel)
			{
				Thing *t = dynamic_cast<Thing*>(i);
				if (t)
				{
					Mail *m = t->IsMail();
					if (m)
					{
						d->BayesLog.Empty();
						
						double SpamRating = 0.0;
						IsSpam(SpamRating, m, true);
						break;
					}
				}
			}
			break;
		}

		// Tools menu
		case IDM_SCRIPTING_CONSOLE:
		case IDM_SHOW_CONSOLE:
		{
			ShowScriptingConsole();

			if (d->ShowConsoleBtn)
				d->ShowConsoleBtn->Image(IMG_CONSOLE_NOMSG);
			break;
		}
		case IDM_EXPORT_TEXT_MBOX:
		{
			Export_UnixMBox(this);
			break;
		}
		case IDM_IMPORT_CSV:
		{
			ImportCsv(this);
			break;
		}
		case IDM_EXPORT_CSV:
		{
			ExportCsv(this);
			break;
		}
		case IDM_IMPORT_EML:
		{
			ImportEml(this);
			break;
		}
		case IDM_EXPORT_SCRIBE:
		{
			ExportScribe(this, NULL/* default mail store */);
			break;
		}
		case IDM_IMPORT_TEXT_MBOX:
		{
			Import_UnixMBox(this);
			break;
		}
		case IDM_IMP_EUDORA_ADDR:
		{
			Import_EudoraAddressBook(this);
			break;
		}
		case IDM_IMP_MOZILLA_ADDR:
		{
			Import_MozillaAddressBook(this);
			break;
		}
		case IDM_IMP_MOZILLA_MAIL:
		{
			Import_MozillaMail(this);
			break;
		}
		#if WINNATIVE
		case IDM_IMPORT_OUTLOOK_PAB:
		{
			Import_OutlookContacts(this);
			break;
		}
		case IDM_IMPORT_OUTLOOK_ITEMS:
		{
			Import_Outlook(this, IMP_OUTLOOK);
			break;
		}
		case IDM_EXPORT_OUTLOOK_ITEMS:
		{
			Export_Outlook(this);
			break;
		}
		#endif
		case IDM_IMP_MBX_EMAIL:
		{
			Import_OutlookExpress(this, false); // v4
			break;
		}
		case IDM_IMP_DBX_EMAIL:
		{
			Import_OutlookExpress(this); // v5
			break;
		}
		case IDM_IMPORT_NS_CONTACTS:
		{
			Import_NetscapeContacts(this);
			break;
		}
		case IDM_CHECK_UPDATE:
		{
			LVariant v;
			GetOptions()->GetValue(OPT_SoftwareUpdateIncBeta, v);
			SoftwareUpdate(this, true, v.CastInt32() != 0, [](auto goingToUpdate)
			{
				if (goingToUpdate)
					LCloseApp();
			});
			break;
		}
		case IDM_LOGOUT:
		{
			CurrentAuthLevel = PermRequireNone; 
			auto i = Menu->FindItem(IDM_LOGOUT);
			if (i)
				i->Enabled(false);
			break;
		}
		case IDM_LAYOUT1:
		{
			LVariant v;
			int TwoThirds = GetClient().Y() >> 1;
			GetOptions()->SetValue(OPT_SplitterPos, v = 200);
			GetOptions()->SetValue(OPT_SubSplitPos, v = TwoThirds);
			SetLayout(FoldersListAndPreview);
			break;
		}
		case IDM_LAYOUT2:
		{
			LVariant v;
			int TwoThirds = GetClient().Y() >> 1;
			GetOptions()->SetValue(OPT_SplitterPos, v = TwoThirds);
			GetOptions()->SetValue(OPT_SubSplitPos, v = 200);
			SetLayout(PreviewOnBottom);
			break;
		}
		case IDM_LAYOUT3:
		{
			LVariant v;
			GetOptions()->SetValue(OPT_SplitterPos, v = 200);
			SetLayout(FoldersAndList);
			break;
		}
		case IDM_LAYOUT4:
		{
			LVariant v;
			GetOptions()->SetValue(OPT_SplitterPos, v = 200);
			GetOptions()->SetValue(OPT_SubSplitPos, v);
			SetLayout(ThreeColumn);
			break;
		}
		case IDM_CRASH:
		{
			int *Crash = 0;
			*Crash = true;
			break;
		}
		case IDM_DUMP_MEM:
		{
			LDumpMemoryStats(0);
			break;
		}
		case IDM_SCRIPT_DEBUG:
		{
			LVariant v;
			if (GetOptions())
				GetOptions()->SetValue(OPT_ScriptDebugger, v = true);

			LVirtualMachine *vm = new LVirtualMachine(d);
			if (!vm)
				break;

			LVmDebugger *dbg = vm->OpenDebugger();
			if (!dbg)
				break;
			break;
		}
		case IDM_SCRIPT_BREAK_ON_WARN:
		{
			auto mi = GetMenu()->FindItem(IDM_SCRIPT_BREAK_ON_WARN);
			if (!mi)
				break;
			LVirtualMachine::BreakOnWarning = !mi->Checked();
			mi->Checked(LVirtualMachine::BreakOnWarning);
			break;
		}
		case IDM_UNIT_TESTS:
		{
			#ifdef _DEBUG
			UnitTests([this](auto ok)
			{
				LgiMsg(this, "UnitTest status: %i", AppName, MB_OK, ok);
			});
			#endif
			break;
		}

		// Help menu
		case IDM_HELP:
		{
			LaunchHelp("index.html");
			// LgiMsg(this, LLoadString(IDS_ERROR_NO_HELP), AppName, MB_OK);
			break;
		}
		case IDM_FEEDBACK:
		{
			LVariant e;
			if (GetOptions()->GetValue("author", e))
				CreateMail(0, e.Str());
			else
				CreateMail(0, AuthorEmailAddr);
			break;
		}
		case IDM_MEMECODE:
		{
			LExecute(AuthorHomepage);
			break;
		}
		case IDM_HOMEPAGE:
		{
			LVariant hp;
			if (GetOptions()->GetValue("homepage", hp))
				LExecute(hp.Str());
			else
				LExecute(ApplicationHomepage);
			break;
		}
		case IDM_VERSION_HISTORY:
		{
			LExecute("http://www.memecode.com/site/ver.php?id=445");
			break;
		}
		case IDM_DEBUG_INFO:
		{
			char s[256];
			sprintf_s(s, sizeof(s), "%s#debug", ApplicationHomepage);
			LExecute(s);
			break;
		}
		case IDM_TUTORIALS:
		{
			LExecute("http://www.memecode.com/scribe/tutorials");
			break;
		}
		case IDM_INSCRIBE_LINK:
		{
			LExecute(CommercialHomepage);
			break;
		}
		case IDM_SCRIBE_FAQ:
		{
			LExecute(FaqHomepage);
			break;
		}
		case IDM_ABOUT:
		{
			extern void ScribeAbout(ScribeWnd *Parent);
			ScribeAbout(this);
			break;
		}

#ifdef WINDOWS
		case IDM_CLEAR_REGISTRY:
		{
			DefaultClient defaultClient;
			defaultClient.CleanRegistry(this);
			break;
		} 
#endif

		default:
		{			
			if (d->ScriptToolbar)
				d->ScriptToolbar->ExecuteCallbacks(this, 0, 0, Cmd);
			break;
		}
	}

	return 0;
}

void ScribeWnd::OnDelete()
{
	LVariant ConfirmDelete;
	GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

	if (!ConfirmDelete.CastInt32() ||
		LgiMsg(this, LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
	{
		LArray<LDataI*> Del;
		
		if (Tree && Tree->Focus())
		{
			ScribeFolder *Item = dynamic_cast<ScribeFolder*>(Tree->Selection());
			if (Item)
			{
				Tree->OnDelete(Item, false);
			}
		}
		else if (MailList
			#ifdef MAC
			&& MailList->Focus()
			#endif
			)
		{
			List<LListItem> Sel;
			MailList->GetSelection(Sel);

			for (auto i: Sel)
			{
				Thing *t = dynamic_cast<Thing*>(i);
				if (t)
					Del.Add(t->GetObject());
			}

			if (Del.Length())
			{
				auto Store = Del[0]->GetStore();
				Store->Delete(Del, true);
			}
			else LgiTrace("%s:%i - Nothing to delete\n", _FL);

			#ifndef MAC
			MailList->Focus(true);
			#endif
		}
	}
}

int ScribeWnd::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_THING_LIST:
		{
			if (n.Type == LNotifyReturnKey)
			{
				LListItem *i = MailList ? MailList->GetSelected() : 0;
				Thing *t = dynamic_cast<Thing*>(i);
				if (t)
				{
					t->DoUI();
				}
			}
			else if (n.Type == LNotifyDeleteKey)
			{
				/* This is now handled by the menu
				OnDelete();
				return true;
				*/
			}
			
			if (SearchView &&
				MailList)
			{
				SearchView->OnNotify(Ctrl, n);
			}
			break;
		}
		case IDC_TEXT:
		{
			if (PreviewPanel)
			{
				PreviewPanel->OnNotify(Ctrl, n);
			}
			break;
		}
	}

	return 0;
}

void ScribeWnd::AddThingSrc(ScribeFolder *src)
{
	if (!d->ThingSources.HasItem(src))
	{
		if (src->GetObject())
			d->ThingSources.Add(src);
		else
			LAssert(!"Not a valid source: no object");
	}
}

void ScribeWnd::RemoveThingSrc(ScribeFolder *src)
{
	d->ThingSources.Delete(src);
}

LArray<ScribeFolder*> ScribeWnd::GetThingSources(Store3ItemTypes Type)
{
	LArray<ScribeFolder*> a;
	LArray<ScribeFolder*> del;

	for (auto f: d->ThingSources)
	{
		if (!f->GetObject())
		{
			LAssert(!"missing object?");
			del.Add(f);
		}
		else if (f->GetItemType() == Type &&
			!f->IsInTrash())
		{
			a.Add(f);
		}
	}

	// Clean up folders that we can't use
	for (auto f: del)
		d->ThingSources.Delete(f);

	return a;
}

bool ScribeWnd::LogFilterActivity()
{
	auto i = Menu->FindItem(IDM_DEBUG_FILTERS);
	return i ? i->Checked() : false;
}

bool ScribeWnd::CreateFolders(LAutoString &FileName)
{
	bool Status = false;

	if (FileName)
	{
		auto Ext = LGetExtension(FileName);
		if (!Ext)
		{
			char File[300];
			strcpy_s(File, sizeof(File), FileName);
			strcat(File, ".mail3");
			FileName.Reset(NewStr(File));
		}

		// Create objects, and then close the file.. it'll be reloaded later
		LAutoPtr<LDataStoreI> m(CreateDataStore(FileName, true));
		if (m)
		{
			m->GetRoot(true);
			Status = true;
		}
		else LgiTrace("%s:%i - CreateDataStore failed.\n", _FL);
	}
	else LgiTrace("%s:%i - No file name for CreateFolder.\n", _FL);

	return Status;
}

bool ScribeWnd::CompactFolders(LMailStore &Store, bool Interactive)
{
	if (!Store.Store)
		return false;

	auto Dlg = new Store3Progress(this, Interactive);

	Dlg->SetDescription(LLoadString(IDS_CHECKING_OBJECTS));

	bool Offline = false;
	if (WorkOffline)
	{
		Offline = WorkOffline->Checked();
		WorkOffline->Checked(true);
	}

	Store.Store->Compact(this, Dlg, [this, Offline, Dlg](auto status)
	{
		LAssert(InThread());

		if (WorkOffline)
			WorkOffline->Checked(Offline);

		delete Dlg;
	});

	return true;
}

CalendarSource *CalendarSource::Create(ScribeWnd *App, const char *ObjName, const char *Id)
{
	if (!Stricmp(ObjName, "RemoteCalendarSource"))
		return new RemoteCalendarSource(App, Id);
	return new FolderCalendarSource(App, Id);
}

int ScribeWnd::GetCalendarSources(LArray<CalendarSource*> &Out)
{
	static bool Loaded = false;

	if (!Loaded)
	{
		Loaded = true;
		CalendarSource::SetCreateIn(NULL);

		LVariant Create;
		GetOptions()->GetValue(OPT_CalendarCreateIn, Create);

		// This should be a list of all calendar folders in ANY mail store...
		auto CalFlds = GetThingSources(MAGIC_CALENDAR);

		LXmlTag *t = GetOptions()->LockTag(OPT_CalendarSources, _FL);
		if (t)
		{
			bool AutoPopulate = t->Children.Length() == 0;

			for (auto c: t->Children)
			{
				auto s = CalendarSource::Create(this,
												c->GetAttr(CalendarSource::OptObject),
												c->GetTag());
				if (s && s->Read())
				{
					// Add known source...
					if (!Stricmp(Create.Str(), c->GetTag()))
					{
						CalendarSource::SetCreateIn(s);							
					}

					// Remove from CalFlds
					FolderCalendarSource *Fcs = dynamic_cast<FolderCalendarSource*>(c);
					if (Fcs)
					{
						auto Path = Fcs->GetPath();
						for (auto c: CalFlds)
						{
							if (c->GetPath().Equals(Path))
							{
								CalFlds.Delete(c);
								break;
							}
						}
					}
				}
			}

			if (AutoPopulate)
			{
				// Now CalFlds should be a list of all calendar folders NOT in the source XML tag
				for (auto c: CalFlds)
				{
					FolderCalendarSource *s = new FolderCalendarSource(this);
					if (s)
					{
						// So add an entry to track it...
						auto Path = c->GetPath();
						s->SetPath(Path);
						s->SetDisplay(true);
						s->SetColour(CalendarSource::FindUnusedColour());
						s->Write();
					}
				}
			}
		
			GetOptions()->Unlock();
		}

		if (!CalendarSource::GetCreateIn() &&
			CalendarSource::GetSources().Length())
		{
			CalendarSource::SetCreateIn(CalendarSource::GetSources().ItemAt(0));
		}
	}

	for (unsigned i=0; i<CalendarSource::GetSources().Length(); i++)
		Out.Add(CalendarSource::GetSources().ItemAt(i));
	return (int)Out.Length();
}

int ScribeWnd::GetFolderType(ScribeFolder *f)
{
	if (!f)
		return -1;

	for (SystemFolderInfo *fi = SystemFolders; fi->PathOption; fi++)
	{
		bool Check = true;
		if (fi->HasOption)
		{
			LVariant v;
			if (GetOptions()->GetValue(fi->HasOption, v))
				Check = v.CastInt32() != 0;
		}
		if (Check)
		{		
			ScribeFolder *c = GetFolder(fi->Id);
			if (c == f)
				return fi->Id;
		}
	}

	return -1;
}

ScribeFolder *ScribeWnd::GetCurrentFolder()
{
	if (!Tree)
		return NULL;

	auto *Item = Tree->Selection();
	if (!Item)
		return NULL;

	return dynamic_cast<ScribeFolder*>(Item);
}

bool ScribeWnd::GetSystemPath(int Folder, LVariant &Path)
{
	char KeyName[64];
	sprintf_s(KeyName, sizeof(KeyName), "Folder-%i", Folder);
	return GetOptions()->GetValue(KeyName, Path);
}

LMailStore *ScribeWnd::GetMailStoreForIdentity(const char *IdEmail)
{
	LVariant Tmp;
	if (!IdEmail)
	{
		// Get current identity
		ScribeAccount *Cur = GetCurrentAccount();
		if (Cur)
		{
			Tmp = Cur->Identity.Email();
			IdEmail = Tmp.Str();
		}
	}
	
	if (!IdEmail)
		return NULL;
	
	ScribeAccount *a = NULL;
	for (auto Acc: Accounts)
	{
		LVariant e = Acc->Identity.Email();
		if (e.Str() && !_stricmp(e.Str(), IdEmail))
		{
			a = Acc;
			break;
		}
	}
	if (!a)
		return NULL;

	auto DestPath = a->Receive.DestinationFolder();
	if (!DestPath.Str())
		return NULL;
	
	return GetMailStoreForPath(DestPath.Str());
}

ScribeFolder *ScribeWnd::GetFolder(int Id, LDataI *s)
{
	if (s)
	{
		for (auto &f: Folders)
			if (s->GetStore() == f.Store)
				return GetFolder(Id, &f);
	}

	return GetFolder(Id);
}

ScribeFolder *ScribeWnd::GetFolder(int Id, LMailStore *Store, bool Quiet)
{
	char KeyName[64];
	sprintf_s(KeyName, sizeof(KeyName), "Folder-%i", Id);
	LVariant FolderName;
	bool NoOption = false;

	if (GetOptions()->GetValue(KeyName, FolderName))
	{
		if (ValidStr(FolderName.Str()) &&
			strlen(FolderName.Str()) > 0)
		{
			if (auto c = GetFolder(FolderName.Str(), Store))
			{
				return c;
			}
			else if (!Quiet)
			{
				LgiTrace("%s:%i - '%s' doesn't exist.\n",
					_FL, FolderName.Str());
			}
		}
	}
	else if (!Quiet)
	{
		NoOption = true;
	}

	switch (Id)
	{
		case FOLDER_INBOX:
		case FOLDER_OUTBOX:
		case FOLDER_SENT:
		case FOLDER_TRASH:
		case FOLDER_CONTACTS:
		case FOLDER_TEMPLATES:
		case FOLDER_FILTERS:
		case FOLDER_CALENDAR:
		case FOLDER_GROUPS:
		case FOLDER_SPAM:
		{
			auto c = GetFolder(DefaultFolderNames[Id], Store);
			if (!c)
			{
				// if (!Quiet)
				// LgiTrace("%s:%i - Default folder '%s' doesn't exist.\n", _FL, DefaultFolderNames[Id]);
			}
			else if (NoOption)
			{
				auto p = c->GetPath();
				GetOptions()->SetValue(KeyName, FolderName = p.Get());
			}
			return c;
		}
	}

	return NULL;
}

bool ScribeWnd::OnMailStore(LMailStore **MailStore, bool Add)
{
	THREAD_UNSAFE(false);
	if (!MailStore)
	{
		LAssert(!"No mail store pointer?");
		return false;
	}
	
	if (Add)
	{
		*MailStore = &Folders.New();
		if (*MailStore)
			return true;
	}
	else
	{
		ssize_t Idx = *MailStore - &Folders[0];
		if (Idx >= 0 && Idx < (ssize_t)Folders.Length())
		{
			Folders.DeleteAt(Idx, true);
			*MailStore = NULL;
			return true;
		}
		else
		{
			LAssert(!"Index out of range.");
		}
	}
	
	return false;
}

LMailStore *ScribeWnd::GetMailStoreForPath(const char *Path)
{
	THREAD_UNSAFE(NULL);
	if (!Path)
		return NULL;

	auto t = LString(Path).SplitDelimit("/");
	if (t.Length() > 0)
	{
		const char *First = t[0];
		
		// Find the mail store that that t[0] refers to
		for (unsigned i=0; i<Folders.Length(); i++)
		{
			if (Folders[i].IsOk())
			{
				const char *RootStr = Folders[i].GetRoot()->GetText();
				if (RootStr && !_stricmp(RootStr, First))
				{
					return &Folders[i];
				}
			}
		}
	}
	
	return NULL;
}

ScribeFolder *ScribeWnd::GetFolder(const char *Name, LMailStore *s)
{
	if (!ValidStr(Name))
		return NULL;

	ScribeFolder *Folder = NULL;
	LString Sep("/");
	auto t = LString(Name).Split(Sep);
	LString TmpName;

	auto trimFirstSeg = [&]()
	{
		TmpName = Sep.Join(t.Slice(1));
		Name = TmpName;
	};

	if (t.Length() > 0)
	{
		if (!s)
		{
			s = GetMailStoreForPath(Name);
			/*
			if (!s)
			{
				// IMAP folders?
				for (auto a: Accounts)
				{
					ScribeProtocol Proto = a->Receive.ProtocolType();
					if (Proto == ProtocolImapFull)
					{
						ScribeFolder *Root = a->Receive.GetRootFolder();
						if (Root)
						{
							const char *RootStr = Root->GetText();
							if (RootStr &&
								a->Receive.GetDataStore() &&
								!_stricmp(RootStr, t[0]))
							{
								tmp.Root = Root;
								tmp.Store = a->Receive.GetDataStore();
								s = &tmp;
								break;
							}
						}
					}
				}
			}
			*/
			if (s)
				trimFirstSeg();
		}
		else if (s->GetRoot())
		{
			// Check if the store name is on the start of the folder
			auto RootName = s->GetRoot()->GetName(true);
			if (RootName.Equals(t[0]))
				trimFirstSeg();
		}
	}

	if (!s)
	{
		s = GetDefaultMailStore();
	}

	if (s && Name)
	{
		if (_stricmp(Name, "/") == 0)
			return s->GetRoot();
	
		Folder = s->GetRoot() ? s->GetRoot()->GetSubFolder(Name) : NULL;
	}

	return Folder;
}

void ScribeWnd::Update(int What)
{
	THREAD_UNSAFE();
	if (What & UPDATE_TREE)
	{
		Tree->Invalidate();
		return;
	}
	if (What & UPDATE_LIST)
	{
		if (MailList) MailList->Invalidate();
		return;
	}
}

void ScribeWnd::DoDebug(char *s)
{
}

Thing *ScribeWnd::CreateThingOfType(Store3ItemTypes Type, LDataI *obj)
{
	THREAD_UNSAFE(NULL);
	Thing *t = NULL;
	switch (Type)
	{
		case MAGIC_CONTACT:
		{
			t = new Contact(this, obj);
			break;
		}
		case MAGIC_MAIL:
		{
			t = new Mail(this, obj);
			break;
		}
		case MAGIC_ATTACHMENT:
		{
			t = new Attachment(this, obj);
			break;
		}
		case MAGIC_FILTER:
		{
			t = new Filter(this, obj);
			break;
		}
		case MAGIC_CALENDAR:
		{
			t = new Calendar(this, obj);
			break;
		}
		case MAGIC_GROUP:
		{
			t = new ContactGroup(this, obj);
			break;
		}
		default:
			break;
	}

	if (t)
	{
		t->App = this;
	}

	return t;
}

void ScribeWnd::GetFilters(List<Filter> &Filters, bool JustIn, bool JustOut, bool JustInternal)
{
	THREAD_UNSAFE();
	auto Srcs = GetThingSources(MAGIC_FILTER);
	for (auto f: Srcs)
	{
		for (auto t: f->Items)
		{
			Filter *Ftr = t->IsFilter();
			if (Ftr)
			{
				if (JustIn && !Ftr->GetIncoming())
					continue;

				if (JustOut && !Ftr->GetOutgoing())
					continue;
				
				if (JustInternal && !Ftr->GetInternal())
					continue;

				Filters.Insert(Ftr);
			}
		}
	}

	Filters.Sort([](auto a, auto b)
		{
			return a->GetIndex() - b->GetIndex();
		});
}

bool ScribeWnd::ShowToolbarText()
{
	THREAD_UNSAFE(false);
	LVariant i;
	if (GetOptions()->GetValue(OPT_ToolbarText, i))
	{
		return i.CastInt32() != 0;
	}

	GetOptions()->SetValue(OPT_ToolbarText, i = true);
	return true;
}

void ScribeWnd::HashContacts(LHashTbl<StrKey<char,false>,Contact*> &Contacts, ScribeFolder *Folder, bool Deep)
{
	THREAD_UNSAFE();
	if (!Folder)
	{
		// Default item is the contacts folder
		Folder = GetFolder(FOLDER_CONTACTS);

		// Also look at all the contact sources...
		auto Srcs = GetThingSources(MAGIC_CONTACT);
		for (auto Src: Srcs)
		{
			for (auto t: Src->Items)
			{
				Contact *c = t->IsContact();
				if (!c)
					continue;

				auto emails = c->GetEmails();
				for (auto e: emails)
				{
					if (!Contacts.Find(e))
						Contacts.Add(e, c);
				}
			}
		}
	}

	// recurse through each folder and make a list
	// of every contact object we find.
	if (Folder)
	{
		Folder->LoadThings();

		for (auto t: Folder->Items)
		{
			Contact *c = t->IsContact();
			if (c)
			{
				auto Emails = c->GetEmails();
				for (auto e: Emails)
					if (e && !Contacts.Find(e))
						Contacts.Add(e, c);
			}
		}

		for (auto f = Folder->GetChildFolder(); Deep && f; f = f->GetNextFolder())
		{
			HashContacts(Contacts, f, Deep);
		}
	}
}

List<Contact> *ScribeWnd::GetEveryone()
{
	THREAD_UNSAFE(NULL);
	return &Contact::Everyone;
}

bool ScribeWnd::GetContacts(List<Contact> &Contacts, ScribeFolder *Folder, bool Deep)
{
	THREAD_UNSAFE(false);
	LArray<ScribeFolder*> Folders;

	if (!Folder)
	{
		Folders = GetThingSources(MAGIC_CONTACT);
		auto f = GetFolder(FOLDER_CONTACTS);
		if (f && !Folders.HasItem(f))
			Folders.Add(f);
	}
	else
		Folders.Add(Folder);

	if (!Folders.Length())
		return false;

	for (auto f: Folders)
	{
		// recurse through each folder and make a list
		// of every contact object we find.
		ScribePerm Perm = f->GetFolderPerms(ScribeReadAccess);
		bool Safe = CurrentAuthLevel >= Perm;
		if (Safe)
		{
			f->LoadThings();
	
			for (auto t: f->Items)
			{
				Contact *c = t->IsContact();
				if (c)
					Contacts.Insert(c);
			}

			for (ScribeFolder *c = f->GetChildFolder(); Deep && c; c = c->GetNextFolder())
				GetContacts(Contacts, c, Deep);
		}
	}
	
	return true;
}

/*
	This function goes through the database and checks for some
	basic requirements and fixes things up if they aren't ok.
*/
bool ScribeWnd::ValidateFolder(LMailStore *s, int Id)
{
	THREAD_UNSAFE(false);
	char OptName[32];
	sprintf_s(OptName, sizeof(OptName), "Folder-%i", Id);
	
	LVariant Path;
	if (!GetOptions()->GetValue(OptName, Path))
	{
		char Opt[256];
		sprintf_s(Opt, sizeof(Opt), "/%s", DefaultFolderNames[Id]);
		GetOptions()->SetValue(OptName, Path = Opt);
	}
	
	// If the path name has the store name at the start, strip that off...
	LString Sep("/");
	LString::Array Parts = LString(Path.Str()).Split(Sep);
	if (Parts.Length() > 1)
	{
		if (Parts[0].Equals(s->Name))
		{
			Parts.DeleteAt(0, true);
			Path = Sep.Join(Parts);
		}
		else
		{
			LMailStore *ms = GetMailStoreForPath(Path.Str());
			if (ms)
			{
				s = ms;
			}
			else
			{
				// Most likely the user has renamed something and broken the
				// path. Lets just error out instead of creating the wrong folder
				return false;
			}			
		}
	}
	
	// Now resolve the path...
	ScribeFolder *Folder = GetFolder(Path.Str(), s);
	if (!Folder)
	{
		char *p = Path.Str();
		if (_strnicmp(p, "/IMAP ", 6) != 0)
		{
			LAssert(DefaultFolderTypes[Id] != MAGIC_NONE);
			Folder = s->GetRoot()->CreateSubFolder(*p=='/'?p+1:p, DefaultFolderTypes[Id]);
		}
	}
	
	if (!Folder)
		return false;

	Folder->SetDefaultFields();
	return true;
}

void ScribeWnd::Validate(LMailStore *s)
{
	THREAD_UNSAFE();
	// Check for all the basic folders

	int Errors = 0;	
	for (SystemFolderInfo *fi = SystemFolders; fi->PathOption; fi++)
	{
		bool Check = true;
		if (fi->HasOption)
		{
			LVariant v;
			if (GetOptions()->GetValue(fi->HasOption, v))
				Check = v.CastInt32() != 0;
		}
		if (Check)
		{
			if (!ValidateFolder(s, fi->Id))
				Errors++;
		}
	}

	if (Errors &&
		LgiMsg(this,
				"There were errors validating the system folders."
				"Would you like to review the mail store's system folder paths?",
				AppName,
				MB_YESNO) == IDYES)
	{
		PostEvent(M_COMMAND, IDM_MANAGE_MAIL_STORES);
	}
}

ThingFilter *ScribeWnd::GetThingFilter()
{
	THREAD_UNSAFE(NULL);
	return SearchView;
}

ScribeAccount *ScribeWnd::GetSendAccount()
{
	THREAD_UNSAFE(NULL);
	LVariant DefSendAcc = 0;

	if (!GetOptions()->GetValue(OPT_DefaultSendAccount, DefSendAcc))
	{
		for (auto a : Accounts)
			if (a->Send.Server().Str())
				return a;
	}

	ScribeAccount *i = Accounts.ItemAt(DefSendAcc.CastInt32());
	if (i && i->Send.Server().Str())
		return i;

	return NULL;
}

LPrinter *ScribeWnd::GetPrinter()
{
	THREAD_UNSAFE(NULL);
	if (!d->PrintOptions)
		d->PrintOptions.Reset(new LPrinter);
	return d->PrintOptions;
}

int ScribeWnd::GetActiveThreads()
{
	THREAD_UNSAFE(0);

	int Status = 0;
	for (ScribeAccount *i: Accounts)
	{
		if (i->IsOnline())
		{
			// LgiTrace("ActiveThread:%s\n", i->Receive.Server().Str());
			Status++;
		}
	}

	return Status;
}

class DefaultClientDlg : public LDialog
{
public:
	bool DontWarn;
	DefaultClientDlg(LView *parent)
	{
		DontWarn = false;
		SetParent(parent);
		LoadFromResource(IDD_WARN_DEFAULT);
		MoveToCenter();
	}

	int OnNotify(LViewI *Ctrl, const LNotification &n) override
	{
		switch (Ctrl->GetId())
		{
			case ID_YES:
			case ID_NO:
			{
				LCheckBox *DW;
				if (GetViewById(IDC_DONT_WARN, DW))
				{
					DontWarn = DW->Value() != 0;
				}
				EndModal(Ctrl->GetId() == ID_YES);
				break;
			}
		}
		return 0;
	}

};

void ScribeWnd::SetDefaultHandler()
{
	THREAD_UNSAFE();
	#if WINNATIVE

	if (LAppInst->GetOption("noreg"))
		return;
		
	LVariant RegisterClient;
	if (!GetOptions()->GetValue(OPT_RegisterWindowsClient, RegisterClient))
		RegisterClient = true;
	
	if (!RegisterClient.CastInt32())
		return;

	// Create IE mail client entries for local machine and current user
	DefaultClient Def;
	bool OldAssert = LRegKey::AssertOnError;
	LRegKey::AssertOnError = false;
	bool RegistryOk =	(
							!Def.IsWindowsXp()
							||
							Def.InstallMailto(true)
						)
						&&
						Def.InstallAsClient("HKLM", true)
						&&
						Def.WinInstall(true);
	LRegKey::AssertOnError = OldAssert;
	if (!RegistryOk)
	{
		// Need write permissions to fix up the registry?
		NeedsCapability("RegistryWritePermissions");
		return;
	}

	// Check if the user wants us to be the default client
	LVariant n = true;
	GetOptions()->GetValue(OPT_CheckDefaultEmail, n);
	if (n.CastInt32())
	{
		// HKEY_CURRENT_USER\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\mailto\UserChoice
		
		LRegKey::AssertOnError = false;
		bool IsDef = Def.IsDefault();
		if (!IsDef)
		{
			// Ask the user...
			auto Dlg = new DefaultClientDlg(this);
			Dlg->DoModal([this, Dlg, Def, OldAssert](auto dlg, auto id)
			{
				if (id)
				{
					auto Error = !Def.SetDefault();
					LVariant v;
					GetOptions()->SetValue(OPT_CheckDefaultEmail, v = (int) (!Dlg->DontWarn));
					OnSetDefaultHandler(Error, OldAssert);
				}
			});
		}
		else OnSetDefaultHandler(false, OldAssert);
	}

	#endif
}

void ScribeWnd::OnSetDefaultHandler(bool Error, bool OldAssert)
{
	THREAD_UNSAFE();
	#if WINDOWS
	LRegKey::AssertOnError = OldAssert;
	#endif
	if (Error)
		NeedsCapability("RegistryWritePermissions");
}

void ScribeWnd::OnSelect(List<Thing> *l, bool ChangeEvent)
{
	THREAD_UNSAFE();
	Mail *m = (l && l->Length() == 1) ? (*l)[0]->IsMail() : 0;

	if (Commands)
	{
		bool NotCreated = m && !TestFlag(m->GetFlags(), MAIL_CREATED);

		Commands->SetCtrlEnabled(IDM_DELETE, l && l->Length() > 0);
		Commands->SetCtrlEnabled(IDM_DELETE_AS_SPAM, l && l->Length() > 0);
		Commands->SetCtrlEnabled(IDM_PRINT, l && l->Length() == 1);
		
		Commands->SetCtrlEnabled(IDM_REPLY, NotCreated);
		Commands->SetCtrlEnabled(IDM_REPLY_ALL, NotCreated);
		Commands->SetCtrlEnabled(IDM_FORWARD, m != 0);
		Commands->SetCtrlEnabled(IDM_BOUNCE, m != 0);
	}

	if (PreviewPanel && GetEffectiveLayoutMode() != 3)
	{
		if (!PreviewPanel->IsAttached())
		{
			SetItemPreview(PreviewPanel);
		}

		Thing *t = (l && l->Length() == 1) ? (*l)[0] : 0;
		PreviewPanel->OnThing(t, ChangeEvent);
		
		/*
		if (d->Debug)
			d->Debug->OnThing(t);
		*/
	}
}

class SpellErrorInst
{
public:
	int Id;
	LString Word;
	LString::Array Suggestions;

	SpellErrorInst(int id)
	{
		Id = id;
		// Decor = LCss::TextDecorSquiggle;
		// DecorColour.Rgb(255, 0, 0);		
	}
	
	~SpellErrorInst()
	{
	}

	bool OnMenu(LSubMenu *m)
	{
		if (Suggestions.Length())
		{
			for (unsigned i=0; i<Suggestions.Length() && i<10; i++)
			{
				m->AppendItem(Suggestions[i], 100 + i, true);
			}

			m->AppendSeparator();
		}

		char Buf[256];
		sprintf_s(Buf, sizeof(Buf), LLoadString(IDS_ADD_TO_DICTIONARY, "Add '%s' to dictionary"), Word.Get());
		m->AppendItem(Buf, 1, true);
		return true;
	}
	
	void OnMenuClick(int i)
	{
		if (i == 1)
		{
			// Add to dictionary...
			/*
			if (PostThreadEvent(SpellHnd, M_ADD_WORD, (LMessage::Param) new LString(Word)))
			{
				// FIXME
				LAssert(!"Impl me.");
				// View->PostEvent(M_DELETE_STYLE, (LMessage::Param) dynamic_cast<LTextView3::LStyle*>(this));
			}
			*/
		}
		else if (i >= 100 && i < 100 + (int)Suggestions.Length())
		{
			// Change spelling..
			char *Replace = Suggestions[i - 100];
			if (Replace)
			{
				char16 *w = Utf8ToWide(Replace);
				if (w)
				{
					/*
					int NewLen = StrlenW(w);
					if (NewLen > Len)
					{
						// Bigger...
						memcpy(View->NameW() + Start, w, Len * sizeof(char16));
						View->Insert(Start + Len, w + Len, NewLen - Len);
					}
					else if (NewLen < Len)
					{
						// Smaller...
						memcpy(View->NameW() + Start, w, NewLen * sizeof(char16));
						View->Delete(Start + NewLen, Len - NewLen);
					}
					else
					{
						// Just copy...
						memcpy(View->NameW() + Start, w, Len * sizeof(char16));
						RefreshLayout(Start, Len);
					}
					*/

					DeleteArray(w);
				}
			}
		}
	}
};

class MailTextView : public LTextView3
{
	ScribeWnd *App = nullptr;
	LSpellCheck *Thread = nullptr;
	LColour c[8]; // different colours for the different reply depths.
	LHashTbl<IntKey<int>, SpellErrorInst*> ErrMap;

	SpellErrorInst *NewErrorInst()
	{
		int Id;
		while (ErrMap.Find(Id = LRand(10000)))
			;
		
		auto Inst = new SpellErrorInst(Id);
		if (!Inst)
			return NULL;
		
		ErrMap.Add(Id, Inst);
		return Inst;
	}

public:
	using TParent = LTextView3;

	MailTextView(ScribeWnd *app, int Id, int x, int y, int cx, int cy, LFontType *FontType) :
		TParent(Id, x, y, cx, cy, FontType)
	{
		App = app;

		int i=0;
		c[i++].Rgb(0x80, 0, 0);
		c[i++].Rgb(0, 0x80, 0);
		c[i++].Rgb(0, 0, 0x80);
		c[i++].Rgb(0x80, 0x80, 0);
		c[i++].Rgb(0x80, 0, 0x80);
		c[i++].Rgb(0, 0x80, 0x80);
		c[i++].Rgb(0x80, 0x80, 0x80);
		c[i++].Rgb(0xc0, 0xc0, 0xc0);

		for (i=0; i<CountOf(c); i++)
		{
			char s[64];
			sprintf_s(s, sizeof(s), "colour-LC_REPLY_LVL_%i", i + 1);
			LColour::GetConfigColour(s, c[i]);
		}
	}

	~MailTextView()
	{
	}

	void PourStyle(size_t Start, ssize_t Length) override
	{
		TParent::PourStyle(Start, Length);

		if (!GetReadOnly())
		{
			ssize_t Origin = Start;
			ssize_t Len = Length;
			while (Start > 0 && !StrchrW(SpellDelim, Text[Start-1]))
				Start--;

			if (Len > 0)
			{
				// Text being added
				Len += Origin - Start;
				while ((ssize_t)Start + Len < Size && !StrchrW(SpellDelim, Text[Start + Len]))
					Len++;
			}
			else if (Len < 0)
			{
				// Text being deleted
				Len = Origin - Start;
				while ((ssize_t)Start + Len < Size && !StrchrW(SpellDelim, Text[Start + Len]))
					Len++;
			}

			if (!Thread)
				Thread = App->GetSpellThread();
			if (Thread && Len > 0)
			{
				LString Str(Text+Start, Len);
				LArray<LVariant> Params;
				Thread->Check(AddDispatch(), Str, Start, Len, &Params);
			}

			// Adjust all the positions of the styles after this.
			for (auto s = Style.begin(); s != Style.end(); )
			{
				if (s->Start >= Origin &&
					s->Owner == 1)
				{
					if (Length < 0 &&
						s->Start < Origin - Length)
					{
						// In the deleted text...
						Style.Delete(s);
						continue;
					}

					// After the deleted text
					s->Start += Length;
					LAssert(s->Start >= 0);
				}

				s++;
			}
		}
	}

	void PourText(size_t Start, ssize_t Len) override
	{
		TParent::PourText(Start, Len);

		for (auto l: Line)
		{
			int n=0;
			auto t = Text + l->Start;
			auto e = t + l->Len;
			while ((*t == ' ' || *t == '>') && t < e)
				if (*t++ == '>') n++;

			if (n > 0)
				l->c = c[(n-1) % CountOf(c)];
		}
	}
	
	constexpr static int BASE_MENU_ID = 1000;
	
	SpellErrorInst *Lookup(LStyle *style)
	{
		auto errId = style->Data.CastInt32();
		if (auto err = ErrMap.Find(errId))
			return err;
			
		LgiTrace("%s:%i - no err in map for id=%i\n", _FL, errId);
		return nullptr;
	}
	
	bool OnStyleMenu(LStyle *style, LSubMenu *m) override
	{
		bool status = false;
		
		if (m && style && style->Owner == STYLE_SPELLING)
		{
			if (auto err = Lookup(style)) // this will emit error if id not found
			{
				int menuId = BASE_MENU_ID;
				for (auto &s: err->Suggestions)
					m->AppendItem(s, menuId++);
				status = true;
			}
		}
	
		return TParent::OnStyleMenu(style, m) || status;
	}
	
	void OnStyleMenuClick(LStyle *style, int menuId) override
	{
		if (style && style->Owner == STYLE_SPELLING)
		{
			if (auto err = Lookup(style)) // this will emit error if id not found
			{
				// Apply the spelling suggestion...
				int index = menuId - BASE_MENU_ID;
				
				if (!err->Suggestions.IdxCheck(index))
				{	
					LgiTrace("%s:%i - suggestion index %i out of range.\n", _FL, index);
				}
				// Replace the old text with the new suggestion
				else if (!Delete(style->Start, style->Len))
				{
					LgiTrace("%s:%i - Delete(%s) failed.\n", _FL, style->GetStr());
				}
				else
				{
					LAutoWString w(Utf8ToWide(err->Suggestions[index]));
					if (!Insert(style->Start, w, Strlen(w.Get())))
					{
						LgiTrace("%s:%i - Insert(%S) failed.\n", _FL, w.Get());
					}
					else
					{
						// LgiTrace("%s:%i - applied suggestion: %i, style=%s\n", _FL, index, style->ToString().Get());
						
						// Move the cursor to the end of the insertion.
						SetCaret(style->End());
					}
				}
			}
		}		
	
		TParent::OnStyleMenuClick(style, menuId);
	}

	LMessage::Result OnEvent(LMessage *m) override
	{
		switch (m->Msg())
		{
			case M_CHECK_TEXT:
			{
				auto Ct = m->AutoA<LSpellCheck::CheckText>();
				if (!Ct || !Thread)
				{
					LgiTrace("%s:%i - missing param %p,%p\n", _FL, Ct.Get(), Thread);
					break;
				}
				
				// LgiTrace("%s:%i - M_CHECK_TEXT: %s\n", _FL, Ct->ToString().Get());
				
				// Clear existing spelling error styles
				for (auto i = Style.begin(); i != Style.end(); )
				{
					if (!i->Overlap(*Ct.Get()))
					{
						// Outside the area we are re-styling.
						i++;
					}
					else
					{
						if (i->Owner == STYLE_SPELLING)
						{
							// Existing error style inside the area
							// LgiTrace("%s:%i - delete existing spell style: %s\n", _FL, i->ToString().Get());
							Style.Delete(i);
						}
						else
						{
							// Existing non-error style...
							i++;
						}
					}
				}

				// Insert the new styles
				for (auto err: Ct->Errors)
				{
					if (auto ErrInst = NewErrorInst())
					{
						LAutoPtr<LTextView3::LStyle> Style(new LTextView3::LStyle(STYLE_SPELLING));
						if (Style)
						{
							Style->View = this;
							Style->Start = Ct->Start + err.Start;
							Style->Len = err.Len;
							Style->Font = GetFont();
							Style->Data = ErrInst->Id;
							Style->DecorColour = LColour::Red;
							Style->Decor = LCss::TextDecorSquiggle;

							ErrInst->Word = LString(Text + Style->Start, Style->End());
							ErrInst->Suggestions = err.Suggestions;
							
							// LgiTrace("%s:%i - insert new spell style: %s\n", _FL, Style->ToString().Get());
							InsertStyle(Style);
						}
						else LgiTrace("%s:%i - style alloc failed.\n", _FL);
					}
					else LgiTrace("%s:%i - NewErrorInst failed.\n", _FL);
				}

				// Update the screen...
				Invalidate();
				break;
			}
			case M_DELETE_STYLE:
			{
				/* Why is this disabled?
				LTextView3::LStyle *s = (LTextView3::LStyle*)m->A();
				if (s && Style.HasItem(s))
				{
					Style.Delete(s);
					Invalidate();
				}
				else LAssert(0);
				*/
				break;
			}
		}

		return LTextView3::OnEvent(m);
	}

	bool OnStyleClick(LStyle *style, LMouse *m) override
	{
		switch (style->Owner)
		{
			case STYLE_URL:
			{
				if (m->Left() &&
					m->Down() &&
					m->Double())
				{
					LString s(Text + style->Start, style->Len);
					LUri u(s);
					if
					(
						u.IsProtocol("mailto")
						&&
						LIsValidEmail(s)
					)
					{
						Mailto m(App, s);
						Mail *email = App->CreateMail();
						if (email)
						{
							m.Apply(email);
							email->DoUI();
							return true;
						}
					}
					else
					{
						// Web link?
						LExecute(s);
						return true;
					}
				}
				break;
			}
			default:
				break;
		}

		return false;
	}
};

class ScribeRichTextEdit : public LRichTextEdit
{
	ScribeWnd *App;

public:
	ScribeRichTextEdit(ScribeWnd *app, int Id, LFontType *FontInfo = NULL) :
		LRichTextEdit(Id, 0, 0, 100, 100, FontInfo),
		App(app)
	{
	}

	bool MaxImageFilter(ImgParams &params)
	{
		auto opts = App->GetOptions();
		LVariant v;
		if (!opts->GetValue(OPT_ResizeImgAttachments, v) ||
			!v.CastInt32())
			return false;

		if (opts->GetValue(OPT_ResizeJpegQual, v))
			params.JpegQuality = v.CastInt32();

		if (!opts->GetValue(OPT_ResizeMaxPx, v))
			return false;

		if (params.Sz.x > v.CastInt32())
			return true;

		if (!opts->GetValue(OPT_ResizeMaxKb, v))
			return false;

		auto byteLimit = (size_t)v.CastInt64() << 10/*KiB->bytes*/;
		if (params.Bytes > byteLimit)
			return true;

		return false;
	}
};

LDocView *ScribeWnd::CreateTextControl(int Id, const char *MimeType, bool Editor, Mail *m)
{
	THREAD_UNSAFE(NULL);
	LDocView *Ctrl = 0;
	
	// Get the default font
	LFontType FontType;
	bool UseFont = FontType.Serialize(GetOptions(), OPT_EditorFont, false);

	if (Editor)
	{
		if (!Stricmp(MimeType, sTextHtml))
		{		
			// Use the built in html editor
			LRichTextEdit *Rte;
			if ((Ctrl = Rte = new ScribeRichTextEdit(this, Id)))
			{
				if (UseFont)
					Ctrl->SetFont(FontType.Create(), true);
				
				// Give the control the speller settings:
				LVariant Check, Lang, Dict;
				if (GetOptions()->GetValue(OPT_SpellCheck, Check) &&
					Check.CastInt32() != 0)
				{
					if (GetOptions()->GetValue(OPT_SpellCheckLanguage, Lang))
						Rte->SetValue(LDomPropToString(SpellCheckLanguage), Lang);
					if (GetOptions()->GetValue(OPT_SpellCheckDictionary, Dict))
						Rte->SetValue(LDomPropToString(SpellCheckDictionary), Dict);
					
					
					// Set the spell thread:
					if (auto t = GetSpellThread())
						Rte->SetSpellCheck(t);
				}
			}
		}
		else
		{
			// Use the built in plain text editor
			Ctrl = new MailTextView(this, Id, 0, 0, 200, 200, (UseFont) ? &FontType : 0);
		}
	}
	else
	{
		// Create a view only control for the mime type:
		LDocView *HtmlCtrl = NULL;
		if (!MimeType ||
			!Stricmp(MimeType, sTextPlain) ||
			!Stricmp(MimeType, sMultipartEncrypted))
		{
			Ctrl = new MailTextView(this, Id, 0, 0, 200, 200, (UseFont) ? &FontType : 0);
		}
		else
		{
			HtmlCtrl = Ctrl = new Html1::LHtml(Id, 0, 0, 200, 200);
		}
		
		if (HtmlCtrl && UseFont)
		{
			LVariant LoadImg;
			if (GetOptions()->GetValue(OPT_HtmlLoadImages, LoadImg))
				HtmlCtrl->SetLoadImages(LoadImg.CastInt32() != 0);
			HtmlCtrl->SetFont(FontType.Create(), true);
		}
	}

	if (Ctrl)
	{
		Ctrl->SetUrlDetect(true);
		Ctrl->SetAutoIndent(true);

		LVariant WrapOption;
		if (GetOptions()->GetValue(OPT_WordWrap, WrapOption))
		{
			if (WrapOption.CastInt32())
			{
				LVariant WrapCols = 80;
				GetOptions()->GetValue(OPT_WrapAtColumn, WrapCols);
				Ctrl->SetWrapAtCol(WrapCols.CastInt32());
			}
			else
			{
				Ctrl->SetWrapAtCol(0);
			}
		}
	}

	return Ctrl;
}

void ScribeWnd::GrowlInfo(LString title, LString text)
{
	THREAD_UNSAFE();
	LGrowl *g = d->GetGrowl();
	if (!g)
		return;

	LAutoPtr<LGrowl::LNotify> n(new LGrowl::LNotify);
	n->Name = "info";
	n->Title = title;
	n->Text = text;
	g->Notify(n);
}

void ScribeWnd::GrowlOnMail(Mail *m)
{
	THREAD_UNSAFE();
	LVariant v;
	LAutoPtr<LGrowl::LNotify> n(new LGrowl::LNotify);
	n->Name = "new-mail";
	n->Title = m->GetSubject();
	
	int Len = 64;
	char sLen[16];
	sprintf_s(sLen, sizeof(sLen), "%i", Len);
	if (m->GetVariant("BodyAsText", v, sLen))
	{
		char *s = v.Str();
		if (s)
		{
			int Words = 0;
			bool Lut[256];
			memset(Lut, 0, sizeof(Lut));
			Lut[(int)' '] = Lut[(int)'\t'] = Lut[(int)'\r'] = Lut[(int)'\n'] = true;
			char *c;
			for (c = s; *c && Words < 30; )
			{
				while (*c && Lut[(int)*c]) c++;
				while (*c && !Lut[(int)*c]) c++;
				Words++;
			}
			
			n->Text.Set(s, c - s);
		}
	}
	
	if (auto g = d->GetGrowl())
	{
		g->Notify(n);
		m->NewEmail = Mail::NewEmailTray;
	}
}

void ScribeWnd::OnNewMailSound()
{
	THREAD_UNSAFE();
	static uint64 PrevTs = 0;
	auto Now = LCurrentTime();
	if (Now - PrevTs > 30000)
	{
		PrevTs = Now;
		LVariant v;
		if (GetOptions()->GetValue(OPT_NewMailSoundFile, v) &&
			LFileExists(v.Str()))
		{
			LPlaySound(v.Str(), SND_ASYNC);
		}
	}
}

void ScribeWnd::OnFolderSelect(ScribeFolder *f)
{
	THREAD_UNSAFE();
	if (SearchView)
		SearchView->OnFolder();
}

void ScribeWnd::OnNewMail(List<Mail> &MailObjs, bool Add)
{
	THREAD_UNSAFE();

	LVariant v;
	bool ShowDetail = MailObjs.Length() < 5;
	List<Mail> NeedsFiltering;
	LArray<Mail*> NeedsBayes;
	LArray<Mail*> NeedsGrowl;
	LArray<ScribeFolder*> Resort;

	for (auto m: MailObjs)
	{
		if (Add)
		{
			#if DEBUG_NEW_MAIL
			LgiTrace("%s:%i - NewMail.OnNewMail t=%p, uid=%s, mode=%s\n",
				_FL, (Thing*)m, m->GetServerUid().ToString().Get(), toString(m->NewEmail));
			#endif

			switch (m->NewEmail)
			{
				case Mail::NewEmailNone:
				{
					auto Loaded = m->GetLoaded();
					#if DEBUG_NEW_MAIL
					LgiTrace("%s:%i - NewMail.OnNewMail.GetLoaded=%i uid=%s\n", _FL, (int)Loaded, m->GetServerUid().ToString().Get());
					#endif
					if (Loaded != Store3Loaded)
					{
						LOG_STORE("\tOnNewMail calling SetLoaded.\n");
						m->SetLoaded();
						m->NewEmail = Mail::NewEmailLoading;
					}
					else
					{
						m->NewEmail = Mail::NewEmailFilter;
						LOG_STORE("\tOnNewMail none->NeedsFiltering.\n");
						NeedsFiltering.Insert(m);
					}
					break;
				}
				case Mail::NewEmailLoading:
				{
					auto Loaded = m->GetLoaded();
					if (Loaded == Store3Loaded)
					{
						m->NewEmail = Mail::NewEmailFilter;
						NeedsFiltering.Insert(m);

						if (m->GetFolder() &&
							!Resort.HasItem(m->GetFolder()))
						{
							Resort.Add(m->GetFolder());
						}
					}
					break;
				}
				case Mail::NewEmailFilter:
				{
					NeedsFiltering.Insert(m);
					break;
				}
				case Mail::NewEmailBayes:
				{
					NeedsBayes.Add(m);
					break;
				}
				case Mail::NewEmailGrowl:
				{
					if (d->Growl)
					{
						NeedsGrowl.Add(m);
						break;
					}
					else
					{
						m->NewEmail = Mail::NewEmailTray;
						// no Growl loaded so fall through to new tray mail
					}
				}
				case Mail::NewEmailTray:
				{
					LAssert(m->GetObject());
					LAssert(!Mail::NewMailLst.HasItem(m));

					m->NewEmail = Mail::NewEmailNone;
					Mail::NewMailLst.Insert(m);
					OnNewMailSound();
					break;
				}
				default:
				{
					LAssert(!"Hmmm what happen?");
					break;
				}
			}
		}
		else
		{
			#if DEBUG_NEW_MAIL
			LgiTrace("%s:%i - NewMail.OnNewMail.RemoveNewMail t=%p, uid=%s\n",
				_FL, (Thing*)m, m->GetServerUid().ToString().Get());
			#endif

			Mail::NewMailLst.Delete(m);
			
			if (m->NewEmail == Mail::NewEmailFilter)
				m->NewEmail = Mail::NewEmailNone;
		}
	}

	if (Add)
	{
		// Do filtering
		if (NeedsFiltering.Length())
		{
			List<Filter> Filters;
			if (!GetOptions()->GetValue(OPT_DisableUserFilters, v) ||
				!v.CastInt32())
			{
				GetFilters(Filters, true, false, false);
			}
		
			if (Filters.Length() > 0)
			{
				// Run the filters
				#if DEBUG_NEW_MAIL
				LgiTrace("%s:%i - NewMail.OnNewMail.Filtering %i mail through %i filters\n",
					_FL, (int)NeedsFiltering.Length(), (int)Filters.Length());
				#endif
				Filter::ApplyFilters(NULL, Filters, NeedsFiltering);
				
				// All the email not filtered now needs to be sent to the bayes filter.
				for (auto m: NeedsFiltering)
				{
					if (m->NewEmail == Mail::NewEmailBayes)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnNewMail.NeedsBayes t=%p, msgid=%s\n",
							_FL, (Thing*)m, m->GetMessageId());
						#endif

						NeedsBayes.Add(m);
					}
					else if (m->NewEmail == Mail::NewEmailGrowl)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnNewMail.NeedsGrowl t=%p, msgid=%s\n",
							_FL, (Thing*)m, m->GetMessageId());
						#endif

						NeedsGrowl.Add(m);
					}
				}
			}
		}

		// Do bayes
		if (NeedsBayes.Length())
		{
			ScribeBayesianFilterMode FilterMode = BayesOff;
			if (GetOptions()->GetValue(OPT_BayesFilterMode, v))
				FilterMode = (ScribeBayesianFilterMode)v.CastInt32();

			for (unsigned i=0; i<NeedsBayes.Length(); i++)
			{
				Mail *m = NeedsBayes[i];
				if (FilterMode != BayesOff)
				{
					double Rating = 0.0;
					
					// This adds the mail to a message ID map, so even if it moves we
					// can do the filtering later on when the Bayesian result is returned.
					m->MailMessageIdMap();
					
					// Start the Bayesian rating process off
					Store3Status Status = IsSpam(Rating, m);
					if (Status == Store3Success)
					{
						// Bayes done... this stops OnBayesResult from passing it back to OnNewMail
						m->NewEmail = Mail::NewEmailGrowl;
						
						// Process bayes result
						if (!OnBayesResult(m, Rating))
						{
							// Not spam... so on to growl
							#if DEBUG_NEW_MAIL
							LgiTrace("%s:%i - NewMail.Bayes.NeedsGrowl t=%p, msgid=%s\n",
								_FL, (Thing*)m, m->GetMessageId());
							#endif

							NeedsGrowl.Add(m);
						}
						else
						{
							// Is spam... do nothing...
							#if DEBUG_NEW_MAIL
							LgiTrace("%s:%i - NEW_MAIL: Bayes->IsSpam t=%p, msgid=%s\n",
								_FL, (Thing*)m, m->GetMessageId());
							#endif

							m->NewEmail = Mail::NewEmailNone;
							m = 0;
						}
					}
					else
					{
						// Didn't get classified immediately, so it'll be further
						// processed when OnBayesResult gets called later.
					}
				}
				else
				{
					// Bayes filter not active... move it to growl
					m->NewEmail = Mail::NewEmailGrowl;
					NeedsGrowl.Add(m);
				}
			}
		}

		if (NeedsGrowl.Length())
		{
			if (d->Growl)
			{
				if (!ShowDetail)
				{
					LAutoPtr<LGrowl::LNotify> n(new LGrowl::LNotify);
					n->Name = "new-mail";
					n->Title = "New Mail";
					n->Text.Printf("%i new messages", (int)MailObjs.Length());
					d->Growl->Notify(n);
				}
				else
				{
					for (auto m: NeedsGrowl)
					{
						#ifdef _DEBUG
						auto state =
						#endif
						m->GetLoaded();
						LAssert(state == Store3Loaded);

						// If loaded then notify			                
						GrowlOnMail(m);
					}
				}
			}

			for (auto m: NeedsGrowl)
			{
				m->NewEmail = Mail::NewEmailNone;

				LAssert(m->GetObject());
				LAssert(!Mail::NewMailLst.HasItem(m));
				Mail::NewMailLst.Insert(m);
				OnNewMailSound();
			}
		}

		if (GetOptions()->GetValue(OPT_NewMailNotify, v) &&
			v.CastInt32())
		{
			PostEvent(M_SCRIBE_NEW_MAIL);
		}

		for (auto f: Resort)
		{
			#if DEBUG_NEW_MAIL
			auto Path = Resort[i]->GetPath();
			LgiTrace("%s:%i - NewMail.OnNewMail.Resort=%s\n", _FL, Path.Get());
			#endif

			f->ReSort();
		}
	}
}

LColour ScribeWnd::GetColour(int i)
{
	THREAD_UNSAFE(LColour());
	
	static LColour MailPreview;
	static LColour UnreadCount;
	#define ReadColDef(Var, Tag, Default)		\
		case Tag: \
		{ \
			if (!Var.IsValid()) \
			{ \
				Var = Default; \
				LColour::GetConfigColour("Colour."#Tag, Var); \
			} \
			return Var; \
			break; \
		}

	switch (i)
	{
		ReadColDef(MailPreview, L_MAIL_PREVIEW, LColour(0, 0, 255));
		ReadColDef(UnreadCount, L_UNREAD_COUNT, LColour(0, 0, 255));
		default:
		{
			return LColour((LSystemColour)i);
			break;
		}
	}

	return LColour();
}

bool WriteXmlTag(LStream &p, LXmlTag *t)
{
	const char *Tag = t->GetTag();
	bool ValidTag = ValidStr(Tag) && !IsDigit(Tag[0]);
	if (ValidTag)
		p.Print("<%s", Tag);
	else
	{
		LAssert(0);
		return false;
	}

	LXmlTree Tree;
	static const char *EncodeEntitiesAttr	= "\'<>\"\n";
	for (unsigned i=0; i<t->Attr.Length(); i++)
	{
		auto &a = t->Attr[i];

		// Write the attribute name
		p.Print(" %s=\"", a.GetName());
		
		// Encode the value
		if (!Tree.EncodeEntities(&p, a.GetValue(), -1, EncodeEntitiesAttr))
		{
			LAssert(0);
			return false;
		}

		// Write the delimiter
		p.Write((void*)"\"", 1);

		if (i<t->Attr.Length()-1 /*&& TestFlag(d->Flags, GXT_PRETTY_WHITESPACE)*/)
		{
			p.Write((void*)"\n", 1);
		}
	}
	
	p.Write(">", 1);
	
	return true;
}

LString ScribeWnd::ProcessReplyForwardTemplate(Mail *m, Mail *r, char *Xml, int &Cursor, const char *MimeType)
{
	THREAD_UNSAFE(LString());

	LStringPipe p(256);

	if (m && r && Xml)
	{
		bool IsHtml = MimeType && !_stricmp(MimeType, sTextHtml);
		LMemStream mem(Xml, strlen(Xml));
		LXmlTag x;
		LXmlTree t(GXT_KEEP_WHITESPACE | GXT_NO_DOM);
		if (t.Read(&x, &mem, 0))
		{
			ScribeDom Dom(this);
			Dom.Email = m;

			if (IsHtml)
			{
				const char *EncodeEntitiesContent	= "\'<>\"";
				
				for (auto Tag: x.Children)
				{
					if (!WriteXmlTag(p, Tag))
					{
						break;
					}
					
					for (const char *c = Tag->GetContent(); c; )
					{
						const char *s = strstr(c, "<?");
						const char *e = s ? strstr(s + 2, "?>") : NULL;
						if (s && e)
						{
							if (s > c)
							{
								t.EncodeEntities(&p, (char*)c, s - c, EncodeEntitiesContent);
							}
							s += 2;
							
							LString Var = LString(s, e - s).Strip();
							LVariant v;
							if (Var)
							{
								LString::Array parts = Var.SplitDelimit(" ");
								if (parts.Length() > 0)
								{
									if (Dom.GetValue(parts[0], v))
									{
										for (unsigned mod = 1; mod < parts.Length(); mod++)
										{
											LString::Array m = parts[mod].SplitDelimit("=", 1);
											if (m.Length() == 2)
											{
												if (m[0].Equals("quote"))
												{
													LVariant Quote;
													if (Dom.GetValue(m[1], Quote))
													{
														LVariant WrapColumn;
														if (!GetOptions()->GetValue(OPT_WrapAtColumn, WrapColumn) ||
															WrapColumn.CastInt32() <= 0)
															WrapColumn = 76;
															
														WrapAndQuote(p, Quote.Str(), WrapColumn.CastInt32(), v.Str(), NULL, MimeType);
														v.Empty();
													}
												}
											}
										}
										
										switch (v.Type)
										{
											case GV_STRING:
											case GV_WSTRING:
											case GV_LSTRING:
											{
												p.Push(v.Str());
												break;
											}
											case GV_DATETIME:
											{
												p.Push(v.Value.Date->Get());
												break;
											}
											case GV_NULL:
												break;
											default:
											{
												LAssert(!"Unsupported type.");
												break;
											}
										}
									}
								}
							}
							
							c = e + 2;
						}
						else
						{						
							p.Print("%s", c);
							break;
						}
					}
				}
			}
			else
			{			
				LArray<LXmlTag*> Tags;
				Tags.Add(&x);
				for (auto Tag: x.Children)
				{
					Tags.Add(Tag);
				}
				
				for (unsigned i=0; i<Tags.Length(); i++)
				{
					LXmlTag *Tag = Tags[i];
					
					LVariant v;
					if (Tag->GetTag() && Dom.GetValue(Tag->GetTag(), v))
					{
						char *s = v.Str();
						if (s)
						{
							const char *Quote;
							if ((Quote = Tag->GetAttr("quote")))
							{
								LVariant q, IsQuote;
								GetOptions()->GetValue(OPT_QuoteReply, IsQuote);
								if (r->GetValue(Quote, q))
								{
									Quote = q.Str();
								}
								else
								{
									Quote = "> ";
								}

								if (Quote && IsQuote.CastInt32())
								{
									LVariant WrapColumn;
									if (!GetOptions()->GetValue(OPT_WrapAtColumn, WrapColumn) ||
										WrapColumn.CastInt32() <= 0)
										WrapColumn = 76;
										
									WrapAndQuote(p, Quote, WrapColumn.CastInt32(), s);
								}
								else
								{
									p.Push(s);
								}
							}
							else
							{
								p.Push(s);
							}
						}
						else if (v.Type == GV_DATETIME &&
								 v.Value.Date)
						{
							char b[64];
							v.Value.Date->Get(b, sizeof(b));
							p.Push(b);
						}
					}
					else if (Tag->IsTag("cursor"))
					{
						auto Buf = p.Peek(p.GetSize());
						if (Buf)
						{
							RemoveReturns(Buf);
							Cursor = LCharLen(Buf, "utf-8");
						}
					}

					if (Tag->GetContent())
					{
						p.Push(Tag->GetContent());
					}
				}
			}
		}
	}

	return p.NewLStr();
}

LString	ScribeWnd::ProcessSig(Mail *m, char *Xml, const char *MimeType)
{
	THREAD_UNSAFE(LString());

	LStringPipe p;

	if (!m || !Xml)
		return LString();
	
	if (MimeType && !_stricmp(MimeType, sTextHtml))
		p.Write(Xml, strlen(Xml));
	else
	{
		LMemStream mem(Xml, strlen(Xml));
		LXmlTag x;
		LXmlTree t(GXT_KEEP_WHITESPACE|GXT_NO_DOM);
		if (t.Read(&x, &mem, 0))
		{
			for (auto Tag: x.Children)
			{
				if (Tag->IsTag("random-line"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("Filename")))
					{
						LFile f(FileName);
						if (f)
						{
							auto Lines = f.Read().SplitDelimit("\r\n");
							char *RandomLine = Lines[LRand((unsigned)Lines.Length())];
							if (RandomLine)
							{
								p.Push(RandomLine);
							}
						}
					}
				}
				else if (Tag->IsTag("random-paragraph"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("Filename")))
					{
						auto File = LReadFile(FileName);
						if (File)
						{
							List<char> Para;
							for (auto f = File.Get(); f && *f; )
							{
								// skip whitespace
								while (strchr(" \t\r\n", *f)) f++;
								if (*f)
								{
									char *Start = f;
									char *n;
									while ((n = strchr(f, '\n')))
									{
										f = n + 1;
										if (f[1] == '\n' ||
											(f[1] == '\r' && f[2] == '\n'))
										{
											break;
										}
									}
									if (f == Start) f += strlen(f);

									Para.Insert(NewStr(Start, f-Start));
								}
							}

							auto RandomPara = Para.ItemAt(LRand((int)Para.Length()));
							if (RandomPara)
							{
								p.Push(RandomPara);
							}

							Para.DeleteArrays();
						}
					}
				}
				else if (Tag->IsTag("include-file"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("filename")))
					{
						auto File = LReadFile(FileName);
						if (File)
							p.Push(File);
					}
				}
				else if (Tag->IsTag("quote-file"))
				{
					char *FileName = 0;
					char *QuoteStr = 0;
					if ((FileName = Tag->GetAttr("filename")) &&
						(QuoteStr = Tag->GetAttr("Quote")))
					{
					}
				}
				else
				{
					p.Push(Tag->GetContent());
				}
			}
		}
	}

	return p.NewLStr();
}

// Get the effective permissions for a resource.
//
// This method can be used by both sync and async code:
// In sync mode, don't supply a callback (ie = NULL) and the return value will be:
//		Store3Error - no access
//		Store3Delayed - no access, asking the user for password
//		Store3Success - allow immediate access
//
// In async mode, supply a callback and wait for the response.
//		callback(false) - no access
//		callback(true) - allow immediate access
// in this mode the same return values as sync mode are used.
Store3Status ScribeWnd::GetAccessLevel(LViewI *Parent, ScribePerm Required, const char *ResourceName, std::function<void(bool)> Callback)
{
	THREAD_UNSAFE(Store3Error);

	if (CurrentAuthLevel >= Required)
	{
		if (Callback) Callback(true);
		return Store3Success;
	}
	
	if (!Parent)
		Parent = this;
	
	switch (Required)
	{
		default:
			break;
		case PermRequireUser:
		{
			LPassword p;
			if (!p.Serialize(GetOptions(), OPT_UserPermPassword, false))
			{
				if (Callback) Callback(true);
				return Store3Success;
			}

			char Msg[256];
			sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ASK_USER_PASS), ResourceName);
				
			auto d = new LInput(Parent, "", Msg, AppName, true);
			d->DoModal([this, d, p, Callback](auto dlg, auto id)
			{
				if (id && d->GetStr())
				{
					char Pass[256];
					p.Get(Pass);
					bool Status = strcmp(Pass, d->GetStr()) == 0;
					if (Status)
					{
						CurrentAuthLevel = PermRequireUser;
						
						auto i = Menu->FindItem(IDM_LOGOUT);
						if (i) i->Enabled(true);
						if (Callback) Callback(true);
					}
					else
					{
						if (Callback) Callback(false);
					}
				}
			});

			return Store3Delayed;
		}
		case PermRequireAdmin:
		{
			LString Key;
			Key.Printf("Scribe.%s", OPT_AdminPassword);
			auto Hash = LAppInst->GetConfig(Key);
			if (ValidStr(Hash))
			{
				if (Callback) Callback(false);
				return Store3Error;
			}

			uchar Bin[256];
			ssize_t BinLen = 0;
			if ((BinLen = ConvertBase64ToBinary(Bin, sizeof(Bin), Hash, strlen(Hash))) != 16)
			{
				LgiMsg(Parent, "Admin password not correctly encoded.", AppName);
				if (Callback) Callback(false);
				return Store3Error;
			}

			auto d = new LInput(Parent, "", LLoadString(IDS_ASK_ADMIN_PASS), AppName, true);
			d->DoModal([this, d, Bin, Callback](auto dlg, auto id)
			{
				if (id && d->GetStr())
				{
					unsigned char Digest[16];
					char Str[256];
					sprintf_s(Str, sizeof(Str), "%s admin", d->GetStr().Get());
					MDStringToDigest(Digest, Str);
						
					if (memcmp(Bin, Digest, 16) == 0)
					{
						CurrentAuthLevel = PermRequireAdmin;
						auto i = Menu->FindItem(IDM_LOGOUT);
						if (i) i->Enabled(true);
						if (Callback) Callback(true);
					}
					else
					{
						if (Callback) Callback(false);
					}
				}
			});

			return Store3Delayed;
		}
	}

	if (Callback) Callback(true);
	return Store3Success;
}

void ScribeWnd::GetAccountSettingsAccess(LViewI *Parent, ScribeAccessType AccessType, std::function<void(bool)> Callback)
{
	THREAD_UNSAFE();

	LVariant Level = (int)PermRequireNone;
	
	// Check if user level access is required
	char *Opt = (char*)(AccessType == ScribeReadAccess ? OPT_AccPermRead : OPT_AccPermWrite);
	GetOptions()->GetValue(Opt, Level);

	GetAccessLevel(Parent ? Parent : this, (ScribePerm)Level.CastInt32(), "Account Settings", Callback);
}

void ScribeWnd::OnBeforeConnect(ScribeAccount *Account, bool Receive)
{
	THREAD_UNSAFE();

	if (Receive)
		Account->Receive.Enabled(false);
	else
		Account->Send.Enabled(true);

	if (StatusPanel)
		StatusPanel->Invalidate();
}

void ScribeWnd::OnAfterConnect(ScribeAccount *Account, bool Receive)
{
	THREAD_UNSAFE();

	if (Account)
	{
		SaveOptions();
		if (ScribeState == ScribeExiting)
		{
			LCloseApp();
		}
	}

	if (Receive)
		Account->Receive.Enabled(true);
	else
		Account->Send.Enabled(true);

	if (StatusPanel)
		StatusPanel->Invalidate();

	if (d->SendAfterReceive)
	{
		bool Online = false;
		for (auto a: Accounts)
		{
			bool p = a->Receive.IsPersistant();
			bool o = a->Receive.IsOnline();
			if (!p && o)
			{
				Online = true;
				break;
			}
		}
		if (!Online)
		{
			Send(-1, true);
			d->SendAfterReceive = false;
		}
	}
}

void ScribeWnd::Send(ssize_t Which, bool Quiet)
{
	THREAD_UNSAFE();

	if (ScribeState == ScribeExiting)
		return;

	if (Which < 0)
	{
		LVariant v;
		if (GetOptions()->GetValue(OPT_DefaultSendAccount, v))
			Which = v.CastInt32();
	}

	LArray<ScribeFolder*> Outboxes;
	unsigned i;
	for (i=0; i<Folders.Length(); i++)
	{
		ScribeFolder *OutBox = GetFolder(FOLDER_OUTBOX, &Folders[i]);
		if (OutBox)
		{
			OutBox->LoadThings();
			Outboxes.Add(OutBox);
		}
	}

	if (Outboxes.Length() < 1)
	{
		LgiMsg(this, LLoadString(IDS_NO_OUTGOING_FOLDER), AppName, MB_OK);
		return;
	}

	int MailToSend = 0;
	List<SendAccountlet> Acc;
	SendAccountlet *Default = 0;
	
	{
		// Create list of accounts
		for (auto a: Accounts)
		{
			Acc.Insert(&a->Send);
			a->Send.Outbox.DeleteObjects();
		
			if (Which < 0 || a->GetIndex() == Which)
				Default = &a->Send;
		}
		
		// If the default it not in the list try the first one..
		if (!Default)
			Default = Acc[0];
	}

	for (i=0; i<Outboxes.Length(); i++)
	{
		auto OutBox = Outboxes[i];
		for (auto t: OutBox->Items)
		{
			auto m = t->IsMail();
			if (!m)
				continue;

			auto Flags = m->GetFlags();
			if (!TestFlag(Flags, MAIL_SENT) &&
				TestFlag(Flags, MAIL_READY_TO_SEND))
			{
				auto To = m->GetObject()->GetList(FIELD_TO);
				if (To && To->Length())
				{
					LAutoPtr<ScribeEnvelope> Out(new ScribeEnvelope);
					if (Out)
					{
						if (m->OnBeforeSend(Out))
						{
							#if 0
								LFile tmp(LFile::Path(ScribeTempPath()) / "mime.txt", O_WRITE);
								tmp.Write(Out->Rfc822);
								tmp.Close();
							#endif

							SendAccountlet *Send = nullptr;
							for (auto a: Acc)
							{
								LVariant Ie = a->GetAccount()->Identity.Email();
								if (ValidStr(Ie.Str()) &&
									a->OnlySendThroughThisAccount())
								{
									if (Ie.Str() &&
										m->GetFromStr(FIELD_EMAIL) &&
										_stricmp(Ie.Str(), m->GetFromStr(FIELD_EMAIL)) == 0)
									{
										Send = a;
										break;
									}
								}
							}

							if (!Send)
							{
								Send = Default;
							}
							
							if (Send)
							{
								LAssert(Out->To.Length() > 0);
								Out->SourceFolder = OutBox->GetPath();
								Send->Outbox.Add(Out.Release());
								MailToSend++;
							}
						}
					}
				}
				else
				{
					LgiMsg(	this,
							LLoadString(IDS_ERROR_NO_RECIPIENTS),
							AppName,
							MB_OK,
							m->GetSubject() ? m->GetSubject() : (char*)"(none)");
				}
			}
		}
	}

	if (MailToSend)
	{
		for (auto a: Acc)
		{
			if (a->Outbox.Length() > 0 && !a->IsOnline())
			{
				if (a->IsConfigured())
				{
					a->Connect(0, Quiet);
				}
				else
				{
					auto d = new LAlert(this,
							AppName,
							LLoadString(IDS_ERROR_NO_CONFIG_SEND),
							LLoadString(IDS_CONFIGURE),
							LLoadString(IDS_CANCEL));
					d->DoModal([this, d, a](auto dlg, auto id)
					{
						if (id == 1)
							a->GetAccount()->InitUI(this, 1, NULL);
					});
				}
			}
		}
	}
	else
	{
		LgiMsg(this, LLoadString(IDS_NO_MAIL_TO_SEND), AppName, MB_OK, Outboxes[0]->GetText());
	}
}

void ScribeWnd::Receive(ssize_t Which)
{
	THREAD_UNSAFE();

	#define LOG_RECEIVE		0
	
	if (ScribeState == ScribeExiting)
	{
		LgiTrace("%s:%i - Won't receive, is trying to exit.\n", _FL);
		return;
	}

	for (ScribeAccount *i: Accounts)
	{
		if (i->GetIndex() != Which)
			continue;
			
		if (i->Receive.IsOnline())
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i already online.\n", _FL, Which);
			#endif
		}
		else if (i->Receive.Disabled() > 0)
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i is disabled.\n", _FL, Which);
			#endif
		}
		else if (!i->Receive.IsConfigured())
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i is not configured.\n", _FL, Which);
			#endif

			auto a = new LAlert(this,
					AppName,
					LLoadString(IDS_ERROR_NO_CONFIG_RECEIVE),
					LLoadString(IDS_CONFIGURE),
					LLoadString(IDS_CANCEL));
			a->DoModal([this, a, i](auto dlg, auto id)
			{
				if (id == 1)
					i->InitUI(this, 2, NULL);
			});
		}
		else
		{
			i->Receive.Connect(0, false);
		}
		break;
	}
}

bool ScribeWnd::GetHelpFilesPath(char *Path, int PathSize)
{
	THREAD_UNSAFE(false);

	const char *Index = "index.html";
	char Install[MAX_PATH_LEN];
	strcpy_s(Install, sizeof(Install), ScribeResourcePath());
	
	for (int i=0; i<5; i++)
	{
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Install, "help");
		LMakePath(p, sizeof(p), p, Index);
		LgiTrace("Trying '%s'\n", p);
		if (LFileExists(p))
		{
			LTrimDir(p);
			strcpy_s(Path, PathSize, p);
			return true;
		}
		#ifdef MAC
		LMakePath(p, sizeof(p), Install, "Resources/Help");
		LMakePath(p, sizeof(p), p, Index);
		// LgiTrace("Trying '%s'\n", p);
		if (LFileExists(p))
		{
			LTrimDir(p);
			strcpy_s(Path, PathSize, p);
			return true;
		}
		#endif
		LTrimDir(Install); // Try all the parent folders...
	}

	LArray<const char*> Ext;
	LArray<char*> Help;
	Ext.Add("index.html");
	LMakePath(Install, sizeof(Install), ScribeResourcePath(), "..");
	LRecursiveFileSearch(Install, &Ext, &Help);
	for (unsigned i=0; i<Help.Length(); i++)
	{
		if (stristr(Help[i], "leveldb"))
			continue;
		strcpy_s(Path, PathSize, Help[i]);
		LTrimDir(Path);
		Help.DeleteArrays();
		return true;
	}
	
	return false;
}

bool ScribeWnd::LaunchHelp(const char *File)
{
	THREAD_UNSAFE(false);

	if (File)
	{
		char *Hash = 0;
		LBrowser *Browse = 0;
		
		// Find help files...
		char Path[MAX_PATH_LEN];
		if (!GetHelpFilesPath(Path, sizeof(Path)))
		{
			LgiTrace("%s:%i - GetHelpFilesPath failed.\n", _FL);
			goto HelpError;
		}

		// Add filename...
		LMakePath(Path, sizeof(Path), Path, File);
		
		// Check existance...
		Hash = strrchr(Path, '#');
		if (Hash) *Hash = 0;
		if (!LFileExists(Path))
		{
			LgiTrace("%s:%i - FileExists('%s') failed.\n", _FL, Path);
			goto HelpError;
		}
		if (Hash) *Hash = '#';

		#if USE_INTERNAL_BROWSER

		Browse = new LBrowser(this, "Help");
		if (Browse)
		{
			auto p = ScribeResourcePath();
			Browse->AddPath(ScribeResourcePath());
			Browse->SetEvents(d);
			return Browse->SetUri(Path);
		}

		#else

			#ifdef MAC

			if (LExecute(Path))
				return true;
			
			#else

			if (Hash) *Hash = '#';
			
			// Get browser...
			char Browser[256];
			if (!LGetAppForMimeType("application/browser", Browser, sizeof(Browser)))
			{
				LgiTrace("%s:%i - LGetAppForMimeType('text/html') failed.\n", _FL);
				goto HelpError;
			}
			
			// Execute browser to view help...
			char Uri[256];
			sprintf_s(Uri, sizeof(Uri), "\"file://%s\"", Path);
			#ifdef WIN32
			char *c;
			while (c = strchr(Uri, '\\')) *c = '/';
			#endif

			LgiTrace("LaunchHelp('%s','%s').\n", Browser, Uri);
			if (!LExecute(Browser, Uri))
			{
				LgiTrace("%s:%i - LExecute('%s','%s') failed.\n", _FL, Browser, Uri);
				goto HelpError;
			}
			return true;
			
			#endif

		#endif
		
		HelpError:
		LgiMsg(this, LLoadString(IDS_ERROR_NO_HELP), AppName, MB_OK);
	}
	
	return false;
}

void ScribeWnd::Preview(ssize_t Which)
{
	THREAD_UNSAFE();

	LArray<ScribeAccount*> a;
	a.Add(Accounts[Which]);
	OpenPopView(this, a);
}

bool MergeSegments(LDataPropI *DstProp, LDataPropI *SrcProp, LDom *Dom)
{
	LDataI *Src = dynamic_cast<LDataI *>(SrcProp);
	LDataI *Dst = dynamic_cast<LDataI *>(DstProp);
	if (!Dst || !Src)
		return false;

	Store3MimeType Mt(Src->GetStr(FIELD_MIME_TYPE));
	if (Mt.IsText())
	{
		// Set the headers...
		Dst->SetStr(FIELD_INTERNET_HEADER, Src->GetStr(FIELD_INTERNET_HEADER));

		// Do mail merge of data part...
		LAutoStreamI Data = Src->GetStream(_FL);
		if (Data)
		{
			// Read data out into a string string...
			LAutoString Str(new char[(int)Data->GetSize()+1]);
			Data->Read(Str, (int)Data->GetSize());
			Str[Data->GetSize()] = 0;

			// Do field insert and save result to segment
			char *Merged = ScribeInsertFields(Str, Dom);
			LAutoStreamI Mem(new LMemStream(Merged, strlen(Merged)));
			Dst->SetStream(Mem);
		}
	}
	else
	{
		// Straight copy...
		Dst->CopyProps(*Src);
	}

	// Merge children segments as well
	LDataIt Sc = Src->GetList(FIELD_MIME_SEG);
	LDataIt Dc = Dst->GetList(FIELD_MIME_SEG);
	if (Dc && Sc)
	{
		for (unsigned i=0; i<Sc->Length(); i++)
		{
			// Create new dest child, and merge the source child across
			LDataPropI *NewDestChild = Dc->Create(Dst->GetStore());
			if (!MergeSegments(NewDestChild, (*Sc)[i], Dom))
				return false;

			LDataI *NewDest = dynamic_cast<LDataI*>(NewDestChild);
			if (NewDest)
				NewDest->Save(Dst);
		}
	}

	return true;
}

// Either 'FileName' or 'Source' will be valid
void ScribeWnd::MailMerge(LArray<ListAddr*> &Contacts, const char *FileName, Mail *Source)
{
	THREAD_UNSAFE();

	ScribeFolder *Outbox = GetFolder(FOLDER_OUTBOX);

	if (Outbox && Contacts.Length() && (FileName || Source))
	{
		LAutoPtr<Mail> ImportEmail;
		if (FileName)
		{
			LAutoPtr<LFile> File(new LFile);
			if (File->Open(FileName, O_READ))
			{
				Thing *t = CreateItem(MAGIC_MAIL, Outbox, false);
				ImportEmail.Reset(Source = t->IsMail());
				if (!Source->Import(Source->AutoCast(File), sMimeMessage))
				{
					Source = 0;
				}
			}
		}
		
		if (Source)
		{
			List<Mail> Msgs;
			ScribeDom Dom(this);

			// Do the merging of the document with the database
			for (unsigned i=0; i<Contacts.Length(); i++)
			{
				ListAddr *la = Contacts[i];
				RecipientItem *ri = (*la)[0];
				Contact *c = ri ? ri->GetContact() : 0;
				Contact *temp = new Contact(this);
				if (!c)
				{
					temp->SetFirst(la->sName);
					temp->SetEmail(la->sAddr);
					c = temp;
				}

				Dom.Con = c;
				Thing *t = CreateItem(MAGIC_MAIL, Outbox, false);
				if (t)
				{
					Dom.Email = t->IsMail();
					
					LAutoString s;
					if (s.Reset(ScribeInsertFields(Source->GetSubject(), &Dom)))
						Dom.Email->SetSubject(s);
					LDataPropI *Recip = Dom.Email->GetTo()->Create(Dom.Email->GetObject()->GetStore());
					if (Recip)
					{
						Recip->CopyProps(*Dom.Con->GetObject());
						Recip->SetInt(FIELD_CC, 0);
						Dom.Email->GetTo()->Insert(Recip);
					}
					MergeSegments(	Dom.Email->GetObject()->GetObj(FIELD_MIME_SEG),
									Source->GetObject()->GetObj(FIELD_MIME_SEG),
									&Dom);

					Msgs.Insert(Dom.Email);
				}

				temp->DecRef();
			}

			// Ask user what to do
			if (Msgs[0])
			{
				char Msg[256];
				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_MAIL_MERGE_Q), Msgs.Length());
				
				auto Ask = new LAlert(this, AppName, Msg,
					LLoadString(IDS_SAVE_TO_OUTBOX), LLoadString(IDS_CANCEL));

				Ask->DoModal([this, Msgs, Outbox](auto dlg, auto id)
				{
					switch (id)
					{
						case 1: // Save To Outbox
						{
							for (size_t i=0; i<Msgs.Length(); i++)
							{
								Msgs[i]->Save(Outbox);
							}
							break;
						}
						case 2: // Cancel
						{
							for (size_t i=0; i<Msgs.Length(); i++)
							{
								Msgs[i]->OnDelete();
							}
							break;
						}
					}
				});
			}
			else
			{
				LgiMsg(this, LLoadString(IDS_MAIL_MERGE_EMPTY), AppName);
			}
		}
	}
}

ScribeFolder *CastFolder(LDataI *f)
{
	if (!f)
	{
		LAssert(!"Null pointer");
		return 0;
	}

	if (f->Type() == MAGIC_FOLDER)
	{
		return (ScribeFolder*)f->UserData;
	}

	return 0;
}

Thing *CastThing(LDataI *t)
{
	if (!t)
	{
		LAssert(!"Null pointer");
		return 0;
	}

	if (t->Type() == MAGIC_FOLDER)
	{
		LAssert(!"Shouldn't be a folder");
		return 0;
	}

	return (Thing*)t->UserData;
}

LString _GetUids(LArray<LDataI*> &items)
{
	LString::Array a;
	for (auto i: items)
		a.New().Printf(LPrintfInt64, i->GetInt(FIELD_SERVER_UID));
	return LString(",").Join(a);
}

/// Received new items from a storage backend.
void ScribeWnd::OnNew
(
	/// The parent folder of the new item
	LDataFolderI *Parent,
	/// All the new items
	LArray<LDataI*> &NewItems,
	/// The position in the parent folder or -1
	int Pos,
	/// Non-zero if the object is a new email.
	bool IsNew,
	/// Account requests filtering of email.
	bool FilterIncoming
)
{
	THREAD_UNSAFE();

	auto Fld = CastFolder(Parent);
	int UnreadDiff = 0;

	if (!Fld)
	{
		// When this happens the parent hasn't been loaded by the UI thread
		// yet, so if say the IMAP back end notices a new sub-folder then we
		// can safely ignore it until such time as the UI loads the parent
		// folder. At which point the child we got told about here will be 
		// loaded anyway.

		#if DEBUG_NEW_MAIL
		LDataFolderI *p = dynamic_cast<LDataFolderI*>(Parent);
		LgiTrace("%s:%i - NewMail.OnNew no UI object for '%s'\n", _FL, p ? p->GetStr(FIELD_FOLDER_NAME) : NULL);
		#endif
		return;
	}
	
	if (!Parent || !NewItems.Length())
	{
		LAssert(!"Param error");
		return;
	}

	for (auto cb: d->Store3EventCallbacks)
		cb->OnNew(Parent, NewItems, Pos, IsNew, FilterIncoming);

	List<Mail> NewMail;
	for (auto Item: NewItems)
	{
		if (Item->Type() == MAGIC_FOLDER)
		{
			// Insert new folder into the right point on the tree...
			LDataFolderI *SubObject = dynamic_cast<LDataFolderI*>(Item);
			if (!SubObject)
			{
				LAssert(!"Not a valid folder.");
				continue;
			}
			
			
			ScribeFolder *Sub = CastFolder(SubObject);
			// LgiTrace("OnNew '%s', Sub=%p Children=%i\n", SubObject->GetStr(FIELD_FOLDER_NAME), Sub, SubObject->SubFolders().Length());

			if (!Sub)
			{
				// New folder...
				if ((Sub = new ScribeFolder))
				{
					Sub->App = this;
					Sub->SetObject(SubObject, false, _FL);

					Fld->Insert(Sub);
				}
			}
			else
			{
				// Existing folder...
				Fld->Insert(Sub, Pos);
			}
		}
		else
		{
			auto t = CastThing(Item);
			if (!t)
			{
				// Completely new thing...
				t = CreateThingOfType((Store3ItemTypes) Item->Type(), Item);
			}
			
			if (t)
			{
				if (t->DeleteOnAdd.Obj)
				{
					// Complete a delayed move
					auto OldFolder = t->App->GetFolder(t->DeleteOnAdd.Path);
					if (!OldFolder)
					{
						LgiTrace("%s:%i - Couldn't resolve old folder '%s'\n", _FL, t->DeleteOnAdd.Path.Get());
						if (t->DeleteOnAdd.Callback)
							t->DeleteOnAdd.Callback(Store3Error);
					}
					else
					{
						auto OldItem = t->DeleteOnAdd.Obj;
						if (!OldFolder->Items.HasItem(OldItem))
						{
							LgiTrace("%s:%i - Couldn't find old obj.\n", _FL);
							if (t->DeleteOnAdd.Callback)
								t->DeleteOnAdd.Callback(Store3Error);
						}
						else
						{
							OldItem->OnDelete();
							if (t->DeleteOnAdd.Callback)
								t->DeleteOnAdd.Callback(Store3Success); // I guess?
						}
					}
				}

				t->SetParentFolder(Fld);
				Fld->Update();

				auto fldPath = Fld->GetPath();
				if (Fld->Select())
				{
					// Existing thing...
					t->SetFieldArray(Fld->GetFieldArray());

					ThingFilter *Filter = GetThingFilter();
					if (!Filter || Filter->TestThing(t))
					{
						MailList->Insert(t, -1, false);
					}
				}

				auto m = t->IsMail();
				if (m)
				{
					bool read = TestFlag(m->GetFlags(), MAIL_READ);
					UnreadDiff += read ? 0 : 1;

					#if 0 // DEBUG_NEW_MAIL
					LgiTrace("%s:%i - NewMail.OnNew t=%p uid=%s read=%i IsNew=%i\n", _FL,
						t, m->GetServerUid().ToString().Get(),
						read, IsNew);
					#endif

					if (IsNew && FilterIncoming)
					{
						LAssert(!NewMail.HasItem(m));
						NewMail.Insert(m);
					}
				}

				t->Update();
			}
		}
	}

	if (UnreadDiff)
		Fld->OnUpdateUnRead(UnreadDiff, false);

	if (MailList && Fld->Select())
	{
		#if 1
			MailList->Sort();
		#else
			auto fieldSort = Fld->GetFieldSort();
			auto dir = fieldSort.Ascend ? 1 : -1;
			MailList->Sort([Fld, dir, fieldSort](auto *a, auto *b)
				{
					return dir * a->Compare(b, fieldSort.Col);
				});
		#endif
	}

	if (NewMail.Length())
		OnNewMail(NewMail, true);
}

void ScribeWnd::OnPropChange(LDataStoreI *store, int Prop, LVariantType Type)
{
	THREAD_UNSAFE();

	switch (Prop)
	{
		case FIELD_IS_ONLINE:
		{
			// This is in case we receive a message after the app has shutdown and
			// deleted 'this'.
			if (ScribeState > ScribeRunning)
				break;
			
			if (StatusPanel)
				StatusPanel->Invalidate();
			
			for (auto a: Accounts)
			{
				if (a->Receive.GetDataStore() == store)
				{
					int64 Online = store->GetInt(FIELD_IS_ONLINE);

					auto Old = ScribeState;

					// This prevents the folders unloading during this call.
					// Which causes crashes.
					ScribeState = ScribeLoadingFolders;

					a->Receive.OnOnlineChange(Online != 0);
					ScribeState = Old;
					break;
				}
			}
			break;
		}
		case FIELD_MAPI_PROFILES:
		{
			// Store the available profiles in the account, so the user can pick from them later:
			auto profiles = store->GetStr(Prop);
			LString userName = store->GetStr(FIELD_NAME);

			for (auto a : Accounts)
			{
				if (a->Receive.ProtocolType() == ProtocolMapi &&
					userName.Equals(a->Receive.UserName().Str()))
				{
					a->Receive.MapiProfiles(profiles);
				}
			}
			break;
		}
	}
}

// This checks the certificate store to see if the user has selected to ALLOW 
// a specific certificate for a host name.
//
// The code that writes to that store is:
// - ScribeWnd::NeedsCapability
// - ScribeWnd::StartAction
// - ScribeWndPrivate::AllowCert
bool ScribeWnd::AllowSslCert(const char *certHost, LArray<uint8_t> *certId)
{
	THREAD_SAFE();
	
	if (!certHost || !certId)
	{
		LAssert(!"Param error");
		return false;
	}
	
	// Get the options and lock the cert info tag:
	if (!d->Options)
	{
		LgiTrace("%s:%i - no options loaded?\n", _FL);
		LAssert(!"No options");
		return false;
	}
	
	auto certIdHex = LHex(LString((const char*)certId->AddressOf(), certId->Length()));
	bool status = false;
	if (auto certOpts = d->Options->LockTag(OPT_SavedCerts, _FL))
	{
		for (auto t: certOpts->Children)
		{
			if (!t->IsTag(OPT_Cert))
				continue;
			
			auto host = t->GetAttr(OPT_Host);
			if (Stricmp(host, certHost))
				continue;
			
			if (auto id = t->GetContent())
			{
				if (certIdHex.Equals(id))
				{
					switch ((TSslAccept)t->GetAsInt(OPT_Accept))
					{
						case SslAcceptOnce:
						{
							// Delete the entry but return true:
							if (t->RemoveTag())
								delete t;
							status = true;
							break;
						}
						case SslAcceptAlways:
						{
							status = true;
							break;
						}
						default:
							break;
					}
				}
			}
			
			if (status)
				break;
		}
		
		d->Options->Unlock();
	}

	return status;
}

// This stores a certificate in the options along with is host and acceptable level.
bool ScribeWndPrivate::AllowCert(TSslAccept accept)
{
	THREAD_SAFE();
	
	auto certOpts = Options->LockTag(OPT_SavedCerts, _FL);
	if (!certOpts)
	{
		Options->CreateTag(OPT_SavedCerts);
		certOpts = Options->LockTag(OPT_SavedCerts, _FL);
	}
	if (!certOpts)
	{
		LAssert(0);
		return false;
	}

	LXmlTag *certTag = nullptr;
	
	for (auto t: certOpts->Children)
	{
		if (!t->IsTag(OPT_Cert))
			continue;
		
		auto host = t->GetAttr(OPT_Host);
		if (Stricmp(host, SslCertHost.Get()))
			continue;
			
		certTag = t;
		break;
	}
	
	if (!certTag)
	{
		// Create a new cert tag...
		certTag = new LXmlTag(OPT_Cert);
		certOpts->InsertTag(certTag);
	}

	bool status = certTag != nullptr;
	if (status)
	{
		certTag->SetAttr(OPT_Accept, (int64_t)accept);
		certTag->SetAttr(OPT_Host, SslCertHost);
		certTag->SetContent(SslCertId);
	}
	else LAssert(0);

	Options->Unlock();
	
	return status;
}

void ScribeWnd::SetContext(const char *file, int line)
{
	THREAD_UNSAFE();

	d->CtxFile = file;
	d->CtxLine = line;
}

bool ScribeWnd::OnChange(LArray<LDataI*> &items, int FieldHint)
{
	THREAD_UNSAFE(false);

	bool UpdateSelection = false;
	List<Mail> NewMail;
	ScribeFolder *Parent = 0;

	// LOG_STORE("OnChange(%i, %i)\n", (int)items.Length(), FieldHint);
	LAssert(d->CtxFile != NULL);

	for (unsigned c=0; c<d->Store3EventCallbacks.Length(); c++)
	{
		d->Store3EventCallbacks[c]->OnChange(items, FieldHint);
	}

	for (unsigned i=0; i<items.Length(); i++)
	{
		LDataI *Item = items[i];
		Thing *t;

		if (Item->Type() == MAGIC_FOLDER)
		{
			auto ItemFolder = dynamic_cast<LDataFolderI*>(Item);
			ScribeFolder *fld = CastFolder(ItemFolder);
			if (fld)
			{
				if (FieldHint == FIELD_STATUS)
				{
					// This is the delayed folder load case:
					fld->IsLoaded(true);
					if (fld->Select())
						fld->Populate(GetMailList(), nullptr);
				}
				else
				{
					fld->Update();
				}
			}
			else
			{
				// LAssert(!"Can't cast to folder?");
			}
		}
		else if ((t = CastThing(Item)))
		{
			ThingUi *Ui = t->GetUI();
			if (Ui)
				Ui->OnChange();
				
			Mail *m = t->IsMail();
			if (m)
			{
				auto StoreFlags = Item->GetInt(FIELD_FLAGS);

				#if 0
				LgiTrace("App.OnChange(%i) handler %p: %s -> %s\n", 
					FieldHint, m,
					EmailFlagsToStr(m->FlagsCache).Get(),
					EmailFlagsToStr(StoreFlags).Get());
				#endif
				
				if (TestFlag(m->FlagsCache, MAIL_NEW) &&
					!TestFlag(StoreFlags, MAIL_NEW))
				{
					Mail::NewMailLst.Delete(m);
					Parent = m->GetFolder();
				}
					
				if (m->FlagsCache != StoreFlags)
				{
					// LgiTrace("%s:%i - OnChange mail flags changed.\n", _FL);
					m->SetFlagsCache(StoreFlags, false, false);
					Parent = m->GetFolder();
				}

				if (m->NewEmail == Mail::NewEmailLoading)
				{
					auto Loaded = m->GetLoaded();
					if (Loaded < Store3Loaded)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnChange.GetBody t=%p, uid=%s, mode=%s, loaded=%s (%s:%i)\n",
							_FL, (Thing*)m, m->GetServerUid().ToString().Get(),
							toString(m->NewEmail), toString(Loaded),
							d->CtxFile, d->CtxLine);
						#endif

						m->GetBody();
					}
					else
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnChange.NewMail t=%p, uid=%s, mode=%s, loaded=%s (%s:%i)\n",
							_FL, (Thing*)m, m->GetServerUid().ToString().Get(),
							toString(m->NewEmail), toString(Loaded),
							d->CtxFile, d->CtxLine);
						#endif

						NewMail.Insert(m);
					}
				}
			}
			else if (t->IsCalendar())
			{
				for (auto cv: CalendarView::CalendarViews)
					cv->OnContentsChanged();
			}

			#ifdef _DEBUG
			if (FieldHint == FIELD_MIME_SEG)
			{
				// This was a hack to fix a dumb bug in a dev build... so so dumb.
				t->SetDirty();
			}
			else 
			#endif
			if (FieldHint != FIELD_FLAGS)
			{
				// Call the on load handler...
				t->IsLoaded(true);
			}

			if (t->GetList())
			{
				t->Update();
				if (FieldHint != FIELD_FLAGS)
					UpdateSelection |= t->Select();
			}
		}
	}

	if (MailList && UpdateSelection)
	{
		List<Thing> Sel;
		if (MailList->GetSelection(Sel))
		{
			OnSelect(&Sel, true);
		}
	}

	if (Parent)
		Parent->OnUpdateUnRead(0, true);

	if (NewMail.Length())
		OnNewMail(NewMail, true);

	d->CtxFile = NULL;
	d->CtxLine = 0;
	return true;
}

ContactGroup *ScribeWnd::FindGroup(char *SearchName)
{
	THREAD_UNSAFE(NULL);

	ScribeFolder *g = GetFolder(FOLDER_GROUPS);
	if (!g || !SearchName)
		return 0;

	g->LoadThings();
	for (Thing *t: g->Items)
	{
		ContactGroup *Grp = t->IsGroup();
		if (!Grp)
			continue;

		auto Name = Grp->GetObject()->GetStr(FIELD_GROUP_NAME);
		if (Name)
		{
			if (!_stricmp(Name, SearchName))
			{
				return Grp;
			}
		}
	}

	return 0;
}

bool ScribeWnd::Match(LDataStoreI *Store, LDataPropI *Address, int ObjType, LArray<LDom*> &Matches)
{
	THREAD_UNSAFE(false);

	if (!Store || !Address)
		return 0;

	// char *Addr = Address->GetStr(ObjType == MAGIC_CONTACT ? FIELD_EMAIL : FIELD_NAME);
	auto Addr = Address->GetStr(FIELD_EMAIL);
	if (!Addr)
		return 0;

	List<Contact> *l = GetEveryone();
	if (!l)
		return 0;

	Matches.Length(0);

	if (ObjType == MAGIC_CONTACT)
	{
		for (auto c: *l)
		{
			if (strchr(Addr, '@'))
			{
				auto Emails = c->GetEmails();
				for (auto e: Emails)
				{
					if (e.Equals(Addr))
					{
						Matches.Add(c);
						break;
					}
				}
			}
		}
	}
	else if (ObjType == MAGIC_GROUP)
	{
		ScribeFolder *g = GetFolder(FOLDER_GROUPS);
		if (!g)
			return false;

		g->LoadThings();
		for (auto t: g->Items)
		{
			ContactGroup *Grp = t->IsGroup();
			if (!Grp)
				continue;

			List<char> GrpAddr;
			if (!Grp->GetAddresses(GrpAddr))
				continue;

			for (auto a: GrpAddr)
			{
				if (_stricmp(a, Addr) == 0)
				{
					Matches.Add(t);
					break;
				}
			}

			GrpAddr.DeleteArrays();
		}
	}


	return Matches.Length() > 0;
}

bool ScribeWnd::OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
{
	THREAD_UNSAFE(false);

	if (!new_parent || items.Length() == 0)
		return false;

	bool Status = false;

	for (auto cb: d->Store3EventCallbacks)
		cb->OnMove(new_parent, old_parent, items);

	ScribeFolder *New = CastFolder(new_parent);
	ScribeFolder *Old = old_parent ? CastFolder(old_parent) : NULL;
	if (New)
	{
		ssize_t SelIdx = -1;
		for (unsigned n=0; n<items.Length(); n++)
		{
			switch ((uint32_t)items[n]->Type())
			{
				case MAGIC_FOLDER:
				{
					ScribeFolder *i = CastFolder(items[n]);
					if (i)
					{
						i->Detach();
						New->Insert(i);
						Status = true;
					}
					break;
				}
				default:
				{
					Thing *t = CastThing(items[n]);
					if (t)
					{
						int UnreadMail = t->IsMail() ? !TestFlag(t->IsMail()->GetFlags(), MAIL_READ) : false;

						if (Old && UnreadMail)
							Old->OnUpdateUnRead(-1, false);

						if (t->GetList())
						{
							if (t->Select() && SelIdx < 0)
								SelIdx = t->GetList()->IndexOf(t);
							t->GetList()->Remove(t);
						}
						
						// This closes the user interface if the object is being moved to the trash...
						if (New->GetSystemFolderType() == Store3SystemTrash)
							t->SetUI();

						t->SetParentFolder(New);
						if (New->Select())
						{
							MailList->Insert(t);
							MailList->Sort();
						}
						if (UnreadMail) New->OnUpdateUnRead(1, false);
						if (New->GetItemType() == MAGIC_ANY &&
							t->IsMail())
						{
							Mail::NewMailLst.Delete(t->IsMail());
						}

						Status = true;
					}
					break;
				}
			}
		}

		if (MailList && SelIdx >= 0 && MailList->Length() > 0)
		{
			if (SelIdx >= (ssize_t)MailList->Length())
				SelIdx = MailList->Length()-1;
			MailList->Value(SelIdx);
		}
	}

	return Status;
}

bool ScribeWnd::OnDelete(LDataFolderI *Parent, LArray<LDataI*> &Items)
{
	THREAD_UNSAFE(false);

	int UnreadAdjust = 0;
	int SelectIdx = -1;

	LOG_STORE("OnDelete(%s, %i)\n", Parent->GetStr(FIELD_FOLDER_NAME), (int)Items.Length());

	if (!Items.Length())
		return false;

	for (unsigned c=0; c<d->Store3EventCallbacks.Length(); c++)
	{
		d->Store3EventCallbacks[c]->OnDelete(Parent, Items);
	}

	ScribeFolder *Fld = NULL;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		LDataI *Item = Items[i];

		if (Item->Type() == MAGIC_FOLDER)
		{
			// Insert new folder into the right point on the tree...
			ScribeFolder *Sub = CastFolder(Item);
			if (!Sub)
				return true;

			LgiTrace("OnDelete folder %p (%i)\n", (ThingType*)Sub, ThingType::DirtyThings.HasItem(Sub));
			// Remove the deleted object from the dirty queue...
			ThingType::DirtyThings.Delete(Sub);
			// And make sure it can't get dirty again...
			Sub->SetWillDirty(false);

			// Remove all the children
			LDataIterator<LDataI*> &SubItems = Sub->GetFldObj()->Children();
			LArray<LDataI*> Children;
			for (LDataI *c = SubItems.First(); c; c = SubItems.Next())
			{
				Children.Add(c);
			}
			OnDelete(Sub->GetFldObj(), Children);

			// Remove the UI element
			Sub->Remove();
			
			// Free the memory for the object
			DeleteObj(Sub);
		}
		else
		{
			Fld = Parent ? CastFolder(Parent) : 0;
			if (!Fld)
				return false;

			Thing *t = CastThing(Item);
			if (!t)
			{
				// This happens when the item moves from one store to another
				// of a different type and a copy of the old object is made. The
				// 'Thing' is detached from 'Item' and attached to the new LDataI
				// object.

				// However if the object is an unread email... we should still decrement the 
				// folder's unread count.
				if (Item->Type() == MAGIC_MAIL &&
					!(Item->GetInt(FIELD_FLAGS) & MAIL_READ))
				{
					Fld->OnUpdateUnRead(-1, false);
				}
			}
			else
			{
				LAssert(!Fld || Fld == t->GetFolder());
				
				// LgiTrace("OnDelete thing %p (%i)\n", (ThingType*)t, ThingType::DirtyThings.HasItem(t));
				
				// Remove the deleted object from the dirty queue...
				ThingType::DirtyThings.Delete(t);
				// And make sure it can't get dirty again...
				t->SetWillDirty(false);
				
				// Was the thing currently being previewed?
				if (PreviewPanel && PreviewPanel->GetCurrent() == t)
					PreviewPanel->OnThing(0, false);

				// Was the object selected in the thing list?
				if (Fld && Fld->Select())
				{
					// If so, select an adjacent item.
					if (SelectIdx < 0 && t->Select())
						SelectIdx = MailList->IndexOf(t);
					MailList->Remove(t);
				}

				// Was the object an unread email?
				Mail *m = t->IsMail();
				if (m)
				{
					if (!TestFlag(m->GetFlags(), MAIL_READ) && Fld)
						UnreadAdjust--;
					Mail::NewMailLst.Delete(m);
				}

				t->SetUI();
				t->SetParentFolder(NULL);
				t->SetObject(NULL, false, _FL);

				if (Fld)
					Fld->Update();

				if (!t->DecRef())
				{
					t->SetDirty(false);
					t->SetWillDirty(false);
				}
			}
		}
	}

	if (Fld)
		Fld->OnUpdateUnRead(UnreadAdjust, false);

	if (SelectIdx >= 0)
	{
		LListItem *i = MailList->ItemAt(SelectIdx);
		if (!i && MailList->Length() > 0)
			i = MailList->ItemAt(MailList->Length()-1);
		if (i)
			i->Select(true);
	}

	return true;
}

bool ScribeWnd::AddStore3EventHandler(LDataEventsI *callback)
{
	THREAD_UNSAFE(false);

	if (!d->Store3EventCallbacks.HasItem(callback))
		d->Store3EventCallbacks.Add(callback);

	return true;
}

bool ScribeWnd::RemoveStore3EventHandler(LDataEventsI *callback)
{
	THREAD_UNSAFE(false);

	d->Store3EventCallbacks.Delete(callback);

	return true;
}

bool ScribeWnd::OnMailTransferEvent(MailTransferEvent *t)
{
	THREAD_SAFE();

	if (!Lock(_FL))
		return false;

	LAssert(t);
	d->Transfers.Add(t);
	Unlock();
	
	return true;
}

bool ScribeWnd::OnTransfer()
{
	THREAD_UNSAFE(false);

	LVariant v;
	LArray<MailTransferEvent*> Local;
	
	// Lock the transfer list
	if (Lock(_FL))
	{
		// Take out a bunch of emails...
		for (int i=0; i<5 && d->Transfers.Length() > 0; i++)
		{
			auto t = d->Transfers[0];
			LAssert(t);
			d->Transfers.DeleteAt(0, true);

			// Save them to a local array
			Local.Add(t);
		}

		Unlock();

		if (Local.Length() == 0)
			// Didn't get any
			return false;
	}

	LArray<LDataStoreI::StoreTrans> Trans;
	for (auto &f: Folders)
	{
		if (f.Store)
			Trans.New() = f.Store->StartTransaction();
	}

	for (auto Transfer: Local)
	{
		ReceiveStatus NewStatus = MailReceivedError;

		if (!Transfer)
		{
			LAssert(0);
			continue;
		}
		
		// We have to set Transfer->Status to something other than "waiting"
		// in all branches of this loop. Otherwise the account thread will hang.
		
		Accountlet *Acc = Transfer->Account;
		#if DEBUG_NEW_MAIL
		LgiTrace(	"%s:%i - NewMail.OnTransfer t=%p receive=%i act=%i\n",
					_FL, Transfer, Acc->IsReceive(), Transfer->Action);
		#endif
		if (Acc->IsReceive())
		{
			switch (Transfer->Action)
			{
				default: break;
				case MailDownload:
				case MailDownloadAndDelete:
				{
					// Import newly received mail from file
					ReceiveAccountlet *Receive = dynamic_cast<ReceiveAccountlet*>(Acc);
					if (Receive)
					{
						LVariant Path = Receive->DestinationFolder();
						ScribeFolder *Inbox = Path.Str() ? GetFolder(Path.Str()) : NULL;
						if (!Inbox)
							Inbox = GetFolder(FOLDER_INBOX);

						if (Inbox)
						{
							Mail *m = new Mail(this, Inbox->GetObject()->GetStore()->Create(MAGIC_MAIL));
							if (m)
							{
								// Just in case
								m->SetParentFolder(Inbox);
								
								// Set the new flag...
								m->SetFlags(m->GetFlags() | MAIL_NEW, true, false);

								// Set the account to, which is used to map the email back
								// to the incoming account in the case that the headers
								// don't have the correct "To" information.
								m->SetAccountId(Receive->Id());

								// Decode the email
								m->OnAfterReceive(Transfer->Rfc822Msg);
								
								LVariant v;
								m->SetServerUid(v = Transfer->Uid);

								// Save to permanent storage
								if (m->Save(Inbox))
								{
									m->SetDirty(false);
									
									NewStatus = MailReceivedOk;

									// Only after we've safely stored the email can we 
									// actually mark it as downloaded.
									if (Transfer->Uid)
									{
										Receive->AddMsg(Transfer->Uid);
									}
								}
								else LgiTrace("%s:%i - Error: Couldn't save mail to folders.\n", _FL);
							}
							else LgiTrace("%s:%i - Error: Memory alloc failed.\n", _FL);
						}
						else LgiTrace("%s:%i - Error: No Inbox.\n", _FL);
					}
					else LgiTrace("%s:%i - Error: Bad ptr.\n", _FL);
					
					break;
				}
				case MailHeaders:
				{
					LList *Lst;
					if (Transfer->Msg &&
						(Lst = Transfer->GetList()))
					{
						//LgiTrace("Using Lst=%p\n", Lst);
						Lst->Insert(Transfer->Msg);
						NewStatus = MailReceivedOk;
					}
					break;
				}
			}
		}
		else if (Transfer->Send)
		{
			ScribeFolder *Outbox = GetFolder(Transfer->Send->SourceFolder);
			if (!Outbox)
				Outbox = GetFolder(FOLDER_OUTBOX);
			if (!Outbox)
				break;
				
			Outbox->GetMessageById(Transfer->Send->MsgId, [this, Transfer](auto m)
			{
				if (!m)
				{
					LAssert(!"Where is the email?");
					LgiTrace("%s:%i - Can't find outbox for msg id '%s'\n", _FL, Transfer->Send->MsgId.Get());
				}
				else
				{
					if (Transfer->OutgoingHeaders)
					{
						m->SetInternetHeader(Transfer->OutgoingHeaders);
						DeleteArray(Transfer->OutgoingHeaders);
					}
					
					m->OnAfterSend();
					m->Save();

					// Do filtering
					LVariant DisableFilters;
					GetOptions()->GetValue(OPT_DisableUserFilters, DisableFilters);
					if (!DisableFilters.CastInt32())
					{
						// Run the filters
						List<Filter> Filters;
						GetFilters(Filters, false, true, false);
						if (Filters[0])
						{													
							List<Mail> In;
							In.Insert(m);
							
							Filter::ApplyFilters(0, Filters, In);
						}
					}

					// Add to bayesian spam whitelist...
					LVariant v;
					ScribeBayesianFilterMode FilterMode = BayesOff;
					GetOptions()->GetValue(OPT_BayesFilterMode, v);
					FilterMode = (ScribeBayesianFilterMode) v.CastInt32();

					if (FilterMode != BayesOff &&
						m->GetObject())
					{
						LDataIt To = m->GetObject()->GetList(FIELD_TO);
						if (To)
						{
							for (LDataPropI *a = To->First();
								a;
								a = To->Next())
							{
								if (a->GetStr(FIELD_EMAIL))
									WhiteListIncrement(a->GetStr(FIELD_EMAIL));
							}
						}
					}

					// FIXME:
					// NewStatus = MailReceivedOk;
				}
			});
		}

		#if DEBUG_NEW_MAIL
		LgiTrace(	"%s:%i - NewMail.OnTransfer t=%p NewStatus=%i\n",
					_FL, Transfer, NewStatus);
		#endif

		if (NewStatus != MailReceivedOk)
		{
			// So tell the thread not to delete it from the server
			LgiTrace("%s:%i - Mail[%i] error: %s\n", _FL, Transfer->Index, ReceiveStatusName(Transfer->Status));
			Transfer->Action = MailNoop;
		}

		Transfer->Status = NewStatus;
	}

	return Local.Length() > 0;
}

bool ScribeWnd::OnIdle()
{
	THREAD_UNSAFE(false);

	bool Status = false;
	
	for (auto a : Accounts)
		Status |= a->Receive.OnIdle();

	Status |= OnTransfer();

	LMessage m(M_SCRIBE_IDLE);
	BayesianFilter::OnEvent(&m);

	SaveDirtyObjects();	

	#ifdef _DEBUG
	static uint64_t LastTs = 0;
	auto Now = LCurrentTime();
	if (Now - LastTs >= 1000)
	{
		LastTs = Now;
		if (Thing::DirtyThings.Length() > 0)
			LgiTrace("%s:%i - Thing::DirtyThings=" LPrintfInt64 "\n", _FL, Thing::DirtyThings.Length());
	}
	#endif

	return Status;
}

void ScribeWnd::OnScriptCompileError(const char *Source, Filter *f)
{
	THREAD_UNSAFE();

	const char *CompileMsg = LLoadString(IDS_ERROR_SCRIPT_COMPILE);
	LArray<const char*> Actions;
	Actions.Add(LLoadString(IDS_SHOW_CONSOLE));
	Actions.Add(LLoadString(IDS_OPEN_SOURCE));
	Actions.Add(LLoadString(IDS_OK));
	if (!d->Bar)
	{
		// FYI Capabilities are handled in ScribeWnd::StartAction.
		d->ErrSource = Source;
		d->ErrFilter = f;
		d->Bar = new MissingCapsBar(this, &d->MissingCaps, CompileMsg, this, Actions);
		AddView(d->Bar, 2);
		AttachChildren();
		OnPosChange();
	}
}

////////////////////////////////////////////////////////////////////////////////////
ScribeFolder *LMailStore::GetRoot() const
{
	return Root;
}

void LMailStore::SetRoot(ScribeFolder *f)
{
	if ((Root = f))
	{
		// Something is deleting the ScribeFolder object that root points to outside of the
		// DeleteRoot function. I want to catch that case and make sure there is no dangling
		// pointer here to cause a crash.
		Root->BeforeDelete = [this]()
		{
			LAssert(InRootDelete);
			if (!InRootDelete)
				Root = NULL;
		};
	}
}

void LMailStore::DeleteRoot()
{
	InRootDelete = true;
	DeleteObj(Root);
	InRootDelete = false;
}

bool LMailStore::IsOk() const
{
	return	Store != NULL &&
			Root  != NULL;
}

int LMailStore::Priority()
{
	return Store ? (int)Store->GetInt(FIELD_VERSION) : 0;
}

void LMailStore::Empty()
{
	Store.Reset();
	Name.Empty();
	Path.Empty();
	Root = NULL;
}

LMailStore &LMailStore::operator =(LMailStore &a)
{
	LAssert(0);
	return *this;
}

ScribeWndPrivate::ScribeWndPrivate(ScribeWnd *app) :
	App(app),
	TextControlFactory(app),
	TrayIcon(new LTrayIcon(app))
{
	htmlStatic = new LHtmlStaticInst;
	NoContact = new NoContactType(app);
	NoContact->DecRef(); // 2->1
	AppWndHnd = LEventSinkMap::Dispatch.AddSink(App);

#ifdef WIN32
	ClipboardFormat = RegisterClipboardFormat(
#ifdef UNICODE
		L"Scribe.Item"
#else
		"Scribe.Item"
#endif
	);
#endif

	LScribeScript::Inst = new LScribeScript(App);
	if (Engine.Reset(new LScriptEngine(App, LScribeScript::Inst, this)))
		Engine->SetConsole(LScribeScript::Inst->GetLog());
}

ScribeWndPrivate::~ScribeWndPrivate()
{
	// Why do we need this? ~LView will take care of it?
	// LEventSinkMap::Dispatch.RemoveSink(App);
	Options.Reset();
	Scripts.DeleteObjects();
	DeleteObj(ImageLoader);
	Engine.Reset();
	DeleteObj(LScribeScript::Inst);
	DeleteObj(htmlStatic);
}
