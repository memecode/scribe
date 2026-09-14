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
#include <cstddef>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

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
#include "lgi/common/Charset.h"
#include "lgi/common/RefCount.h"
#include "lgi/common/PopupNotification.h"
#include "lgi/common/Base64.h"
#include "lgi/common/Html2.h"
#include "lgi/common/LiteHtmlView.h"

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

enum TrayIconIndex
{
	TRAY_ICON_NORMAL = 0,
	TRAY_ICON_ERROR,
	TRAY_ICON_MAIL,
	TRAY_ICON_NONE
};

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
	{SystemFolderInbox,		FOLDER_INBOX,		OPT_Inbox,		NULL},
	{SystemFolderOutbox,	FOLDER_OUTBOX,		OPT_Outbox,		NULL},
	{SystemFolderSent,		FOLDER_SENT,		OPT_Sent,		NULL},
	{SystemFolderContacts,	FOLDER_CONTACTS,	OPT_Contacts,	NULL},
	{SystemFolderTrash,		FOLDER_TRASH,		OPT_Trash,		NULL},
	{SystemFolderCalendar,	FOLDER_CALENDAR,	OPT_Calendar,	OPT_HasCalendar},
	{SystemFolderTemplates,	FOLDER_TEMPLATES,	OPT_Templates,	OPT_HasTemplates},
	{SystemFolderFilters,	FOLDER_FILTERS,		OPT_Filters,	OPT_HasFilters},
	{SystemFolderGroups,	FOLDER_GROUPS,		OPT_Groups,		OPT_HasGroups},
	{SystemFolderSpam,		FOLDER_SPAM,		OPT_SpamFolder,	OPT_HasSpam},
	{SystemFolderMax,		IDC_STATIC,			nullptr,		nullptr}
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
		auto w = WindowHandle();
		if (w->Thread() < 0 &&
			w->Lock())
		{
			w->Run();
			w->Unlock();
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
	
	setString(NET_LOG_NONE, LLoadString(IDS_NO_LOG));
	setString(NET_LOG_HEX_DUMP, LLoadString(IDS_HEX_LOG));
	setString(NET_LOG_ALL_BYTES, LLoadString(IDS_BYTE_LOG));
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
	if (LGetSystemPath(v.CastInt32() ? LSP_APP_INSTALL : LSP_APP_DATA, p, sizeof(p)))
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

	switch (StrToDom(Name))
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
				auto selectedFolder = Tree ? Tree->Selection() : nullptr;
				Calendar::SummaryOfToday(this,
					[this, selectedFolder](auto s)
					{
						// This is called sometime after the value is returned to the caller...
						d->CalendarSummary = s;
						
						auto curFolder = Tree ? Tree->Selection() : nullptr;
						if (curFolder == selectedFolder)
						{
							// But we can refresh the display, with the new value:
							LoadTitleListPane();
						}
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

bool ScribeWnd::ScanForOptionsFiles(LArray<OptionsInfo> &Files, const char *BasePath)
{
	if (!BasePath)
		return false;

	LFile::Path Root(BasePath);
	if (Root[0].Equals("~"))
		Root = Root.Absolute();
	
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

bool ScribeWnd::ScanForOptionsFiles(LArray<OptionsInfo> &Files, LSystemPath PathType)
{
	return ScanForOptionsFiles(Files, LGetSystemPath(PathType));
}

bool ScribeWnd::IsUnitTest = false;

#ifdef LINUX
// in 2026 the default location for the options file moved from ~/.Scribe to ~/.config/Scribe
// check for the old location and move it....
bool ScribeWnd::LinuxMigrate()
{
	LArray<OptionsInfo> Files;

	const char *OldPath = "~/.Scribe";
	const char *DontMigrate = ".dont-migrate";
	if (!d->Options &&
		ScanForOptionsFiles(Files, OldPath))
	{
		auto NewConfig = LGetSystemPath(LSP_APP_CONFIG);
		auto NewData   = LGetSystemPath(LSP_APP_DATA);
		auto NewCache  = LGetSystemPath(LSP_APP_CACHE);
		LFile::Path DontMigratePath(OldPath, DontMigrate);		
		if (!DontMigratePath.Exists())
		{	
			// Ask the user to migrate:
			auto msg = LString::Fmt("Can I migrate the settings and folders from the old location?"
									"	%s\n"
									"\n"
									"to these new locations:\n"
									"	config: %s\n"
									"	data:   %s\n"
									"	cache:  %s\n"
									"\n"
									"This better reflects where Linux apps would normally store data.",
									OldPath,
									NewConfig.Get(),
									NewData.Get(),
									NewCache.Get());
			auto dlg = new LAlert(nullptr, AppName, msg, "Yes", "No", "Don't Ask Again");
			auto gtkWnd = GtkCast(dlg->WindowHandle(), gtk_window, GtkWindow);
			auto gtkDlg = GtkCast(dlg->WindowHandle(), gtk_dialog, GtkDialog);
			
			// This moves the dialog to the center of the screen, rather than the top-left:
			gtk_window_set_modal(gtkWnd, true);
			
			int dlgCode = -1;
			dlg->DoModal([	this,
							NewConfig,
							NewData,
							NewCache,
							OldPath,
							ptr=&dlgCode](auto Dlg, auto Code)
				{
					*ptr = Code;
				});
				
			// Run the dialog and get the response...
			gtk_dialog_run(gtkDlg);
			printf("dlgCode=%i\n", dlgCode);
			switch (dlgCode)
			{
				case 3: // Don't ask
				{
					LFile f(DontMigratePath, O_WRITE);
					f.SetSize(0);
					f.Write("user asked not to migrate");
					break;
				}
				case 2: // No
					break;
				case 1: // Yes
				{
					LDirectory inDir;
					int errors = 0;
					LStringPipe errLog;
					for (auto b=inDir.First(LFile::Path(OldPath).Absolute()); b; b=inDir.Next())
					{
						LString outDir;
						auto name = inDir.GetName();
						if (!Strnicmp(name, "ScribeOptions", 13))
						{
							// Options file -> config
							outDir = NewConfig;
						}
						else if (MatchStr("*.idx", name))
						{
							// Index file -> config
							outDir = NewConfig;
						}
						else if (inDir.IsDir() &&
							(
								MatchStr("*.mail3", name) ||
								!Stricmp("Aspell", name)
							))
						{
							// Mail folders|Aspell -> data
							outDir = NewData;
						}
						else if (!Stricmp(name, "tmp") ||
								 !Stricmp(name, "ImapCache") ||
								 !Stricmp(name, "scribe.txt"))
						{
							// Cache folders
							outDir = NewCache;
						}
						else printf("%s: %s\n", inDir.IsDir()?"dir":"file", name);
						
						if (outDir)
						{
							LFile::Path outPath(outDir);
							outPath += name;
							printf("attempt to move:\n"
								"\t%s\n"
								"\t%s\n",
								inDir.FullPath(),
								outPath.GetFull().Get());
								
							// what if the file / dir exists at the destination?
							LError err;
							if (outPath.Exists())
							{
								if (LgiMsg(nullptr, "Overwrite '%s'?", AppName, MB_OK, outPath.GetFull().Get()) != IDYES)
									continue;
								
								// delete the dest...
								if (outPath.IsFolder())
								{
									if (!FileDev->RemoveFolder(outPath, true, &err))
									{
										errLog.Print("Can't remove dir '%s': %s\n", outPath.GetFull().Get(), err.ToString().Get());
										errors++;
									}
								}
								else
								{
									if (!FileDev->Delete(outPath, &err))
									{
										errLog.Print("Can't delete file '%s': %s\n", outPath.GetFull().Get(), err.ToString().Get());
										errors++;
									}
								}
							}

							if (!FileDev->Move(inDir.FullPath(), outPath, &err))
							{
								errLog.Print("Can't move '%s' to '%s': %s\n",
									inDir.FullPath(),
									outPath.GetFull().Get(),
									err.ToString().Get());
								errors++;
							}
						}
					}	// dir loop
					
					if (errors > 0)
					{
						LgiMsg(nullptr, "There were %i errors:\n\n%s", AppName, MB_OK, errors, errLog.NewLStr().Get());
					}
					
				}	// case 1
			}	// switch
		}	// DontMigratePath.Exists
	}

	return true;
}
#endif

bool ScribeWnd::LoadOptions()
{
	bool Load = false;
	
	THREAD_UNSAFE(false);

	// Check if we are running unit tests...
	if ((IsUnitTest = LAppInst->GetOption("unittest")))
	{
		d->UnitTestServer.Reset(new LUnitTestServer(this));
	}

	LArray<OptionsInfo> Files;

	#ifdef LINUX
	LinuxMigrate();
	#endif

	// Now look in the application install folder
	if (!d->Options &&
		ScanForOptionsFiles(Files, LSP_APP_INSTALL))
	{
		// File is in the install folder...
		d->SetInstallMode(LOptionsFile::PortableMode);
		LgiTrace("Selecting portable mode based on options file path.\n");
	}
	
	// Look in the app's config folder:
	if
	(
		!d->Options &&
		ScanForOptionsFiles(Files, LSP_APP_CONFIG)
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

			if (!LGetSystemPath(LSP_APP_DATA, Path, sizeof(Path)))
			{
				Log.Print("Error: LgiGetSystemPath(LSP_APP_DATA) failed.\n");
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

