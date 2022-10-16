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

int LgiMain(OsAppArguments &AppArgs)
{
	#if 0 && defined(__GTK_H__) && defined(_DEBUG)
	LArray<char*> a;
	for (int i=0; i<AppArgs.Args; i++)
		a[i] = AppArgs.Arg[i];
	a.Add("--g-fatal-warnings");
	AppArgs.Arg = &a[0];
	AppArgs.Args = a.Length();
	#endif

	LAppArguments Opts;


	ScribeApp App(AppArgs, &Opts);
	if (!App.IsOk())
	{
		LgiTrace("LApp initialization failed.\n");
		return -1;
	}

	InitStrToDom();
	if (App.GetOption("crtcheck"))
		return 0;

	#if !defined(MAC) || defined(__GTK_H__)
	LAutoPtr<LFont> f(new LEmojiFont());
	if (f && f->Create())
		LFontSystem::Inst()->AddFont(f);
	#endif

    ScribeWnd *Wnd = NULL;
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
	else if (!(Wnd = new ScribeWnd))
	{
		LgiTrace("Memory alloc failed.\n");
		return -2;
	}
	
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

	return 0;
}
