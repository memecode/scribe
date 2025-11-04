/*
**	FILE:		    OptionsDlg.cpp
**	AUTHOR:		    Matthew Allen
**	DATE:		    5/8/2011
**	DESCRIPTION:	Scribe email options dialog
**
**	Copyright (C) 2011, Matthew Allen
**		fret@memecode.com
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lgi/common/Lgi.h"
#include "lgi/common/Edit.h"
#include "lgi/common/RadioGroup.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Button.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/ControlTree.h"
#include "lgi/common/TabView.h"
#include "lgi/common/SpellCheck.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/EventTargetThread.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/FileSelect.h"

#include "Scribe.h"
#include "ScribePrivate.h"
#include "resdefs.h"
#include "CalendarView.h"

static char AutoInBrackets[] = "(auto)";

#ifndef IDC_TAB
#define IDC_TAB 100
#endif

#if defined WIN32

#define DLG_X 370
#define DLG_Y 420
#define TAB_X (DLG_X-26)
#define TAB_Y (DLG_Y-74)

#else

#define DLG_X 380
#define DLG_Y 390
#define TAB_X (DLG_X-20)
#define TAB_Y (DLG_Y-50)

#endif

class UtfEditor : public LDialog
{
	ScribeWnd *App = nullptr;
	LTabView *Tabs = nullptr;
	LTabPage *TabText = nullptr, *TabHtml = nullptr;
	LButton *Ok = nullptr, *Cancel = nullptr;
	LTextView3 *Txt = nullptr;
	LRichTextEdit *Html = nullptr;
	const char *TextOpt = nullptr, *HtmlOpt = nullptr;

public:
	UtfEditor(ScribeWnd *app, LWindow *parent, const char *txtopt, const char *htmlopt, const char *desc);
	void OnPosChange() override;
	int OnNotify(LViewI *c, const LNotification &n) override;
};

class AccountItem : public LListItem
{
	OptionsDlg *Dlg;
	LVariant Cache;

public:
	ScribeAccount *Account = nullptr;
	LListItemCheckBox *Disable = nullptr;

	AccountItem(OptionsDlg *d, ScribeAccount *a)
	{
		Dlg = d;
		Account = a;
		Account->Views.Add(this);
		Disable = 0;
		Disable = new LListItemCheckBox(this, 3, a->Send.Disabled() > 0);
	}

	~AccountItem()
	{
		LAssert(Account->Views.HasItem(this));
		Account->Views.Delete(this);
	}

	ScribeAccount *GetAccount()
	{
		return Account;
	}

	const char *GetText(int i);

	void OnMouseClick(LMouse &m)
	{
		if (m.Double() && Account)
		{
			Dlg->App->GetAccountSettingsAccess(Dlg, ScribeReadAccess, [this](auto Allow)
			{
				if (Allow)
				{
					Account->InitUI(Parent, 0, [this](auto status)
					{
						if (status)
							Update();
					});
				}
			});
		}

		LListItem::OnMouseClick(m);
	}

	void OnColumnNotify(int Col, int64 Data)
	{
		if (Disable && Col == 3)
		{
			Dlg->OnAccountEnable(Account, !Data);
		}
		LListItem::OnColumnNotify(Col, Data);
	}
	
	int Compare(LListItem *To, ssize_t Field = 0)
	{
		auto bItem = dynamic_cast<AccountItem*>(To);
		if (!bItem || !bItem->GetAccount())
		{
			LAssert(!"Not the right object.");
			return 0;
		}
		auto b = bItem->GetAccount();
		return Account->Compare(b);
	}
 };

const char *AccountItem::GetText(int i)
{
	if (!Account)
		return "#noAccount";

	switch (i)
	{
		case 0:
			Cache = Account->Receive.Name();
			if (!Cache.Str())
			{
				Cache = Account->Receive.Server();
				if (!Cache.Str())
					Cache = Account->Send.Server();
			}
			return Cache.Str();
		case 1:
			return Account->Send.Server().Str() ? "yes" : NULL;
		case 2:
			return Account->Receive.Server().Str() ? "yes" : NULL;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
class RemoteContentDlg : public LDialog, public LXmlTreeUi
{
	ScribeWnd *App;
	LOptionsFile *Opts;

public:
	RemoteContentDlg(LView *Parent, ScribeWnd *app) : App(app)
	{
		SetParent(Parent);
		Opts = App->GetOptions();
		if (LoadFromResource(IDD_REMOTE_CONTENT))
		{
			Map(OPT_RemoteContentWhiteList, IDC_WHITELIST, GV_STRING);
			Map(OPT_RemoteContentBlackList, IDC_WHITELIST, GV_STRING);

			Convert(Opts, this, true);
			
			MoveSameScreen(Parent);
		}
	}
	
	int OnNotify(LViewI *Ctrl, const LNotification &n) override
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
				Convert(Opts, this, false);
				App->RemoteContent_ClearCache();
				// Fall thru
			case IDCANCEL:
				EndModal(Ctrl->GetId() == IDOK);
				break;
		}
		
		return 0;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////
class OptionsDlgPrivate
{
public:
	LString::Array Langs;
	LString::Array Dictionaries;
};

OptionsDlg::OptionsDlg(ScribeWnd *window) :
	TabDialog(IDC_TAB, ID_BTN_TBL)
{
	d = new OptionsDlgPrivate;
	SetParent(App = window);
	SinkHnd = LEventSinkMap::Dispatch.AddSink(this);

	LRect r(0, 0, DLG_X, DLG_Y);
	SetPos(r);
	MoveSameScreen(App);
    
	if (!LoadFromResource(IDD_SETTINGS, window->GetUiTags()))
	{
		LgiMsg(window, "Options resource missing.", "Error");
		return;
	}

	MoveToCenter();

	LList *AccountLst;
	if (GetViewById(IDC_ACCOUNTS, AccountLst))
	{
		for (auto a: *App->GetAccounts())
		{
			if (auto i = new AccountItem(this, a))
			{
				AccountLst->Insert(i);
				if (AccountLst->Length() == 1)
					i->Select(true);
			}
		}

		LNotification note(LNotifyItemInsert);
		OnNotify(AccountLst, note);
		AccountLst->Sort([](auto *a, auto *b)
			{
				return a->Compare(b);
			});

		LArray<AccountItem*> all;
		AccountLst->GetAll(all);
		for (int i=0; i<all.Length(); i++)
		{
			auto item = all[i];
			auto acc = item->GetAccount();
			LgiTrace("load[%i] acc=%p(%s) index=%i\n",
				i, acc, acc->Receive.Name().Str(), (int)acc->GetIndex());
		}

		// Enable re-ordering via drag
		AccountLst->SetDragItem(LItemContainer::ITEM_DRAG_REORDER);
	}

	// Identity tab
	Map(OPT_UserName, IDC_NAME, GV_STRING);

	// Accounts tab
	Map(OPT_DefaultSendAccount, IDC_DEF_SEND, GV_INT32);
	Map(OPT_ExtraHeaders, IDC_EXTRA_HEADERS, GV_STRING);
	Map(OPT_HideId, IDC_HIDE_ID, GV_BOOL);

	// General tab
	Map(OPT_QuoteReply, IDC_QUOTE, GV_BOOL);
	Map(OPT_QuoteReplyStr, IDC_QUOTE_STR, GV_STRING);
	Map(OPT_ReplyWithSig, IDC_REPLYWITHSIG, GV_BOOL);
	Map(OPT_DefaultReplyAllSetting, IDC_DEF_REPLYALL_SETTING, GV_INT32);
	Map(OPT_MinimizeToTray, IDC_TRAY, GV_BOOL);
	Map(OPT_NewMailSoundFile, IDC_NEW_MAIL_SOUND, GV_STRING);
	#if WINNATIVE
	Map(OPT_CheckDefaultEmail, IDC_CHECK_DEFAULT, GV_BOOL);
	Map(OPT_RegisterWindowsClient, IDC_REGISTER_CLIENT, GV_BOOL);
	#endif
	Map(OPT_ConfirmDelete, IDC_CONFIRM_DEL, GV_BOOL);
	Map(OPT_DelDirection, IDC_MSG_DEL_ACTION, GV_INT32);
	Map(OPT_NewMailNotify, IDC_NEW_MAIL_NOTIFY, GV_BOOL);
	Map(OPT_RecipientFromClipboard, IDC_POP_RECIP, GV_BOOL);
	Map(OPT_MarkReadAfterPreview, IDC_PREVIEW_READ, GV_BOOL);
	Map(OPT_MarkReadAfterSeconds, IDC_PREVIEW_SECONDS, GV_INT32);
	Map(OPT_AutoDeleteExe, IDC_AUTO_DELETE_EXE, GV_BOOL);
	Map(OPT_BlinkNewMail, IDC_BLINK, GV_BOOL);
		
	// Connection Tab
	Map(OPT_UseSocks, IDC_SOCKS5, GV_BOOL);
	Map(OPT_Socks5Server, IDC_SOCKS5_SERVER, GV_STRING);
	Map(OPT_Socks5UserName, IDC_SOCKS5_USER, GV_STRING);
	Map(OPT_Socks5Password, IDC_SOCKS5_PASSWORD, GV_STRING);
	Map(OPT_Pop3OnStart, IDC_POP3_ON_START, GV_BOOL);
	Map(OPT_Pop3DefAction, IDC_DEF_ACTION, GV_INT32);
	Map(OPT_HttpProxy, IDC_HTTP_PROXY, GV_STRING);
	Map(OPT_CheckForDialUp, IDC_CHECKDIALUP, GV_BOOL);

	// Appearance/Look Tab
	Map(OPT_EditControl, IDC_EDIT_CONTROL, GV_INT32);
	Map(OPT_WordWrap, IDC_WRAP, GV_BOOL);
	Map(OPT_WrapAtColumn, IDC_WRAP_COLS, GV_INT32);
	Map(OPT_DefaultAlternative, IDC_DEFAULT_ALT, GV_INT32);
	Map(OPT_GridLines, IDC_GRID_LINES, GV_BOOL);
	Map(OPT_PreviewLines, IDC_PREVIEW_LINES, GV_BOOL);
	Map(OPT_ToolbarText, IDC_TOOLBAR_TEXT, GV_BOOL);
	Map(OPT_BoldUnread, IDC_BOLD_UNREAD, GV_BOOL);
	Map(OPT_DateFormat, IDC_DATE_FORMAT, GV_INT32);
	Map(OPT_UiFontSize, IDC_UI_FNT_SIZE, GV_INT32);
	Map(OPT_GlyphSub, IDC_GLYPH_SUB, GV_BOOL);
	Map(OPT_HtmlLoadImages, IDC_HTML_IMAGES, GV_BOOL);
	Map(OPT_ShowFolderTotals, IDC_SHOW_TOTALS, GV_BOOL);
	Map(OPT_Theme, IDC_THEME, GV_STRING);
	Map(OPT_CalendarFirstDayOfWeek, IDC_START_WEEK);

	// Debug tab
	Map("*", IDC_ADVANCED, GV_DOM);

	// Processed fields
	LControlTree *Ct;
	if (GetViewById(IDC_ADVANCED, Ct))
	{
		Ct->SetPourLargest(true);
		auto ci = Ct->Find(OPT_LogFormat);
		if (ci)
		{
			LAutoPtr<LControlTree::Item::EnumArr> Enum(new LControlTree::Item::EnumArr);
			if (Enum)
			{
				Enum->New().Set(LLoadString(IDS_NO_LOG), NET_LOG_NONE);
				Enum->New().Set(LLoadString(IDS_HEX_LOG), NET_LOG_HEX_DUMP);
				Enum->New().Set(LLoadString(IDS_BYTE_LOG), NET_LOG_ALL_BYTES);

				ci->SetEnum(Enum);
			}
		}

		LSpellCheck *SpellThread = App->GetSpellThread(true);
		if (SpellThread)
		{
			if (!SpellThread->EnumLanguages(SinkHnd))
				LgiTrace("%s:%i - Failed to EnumLanguages.\n", _FL);

			LVariant Lang;
			if (App->GetOptions()->GetValue(OPT_SpellCheckLanguage, Lang))
				SpellThread->EnumDictionaries(SinkHnd, Lang.Str());
			else
				LgiTrace("%s:%i - Failed to EnumDictionaries.\n", _FL);
		}
		else
			LgiTrace("%s:%i - Failed to get spell thread.\n", _FL);
			
		if ((ci = Ct->Find(OPT_SoftwareUpdateTime)))
		{
			LAutoPtr<LControlTree::Item::EnumArr> Enum(new LControlTree::Item::EnumArr);
			if (Enum)
			{
				LControlTree::EnumValue *v = &Enum->New();
				v->Name = (char*)LLoadString(IDS_WEEK);
				v->Value = 0;

				v = &Enum->New();
				v->Name = (char*)LLoadString(IDS_MONTH);
				v->Value = 1;

				v = &Enum->New();
				v->Name = (char*)LLoadString(IDS_YEAR);
				v->Value = 2;

				ci->SetEnum(Enum);
			}
		}
	}
	else LAssert(!"GetViewById failed.");

	LCombo *c;
	if (GetViewById(IDC_THEME, c))
	{
		LVariant CurTheme;
		App->GetOptions()->GetValue(OPT_Theme, CurTheme);

		c->Insert(LLoadString(IDS_DEFAULT));
		auto Paths = ScribeThemePaths();
		for (auto p: Paths)
		{
			LDirectory d;
			for (auto b = d.First(p); b; b = d.Next())
			{
				if (d.IsDir())
				{
					c->Insert(d.GetName());
					if (!Stricmp(d.GetName(), CurTheme.Str()))
						c->Value(c->Length()-1);
				}
			}
		}
	}

	// Load
	Convert(App->GetOptions(), this, true);

	// Get pointers to controls
	if (GetViewById(IDC_DEFAULT_ALT, c))
	{
		c->Insert("text/plain");
		c->Insert("text/html");
		#ifdef WINDOWS
		c->Insert("application/internet-explorer");
		#endif
	}
	else LAssert(!"GetViewById failed.");
		
	if (GetViewById(IDC_DEF_REPLYALL_SETTING, c))
	{
		c->Insert("To");
		c->Insert("Cc");
		c->Insert("Bcc");
	}
	else LAssert(!"GetViewById failed.");

	if (GetViewById(IDC_EDIT_CONTROL, c))
	{
		c->Insert("text/plain");
		c->Insert("text/html (experimental)");
	}
	else LAssert(!"GetViewById failed.");

	LVariant SizeAdj;
	if (!App->GetOptions()->GetValue(OPT_UiFontSize, SizeAdj))
	{
		App->GetOptions()->SetValue(OPT_UiFontSize, SizeAdj = 2);
	}

	if (GetViewById(IDC_UI_FNT_SIZE, c))
	{
		c->Insert("-2pt");
		c->Insert("-1pt");
		c->Insert(AutoInBrackets);
		c->Insert("+1pt");
		c->Insert("+2pt");
	}
	else LAssert(!"IDC_UI_FNT_SIZE missing.");

	if (GetViewById(IDC_UI_LANG, UiLang))
	{
		LResources *Res = LgiGetResObj();
		if (Res && Res->GetLanguages())
		{
			LArray<LLanguageId> *InLangs = Res->GetLanguages();
			for (unsigned n=0; n<InLangs->Length(); n++)
			{
				LLanguage *Lang = LFindLang((*InLangs)[n]);
				if (Lang)
				{
					Langs.Insert(Lang);
				}
			}

			Langs.Sort([](auto a, auto b) { return Stricmp(a->Name, b->Name); });
			int n = 0;
			LVariant LangId;
			App->GetOptions()->GetValue(OPT_UiLanguage, LangId);
			if (LangId.Str())
			{
				for (auto l: Langs)
				{
					char *LocalName = Res->LanguageNames.Find(l->Id);
					if (LocalName && _stricmp(l->Id, "en"))
					{
						char Txt[256];
						sprintf_s(Txt, sizeof(Txt), "%s (%s)", LocalName, l->Name);
						UiLang->Insert(Txt);
					}
					else
					{
						UiLang->Insert(l->Name);
					}

					if (_stricmp(l->Id, LangId.Str()) == 0)
					{
						UiLang->Value(n);
					}
					n++;
				}
			}
		}
	}
	else LAssert(!"IDC_UI_LANG missing.");

	if (GetViewById(IDC_START_WEEK, c))
	{
		for (int i=0; i<CountOf(LDateTime::WeekdaysLong); i++)
			c->Insert(LDateTime::WeekdaysLong[i]);
	}
	else LAssert(!"IDC_START_WEEK missing.");

	if (GetViewById(IDC_DATE_FORMAT, c))
	{
		c->Insert(AutoInBrackets);
			
		char s[256];
		const char *sDay	= LLoadString(IDS_DAY);
		const char *sMonth	= LLoadString(IDS_MONTH);
		const char *sYear	= LLoadString(IDS_YEAR);

		sprintf_s(s, sizeof(s), "%s/%s/%s 12h", sDay, sMonth, sYear);
		c->Insert(s);
		sprintf_s(s, sizeof(s), "%s/%s/%s 12h", sMonth, sDay, sYear);
		c->Insert(s);
		sprintf_s(s, sizeof(s), "%s/%s/%s 12h", sYear, sMonth, sDay);
		c->Insert(s);

		sprintf_s(s, sizeof(s), "%s/%s/%s 24h", sDay, sMonth, sYear);
		c->Insert(s);
		sprintf_s(s, sizeof(s), "%s/%s/%s 24h", sMonth, sDay, sYear);
		c->Insert(s);
		sprintf_s(s, sizeof(s), "%s/%s/%s 24h", sYear, sMonth, sDay);
		c->Insert(s);
	}
	else LAssert(!"IDC_DATE_FORMAT missing.");

	EditorFont.Serialize(App->GetOptions(), OPT_EditorFont, false);
	HtmlFont.Serialize(App->GetOptions(), OPT_HtmlFont, false);
	UpdateFontDescription();

	LNotification note(LNotifyValueChanged);
	OnNotify(FindControl(IDC_REGISTER_CLIENT), note);
	OnNotify(FindControl(IDC_SOCKS5), note);
}

OptionsDlg::~OptionsDlg()
{
	LEventSinkMap::Dispatch.RemoveSink(this);
	DeleteObj(d);
}

void OptionsDlg::UpdateDefaultSendAccounts()
{
	LCombo *c;
	if (GetViewById(IDC_DEF_SEND, c))
	{
		bool ResetDefault = false;
		int64 CurIdx = c->Value();
		while (c->Delete((size_t)0));
		int FirstValid = -1;

		int i = 0;
		for (auto a: *App->GetAccounts())
		{
			LVariant Server = a->Send.Server();
			int Disabled = a->Send.Disabled();
			if (Server.Str() && !Disabled)
			{
				LVariant v = a->Send.Name();
				c->Insert(v.Str());
				if (FirstValid < 0)
					FirstValid = i;
			}
			else
			{
				c->Insert("----");
				if (i == CurIdx)
					ResetDefault = true;
			}
			i++;
		}

		if (ResetDefault)
		{
			if (FirstValid >= 0)
				c->Value(FirstValid);
		}
		else
			c->Value(CurIdx);
	}
}

void OptionsDlg::OnAccountEnable(ScribeAccount *Acc, bool Enable)
{
	Acc->Send.Disabled(!Enable);
	UpdateDefaultSendAccounts();
}

bool OptionsDlg::PasswordCtrlValue(int CtrlId, char *Option, bool ToWindow)
{
	bool Status = false;
	LViewI *w = FindControl(CtrlId);
	LVariant v;

	if (w &&
		Option &&
		App->GetOptions())
	{
		App->GetOptions()->GetValue(Option, v);
		if (ToWindow)
		{
			if (v.Str())
			{
				// option -> window
				// Status = w->Name(v);
			}
		}
		else
		{
			// window -> option
			const char *s = w->Name();
			if (ValidStr(s))
			{
				// user has modified the string
				// encrypt and store
				LPassword p;
				p.Set(s);
				p.Serialize(App->GetOptions(), Option, true);
				Status = true;
			}
		}
	}

	return Status;
}

void RemoveCtrl(LView *p, int i)
{
	LViewI *v = p->FindControl(i);
	DeleteObj(v);
}

void OptionsDlg::OnCreate()
{
    TabDialog::OnCreate();
    
	LList *lst;
	if (GetViewById(IDC_ACCOUNTS, lst))
	{
		lst->ResizeColumnsToContent();
		lst->Select(lst->ItemAt(0));
	}
}

void OptionsDlg::UpdateFontDescription()
{
	SetCtrlName(IDC_FONT, EditorFont.GetDescription());
	SetCtrlName(IDC_HTML_FONT, HtmlFont.GetDescription());
}

void OptionsDlg::WriteNativeText(LFile &f, char *t)
{
	#ifdef WIN32
	for (char *s=t; *s; s++)
	{
		if (*s == '\n')
		{
			f.Write("\r\n", 2);
		}
		else
		{
			f << *s;
		}
	}
	#else
	f.Write(t, (int)strlen(t));
	#endif
}

void OptionsDlg::ReindexAccounts()
{
	App->GetAccountSettingsAccess(this,
		ScribeWriteAccess,
		[this](auto Allow)
		{
			if (!Allow)
				return;

			LList *AccountLst;
			List<AccountItem> a;
			if (!GetViewById(IDC_ACCOUNTS, AccountLst) ||
				!AccountLst->GetAll(a))
				return;

			LHashTbl<IntKey<ssize_t>, ScribeAccount*> map;
			ssize_t maxIndex = 0;
			for (auto item: a)
			{
				auto i = item->GetAccount()->GetIndex();
				LAssert(i >= 0);
				maxIndex = MAX(i, maxIndex);
				map.Add(i, item->GetAccount());
			}

			ssize_t tmpIndex = maxIndex + 10;
			LVariant name, email;

			for (size_t i=0; i<a.Length(); i++)
			{
				if (auto acc = a[i]->GetAccount())
				{
					auto sName = (name = acc->Send.Name()).Str();
					auto sEmail = (email = acc->Identity.Email()).Str();
					auto idx = acc->GetIndex();
					if (idx == i)
						continue;

					if (auto target = map.Find(i))
					{
						// Move account at the target index to a temporary index
						LgiTrace("tmp: account %i to %i (%s/%s)\n", (int)target->GetIndex(), (int)tmpIndex, sName, sEmail);
						target->ReIndex(tmpIndex++);
					}

					acc->ReIndex(i);
				}
				else LAssert(!"no account object?");
			}

			// Rename any tmp value ones back to their proper locations...
			for (size_t i=0; i<a.Length(); i++)
			{
				auto item = a[i];
				if (auto acc = item->GetAccount())
				{
					auto sName = (name = acc->Send.Name()).Str();
					auto sEmail = (email = acc->Identity.Email()).Str();
					auto idx = acc->GetIndex();
					if (i != idx)
					{
						LgiTrace("restore: account %i to %i (%s/%s)\n", (int)idx, (int)i, sName, sEmail);
						acc->ReIndex(i);
					}
				}
			}
		});
}

int OptionsDlg::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	if (!Ctrl) return 0;

	switch (Ctrl->GetId())
	{
		case IDC_BROWSE_THEMES:
		{
			auto pos = Ctrl->GetPos();
			LPoint pt(0, pos.Y());
			Ctrl->PointToScreen(pt);
			LSubMenu sub;
			auto Paths = ScribeThemePaths();
			int n = 1;
			for (auto p: Paths)
				sub.AppendItem(p, n++, true);

			auto res = sub.Float(Ctrl->GetWindow(), pt.x, pt.y, LSubMenu::BtnLeft);
			if (res)
			{
				auto browse = Paths[res-1];
				FileDev->CreateFolder(browse, true);
				LBrowseToFile(browse);
			}
			break;
		}
		case IDC_ADVANCED:
		{
			if (n.Type == IDC_SPELL_LANG)
			{
				// If the user changes the language we have to clear the dictionary
				LControlTree *Ct;
				if (GetViewById(IDC_ADVANCED, Ct))
				{
					LControlTree::Item *ci;
					if ((ci = Ct->Find(OPT_SpellCheckDictionary)))
					{
						LVariant v;
						ci->SetValue(v);
					}
				}				
			}
			break;
		}
		case IDC_REGISTER_CLIENT:
		{
			if (Ctrl->Value())
			{
				SetCtrlEnabled(IDC_CHECK_DEFAULT, true);
			}
			else
			{
				SetCtrlEnabled(IDC_CHECK_DEFAULT, false);
				SetCtrlValue(IDC_CHECK_DEFAULT, false);
			}
			break;
		}
		case IDC_LAUNCH_HELP:
		{
			char Path[256] = "install.html";
			switch (GetCtrlValue(IDC_TAB))
			{
				case 0: // identity
					strcat_s(Path, sizeof(Path), "#id");
					break;
				case 1: // accounts
					strcat_s(Path, sizeof(Path), "#accounts");
					break;
				case 2: // general
					strcat_s(Path, sizeof(Path), "#general");
					break;
				case 3: // connection
					strcat_s(Path, sizeof(Path), "#connect");
					break;
				case 4: // appearence
					strcat_s(Path, sizeof(Path), "#appear");
					break;
				case 5: // other
					strcat_s(Path, sizeof(Path), "#other");
					break;
			}
			App->LaunchHelp(Path);
			break;
		}
		case IDC_SOCKS5:
		{
			bool SocksOn = Ctrl->Value() != 0;
			SetCtrlEnabled(IDC_SOCKS5_SERVER, SocksOn);
			SetCtrlEnabled(IDC_SOCKS5_USER, SocksOn);
			SetCtrlEnabled(IDC_SOCKS5_PASSWORD, SocksOn);
			break;			
		}
		case IDC_EDIT_REPLY:
		{
			if (auto dlg = new UtfEditor(App, this,
										OPT_TextReplyFormat,
										OPT_HtmlReplyFormat,
										LLoadString(IDS_REPLY)))
				dlg->DoModal(nullptr);
			break;
		}
		case IDC_RESET_REPLY:
		{
			LVariant v;
			App->GetOptions()->SetValue(OPT_TextReplyFormat, v = DefaultTextReplyTemplate);
			App->GetOptions()->SetValue(OPT_HtmlReplyFormat, v = DefaultHtmlReplyTemplate);
			break;
		}
		case IDC_EDIT_FORWARD:
		{
			if (auto dlg = new UtfEditor(App, this,
										OPT_TextForwardFormat,
										OPT_HtmlForwardFormat,
										LLoadString(IDS_FORWARD)))
				dlg->DoModal(nullptr);
			break;
		}
		case IDC_RESET_FORWARD:
		{
			LVariant v;
			App->GetOptions()->SetValue(OPT_TextForwardFormat, v = DefaultTextReplyTemplate);
			App->GetOptions()->SetValue(OPT_HtmlForwardFormat, v = DefaultHtmlReplyTemplate);
			break;
		}
		case IDC_ACCOUNTS:
		{
			switch (n.Type)
			{
				default:
					break;
				case LNotifyItemInsert:
				case LNotifyItemDelete:
				{
					UpdateDefaultSendAccounts();
					break;
				}
				case LNotifyContainerReorder:
				{
					// Save the reordered list indexes into the accounts themselves:
					ReindexAccounts();
					break;
				}
			}
			break;
		}
		case IDC_ADD:
		{
			App->GetAccountSettingsAccess(this, ScribeWriteAccess, [this](auto Allow)
			{
				if (!Allow)
					return;
				List<ScribeAccount> *AList = App->GetAccounts();
				LList *ACtrl;
				if (AList && GetViewById(IDC_ACCOUNTS, ACtrl))
				{
					int Items = (int)AList->Length();
					ScribeAccount *a = new ScribeAccount(App, Items);
					if (a)
					{
						a->Create();

						// Open the UI
						a->InitUI(this, 0, [this, ACtrl, AList, a](auto status)
						{
							if (status)
							{
								ACtrl->Insert(new AccountItem(this, a));
								AList->Insert(a);
								UpdateDefaultSendAccounts();
							}
							else delete a;
						});
					}
				}
			});
			break;
		}
		case IDC_DELETE:
		{
			App->GetAccountSettingsAccess(this, ScribeWriteAccess, [this](auto Allow)
			{
				if (!Allow)
					return;
				List<AccountItem> Sel;
				LList *ACtrl;
				if (GetViewById(IDC_ACCOUNTS, ACtrl) && ACtrl->GetSelection(Sel))
				{
					List<ScribeAccount> *AList = App->GetAccounts();

					// For all selected
					for (auto i: Sel)
					{
						auto a = i->GetAccount();
						if (a->IsOnline())
						{
							LgiMsg(	this,
									"You can't delete the account while it's still active.",
									AppName);
						}
						else
						{
							if (AList)
							{
								// Delete the applications reference
								AList->Delete(a);
							}

							a->Delete();
							delete a; // delete actual account object
						}
					}

					ReindexAccounts();
				}
			});
			break;
		}
		case IDC_PROPERTIES:
		{
			App->GetAccountSettingsAccess(this, ScribeReadAccess, [this](auto Allow)
			{
				if (!Allow)
					return;

				List<LListItem> Sel;
				LList *ACtrl;
				if (GetViewById(IDC_ACCOUNTS, ACtrl) && ACtrl->GetSelection(Sel))
				{
					AccountItem *i = dynamic_cast<AccountItem*>(Sel[0]);
					if (i)
					{
						i->GetAccount()->InitUI(this, 0, [this, i](auto status)
						{
							if (status)
							{
								i->Update();
								UpdateDefaultSendAccounts();
							}
						});
					}
				}
				else
				{
					LgiMsg(this, "No account selected.", AppName, MB_OK);
				}
			});
			break;
		}
		case IDC_SET_SOUND:
		{
			auto Select = new LFileSelect(this);
			Select->Type("Sound", "*.wav");
			Select->Open([this](auto dlg, auto status)
			{
				if (status)
				{
					LEdit *LogFile;
					if (GetViewById(IDC_NEW_MAIL_SOUND, LogFile))
						LogFile->Name(dlg->Name());
				}
			});
			break;
		}
		case IDC_SET_FONT:
		{
			EditorFont.DoUI(this, [this](auto fontType)
			{
				UpdateFontDescription();
			});
			break;
		}
		case IDC_SET_HTML_FONT:
		{
			HtmlFont.DoUI(this, [this](auto fontType)
			{
				UpdateFontDescription();
			});
			break;
		}
		case IDC_CONFIGURE_REMOTE_CONTENT:
		{
			auto Dlg = new RemoteContentDlg(this, App);
			Dlg->DoModal(NULL);
			break;
		}
		case IDOK:
		{
			LOptionsFile *Opts = App->GetOptions();
			
			// Font
			EditorFont.Serialize(Opts, OPT_EditorFont, true);
			HtmlFont.Serialize(Opts, OPT_HtmlFont, true);

			if (UiLang)
			{
				LLanguage *Lang = Langs[(int)UiLang->Value()];
				if (Lang)
				{
					LVariant v;
					Opts->SetValue(OPT_UiLanguage, v = Lang->Id);
				}
				else
				{
					Opts->DeleteValue(OPT_UiLanguage);
				}
			}
			
			LList *ACtrl;
			if (GetViewById(IDC_ACCOUNTS, ACtrl))
			{
				List<AccountItem> a;
				ACtrl->GetAll(a);
				for (auto ai: a)
				{
					ai->Account->Send.Disabled(ai->Disable->Value() != 0);
				}
			}

			LVariant Cur[2], New[2], v;
			Opts->GetValue(OPT_SpellCheckLanguage, Cur[0]);
			Opts->GetValue(OPT_SpellCheckDictionary, Cur[1]);
			
			Convert(App->GetOptions(), this, false);
			
			Opts->GetValue(OPT_SpellCheckLanguage, New[0]);
			Opts->GetValue(OPT_SpellCheckDictionary, New[1]);
			if (Cur[0] != New[0] ||
				Cur[1] != New[1])
			{
				LgiTrace("%s:%i - OnSpellerSettingChange: Lang(%s), Dict(%s)\n",
						_FL,
						New[0].Str(),
						New[1].Str());
				App->OnSpellerSettingChange();
			}
			
			if (Opts->GetValue(OPT_SizeInKiB, v))
				OptionSizeInKiB = v.CastInt32() != 0;
			if (Opts->GetValue(OPT_RelativeDates, v))
				ShowRelativeDates = v.CastInt32() != 0;
			
			CalendarViewWnd::OnOptionsChange();

			EndModal(1);
			break;
		}
		case IDCANCEL:
		{
			EndModal(0);
			break;
		}
	}
	
	return 0;
}

LMessage::Result OptionsDlg::OnEvent(LMessage *m)
{
	switch (m->Msg())
	{
		case M_ENUMERATE_LANGUAGES:
		{
			LControlTree *Ct;
			if (!GetViewById(IDC_ADVANCED, Ct))
			{
				LgiTrace("%s:%i - No control tree.\n", _FL);
				break;
			}

			auto Langs = m->AutoA< LArray<LSpellCheck::LanguageId> >();
			if (!Langs)
			{
				LgiTrace("%s:%i - Error: No dictionary list.\n", _FL);
				break;
			}
			
			LControlTree::Item *ci;
			if (!(ci = Ct->Find(OPT_SpellCheckLanguage)))
			{
				LgiTrace("%s:%i - Error: No OPT_SpellCheckLanguage leaf.\n", _FL);
				break;
			}

			LAutoPtr<LControlTree::Item::EnumArr> Enum(new LControlTree::Item::EnumArr);
			if (!Enum)
			{
				LgiTrace("%s:%i - Error: alloc failed.\n", _FL);
				break;
			}

			for (unsigned i=0; i<Langs->Length(); i++)
			{
				LSpellCheck::LanguageId &p = (*Langs)[i];
						
				LControlTree::EnumValue &e = Enum->New();
				if (p.EnglishName && p.NativeName)
				{
					LString s;
					s.Printf("%s / %s", p.NativeName.Get(), p.EnglishName.Get());
					e.Name = s;
				}
				else if (p.EnglishName)
					e.Name = p.EnglishName;
				else if (p.LangCode)
					e.Name = p.LangCode;
				else
					LAssert(!"Null object.");

				e.Value = p.EnglishName ? p.EnglishName : p.LangCode;
			}

			ci->SetEnum(Enum);
			break;
		}
		case M_ENUMERATE_DICTIONARIES:
		{
			LControlTree *Ct;
			if (!GetViewById(IDC_ADVANCED, Ct))
				break;

			auto Dicts = m->AutoA<LArray<LSpellCheck::DictionaryId>>();			
			if (!Dicts)
			{
				LgiTrace("%s:%i - Error: No dictionary list.\n", _FL);
				break;
			}
			
			LControlTree::Item *ci;
			if (!(ci = Ct->Find(OPT_SpellCheckDictionary)))
			{
				LgiTrace("%s:%i - Error: No OPT_SpellCheckDictionary leaf.\n", _FL);
				break;
			}

			LAutoPtr<LControlTree::Item::EnumArr> Enum(new LControlTree::Item::EnumArr);
			if (!Enum)
			{
				LgiTrace("%s:%i - Error: alloc failed.\n", _FL);
				break;
			}

			for (unsigned i=0; i<Dicts->Length(); i++)
			{
				LSpellCheck::DictionaryId &s = (*Dicts)[i];
				LControlTree::EnumValue &e = Enum->New();
				e.Name = s.Dict;
				e.Value = s.Dict;
			}

			ci->SetEnum(Enum);
			break;
		}
	}
	
	return TabDialog::OnEvent(m);
}

/////////////////////////////////////////////////////////////////////////////
UtfEditor::UtfEditor(ScribeWnd *app, LWindow *parent,
	const char *txtopt, const char *htmlopt, const char *desc)
{
	LRect r(0, 0, 600, 500);
	SetPos(r);
	MoveToCenter();
	SetParent(parent);
	App = app;
	TextOpt = txtopt;
	HtmlOpt = htmlopt;

	Children.Insert(new LTextLabel(-1, 5, 5, -1, -1, desc));
	Children.Insert(Tabs = new LTabView(200, 0, 30, 300, 300));
	if (Tabs)
	{
		Tabs->SetPourLargest(false);
		Tabs->SetPourChildren(true);
		if ((TabText = Tabs->Append("Text")))
		{
			TabText->Append(Txt = new LTextView3(IDC_TEXT_VIEW, 0, 0, 100, 100));
			Txt->SetPourLargest(true);
			Txt->Sunken(true);
		}
		if ((TabHtml = Tabs->Append("Html")))
		{
			TabHtml->Append(Html = new LRichTextEdit(IDC_HTML_VIEW, 0, 0, 100, 100));
			Html->SetPourLargest(true);
			Html->Sunken(true);
		}
	}
	Children.Insert(Ok = new LButton(IDOK, 5, 5, 75, 20, LLoadString(IDS_OK)));
	Children.Insert(Cancel = new LButton(IDCANCEL, 75, 5, 75, 20, LLoadString(IDS_CANCEL)));

	LVariant s;
	if (Txt && App->GetOptions()->GetValue(TextOpt, s))
		Txt->Name(s.Str());
	if (Html && App->GetOptions()->GetValue(HtmlOpt, s))
		Html->Name(s.Str());
}

void UtfEditor::OnPosChange()
{
	if (Cancel)
	{
		LRect c = GetClient();
		LRect r = Cancel->GetPos();
		r.Offset(c.X() - Cancel->X() - 5 - r.x1, 0);
		Cancel->SetPos(r);
		r = Ok->GetPos();
		r.Offset(Cancel->GetPos().x1 - Ok->X() - 5 - r.x1, 0);
		Ok->SetPos(r);
		r = Tabs->GetPos();
		r.x2 = c.x2;
		r.y2 = c.y2;
		Tabs->SetPos(r);
	}
}

int UtfEditor::OnNotify(LViewI *c, const LNotification &n)
{
	switch (c->GetId())
	{
		case IDOK:
		{
			LVariant v;
			LOptionsFile *Opts = App->GetOptions();
			if (Txt)
				Opts->SetValue(TextOpt, v = Txt->Name());
			if (Html)
			{
				const char *n = Html->Name();
				Opts->SetValue(HtmlOpt, v = n);
			}
			
			// fall thru
		}
		case IDCANCEL:
		{
			Quit();
			break;
		}
	}

	return 0;
}

