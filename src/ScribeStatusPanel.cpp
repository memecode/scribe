#include "Scribe.h"
#include "ScribePrivate.h"
#include "ScribeStatusPanel.h"
#include "resdefs.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/TextView4.h"
#include "lgi/common/TextLog.h"
#include "lgi/common/TabView.h"

////////////////////////////////////////////////////////////////////////////
#define STATUS_BASE			22

#define UP_ARROW			4
#define DOWN_ARROW			5
#define BLACK_ARROW			6

#define FTP_SIZE			22

#define IDM_SEND			100
#define IDM_RECEIVE			101
#define IDM_PREVIEW			102
#define IDM_CONFIG			103

const char *AccountStatusTxt[] = { "Idle", "Connected", "Waiting", "Error" };

////////////////////////////////////////////////////////////////////////////

#if 1
typedef LTsTextView<LTextView3> LAccountLogParent;
#else
typedef LTsTextView<LTextView4> LAccountLogParent;
#endif

class LAccountLog : public LAccountLogParent
{
	LArray<uint32_t> Rgb;
	LString sEmpty;

public:
	LogEntry *Prev = nullptr;

	LAccountLog() : LAccountLogParent(-1)
	{
		SetPourLargest(true);
		SetWrapType(L_WRAP_NONE);
		sEmpty.Printf("(%s)", LLoadString(IDS_EMPTY));

		if (auto f = new LFont)
		{
			*f = *LSysFont;
			f->PointSize(f->PointSize() - 1);
			SetFont(f, true);
		}
	}

	void Empty()
	{
		Name(nullptr);
		Rgb.Empty();
	}

	void AddText(char16 *Txt, size_t Chars, LColour &c)
	{
		auto end = Txt + Chars;
		for (auto *p = Txt; p < end; p++)
			if (*p == '\n')
				Rgb.Add(c.c32());
		
		LAccountLogParent::Add(Txt, Chars);
	}

	void PourStyle(size_t Start, ssize_t Length)
	{
		size_t i = 0;
		for (auto tl: Line)
		{
			if (i >= Rgb.Length())
				break;
			tl->c.c32(Rgb[i]);
			i++;
		}
	}

	void OnPaint(LSurface *pDC)
	{
		if (LAccountLogParent::Length() == 0)
		{
			LColour f(192,192,192), b(L_WORKSPACE);
			auto Fnt = GetFont();
			LDisplayString ds(Fnt, sEmpty);
			auto c = GetClient();
			Fnt->Transparent(false);
			Fnt->Colour(f, b);
			ds.Draw(pDC, 4, 4, &c);
		}
		else LAccountLogParent::OnPaint(pDC);
	}
};

class LAccountLogFactory : public LViewFactory
{
	LView *NewView
	(
		/// The name of the class to create
		const char *Class,
		/// The initial position of the view
		LRect *Pos,
		/// The initial text of the view
		const char *Text
	)
	{
		if (!_stricmp(Class, "LAccountLog"))
			return new LAccountLog();
		return NULL;
	}
}	AccountLogFactory;

////////////////////////////////////////////////////////////////////////////
AccountStatusItem::AccountStatusItem(AccountStatusPanel *panel, ScribeAccount *account, LImageList *imglst)
{
	Buf[0] = 0;

	Panel = panel;
	ImgLst = imglst;
	Account = account;
	Account->Views.Add(this);
	State = STATUS_OFFLINE;

	SetImage(ICON_UNSENT_MAIL);
}

AccountStatusItem::~AccountStatusItem()
{
	LAssert(Account->Views.HasItem(this));
	Account->Views.Delete(this);
}

const char *AccountStatusItem::GetText(int Col)
{
	char *Status = 0;

	if (Account)
	{
		if (Col == 2)
		{
			LVariant v = Account->Receive.Name();
			if (!v.Str())
			{
				v = Account->Receive.Server();
			}
			if (!v.Str())
			{
				v = Account->Send.Server();
			}

			static char Buf[64];
			if (v.Str())
			{
				strcpy_s(Buf, sizeof(Buf), v.Str());
				Status = Buf;
			}
		}
		else if (Col == 3)
		{
			return Buf;
		}
	}

	return Status;
}

int AccountStatusItem::Compare(LListItem *To, ssize_t Field)
{
	auto b = dynamic_cast<AccountStatusItem*>(To);
	if (!b || !Account || !b->Account)
	{
		LAssert(!"invalid data");
		return 0;
	}		
	return Account->Compare(b->Account);
}

void AccountStatusItem::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
{
	LListItem::OnPaintColumn(Ctx, i, c);

	if (ImgLst && Account)
	{
		LRect *Bounds = ImgLst->GetBounds();
		if (Bounds)
		{
			int Icon = -1;
			
			if (i == 0)
			{
				// Send Status
				if (Account->Send.IsConfigured())
					Icon = STATUS_BASE + (Account->Send.IsOnline() ? STATUS_ONLINE : STATUS_OFFLINE);
			}
			else if (i == 1)
			{
				// Receive Status
				if (Account->Receive.IsConfigured())
					Icon = STATUS_BASE + Panel->AccountStatus(&Account->Receive);
			}

			if (Icon >= 0)
			{
				Bounds += Icon;

				int x = Ctx.x1 + ((Ctx.X()-Bounds->X())/2) - Bounds->x1;
				int y = Ctx.y1 + ((Ctx.Y()-Bounds->Y())/2) - Bounds->y1;

				LColour Back(Ctx.Back);
				ImgLst->Draw(Ctx.pDC, x, y, Icon, Back);
			}
		}
	}
}

void AccountStatusItem::OnPulse()
{
	if (Account)
	{
		Buf[0] = 0;
		Account->OnPulse(Buf, sizeof(Buf));
		Update();
	}
}

void AccountStatusItem::OnMouseClick(LMouse &m)
{
	if (!Account)
		return;

	if (m.IsContextMenu())
	{
		LSubMenu RClick;
		LArray<ScribeAccount*> Acc;
		List<LListItem> Sel;
		int RecAcc = 0;
		int SendAcc = 0;
		if (GetList()->GetSelection(Sel))
		{
			for (auto i: Sel)
			{
				auto *si = dynamic_cast<AccountStatusItem*>(i);
				auto account = si->Account;
				auto name = account->Identity.Name();

				Acc.Add(account);

				if (!account->Receive.Disabled())
				{
					auto receiveServer = account->Receive.Server();
					auto sendServer = account->Send.Server();

					RecAcc += receiveServer.Str() ? 1 : 0;
					SendAcc += sendServer.Str() ? 1 : 0;

					// LgiTrace("%s:%i - account '%s' servers: %s/%s\n", _FL, name.Str(), receiveServer.Str(), sendServer.Str());
				}
				// else LgiTrace("%s:%i - account '%s' is disabled.\n", _FL, name.Str());
			}
		}

		RClick.AppendItem(LString(LLoadString(IDS_SEND)) + " " + Account->Send.Server().Str(), IDM_SEND, SendAcc != 0);
		RClick.AppendItem(LString(LLoadString(IDS_RECEIVE)) + " " + Account->Receive.Server().Str(), IDM_RECEIVE, RecAcc != 0);
		RClick.AppendItem(LLoadString(IDS_PREVIEW), IDM_PREVIEW, RecAcc != 0);
		RClick.AppendSeparator();
		RClick.AppendItem(LLoadString(IDS_CONFIGURE), IDM_CONFIG, Acc.Length() == 1);

		switch (RClick.Float(GetList(), m))
		{
			case IDM_SEND:
			{
				if (!Account->Send.IsOnline())
				{
					auto Idx = Account->GetIndex();
					Panel->App->Send(Idx);
				}
				break;
			}
			case IDM_RECEIVE:
			{
				if (!Account->Receive.IsOnline())
				{
					Account->Receive.Connect(0, false);
				}
				break;
			}
			case IDM_PREVIEW:
			{
				OpenPopView(Panel->App, Acc);
				break;
			}
			case IDM_CONFIG:
			{
				Account->GetApp()->GetAccountSettingsAccess(GetList(),
															ScribeReadAccess,
															[this](auto status)
															{
																if (status)
																	Account->InitUI(Parent, 0, NULL);
															});
				break;
			}
		}
	}
	else if (m.Left() && m.Double())
	{
		// receive
		if (Account->Receive.IsConfigured())
		{
			Panel->App->Receive(Account->GetIndex());
		}
		else if (Account->Send.IsConfigured())
		{
			Panel->App->Send(Account->GetIndex());
		}
	}
}

////////////////////////////////////////////////////////////////////////////
#define OPT_StatusOpen					"ScribeUI.StatusOpen"

AccountStatusPanel::AccountStatusPanel(ScribeWnd *app, LImageList *imglst) :
	ScribePanel(app, LLoadString(IDS_STATUS), 20, false)
{
	App = app;
	Accounts = App ? App->GetAccounts() : nullptr;
	ImgLst = imglst;

	Alignment(GV_EDGE_BOTTOM);

	LVariant Op = false;
	App->GetOptions()->GetValue(OPT_StatusOpen, Op);
	if (Op.CastInt32())
	{
		Open(Op.CastInt32() != 0);
	}

	LRect *Bounds = ImgLst ? ImgLst->GetBounds() : nullptr;
	LString n;
	LRect p;
	if (Bounds && LoadFromResource(IDD_STATUS, this, &p, &n))
	{
		int MaxY = 0;
		for (auto c: Children)
		{
			MaxY = MAX(MaxY, c->GetPos().y2);
		}
		SetOpenSize(MaxY + 10);

		GetViewById(IDC_ACCOUNT_TBL, AccountTbl);
		GetViewById(IDC_PROGRESS_TBL, ProgressTbl);
		GetViewById(IDC_OP_PROG, Total);
		GetViewById(IDC_EMAIL_PROG, Sub);
		
		if (GetViewById(IDC_ACC_LOG, Log))
		{
			Log->SetPourChildren(true);
		}

		if (GetViewById(IDC_ACC_LIST, Lst))
		{
			Lst->SetImageList(ImgLst, false);

			int x = MAX(Bounds[STATUS_BASE + UP_ARROW].X(), Bounds[STATUS_BASE + STATUS_OFFLINE].X());
			LItemColumn *c = Lst->AddColumn("Send", x + 6);
			if (c)
			{
				c->Image(STATUS_BASE + UP_ARROW);
				c->Resizable(false);
			}

			x = MAX(Bounds[STATUS_BASE + DOWN_ARROW].X(), Bounds[STATUS_BASE + STATUS_OFFLINE].X());
			c = Lst->AddColumn("Receive", x + 6);
			if (c)
			{
				c->Image(STATUS_BASE + DOWN_ARROW);
				c->Resizable(false);
			}

			c = Lst->AddColumn(LLoadString(IDS_SERVER), 120);
			c = Lst->AddColumn(LLoadString(IDS_TIME), 50);
		}
	}
};

AccountStatusPanel::~AccountStatusPanel()
{
	LVariant v;
	auto Opts = App->GetOptions();
	Opts->SetValue(OPT_StatusOpen, v = Open());
}

void AccountStatusPanel::OnPosChange()
{
	LLayoutRect c(this);
	if (Open() && c.Valid())
	{
		c.x1 += 20;
		int FontHt = LSysFont->GetHeight();
		c.Left(AccountTbl, 170 + FontHt * 6);
		c.Left(ProgressTbl, 200 + FontHt * 12);
		c.Remaining(Log);
	}
}

bool AccountStatusPanel::_Lock()
{
	return App->Lock(_FL);
}

void AccountStatusPanel::_Unlock()
{
	App->Unlock();
}

int AccountStatusPanel::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_STOP:
		{
			// stop this client now
			static bool Stopping = false;
			if (!Stopping)
			{
				Stopping = true;

				if (_Lock())
				{
					Accountlet *AccLet = nullptr;
					if (Current)
					{
						if (Current->Receive.IsOnline())
							AccLet = &Current->Receive;
						else if (Current->Send.IsOnline())
							AccLet = &Current->Send;
					}

					_Unlock();

					if (AccLet)
						AccLet->Disconnect();
				}

				Stopping = false;
			}
			else
			{
				printf("Recursion lockout.\n");
			}
			break;
		}
	}

	return 0;
}

void AccountStatusPanel::OnAccountSelect(AccountStatusItem *Item)
{
	Current = (Item) ? Item->Account : 0;

	if (Total && Sub && _Lock())
	{
		Accountlet *AccLet = 0;
		if (Current)
		{
			bool IsSending = Current->Send.IsOnline();
			bool IsReceiving = Current->Receive.IsOnline();

			if ((IsSending ^ IsReceiving) == 0)
			{
				// Accounts are either both online or both offline
				if (Current->Send.GetLastOnline() > 
					Current->Receive.GetLastOnline())
				{
					AccLet = &Current->Send;
				}
				else
				{
					AccLet = &Current->Receive;
				}
			}
			else
			{
				// Only one is online
				if (IsSending)
				{
					AccLet = &Current->Send;
				}
				else if (IsReceiving)
				{
					AccLet = &Current->Receive;
				}
			}
		}

		bool IsCur = AccLet ? AccLet->IsOnline() : false;
		Total->Enabled(IsCur);
		Sub->Enabled(IsCur);
		SetCtrlEnabled(IDC_STATUS_TXT, IsCur);

		if (Accounts && Item)
		{
			int Status = AccountStatus(AccLet);

			// update the list item if needed
			if (Item->State != Status)
			{
				Item->Update();
				Item->State = Status;
			}

			// get ptrs to the progress objects
			const char *OpDesc = AccLet ? AccLet->GetStateName() : 0;
			SetCtrlName(IDC_STATUS_TXT, OpDesc);
		}
		else
		{
			SetCtrlName(IDC_STATUS_TXT, LLoadString(IDS_SELECT_ACCOUNT));
		}

		if (AccLet)
		{
			// Connection
			char Str[256];
			if (AccLet->Group.Range)
			{
				Total->SetRange(AccLet->Group.Range);
				Total->Value(AccLet->Group.Value);
				int Ch = sprintf_s(Str, sizeof(Str), LLoadString(IDS_EMAIL_PROGRESS), AccLet->Group.Value, AccLet->Group.Range);
				if (AccLet->Group.Start)
				{
					double Sec = (double)(LCurrentTime() - AccLet->Group.Start)/1000.0;
					if (Sec > 0.0)
					{
						double Rate = (double)AccLet->Group.Value / Sec;
						sprintf_s(Str+Ch, sizeof(Str)-Ch, " (%.1f/s)", Rate);
					}
				}
				
				SetCtrlName(IDC_OP_TXT, Str);
			}
			else
			{
				SetCtrlName(IDC_OP_TXT, 0);
				Total->Value(0);
			}

			// Mail
			if (AccLet->Item.Range)
			{
				Sub->SetRange(AccLet->Item.Range);
				Sub->Value(AccLet->Item.Value);
			}
			else
			{
				Sub->Value(0);
			}

			if (AccLet->Item.Start)
			{
				double Rate = 0.0;
				uint64 Period = LCurrentTime() - AccLet->Item.Start;
				double Sec = (double)Period / 1000.0;
				if (Period > 0)
					Rate = (double)AccLet->Item.Value / Sec;

				if (AccLet->Item.Range)
				{
					char sVal[32], sRange[32], sRate[32];
					LFormatSize(sVal, sizeof(sVal), AccLet->Item.Value);
					LFormatSize(sRange, sizeof(sRange), AccLet->Item.Range);
					LFormatSize(sRate, sizeof(sRate), (uint64)Rate);
					sprintf_s(Str, sizeof(Str), "%s of %s (%s/second)", sVal, sRange, sRate);
				}
				else
				{
					char sRate[32];
					LFormatSize(sRate, sizeof(sRate), (uint64)Rate);
					sprintf_s(Str, sizeof(Str), "%s/second", sRate);
				}

				SetCtrlName(IDC_EMAIL_TXT, Str);
			}
			else
			{
				SetCtrlName(IDC_EMAIL_TXT, 0);
			}
		}
		else
		{
			SetCtrlName(IDC_OP_TXT, 0);
			SetCtrlValue(IDC_OP_PROG, 0);
			SetCtrlName(IDC_EMAIL_TXT, 0);
			SetCtrlValue(IDC_EMAIL_PROG, 0);

			Total->Value(0);
			Sub->Value(0);
		}
		
		if (ProgressTbl)
			ProgressTbl->InvalidateLayout();

		SetCtrlEnabled(IDC_STOP, AccLet && AccLet->IsOnline());

		// do log list
		if (Log)
		{
			int Ctrls[2] = {IDC_SEND_LOG, IDC_RECEIVE_LOG};

			if (CurStatusItem != Item)
			{
				CurStatusItem = Item;
				for (int i=0; i<CountOf(Ctrls); i++)
				{
					LAccountLog *al;
					if (Log->GetViewById(Ctrls[i], al))
						al->Empty();
				}
			}

			if (Current)
			{
				Accountlet *accountlets[2] =
				{
					&Current->Send,
					&Current->Receive
				};

				for (int i=0; i<CountOf(Ctrls); i++)
				{
					LAccountLog *al; // text log view
					if (Log->GetViewById(Ctrls[i], al))
					{
						auto logs = accountlets[i]->Lock(_FL);
						if (!logs)
							continue;

						auto &l = logs->Log;
						if (l.Length())
						{
							if (!al->Prev ||
								al->Prev != l[0])
							{
								// Changed content
								al->Empty();
								al->Prev = l[0];
							}

							auto StartTs = LCurrentTime();
							size_t pos = 0;
							bool addTimeout = false;
							for (auto &e: l)
							{
								auto existingLen = al->LAccountLogParent::Length();
								size_t end = pos + e->Txt.Length();
								if (end > existingLen)
								{
									// Hasn't been added (fully)
									ssize_t offset = 0;
									size_t len = e->Txt.Length();
									if (existingLen > pos)
									{
										// Partial add:
										offset = existingLen - pos;
										len -= offset;
									}

									auto col = e->GetColour();
									al->AddText(e->Txt.AddressOf(offset), len, col);
								}
								pos += e->Txt.Length();

								auto CurTs = LCurrentTime();
								if (CurTs - StartTs >= 200)
								{
									LgiTrace("%s:%i - adding log entries timeout.\n", _FL);
									addTimeout = true;
									break;
								}
							}

							if (addTimeout)
							{
								al->Empty();
								
								// Delete the first 1/3 of the log...
								LRange r(0, (ssize_t)(l.Length() * 0.33));
								if (r.Len == 0)
									r.Len++;

								LgiTrace("%s:%i - Log processing blocking, removing " LPrintfSizeT " oldest items.\n",
										_FL, r.Len);

								l.DeleteRange(r);
								al->Prev = l[0];
							}
						}
						else if (!al->Length())
						{
							al->Empty();
						}
					}
				}
			}
		}

		_Unlock();
	}
}

int AccountStatusPanel::AccountStatus(Accountlet *a)
{
	if (!a)
	{
		LAssert(!"should always have an account ptr?");
		return STATUS_ERROR;
	}
	
	auto ico = a->GetStatusIcon();
	if (ico == STATUS_ERROR)
	{
		int asd=0;
	}
	return ico;
}

int AccountStatusPanel::CalcWidth()
{
	int BaseX = LPanel::CalcWidth();

	LRect *Bounds = ImgLst ? ImgLst->GetBounds() : 0;
	if (Bounds)
	{
		auto Send = App->GetSendAccount();

		int UpArrow = STATUS_BASE + UP_ARROW;
		int SendIcon = STATUS_BASE + (Send ? AccountStatus(&Send->Send) : 0);
		int DownArrow = STATUS_BASE + DOWN_ARROW;

		BaseX += Bounds[UpArrow].X() + 2;
		BaseX += Bounds[SendIcon].X() + 2;
		BaseX += 8;
		BaseX += Bounds[DownArrow].X() + 2;

		for (size_t i=0; i<Accounts->Length(); i++)
		{
			ScribeAccount *Acc = Accounts->ItemAt(i);
			if (Acc && Acc->Receive.IsConfigured())
			{
				int ReceiveIcon = STATUS_BASE + AccountStatus(&Acc->Receive);
				BaseX += Bounds[ReceiveIcon].X() + 2;
			}
		}
	}

	return BaseX;
}

void AccountStatusPanel::OnPaint(LSurface *pDC)
{
	#ifdef __GTK_H__
	LDoubleBuffer Buf(pDC);
	#endif

	LPanel::OnPaint(pDC);

	if (!IsOpen && ImgLst && Accounts)
	{
		int x = LPanel::CalcWidth() - 8, y = 3;

		auto Accs = Accounts->Length();
		auto Send = App->GetSendAccount();
		LColour Background(L_MED);

		if (auto Bounds = ImgLst->GetBounds())
		{
			// SMTP
			int UpArrow = STATUS_BASE + UP_ARROW;
			ImgLst->Draw(pDC, x, y, UpArrow, Background); // arrow
			x += Bounds[UpArrow].X() + 2;
			if (Send)
			{
				int SendIcon = STATUS_BASE + AccountStatus(&Send->Send);
				ImgLst->Draw(pDC, x, y, SendIcon, Background);
				x += Bounds[SendIcon].X() + 2;
			}

			// break
			x += 8;

			// Receive accounts
			int DownArrow = STATUS_BASE + DOWN_ARROW;
			ImgLst->Draw(pDC, x, y, DownArrow, Background); // arrow
			x += Bounds[DownArrow].X() + 2;
			for (size_t i=0; i<Accs; i++)
			{
				ScribeAccount *Acc = Accounts->ItemAt(i);
				if (Acc && Acc->Receive.IsConfigured())
				{
					int ReceiveIcon = STATUS_BASE + AccountStatus(&Acc->Receive);
					ImgLst->Draw(pDC, x, y, ReceiveIcon, Background);
					x += Bounds[ReceiveIcon].X() + 2;
				}
			}
		}
	}
}

void AccountStatusPanel::OnPulse()
{
	if (Accounts && PrevAccounts != Accounts->Length())
	{
		OnAccountListChange();
	}

	if (!IsOpen)
	{
		// Invalidate();
	}
	else if (Lst)
	{
		OnAccountSelect(dynamic_cast<AccountStatusItem*>(Lst->GetSelected()));
	}

	if (Lst)
	{
		List<AccountStatusItem> All;
		Lst->GetAll(All);
		for (auto a: All)
		{
			a->OnPulse();
		}
	}
}

void AccountStatusPanel::Empty()
{
	OnAccountSelect(0);	

	if (Lst)
	{
		Lst->Empty();
	}
}

void AccountStatusPanel::OnAccountListChange()
{
	if (Lst)
	{
		Lst->Empty();

		if (Accounts)
		{
			PrevAccounts = Accounts->Length();
			RePour();

			for (auto a: *Accounts)
				Lst->Insert(new AccountStatusItem(this, a, ImgLst));
			
			Lst->Sort([](auto *a, auto *b)
				{
					return a->Compare(b);
				});

			auto All = Lst->begin();
			if (*All && !Lst->GetSelected())
			{
				(*All)->Select(true);
			}
		}
	}

	Invalidate();
}

LXmlTag *AccountStatusPanel::GetOptions()
{
	return 0;
}

void AccountStatusPanel::SetDataRate(int Percent)
{
}

