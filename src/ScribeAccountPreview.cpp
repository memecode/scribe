/*
**	FILE:			ScribePopView.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			5/5/1999
**	DESCRIPTION:	Scribe Pop Account Viewer
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "ScribePrivate.h"
#include "ScribeAccountPreview.h"
#include "resdefs.h"
#include "lgi/common/Button.h"
#include "lgi/common/List.h"
#include "lgi/common/LgiRes.h"

//////////////////////////////////////////////////////////////////////////////
AccountMessage::AccountMessage(ScribeAccount *to)
{
	To = to;

	Download = new LListItemCheckBox(this, 5);
	Delete = new LListItemCheckBox(this, 6);
}

const char *AccountMessage::GetText(int i)
{
	static char Buf[128];

	switch (i)
	{
		case 0:
		{
			LFormatSize(Buf, sizeof(Buf), Size);

			#ifdef _DEBUG
			size_t Len = strlen(Buf);
			sprintf_s(Buf+Len, sizeof(Buf)-Len, " [%i]", Index);
			#endif

			return Buf;
			break;
		}
		case 1:
		{
			return From;
			break;
		}
		case 2:
		{
			LVariant e = To ? To->Identity.Email() : 0;
			if (!e.Str())
			{
				To->GetApp()->GetOptions()->GetValue(OPT_Email, e);
			}
			if (e.Str())
			{
				strcpy_s(Buf, sizeof(Buf), e.Str());
				return Buf;
			}
			break;
		}
		case 3:
		{
			return Subject;
			break;
		}
		case 4:
		{
			Date.Get(Buf, sizeof(Buf));
			return Buf;
			break;
		}
	}

	return 0;
}

int AccountMessage::GetImage(int Flags)
{
	if (Attachments)
	{
		return (New) ? ICON_UNREAD_ATT_MAIL : ICON_READ_ATT_MAIL;
	}

	return (New) ? ICON_UNREAD_MAIL : ICON_READ_MAIL;
}

ReceiveAction AccountMessage::GetAction()
{
	bool Down = Download && Download->Value();
	bool Del = Delete && Delete->Value();
	if (Down && Del)
	{
		return MailDownloadAndDelete;
	}
	else if (Down)
	{
		return MailDownload;
	}
	else if (Del)
	{
		return MailDelete;
	}

	return MailNoop;
}

int IndexCompare(AccountMessage *a, AccountMessage *b, NativeInt d)
{
	return a->Index - b->Index;
}

////////////////////////////////////////////////////////////////////////////////////
class ScribeAccountPreviewPrivate
{
public:
	ScribeWnd *App;
	LList *Lst;
	LArray<ScribeAccount*> Accounts;
	int SortCol;

	ScribeAccountPreviewPrivate()
	{
		App = 0;
		Lst = 0;
		SortCol = 0;
	}

	~ScribeAccountPreviewPrivate()
	{
	}
};

ScribeAccountPreview::ScribeAccountPreview(ScribeWnd *app, LArray<ScribeAccount*> &Lst)
{
	d = new ScribeAccountPreviewPrivate;
	d->App = app;
	d->Accounts = Lst;

	bool Status = false;

	SetParent(d->App);
	SetQuitOnClose(false);
	
	LRect WndPos(100, 100, 800, 600);

	LString Nme;
	LoadFromResource(	IDD_POPVIEW,
						this,
						NULL,
						&Nme);

	if (!SerializeState(d->App->GetOptions(), OPT_PreviewWndPos, true))
		SetPos(WndPos);

	if (GetViewById(IDC_LIST, d->Lst))
	{
		d->Lst->SetImageList(d->App->GetIconImgList(), false);
		Status = true;
	}

	Name(Nme);

	// Load the window and column sizes
	LVariant Data;
	if (d->App->GetOptions()->GetValue(OPT_PreviewWndCols, Data))
	{
		LString s = Data.Str();
		LString::Array p = s.Split(",");
		if (d->Lst)
		{
			for (unsigned i=0; i < (unsigned)d->Lst->GetColumns() && i < p.Length(); i++)
			{
				LItemColumn *c = d->Lst->ColumnAt(i);
				if (c) c->Width((int) p[i].Int());
			}
		}
	}

	LPoint p(215, 300);
	SetMinimumSize(p);

	if (Attach(0))
	{
		AttachChildren();
		Visible(true);
		
		LViewI *v;
		if (GetViewById(IDC_REFRESH, v))
			v->SendNotify(LNotifyActivate);
	}
	else
	{
		LgiMsg(d->App, "Error: Window Creation failed.", AppName, MB_OK);
	}
}

ScribeAccountPreview::~ScribeAccountPreview()
{
	// Remember the window and column sizes
	SerializeState(d->App->GetOptions(), OPT_PreviewWndPos, false);

	if (d->Lst)
	{
		LString s = ",";
		LString::Array p;
		
		for (int i=0; i < d->Lst->GetColumns(); i++)
		{
			LItemColumn *c = d->Lst->ColumnAt(i);
			if (c)
				p.New().Printf("%i", c->Width());
		}

		s = s.Join(p);
		
		LVariant Data = s.Get();
		d->App->GetOptions()->SetValue(OPT_PreviewWndCols, Data);
	}

	for (unsigned i=0; i<d->Accounts.Length(); i++)
	{
		ScribeAccount *sa = d->Accounts[i];

		if (sa->Receive.IsOnline())
			sa->Disconnect();

		sa->Receive.SetItems(0);
	}

	delete d;
}

void ScribeAccountPreview::OnPaint(LSurface *pDC)
{
	LWindow::OnPaint(pDC);

	// draw resize tab
	LRect r = GetClient();
	int Ox = r.X() - 12;
	int Oy = r.Y() - 12;
	for (int y=0; y<3; y++)
	{
		for (int x=0; x<3; x++)
		{
			if (x + y > 1)
			{
				pDC->Colour(L_LOW);
				pDC->Set(Ox + (x * 4), Oy + (y * 4));

				pDC->Colour(L_LIGHT);
				pDC->Set(Ox + (x * 4) + 1, Oy + (y * 4) + 1);
			}
		}
	}
}

void ScribeAccountPreview::OnPulse()
{
	int Online = 0;
	for (unsigned i=0; i<d->Accounts.Length(); i++)
	{
		if (!d->Accounts[i]->Receive.IsOnline())
		{
			d->Accounts[i]->Receive.SetItems(0);
		}
		else
		{
			Online++;
		}
	}

	if (Online == 0)
	{
		Enable(true);
		SetPulse();
	}
}

void ScribeAccountPreview::GetMsgs(List<AccountMessage> &All)
{
	if (d->Lst)
	{
		List<LListItem> l;
		if (d->Lst->GetAll(l))
		{
			for (auto Item: l)
			{
				AccountMessage *Msg = dynamic_cast<AccountMessage*>(Item);
				if (Msg)
				{
					All.Insert(Msg);
				}
			}
		}
	}
}

int MsgCompare(LListItem *A, LListItem *B, NativeInt SortCol)
{
	int Status = 0;
	AccountMessage *a = dynamic_cast<AccountMessage*>(A);
	AccountMessage *b = dynamic_cast<AccountMessage*>(B);
	if (a && b)
	{
		int Col = abs((int)SortCol)-1;
		switch (Col)
		{
			case 0: // Size
			{
				Status = (int) (a->Size - b->Size);
				break;
			}
			case 1: // From
			{
				if (a->From && b->From)
				{
					Status = _stricmp(a->From, b->From);
				}
				break;
			}
			case 2: // To
			{
				const char *sa = A->GetText(Col);
				const char *sb = B->GetText(Col);
				if (sa && sb)
				{
					Status = _stricmp(sa, sb);
				}
				break;
			}
			case 3: // Subject
			{
				if (a->Subject && b->Subject)
				{
					Status = _stricmp(a->Subject, b->Subject);
				}
				break;
			}
			case 4: // Date
			{
				Status = a->Date.Compare(&b->Date);
				break;
			}
		}

		if (SortCol < 0) Status = -Status;
	}
	return Status;
}

void ScribeAccountPreview::SetSort(int s)
{
	if (d->SortCol != s)
	{
		d->SortCol = s;

		if (d->Lst)
		{
			int Col = abs(d->SortCol)-1;
			int Ascend = d->SortCol > 0;
			d->Lst->SetSortingMark(Col, !Ascend);
			d->Lst->Sort<NativeInt>(MsgCompare, d->SortCol);
		}
	}
}

int ScribeAccountPreview::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_UNDELETE:
		{
			if (d->Accounts.Length())
			{
				List<AccountMessage> All;
				GetMsgs(All);
				for (auto m: All)
				{
					if (m->Select())
					{
						m->To->Receive.RemoveFromSpamIds(m->ServerUid);
						m->Delete->Value(false);
						m->Update();
					}
				}
			}
			break;
		}
		case IDC_UNREAD:
		{
			if (d->Accounts.Length())
			{
				List<AccountMessage> All;
				GetMsgs(All);
				for (auto m: All)
				{
					if (m->Select() && m->ServerUid)
					{
						m->To->Receive.RemoveMsg(m->ServerUid);
						m->New = true;
						m->Update();
					}
				}
			}
			break;
		}
		case IDC_SET_READ:
		{
			if (d->Accounts.Length())
			{
				List<AccountMessage> All;
				GetMsgs(All);
				for (auto m: All)
				{
					if (m->Select() && m->ServerUid)
					{
						m->To->Receive.AddMsg(m->ServerUid);
						m->New = false;
						m->Update();
					}
				}
			}
			break;
		}
		case IDC_LIST:
		{
			if (d->Lst && n.Type == LNotifyItemColumnClicked)
			{
				int Col = -1;
				LMouse m;
				if (d->Lst->GetColumnClickInfo(Col, m) && m.Left())
				{
					switch (Col)
					{
						case 0: // Size
						case 1: // From
						case 2: // To
						case 3: // Subject
						case 4: // Date
						{
							if (abs(d->SortCol)-1 == Col)
							{
								SetSort(-d->SortCol);
							}
							else
							{
								SetSort(Col + 1);
							}
							break;
						}
						case 5: // Download
						{
							List<AccountMessage> All;
							GetMsgs(All);
							
							int v = 0;
							for (auto m: All)
							{
								v += (int)m->Download->Value();
							}

							for (auto m: All)
							{
								m->Download->Value(v ? false : true);
							}
							break;
						}
						case 6: // Delete
						{
							List<AccountMessage> All;
							GetMsgs(All);

							int v = 0;
							for (auto m: All)
							{
								v += (int)m->Delete->Value();
							}

							for (auto m: All)
							{
								m->Delete->Value(v ? false : true);
							}
							break;
						}
					}
				}
			}
			break;
		}		
		case IDOK:
		{
			// Clean up... mail client thread will continue doing it's job
			for (auto a: d->Accounts)
				a->Receive.SetItems(NULL);

			// Do actions
			if (d->Lst)
			{
				List<AccountMessage> All;
				GetMsgs(All);
				All.Sort(IndexCompare);

				for (auto a: d->Accounts)
				{
					LArray<ReceiveAction> Actions;

					for (auto m: All)
					{
						if (m->To == a)
						{
							auto action = m->GetAction();
							Actions[m->Index] = action;

							if (action == MailDownload ||
								action == MailDownloadAndDelete)
							{
								// Assume the user thinks it's NOT spam...
								// Make sure the spam ID is removed.
								a->Receive.RemoveFromSpamIds(m->ServerUid);
							}
						}
					}

					if (Actions.Length())
					{
						a->Receive.SetActions(&Actions);
						a->Receive.Connect(0, false);
					}
				}
			}
			
			d->Accounts.Length(0);

			Quit();
			break;
		}
		case IDCANCEL:
		{
			bool Connected = false;

			for (unsigned i=0; i<d->Accounts.Length(); i++)
			{
				if (d->Accounts[i]->IsOnline())
				{
					Connected = true;
					d->Accounts[i]->Disconnect();
					d->Accounts[i]->Receive.SetItems(0);
				}
			}

			if (!Connected)
			{
				SetPulse();
				Quit();
			}
			break;
		}

		case IDC_REFRESH:
		{
			if (d->Lst)
			{
				d->Lst->Empty();
			}

			int Connecting = 0;
			LStringPipe Already;

			for (unsigned i=0; i<d->Accounts.Length(); i++)
			{
				ScribeAccount *sa = d->Accounts[i];
				LVariant Proto = sa->Receive.Protocol();
				// ScribeProtocol Protocol = ProtocolStrToEnum(Proto.Str());
				
				if (sa->Receive.IsOnline())
				{
					// already online..
					Already.Print("\t%s\n", sa->Identity.Email().Str());
				}
				else
				{
					// go online to list contents
					if (sa->Receive.SetItems(d->Lst))
					{
						sa->Receive.Connect(this, false);
						Connecting++;
					}
				}
			}

			if (Connecting)
			{
				Enable(false);
				SetPulse(300);
			}

			if (Already.GetSize())
			{
				char *a = Already.NewStr();
				if (a)
				{
					LgiMsg(this, LLoadString(IDS_ERROR_ALREADY_ONLINE), AppName, MB_OK, a);
					DeleteArray(a);
				}
			}
			break;
		}
	}

	return 0;
}

void ScribeAccountPreview::Enable(bool e)
{
	SetCtrlEnabled(IDOK, e);
	SetCtrlEnabled(IDC_REFRESH, e);
	SetCtrlEnabled(IDC_UNREAD, e);
	SetCtrlEnabled(IDC_SET_READ, e);
	SetCtrlEnabled(IDC_UNDELETE, e);
}

bool OpenPopView(ScribeWnd *Parent, LArray<ScribeAccount*> &Lst)
{
	ScribeAccountPreview *Dlg = new ScribeAccountPreview(Parent, Lst);
	return Dlg != 0;
}
