/*
**	FILE:			ScribeMain.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			9/6/98
**	DESCRIPTION:	Scribe app entry point
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

#include "Scribe.h"
#include "ScribePrivate.h"
#include "lgi/common/Css.h"

#include "lgi/common/Filter.h"
#include "lgi/common/DateTime.h"
#include "lgi/common/DateTime.h"
#include "lgi/common/EmojiFont.h"

#if WINNATIVE && 0
	#include "crashpad_client.h"
	crashpad::CrashpadClient CrashClient;
	#pragma comment(lib, "base.lib")
	#pragma comment(lib, "client.lib")
	#pragma comment(lib, "util.lib")
#endif

#if 0 // WINNATIVE
#define STACK_SIZE 12
#include "LSymLookup.h"

int __cdecl _purecall(void)
{
	LSymLookup::Addr Stack[STACK_SIZE];
	ZeroObj(Stack);
	LSymLookup *Lu = LAppInst->GetSymLookup();
	int Frames = Lu ? Lu->BackTrace(0, 0, Stack, STACK_SIZE) : 0;

	char Buffer[1024] = "";
	Lu->Lookup(Buffer, sizeof(Buffer)-1, Stack, Frames);

	#if defined WIN32
	OutputDebugStringA(Buffer);
	#else
	printf("Trace: %s", Buffer);
	#endif
	
	LgiMsg(NULL, Buffer, "Error: Pure Virtual Call");
	LExitApp();
	return 0;
}
#endif

ScribeApp::ScribeApp(OsAppArguments &AppArgs, LAppArguments *Opts) :
	LApp(AppArgs, "Scribe", Opts)
{
	Status = true;
}

bool ScribeOnIdle(void *Param)
{
	LApp *a = (LApp*)Param;
	ScribeWnd *w = dynamic_cast<ScribeWnd*>(a->AppWnd);
	return (w) ? w->OnIdle() : false;
}

extern char *RelativeTime(LDateTime &d);
extern bool ImapParseStructure(LXmlTag *t, char *&s);

#include "lgi/common/TextLog.h"
void Test()
{
	const char *Input[] =
	{
		"\"Sound&Secure@speedytechnical.com\" <soundandsecure@speedytechnical.com>",
		"\"@MM-Social Mailman List\" <social@cisra.canon.com.au>",
		"'Matthew Allen (fret)' <fret@memecode.com>",
		"Matthew Allen (fret) <fret@memecode.com>",
		"\"'Matthew Allen'\" <fret@memecode.com>",
		"Matthew Allen",
		"fret@memecode.com",
		"\"<Matthew Allen>\" <fret@memecode.com>",
		"<Matthew Allen> (fret@memecode.com)",
		"Matthew Allen <fret@memecode.com>",
		"\"Matthew, Allen\" (fret@memecode.com)",
		"Matt'hew Allen <fret@memecode.com>",
		"john.omalley <john.O'Malley@testing.com>",
		"Bankers' Association (ABA)<survey@aawp.org.au>",
		"'Amy's Mum' <name@domain.com>",
		"\"Philip Doggett (JIRA)\" <jira@audinate.atlassian.net>",
		0
	};

	LAutoString Name, Addr;
	for (const char **i = Input; *i; i++)
	{
		Name.Reset();
		Addr.Reset();
		DecodeAddrName(*i, Name, Addr, "name.com");
		LgiTrace("N=%-#32s A=%-32s\n", Name.Get(), Addr.Get());
	}
}

#ifdef _DEBUG
#include "Store3Mail3/Mail3.h"
#include "lgi/common/Json.h"
void GMail3Cal_Test()
{
	GMail3Store Store("c:\\Users\\matthew\\AppData\\Roaming\\Scribe\\Folders.mail3", NULL, false);
	GMail3Calendar *Cal = new GMail3Calendar(&Store);

	LString Old = "Matthew Allen <fret@memecode.com>,Maggie Allen <maggie@memecode.com>";
	Cal->SetStr(FIELD_TO, Old);
	LString Old2 = Cal->GetStr(FIELD_TO);
	LString New = Cal->GetStr(FIELD_ATTENDEE_JSON);
	LAssert(Old == Old2);

	LJson j(New);
	for (auto e: j.GetArray(NULL))
		LgiTrace("%s %s\n", e.Get("name").Get(), e.Get("email").Get());

	Cal->SetStr(FIELD_ATTENDEE_JSON, New);
	LString Old3 = Cal->GetStr(FIELD_TO);
}

void SwapTest()
{
	LXmlTag a, b;
	
	LXmlTree at;
	LFile af("C:\\Users\\matthew\\AppData\\Roaming\\Scribe\\ImapCache\\1434419972\\INBOX\\Folder.xml", O_READ);
	at.Read(&a, &af);

	LXmlTree bt;
	LFile bf("C:\\Users\\matthew\\AppData\\Roaming\\Scribe\\ImapCache\\1434419972\\INBOX\\Accounts\\Folder.xml", O_READ);
	bt.Read(&b, &bf);

	a.Swap(b);
}
#endif

class ParserTest : public LHtmlParser
{
public:
	LHtmlElement *CreateElement(LHtmlElement *Parent)
	{
		return new LHtmlElement(Parent);
	}
};

#include "lgi/common/Html.h"
void Test2()
{
	LFile f("C:\\tmp\\parser-error.html", O_READ);
	auto html = f.Read();
	LHtmlElement Root(NULL);
	Html1::LHtml ctrl(100, 0, 0, 100, 100);;
	ctrl.Name(html);
}

int LgiMain(OsAppArguments &AppArgs)
{
	#if 0
	LDateTime Now, CutOff;
	Now.SetNow();
	CutOff.SetDate("24/8/2004");
	if (Now > CutOff)
	{
		LgiMsg(0, "This demo of Scribe has expired.", "Demo");
		exit(-1);
	}
	else
	{
		char s[256];
		CutOff.GetDate(s);
		LgiMsg(0, "This demo of Scribe will expire on %s.", "Demo", MB_OK, s);
	}
	#endif
	
	#if 0 && defined(__GTK_H__) && defined(_DEBUG)
	LArray<char*> a;
	for (int i=0; i<AppArgs.Args; i++)
		a[i] = AppArgs.Arg[i];
	a.Add("--g-fatal-warnings");
	AppArgs.Arg = &a[0];
	AppArgs.Args = a.Length();
	#endif

	LAppArguments Opts;

	#if WINDOWS && defined(CRASHPAD_CLIENT_CRASHPAD_CLIENT_H_) // Crashpad setup
	Opts.NoCrashHandler = true;
	{
		std::map<std::string, std::string> annotations, file_attachments;
		std::vector<std::string> arguments;
		arguments.push_back("--no-rate-limit");
		std::string url("https://submit.backtrace.io/<Universe>/98a6821fd44c68f699a69efa6520afa73dc2210557d0ea589826a57c24011c55/minidump");

		bool b = CrashClient.StartHandlerForBacktrace(
					base::FilePath(L"P:\\Code\\Scribe\\crashpad-release-x86-64-stable\\bin\\crashpad_handler.exe"),
					base::FilePath(L"C:\\Users\\matthew\\AppData\\Roaming\\Scribe\\Crashes"),
					base::FilePath(),
					"https://memecode.sp.backtrace.io:6098/",
					annotations, arguments, file_attachments,
					false,
					false);
	}
	#endif
	
	ScribeApp App(AppArgs, &Opts);
	if (App.IsOk())
	{
		#ifdef _DEBUG
		// Test2();
		#endif

		InitStrToDom();
		if (App.GetOption("crtcheck"))
			return 0;

		#if !defined(MAC) || defined(__GTK_H__)
		LAutoPtr<LFont> f(new LEmojiFont());
		if (f && f->Create())
			LFontSystem::Inst()->AddFont(f);
		#endif
	
        ScribeWnd *Wnd;
		if (App.GetOption("help"))
		{
			printf(	"\n"
					"Options:\n"
					"    -m<address[,address]>          New email to address.\n"
					"    -c<address[,address]>          Cc recipients.\n"
					"    -s<\"subject\">                Message's subject.\n"
					"    -f<filename>                   Body of message.\n"
					"    -b                             Attach body as an attachment instead.\n"
					"    -nch                           [Linux] Don't use KDE crash handler.\n"
					"\n");
		}
		else if ((Wnd = new ScribeWnd))
		{
            if (Wnd->GetScribeState() == ScribeWnd::ScribeInitializing)
            {
                if (App.AppWnd->Attach(0))
                {
					auto State = Wnd->GetScribeState();
                    if (State == ScribeWnd::ScribeRunning)
                    {
                        #if 0
				        App.Run(true, ScribeOnIdle, &App);
				        #else
				        App.Run();
				        #endif
				    }
				    else
				    {
				    	LgiTrace("%s:%i - GetScribeState() not running.\n", _FL);
				    }
			    }
			    else
			    {
				    LgiTrace("Couldn't create main window.\n");
				    LgiMsg(0, "Couldn't create main window.");
			    }
			}
		}
		else LgiTrace("Memory alloc failed.\n");
	}
	else LgiTrace("LApp initialization failed.\n");

	return 0;
}
