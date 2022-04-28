#include "Scribe.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Combo.h"
#include "resdefs.h"
#include "LMarkColourSelect.h"
#include "lgi/common/Json.h"
#include "lgi/common/LgiRes.h"

#define IDM_EDIT_BASE			3000

LSearchView::LSearchView(ScribeWnd *app)
{
	App = app;

	Unread = false;
	LimitField = -1;

	int YPx = LSysFont->GetHeight() * 3;
	LRect r;
	if (LoadFromResource(IDD_FILTER_ITEMS, this, &r))
	{
		OnFolder();
	}

	GetCss(true)->Height(LCss::Len(LCss::LenPx, (float)YPx));
}

void LSearchView::OnFolder()
{
	LList *Lst = App->GetItemList();
	if (!Lst)
		return;

	LCombo *c;
	if (!GetViewById(IDC_LIMIT_TO, c))
		return;

	c->Empty();
	c->Insert("(none)");

	int Columns = Lst ? Lst->GetColumns() : 0;
	for (int i=0; i<Columns; i++)
	{
		auto col = Lst->ColumnAt(i);
		auto txt = col->Name();
		c->Insert(ValidStr(txt) ? txt : "-");
	}
}

bool LSearchView::TestThing(Thing *Thing)
{
	bool Status = true;
	LList *Lst = App->GetItemList();
	int Columns = Lst ? Lst->GetColumns() : 0; 

	if (Thing)
	{
		Mail *m = Thing->IsMail();

		if (Unread && m)
		{
			auto f = m->GetFlags();
			Status &= !(f & MAIL_READ);
		}

		if (Keywords.Length())
		{
			// Check all the columns
			LArray<unsigned> Unmatch;
			for (unsigned i=0; i<Keywords.Length(); i++)
				Unmatch.Add(i);

			for (int i=0; Unmatch.Length() > 0 && i < Columns; i++)
			{
				if (LimitField >= 0 && i != LimitField)
					continue;
			
				const char *Text = Thing->GetText(i);
				if (ValidStr(Text))
				{
					for (auto It = Unmatch.begin(); It != Unmatch.end();)
					{
						auto &k = Keywords[*It];
						bool Matches = false;
						if (strchr(k, '*') ||
							strchr(k, '?'))
							Matches = MatchStr(k, Text);
						else
							Matches = stristr(Text, k) != NULL;

						if (Matches)
							Unmatch.Delete(It);
						else
							It++;
					}
				}
			}

			Status = Unmatch.Length() == 0;
		}

		if (Colours.Length() && m)
		{
			auto c = m->GetMarkColour();
			Status = c > 0 && Colours.HasItem((uint32_t)c);
		}
	}

	return Status;
}

void LSearchView::OnPosChange()
{
	LTableLayout *t;
	if (GetViewById(IDC_TABLE, t))
		t->SetPos(GetClient());
}

void LSearchView::OnCreate()
{
	AttachChildren();
	Focus(true);
}

void LSearchView::OnPaint(LSurface *pDC)
{
	pDC->Colour(L_LOW);
	pDC->Rectangle();
}

int LSearchView::OnNotify(LViewI *c, LNotification n)
{
	bool Update = false;

	switch (c->GetId())
	{
		case IDC_LIMIT_TO:
		{
			auto v = c->Value() - 1;
			if (v != LimitField)
			{
				LimitField = (int)v;
				Update = true;
			}
			break;
		}
		case IDC_KEYWORDS:
		{
			if (n.Type == LNotifyEscapeKey)
			{
				LView *kw;
				if (!GetViewById(IDC_KEYWORDS, kw))
					return false;

				auto c = kw->Name();
				if (ValidStr(c))
				{
					kw->Name(NULL);
					Keywords.Length(0);
					Update = true;
				}
				else if (App)
				{
					App->SetCtrlValue(IDM_ITEM_FILTER, false);
					App->PostEvent(M_COMMAND, IDM_ITEM_FILTER);
				}
			}
			else if (n.Type == LNotifyReturnKey)
			{
				LString s = c->Name();
				if (!s.Length())
				{
					if (Keywords.Length())
					{
						Keywords.Length(0);
						Update = true;
					}
				}
				else
				{
					Keywords.Empty();
					for (char *c = s; *c; )
					{
						SkipWs(c);
						if (*c == '\'' || *c == '\"')
						{
							char delim = *c++;
							char *e = strchr(c, delim);
							if (!e)
							{
								Keywords.New() = c;
								break;
							}
							
							Keywords.New().Set(c, e - c);
							c = e + 1;
						}
						else
						{
							char *e = c;
							while (*e && !strchr(" \t\r\n", *e))
								e++;
							if (e <= c)
								break;
							Keywords.New().Set(c, e - c);
							if (!*e)
								break;
							c = e + 1;
						}
					}

					Update = true;
				}
			}
			break;
		}
		case IDC_UNREAD:
		{
			bool u = c->Value() != 0;
			if (u ^ Unread)
			{
				Unread = u;
				Update = true;
			}
			break;
		}
		case IDC_COLOUR:
		{
			LMarkColourSelect *Mcs = dynamic_cast<LMarkColourSelect*>(c);
			if (!Mcs)
				break;

			if (n.Type == LNotifyValueChanged)
			{
				auto a = Mcs->GetSelected();
				if (a.Length() != Colours.Length() ||
					memcmp(a.AddressOf(), Colours.AddressOf(), sizeof(uint32_t)*a.Length()))
				{
					Colours = a;
					Update = true;
				}
			}
			break;
		}
		case IDC_CLEAR_KEYWORDS:
		{
			SetCtrlName(IDC_KEYWORDS, NULL);
			SetCtrlValue(IDC_UNREAD, false);

			LMarkColourSelect *Mcs;
			if (GetViewById(IDC_COLOUR, Mcs))
				Mcs->SelectNone();
			Keywords.Length(0);
			Colours.Length(0);
			Unread = false;

			Update = true;
			break;
		}
	}

	LList *Lst = App->GetItemList();
	ScribeFolder *Folder = App->GetCurrentFolder();
	if (Folder && Lst && Update)
		Folder->Populate(dynamic_cast<ThingList*>(Lst));

	return 0;
}

void LSearchView::Focus(bool Foc)
{
	LViewI *v;
	if (GetViewById(IDC_KEYWORDS, v))
		v->Focus(Foc);
}
