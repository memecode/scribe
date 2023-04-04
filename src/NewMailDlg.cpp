#include "Scribe.h"
#include "resdefs.h"

/////////////////////////////////////////////////////////////
class LNewMailItem : public LListItem
{
	Mail *m;
	LNewMailDlg *Parent;

public:
	LNewMailItem(LNewMailDlg *parent, Mail *mail)
	{
		m = mail;
		Parent = parent;
	}

	Mail *GetMail() { return m; }

	const char *GetText(int i)
	{
		if (m)
		{
			switch (i)
			{
				case 0:
				{
					if (m->GetFromStr(FIELD_NAME))
					{
						return m->GetFromStr(FIELD_NAME);
					}
					else
					{
						return m->GetFromStr(FIELD_EMAIL);
					}
					break;
				}
				case 1:
				{
					return m->GetSubject();
					break;
				}
			}

			return 0;
		}

		return (char*)"<no mail>";
	}

	void OnMouseClick(LMouse &ms)
	{
		if (ms.Left() && ms.Double())
		{
			m->DoUI();
			
			if (Parent)
				Parent->PostEvent(M_CHANGE, IDOK);
		}
	}
};

/////////////////////////////////////////////////////////////
LNewMailDlg::LNewMailDlg(ScribeWnd *app, LNewMailDlg **ptr)
{
	App = app;
	Ptr = ptr;

	if (LoadFromResource(IDD_NEW_MAIL_NOTIFY))
	{
		MoveToCenter();
		DoModeless();
	}
}

LNewMailDlg::~LNewMailDlg()
{
	if (App->Lock(_FL))
	{
		if (Ptr)
		{
			*Ptr = 0;
		}

		App->Unlock();
	}
}

void LNewMailDlg::AddThings(List<Mail> *NewThings)
{
	LList *l;
	if (GetViewById(IDC_NEW_MAIL, l) && NewThings)
	{
		l->MultiSelect(true);

		LNewMailItem *Select = 0;
		for (auto m: *NewThings)
		{
			bool Has = false;

			for (auto li : *l)
			{
				LNewMailItem *mi = dynamic_cast<LNewMailItem*>(li);
				if (mi->GetMail() == m)
				{
					Has = true;
				}
			}

			if (!Has)
			{
				LNewMailItem *New = new LNewMailItem(this, m);
				if (New)
				{
					if (!Select)
					{
						Select = New;
					}

					l->Insert(New);
				}
			}
		}

		if (Select)
		{
			Select->Select(true);
		}
	}
}

int LNewMailDlg::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDC_OPEN:
		{
			LList *l;
			if (GetViewById(IDC_NEW_MAIL, l))
			{
				List<LNewMailItem> a;
				l->GetAll(a);
				for (auto i: a)
				{
					Mail *m = i->GetMail();
					if (m)
					{
						m->DoUI();
					}
				}
			}
			
			// fall through
		}
		case IDOK:
		{
			EndModeless();
			break;
		}
	}

	return 0;
}

	
