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

#include "ScribeWndPrivate.h"

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

