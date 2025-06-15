#include "lgi/common/Lgi.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/TabView.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/Button.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/Charset.h"

#include "Scribe.h"
#include "ScribePrivate.h"
#include "ScribeAccountUI.h"
#include "resdefs.h"
#include "SubFolderDlg.h"

#define DefaultPortValue		LLoadString(IDS_DEFAULT)
#define DEFAULT_GOOGLE_SERVER	"imap.gmail.com"

void PopulateTypes(LCombo *c)
{
	if (c)
	{
		c->Insert(PROTOCOL_POP3);
		c->Insert(PROTOCOL_IMAP4_FETCH);
		c->Insert(PROTOCOL_IMAP4);
		c->Insert(PROTOCOL_CALENDAR);
		c->Insert(PROTOCOL_POP_OVER_HTTP);
		#ifdef WIN32
		c->Insert(PROTOCOL_MAPI);
		#endif
		c->Insert(PROTOCOL_GMAIL);
		
		// Old order:
		// Pop, Imap(fetch), Mapi, Cal, Http
	}
}

AccountDlg::AccountDlg(LView *p, ScribeWnd *app, ScribeAccount *a, int Tab) :
    TabDialog(IDC_TAB, ID_BTN_TBL)
{
	SetParent(p);
	App = app;
	Account = a;

	if (LoadFromResource(IDD_ACCOUNT, App->GetUiTags()))
	{
		MoveToCenter();
		
		LRichTextEdit *Rte;
		if (GetViewById(IDC_SIG_HTML, Rte))
			Rte->SetStylePrefix("Sig");

		PopulateTypes(dynamic_cast<LCombo*>(FindControl(IDC_REC_TYPE)));
		FillWithCharsets(IDC_SEND_CHARSET1, false);
		FillWithCharsets(IDC_SEND_CHARSET2, false);
		FillWithCharsets(IDC_REC_8BIT_CS, true);
		FillWithCharsets(IDC_REC_ASCII_CP, true);

		LCombo *c;
		if (GetViewById(IDC_SEND_AUTH_TYPE, c))
		{
			c->Insert(LLoadString(IDS_ALL));
			c->Insert("PLAIN");
			c->Insert("LOGIN");
			c->Insert("CRAM-MD5");
			c->Insert("OAUTH2");
		}

		if (GetViewById(IDC_RECEIVE_AUTH_TYPE, c))
		{
			c->Insert(LLoadString(IDS_ALL));
			c->Insert("PLAIN");
			c->Insert("LOGIN");
			c->Insert("NTLM");			
			c->Insert("OAUTH2");
		}

		const char *DefServerTxt = "i.e. mail.isp.com";
		if (GetViewById(IDC_SMTP_SERVER, SendServer))
			SendServer->SetEmptyText(DefServerTxt);
		if (GetViewById(IDC_REC_SERVER, ReceiveServer))
			ReceiveServer->SetEmptyText(DefServerTxt);
		GetViewById(IDC_SMTP_PORT, SendPort);
		GetViewById(IDC_REC_PORT, ReceivePort);

		Account->SerializeUi(this, true);

		SetCtrlEnabled(IDC_SMTP_PASSWORD, GetCtrlValue(IDC_SMTP_AUTH) != 0);
		LNotification note(LNotifyValueChanged);
		OnNotify(FindControl(IDC_SMTP_SERVER), note);
		SetCtrlValue(IDC_TAB, Tab);
		SetCtrlEnabled(IDC_REC_CHECK, GetCtrlValue(IDC_CHECK_EVERY) != 0);

		if (auto v = FindControl(IDC_POP3_LEAVE))
			OnNotify(v, note);
		if (auto v = FindControl(IDC_REC_TYPE))
			OnNotify(v, note);
		
		LLayout *t;
		if (GetViewById(IDC_SIG, t))
		{
		    t->SetPourLargest(true);
		    t->Sunken(true);
		}
		if (GetViewById(IDC_SIG_HTML, t))
		{
		    t->SetPourLargest(true);
		    t->Sunken(true);
		}

		UseGoogle = !Stricmp(GetCtrlName(IDC_REC_TYPE), PROTOCOL_GMAIL);
		OnUseGoogle();
	}
}

void AccountDlg::OnCreate()
{
    LTabView *t;
    if (GetViewById(IDC_SIGNATURE_TAB, t))
        t->SetPourChildren(true);

    TabDialog::OnCreate();
}

void AccountDlg::FillWithCharsets(int id, bool All)
{
	LCombo *c;
	if (!GetViewById(id, c))
		return;

	c->Value(-1);
	c->Sort(true);
	c->Sub(GV_STRING);

	for (auto Cs = LGetCsInfo("us-ascii"); Cs->Charset; Cs++)
	{
		if (All ||
			Cs->Type == CpMapped ||
			Cs->Type == CpUtf8)
		{
			c->Insert(Cs->Charset);
		}
	}
}

void AccountDlg::UpdateDefaultPort(bool Send)
{
	if (Send)
	{
		int DefPort = SMTP_PORT;
		int64 Ssl = GetCtrlValue(IDC_SEND_SSL);
		if (Ssl == 2)
			DefPort = SMTP_SSL_PORT;
		
		char s[256];
		sprintf_s(s, sizeof(s), "%s: %i", LLoadString(IDC_DEFAULT), DefPort);
		if (SendPort)
		{
			if (SendPort->Value() == 0)
				SendPort->Name(NULL);
			SendPort->SetEmptyText(s);
		}
	}
	else
	{
		int DefPort = 0;
		const char *Type = GetCtrlName(IDC_REC_TYPE);
		int64 Ssl = GetCtrlValue(IDC_RECEIVE_SSL);
		if (Type)
		{
			if (!_stricmp(Type, PROTOCOL_POP3))
			{
				DefPort = (Ssl == 2) ? POP3_SSL_PORT : POP3_PORT;
			}
			else if (!_stricmp(Type, PROTOCOL_IMAP4) ||
					!_stricmp(Type, PROTOCOL_IMAP4_FETCH))
			{
				DefPort = (Ssl == 2) ? IMAP_SSL_PORT : IMAP_PORT;
			}
			else if (!_stricmp(Type, PROTOCOL_POP_OVER_HTTP))
			{
				DefPort = (Ssl == 2) ? HTTPS_PORT : HTTP_PORT;
			}
		}
		
		if (DefPort)
		{
			char s[256];
			sprintf_s(s, sizeof(s), "%s: %i", LLoadString(IDC_DEFAULT), DefPort);
			if (ReceivePort)
			{
				if (ReceivePort->Value() == 0)
					ReceivePort->Name(NULL);
				ReceivePort->SetEmptyText(s);
			}
		}
	}
}

void AccountDlg::OnUseGoogle()
{
	if (UseGoogle)
	{
		SetCtrlName(IDC_REC_SERVER, DEFAULT_GOOGLE_SERVER);
		SetCtrlName(IDC_REC_PORT, NULL);
		SetCtrlValue(IDC_RECEIVE_SSL, 2);
		SetCtrlName(IDC_REC_TYPE, ToString(ProtocolGoogle));
		SetCtrlValue(IDC_REMEMBER_PSW, false);
		SetCtrlValue(IDC_RECEIVE_AUTH_TYPE, 4);
	}

	SetCtrlEnabled(IDC_REC_SERVER, !UseGoogle);
	SetCtrlEnabled(IDC_REC_PORT, !UseGoogle);
	SetCtrlEnabled(IDC_REMEMBER_PSW, !UseGoogle);
}

int AccountDlg::OnNotify(LViewI *c, const LNotification &n)
{
	if (!c) return 0;
	
	switch (c->GetId())
	{
		default:
		{
			Account->OnNotify(c, n);
			break;
		}
		case IDC_GOOGLE:
		{
			UseGoogle = true;
			OnUseGoogle();
			break;
		}
		case IDC_POP3_LEAVE:
		{
			bool Leave = c->Value() != 0;
			SetCtrlEnabled(IDC_DELETE_AFTER, Leave);
			SetCtrlEnabled(IDC_DELETE_LARGER, Leave);

			bool Delete = GetCtrlValue(IDC_DELETE_AFTER) != 0;
			SetCtrlEnabled(IDC_DELETE_DAYS, Leave && Delete);
			SetCtrlEnabled(IDC_TXT_DAYS, Leave && Delete);

			Delete = GetCtrlValue(IDC_DELETE_LARGER) != 0;
			SetCtrlEnabled(IDC_DELETE_SIZE, Leave && Delete);
			SetCtrlEnabled(IDC_TXT_SIZE, Leave && Delete);
			break;
		}
		case IDC_DELETE_AFTER:
		{
			bool Delete = c->Value() != 0;
			SetCtrlEnabled(IDC_DELETE_DAYS, Delete);
			SetCtrlEnabled(IDC_TXT_DAYS, Delete);
			break;
		}
		case IDC_DELETE_LARGER:
		{
			bool Delete = c->Value() != 0;
			SetCtrlEnabled(IDC_DELETE_SIZE, Delete);
			SetCtrlEnabled(IDC_TXT_SIZE, Delete);
			break;
		}
		case IDC_CHECK_EVERY:
		{
			SetCtrlEnabled(IDC_REC_CHECK, GetCtrlValue(IDC_CHECK_EVERY) != 0);
			break;
		}
		case IDC_SMTP_SERVER:
		{
			bool HasSend = ValidStr(GetCtrlName(IDC_SMTP_SERVER));
			SetCtrlEnabled(IDC_ONLY_SEND_THIS, HasSend);
			if (!HasSend) SetCtrlValue(IDC_ONLY_SEND_THIS, false);
			break;
		}
		case IDC_LAUNCH_HELP:
		{
			char Page[256] = "install.html";
			switch (GetCtrlValue(IDC_TAB))
			{
				case 0:
					strcat_s(Page, sizeof(Page), "#acc_id");
					break;
				case 1:
					strcat_s(Page, sizeof(Page), "#acc_send");
					break;
				case 2:
					strcat_s(Page, sizeof(Page), "#acc_receive");
					break;
				case 3:
					strcat_s(Page, sizeof(Page), "#acc_connect");
					break;
			}
			App->LaunchHelp(Page);
			break;
		}
		case IDC_TAB:
		{
			if (c->Value() == 1)
			{
				UpdateDefaultPort(true);
			}
			else if (c->Value() == 2)
			{
				UpdateDefaultPort(false);
			}
			break;
		}
		case IDC_REC_TYPE:
		{
			UseGoogle = !Stricmp(c->Name(), PROTOCOL_GMAIL);
			OnUseGoogle();

			int64 Type = c->Value();
			bool PopType = Type == 0 || Type == 1;
			SetCtrlEnabled(IDC_POP3_LEAVE, PopType);
			SetCtrlEnabled(IDC_DELETE_AFTER, PopType);
			SetCtrlEnabled(IDC_TXT_DAYS, PopType);
			SetCtrlEnabled(IDC_DELETE_DAYS, PopType);
			SetCtrlEnabled(IDC_DELETE_LARGER, PopType);
			SetCtrlEnabled(IDC_DELETE_SIZE, PopType);
			SetCtrlEnabled(IDC_TXT_MIN, PopType);
			SetCtrlEnabled(IDC_TXT_KBS, PopType);
			SetCtrlEnabled(IDC_TXT_DOWNLOAD, PopType);
			SetCtrlEnabled(IDC_CHECK_EVERY, PopType);
			SetCtrlEnabled(IDC_REC_CHECK, PopType);
			SetCtrlEnabled(IDC_MAX_SIZE, PopType);
			if (!PopType)
			{
				SetCtrlValue(IDC_POP3_LEAVE, 0);
				SetCtrlValue(IDC_DELETE_AFTER, 0);
				SetCtrlValue(IDC_DELETE_LARGER, 0);
			}

			// fall through
		}		
		case IDC_RECEIVE_SSL:
		{
			UpdateDefaultPort(false);
			break;
		}
		case IDC_SEND_SSL:
		{
			UpdateDefaultPort(true);
			break;
		}
		case ID_SUB_FOLDERS:
		{
			LString optName = Account->Receive.OptionName(OPT_ReceiveSubFolders);
			auto options = App->GetOptions();
			auto locked = options->LockTag(optName, _FL);
			if (!locked)
			{
				options->CreateTag(optName);
				locked = options->LockTag(optName, _FL);
			}

			if (!locked)
			{
				LAssert(!"failed to create/lock the option");
				break;
			}

			subFolderOpts.Copy(*locked, true);
			options->Unlock();

			if
			(
				auto dlg = new SubFolderDlg
					(
						this,
						App,
						&subFolderOpts,
						[this](auto Parent, auto App, auto Limit, auto cb)
						{
							// FIXME: impl selector of folder
						}
					)
			)
				dlg->DoModal([this, optName](auto dlg, auto ctrl)
					{
						if (ctrl != IDOK)
							return;

						for (auto &a: subFolderOpts.Attr)
							LgiTrace("sub %s=%s\n", a.GetName(), a.GetValue());

						// If user selected ok, copy 'subFolderOpts' back into the options:
						auto options = App->GetOptions();
						if (auto locked = options->LockTag(optName, _FL))
						{
							locked->Copy(subFolderOpts, true);
							options->Unlock();
						}
					});
			break;
		}
		case IDOK:
		{
			App->GetAccountSettingsAccess(this, ScribeWriteAccess, [this, id=c->GetId()](auto Allow)
			{
				if (Allow)
				{
					if (!ValidStr(GetCtrlName(IDC_ACCOUNT_NAME)))
						SetCtrlName(IDC_ACCOUNT_NAME, "My ISP");
					Account->SerializeUi(this, false);

					EndModal(id);
				}
			});
			break;
		}
		case IDCANCEL:
		{
			EndModal(c->GetId());
			break;
		}
	}

	return 0;
}

