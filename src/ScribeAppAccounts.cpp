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

const char *ScribeWnd::toString(CmdLineEvent e)
{
	switch (e)
	{
		case CmdLineEvent::IpcEvent: return "IpcEvent";
		case CmdLineEvent::StartupEvent: return "StartupEvent";
	}
	return nullptr;
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
			LgiTrace("%s:%i - OnCommandLineEvent(%s) has all flags: calling OnCommandLine.\n", _FL, toString(event));
			OnCommandLine();
		}
		else LgiTrace("%s:%i - OnCommandLineEvent(%s) hasn't got all flags yet.\n", _FL, toString(event));
	}
	else LgiTrace("%s:%i - OnCommandLineEvent(%s): flag %x already set?\n", _FL, toString(event), flag);
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
			d->TrayIcon->Value((d->TrayIcon->Value() == TRAY_ICON_MAIL) ?
								TRAY_ICON_NONE :
								TRAY_ICON_MAIL);
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

		Commands->AppendButton(RemoveAmp(LLoadString(IDS_HELP)), ID_HELP, TBT_PUSH, true, IMG_HELP);

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
		case ID_BUILD_BAYES_DB:
		{
			BuildSpamDb();
			break;
		}
		case ID_BAYES_STATS:
		{
			BuildStats();
			break;
		}
		case ID_CHK_FOLDERS:
		{
			CheckFolders();
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

				auto m = ((ScribeBayesianFilterMode)i.CastInt32());
				if (m != BayesOff)
				{
					LVariant SpamPaths, ProbablyPath;
					GetOptions()->GetValue(OPT_SpamFolder, SpamPaths);
					GetOptions()->GetValue(OPT_BayesMoveTo, ProbablyPath);
											
					if (m == BayesFilter)
					{
						ScribeFolder *Spam = nullptr;
						for (auto SpamPath: LString(SpamPaths.Str()).SplitDelimit("\n"))
						{
							Spam = GetFolder(SpamPath);
							if (Spam)
								continue;

							if (auto RelevantStore = GetMailStoreForPath(SpamPath))
							{
								auto a = SpamPath.SplitDelimit("/");
									
								Spam = RelevantStore->GetRoot();
								for (unsigned i=1; i<a.Length(); i++)
								{
									auto c = Spam->GetSubFolder(a[i]);
									if (!c)
										c = Spam->CreateSubFolder(a[i], MAGIC_MAIL);
									Spam = c;
								}

								break;
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
		case ID_HELP:
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
	THREAD_UNSAFE(nullptr);
	if (!Path)
		return nullptr;

	auto t = LString(Path).SplitDelimit("/");
	if (t.Length() > 0)
	{
		const char *First = t[0];
		
		// Find the mail store that that t[0] refers to
		for (auto &f: Folders)
		{
			if (!f.IsOk())
			{
				printf("%s:%i - folder not ok.\n", _FL);
				continue;
			}
			
			if (auto r = f.GetRoot())
			{
				auto rootStr = r->GetText();
				if (!Stricmp(rootStr, First))
					return &f;
			}
			else printf("%s:%i - no root folder?\n", _FL);
		}
	}
	
	return nullptr;
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

	if (s)
	{
		if (!Name || Stricmp(Name, "/") == 0)
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
		LDocView *HtmlCtrl = nullptr;
		if (!MimeType ||
			!Stricmp(MimeType, sTextPlain) ||
			!Stricmp(MimeType, sMultipartEncrypted))
		{
			Ctrl = new MailTextView(this, Id, 0, 0, 200, 200, (UseFont) ? &FontType : 0);
		}
		else
		{
			LVariant htmlViewCtrl;
			GetOptions()->GetValue(OPT_HtmlViewCtrl, htmlViewCtrl);

			THtmlViewCtrl ctrlType =
				!htmlViewCtrl.IsNull() ?
				(THtmlViewCtrl)htmlViewCtrl.CastInt32() :
				TLgiHtml1;

			switch (ctrlType)
			{
				default:
				case TLgiHtml1:
					HtmlCtrl = Ctrl = new Html1::LHtml(Id, 0, 0, 200, 200);
					break;
				case TLgiHtml2:
					HtmlCtrl = Ctrl = new Html2::LHtml(Id, 0, 0, 200, 200);
					break;
				case TLiteHtmlView:
					HtmlCtrl = Ctrl = new LiteHtmlView(Id);
					break;
			}
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

