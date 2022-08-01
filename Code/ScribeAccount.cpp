/*
**	FILE:			ScribeAccount.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			19/10/99
**	DESCRIPTION:	Scribe account
**
**	Copyright (C) 2002, Matthew Allen
**		fret@memecode.com
*/

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "lgi/common/Edit.h"
#include "ScribeAccountUI.h"
#include "../Resources/resdefs.h"
#include "lgi/common/LgiRes.h"
#include "ScribeFolderSelect.h"

////////////////////////////////////////////////////////////////////////////
class ScribeAccountPrivate
{
public:
	// Data
	int Index;
	LMenuItem *IdentityItem;
	
	ScribeAccountPrivate(ScribeWnd *App, int i)
	{
		IdentityItem = 0;
		Index = i;
	}
};

////////////////////////////////////////////////////////////////////////////
ScribeAccount::ScribeAccount(ScribeWnd *App, int Index) :
	d(new ScribeAccountPrivate(App, Index)),
	Parent(App),
	Identity(this),
	Send(this),
	Receive(this)
{
	CreateMaps();
}

ScribeAccount::~ScribeAccount()
{
	Receive.OnBeforeDelete();
	Send.OnBeforeDelete();
	Identity.OnBeforeDelete();
	DeleteObj(d);
}

bool ScribeAccount::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType f = StrToDom(Name);
	char k[128];
	LOptionsFile *Opts = GetApp()->GetOptions();

	switch (f)
	{
		case SdIdentity: // Type: Accountlet.AccountIdentity
			Value = (LDom*)&Identity;
			break;
		case SdSend: // Type: Accountlet.SendAccountlet
			Value = (LDom*)&Send;
			break;
		case SdReceive: // Type: Accountlet.ReceiveAccountlet
			Value = (LDom*)&Receive;
			break;
		case SdIsValid: // Type: Bool
			Value = IsValid();
			break;
		case SdIsOnline: // Type: Bool
			Value = IsOnline();
			break;
		case SdScribe: // Type: ScribeWnd
			Value = (LDom*)Parent;
			break;
		case SdName:			// Type: String
			return Opts->GetValue(Identity.OptionName(OPT_AccountName, k, sizeof(k)), Value);
		case SdId:				// Type: String
			return Opts->GetValue(Identity.OptionName(OPT_AccountUID, k, sizeof(k)), Value);
		case SdDisable:			// Type: Bool
			return Opts->GetValue(Identity.OptionName(OPT_AccountDisabled, k, sizeof(k)), Value);
		case SdAccountExpanded: // Type: Bool
			return Opts->GetValue(Identity.OptionName(OPT_AccountExpanded, k, sizeof(k)), Value);
		default:
			return false;
	}	

	return true;
}

bool ScribeAccount::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType f = StrToDom(Name);
	char k[128];
	LOptionsFile *Opts = GetApp()->GetOptions();

	switch (f)
	{
		case SdName:			// Type: String
			return Opts->SetValue(Identity.OptionName(OPT_AccountName, k, sizeof(k)), Value);
		case SdId:				// Type: String
			return Opts->SetValue(Identity.OptionName(OPT_AccountUID, k, sizeof(k)), Value);
		case SdDisable:			// Type: Bool
			return Opts->SetValue(Identity.OptionName(OPT_AccountDisabled, k, sizeof(k)), Value);
		case SdAccountExpanded: // Type: Bool
			return Opts->SetValue(Identity.OptionName(OPT_AccountExpanded, k, sizeof(k)), Value);
		default:
			return false;
	}	

	return true;
}

bool ScribeAccount::CallMethod(const char *MethodName, LVariant *ReturnValue, LArray<LVariant*> &Args)
{
	ScribeDomType m = StrToDom(MethodName);
	switch (m)
	{
		case SdStop: // Type: ()
			Stop();
			break;
		case SdDisconnect: // Type: ()
			Disconnect();
			break;
		case SdKill: // Type: ()
			Kill();
			break;
		case SdEndSession: // Type: ()
			OnEndSession();
			break;
		default:
			return false;
	}
	
	return true;
}

bool ScribeAccount::IsValid()
{
	char Key[128];
	Receive.OptionName(OPT_AccountUID, Key, sizeof(Key));
	LVariant v;
	if (Parent->GetOptions()->GetValue(Key, v))
	{
		return v.CastInt32() > 0;
	}
	return false;	
}

bool ScribeAccount::Create()
{
	LVariant v;
	bool Status = false;
	char Key[128];
	Receive.OptionName(0, Key, sizeof(Key));
	
	// Check our tag exists
	LXmlTag *Tag = Parent->GetOptions()->LockTag(Key, _FL);
	if (!Tag)
	{
		Parent->GetOptions()->CreateTag(Key);
		Tag = Parent->GetOptions()->LockTag(Key, _FL);
	}
	if (Tag)
	{
		// Check our ID exists
		if (!Tag->GetAttr(OPT_AccountUID))
		{
			int NewId;
			
			do
			{
				NewId = LRand() & 0x7fffffff;
			}
			while (Parent->GetAccountById(NewId));
			
			Tag->SetAttr(OPT_AccountUID, NewId);
		}

		// Create our child tags, if missing
		Tag->GetChildTag("Identity", true);
		Tag->GetChildTag("Send", true);
		Tag->GetChildTag("Receive", true);

		Status = true;
		Parent->GetOptions()->Unlock();
		
		// Check for the defaults
		v = Receive.Name();
		if (!v.Str())
		{
			Receive.Name("My ISP");
		}

		v = Send.PrefCharset1();
		if (!v.Str())
		{
			SetDefaults();
		}
	}

	return Status;
}

bool ScribeAccount::Delete()
{
	ReIndex(-1);
	
	while (Views.Length())
	{
		LListItem *i = Views[0];
		DeleteObj(i);
	}

	return true;
}

void ScribeAccount::SetDefaults()
{
	// Set the default charsets...
	const char *DefCharset = LLoadString(IDS_DEFAULT_CHARSET_SEND);
	if (DefCharset)
	{
		GToken t(DefCharset, ",");
		for (unsigned i=0; i<t.Length(); i++)
		{
			if (i == 0)
				Send.PrefCharset1(t[i]);
			else if (i == 1)
				Send.PrefCharset2(t[i]);
			else break;
		}
	}

	DefCharset = LLoadString(IDS_DEFAULT_CHARSET_RECEIVE);
	if (DefCharset)
	{
		GToken t(DefCharset, ",");
		for (unsigned i=0; i<t.Length(); i++)
		{
			if (i == 0)
				Receive.Assume8BitCharset(t[i]);
			else if (i == 1)
				Receive.AssumeAsciiCharset(t[i]);
			else break;
		}
	}
}

void ScribeAccount::SetIndex(int i)
{
	d->Index = i;
}

ScribeFolder *&ScribeAccount::GetRoot()
{
	return Receive.Root;
}

LMenuItem *ScribeAccount::GetMenuItem()
{
	return d->IdentityItem;
}

void ScribeAccount::SetMenuItem(LMenuItem *i)
{
	d->IdentityItem = i;
}

void ScribeAccount::SetCheck(bool c)
{
	if (d->IdentityItem)
	{
		d->IdentityItem->Checked(c);
	}
}

void ScribeAccount::CreateMaps()
{
	char Name[256] = "";
	
	Map(Send.OptionName(OPT_AccountName, Name, sizeof(Name)), IDC_ACCOUNT_NAME, GV_STRING);

	Identity.CreateMaps();
	Send.CreateMaps();
	Receive.CreateMaps();
}

void ScribeAccount::ReIndex(int i)
{
	char Key[128];
	Receive.OptionName(0, Key, sizeof(Key));
	EmptyMaps();

	if (i >= 0)
	{
		LXmlTag *Tag = GetApp()->GetOptions()->LockTag(Key, _FL);
		if (Tag)
		{
			sprintf_s(Key, sizeof(Key), "Account-%i", i);
			Tag->SetTag(Key);
			d->Index = i;
			
			GetApp()->GetOptions()->Unlock();
		}

		CreateMaps();
	}
	else
	{
		GetApp()->GetOptions()->DeleteTag(Key);
	}
}

int ScribeAccount::GetIndex()
{
	LAssert(d != NULL);
	return d ? d->Index : -1;
}

bool ScribeAccount::IsOnline()
{
	return Send.IsOnline() || Receive.IsOnline();
}

bool ScribeAccount::InitMenus()
{
	bool Status = Send.InitMenus();
	return Receive.InitMenus() && Status;
}

void ScribeAccount::Stop()
{
	if (Send.IsOnline())
	{
		Send.Disconnect();
	}
	if (Receive.IsOnline())
	{
		Receive.Disconnect();
	}
}

void ScribeAccount::Kill()
{
	if (Send.IsOnline())
	{
		Send.Kill();
	}
	if (Receive.IsOnline())
	{
		Receive.Kill();
	}
}

void ScribeAccount::OnPulse(char *s, int s_len)
{
	Send.OnPulse(s, s_len);
	Receive.OnPulse(s, s_len);
}

void ScribeAccount::SerializeUi(LView *Wnd, bool Load)
{
	if (Wnd)
	{
		LVariant v;

		// Special Fields
		LEdit *Pop3Folder;

		if (Wnd->GetViewById(IDC_FOLDER, Pop3Folder))
		{
			Pop3Folder->Enabled(false);
		}

		GPassword s, r;
		char Buf[128];
		if (Load)
		{
			LEdit *e;
			bool HasPass;
			
			HasPass = s.Serialize(Parent->GetOptions(), Receive.OptionName(OPT_EncryptedSmtpPassword, Buf, sizeof(Buf)), false);
			Wnd->SetCtrlValue(IDC_SMTP_AUTH, HasPass);
			if (HasPass && Wnd->GetViewById(IDC_SMTP_PASSWORD, e))
				e->SetEmptyText(LLoadString(IDS_PASSWORD_SAVED));

			HasPass = r.Serialize(Parent->GetOptions(), Receive.OptionName(OPT_EncryptedPop3Password, Buf, sizeof(Buf)), false);
			Wnd->SetCtrlValue(IDC_REMEMBER_PSW, HasPass);
    		Wnd->SetCtrlEnabled(IDC_REC_PASSWORD, HasPass);
			if (HasPass && Wnd->GetViewById(IDC_REC_PASSWORD, e))
				e->SetEmptyText(LLoadString(IDS_PASSWORD_SAVED));
		}
		else
		{
			if (Wnd->GetCtrlValue(IDC_REMEMBER_PSW))
			{
				const char *p = Wnd->GetCtrlName(IDC_REC_PASSWORD);
				if (ValidStr(p))
				{
					r.Set(p);
					r.Serialize(Parent->GetOptions(), Receive.OptionName(OPT_EncryptedPop3Password, Buf, sizeof(Buf)), true);
				}
			}
			else r.Delete(Parent->GetOptions(), Receive.OptionName(OPT_EncryptedPop3Password, Buf, sizeof(Buf)));

			if (Wnd->GetCtrlValue(IDC_SMTP_AUTH))
			{
				const char *p = Wnd->GetCtrlName(IDC_SMTP_PASSWORD);
				if (ValidStr(p))
				{
					s.Set(p);
					s.Serialize(Parent->GetOptions(), Send.OptionName(OPT_EncryptedSmtpPassword, Buf, sizeof(Buf)), true);
				}
			}
			else s.Delete(Parent->GetOptions(), Send.OptionName(OPT_EncryptedSmtpPassword, Buf, sizeof(Buf)));
		}

		// Normal Fields
		Convert(Parent->GetOptions(), Wnd, Load);
	}
}

void ScribeAccount::InitUI(LView *Parent, int Tab, std::function<void(bool)> callback)
{
	auto Dlg = new AccountDlg(Parent, this->Parent, this, Tab);
	Dlg->DoModal([callback](auto dlg, auto id)
	{
		if (callback)
			callback(id);
		delete dlg;
	});
}

int ScribeAccount::OnNotify(LViewI *Ctrl, LNotification &n)
{
	LWindow *Wnd = Ctrl->GetWindow();
	
	if (!Wnd)
		return 0;
	
	switch (Ctrl->GetId())
	{
		case IDC_REMEMBER_PSW:
		{
			Wnd->SetCtrlEnabled(IDC_REC_PASSWORD, Ctrl->Value() != 0);

			if (Ctrl->Value())
			{
				LViewI *c = Wnd->FindControl(IDC_REC_PASSWORD);
				if (c && c->IsAttached())
				{
					c->Focus(true);
				}
			}
			else
			{
			    Wnd->SetCtrlName(IDC_REC_PASSWORD, 0);
			}
			break;
		}
		case IDC_SMTP_AUTH:
		{
			Wnd->SetCtrlEnabled(IDC_SMTP_PASSWORD, Ctrl->Value() != 0);
			break;
		}
		case IDC_PICK_FOLDER:
		{
			auto Dlg = new FolderDlg(Parent, Parent);
			Dlg->DoModal([this, Dlg, Ctrl](auto dlg, auto id)
			{
				if (id)
					Ctrl->GetWindow()->SetCtrlName(IDC_FOLDER, Dlg->Get());
				delete dlg;
			});
			break;
		}
	}

	return 0;
}

bool ScribeAccount::Disconnect()
{
	if (Send.IsOnline())
	{
		Send.Disconnect();
	}
	
	if (Receive.IsOnline())
	{
		Receive.Disconnect();
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////
AccountThread::AccountThread(Accountlet *acc) : LThread("AccountThread")
{
	DeleteOnExit = false;
	Acc = acc;
}

AccountThread::~AccountThread()
{
	if (!LAppInst->InThread())
	{
		LAssert(0);
	}
	
	if (!IsExited())
		Cancel(true);

	uint64 Start = LCurrentTime();
	while (!IsExited())
	{
		// Give the thread a tiny bit of time to exit
		// It's just got to return after posting the message
		LSleep(100);

		if (LCurrentTime() - Start > 1000)
		{
			// Took too long... is it really exiting?
			LAssert(0);
		}

		if (LCurrentTime() - Start > 30000)
		{
			// Broken... cut out losses and exit.
			Terminate();
			break;
		}
	}
}

void AccountThread::OnAfterMain()
{
	if (Acc && Acc->GetApp())
	{
        Acc->GetApp()->PostEvent(M_SCRIBE_THREAD_DONE, (LMessage::Param)this, (LMessage::Param)Acc);
	}
	else
	{
		LAssert(0);
	}
}

void AccountThread::Disconnect()
{
	Cancel(true);
}

bool AccountThread::Kill()
{
	Cancel(true);
	Terminate();
	return true;
}
