#include "Scribe.h"
#include "ScribePrivate.h"
#include "ScribeAccountUI.h"
#include "resdefs.h"

#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/TabView.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/Button.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/Charset.h"

#define DefaultPortValue		LLoadString(IDS_DEFAULT)

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
		
		// Old order:
		// Pop, Imap(fetch), Mapi, Cal, Http
	}
}

void FillWithCharsets(LCombo *c, bool All)
{
	if (c)
	{
		c->Value(-1);
		c->Sort(true);
		c->Sub(GV_STRING);

		for (LCharset *Cs = LGetCsInfo("us-ascii"); Cs->Charset; Cs++)
		{
			if (All ||
				Cs->Type == CpMapped ||
				Cs->Type == CpUtf8)
			{
				c->Insert(Cs->Charset);
			}
		}
	}
}

AccountDlg::AccountDlg(LView *p, ScribeWnd *app, ScribeAccount *a, int Tab) :
    TabDialog(IDC_TAB, IDC_LAUNCH_HELP)
{
	SetParent(p);
	App = app;
	Account = a;
	Plugins = 0;
	SendServer = NULL;
	SendPort = NULL;
	ReceiveServer = NULL;
	ReceivePort = NULL;

	if (LoadFromResource(IDD_ACCOUNT, App->GetUiTags()))
	{
		MoveToCenter();
		
		LRichTextEdit *Rte;
		if (GetViewById(IDC_SIG_HTML, Rte))
			Rte->SetStylePrefix("Sig");

		PopulateTypes(dynamic_cast<LCombo*>(FindControl(IDC_REC_TYPE)));
		FillWithCharsets(dynamic_cast<LCombo*>(FindControl(IDC_SEND_CHARSET1)), false);
		FillWithCharsets(dynamic_cast<LCombo*>(FindControl(IDC_SEND_CHARSET2)), false);
		FillWithCharsets(dynamic_cast<LCombo*>(FindControl(IDC_REC_8BIT_CS)), true);
		FillWithCharsets(dynamic_cast<LCombo*>(FindControl(IDC_REC_ASCII_CP)), true);

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

		LViewI *v = FindControl(IDC_POP3_LEAVE);
		if (v) OnNotify(v, note);
		v = FindControl(IDC_REC_TYPE);
		if (v) OnNotify(v, note);
		
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
	}
}

void AccountDlg::OnCreate()
{
    LTabView *t;
    if (GetViewById(IDC_SIGNATURE_TAB, t))
    {
        t->SetPourChildren(true);
    }

    TabDialog::OnCreate();
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

int AccountDlg::OnNotify(LViewI *c, LNotification n)
{
	if (!c) return 0;
	
	switch (c->GetId())
	{
		default:
		{
			Account->OnNotify(c, n);
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

