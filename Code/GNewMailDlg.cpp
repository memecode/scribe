#include "Scribe.h"
#include "../Resources/resdefs.h"

/////////////////////////////////////////////////////////////
class GNewMailItem : public LListItem
{
	Mail *m;
	GNewMailDlg *Parent;

public:
	GNewMailItem(GNewMailDlg *parent, Mail *mail)
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
GNewMailDlg::GNewMailDlg(ScribeWnd *app, GNewMailDlg **ptr)
{
	App = app;
	Ptr = ptr;

	if (LoadFromResource(IDD_NEW_MAIL_NOTIFY))
	{
		MoveToCenter();
		DoModeless();
	}
}

GNewMailDlg::~GNewMailDlg()
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

void GNewMailDlg::AddThings(List<Mail> *NewThings)
{
	LList *l;
	if (GetViewById(IDC_NEW_MAIL, l) && NewThings)
	{
		l->MultiSelect(true);

		GNewMailItem *Select = 0;
		for (auto m: *NewThings)
		{
			bool Has = false;

			for (auto li : *l)
			{
				GNewMailItem *mi = dynamic_cast<GNewMailItem*>(li);
				if (mi->GetMail() == m)
				{
					Has = true;
				}
			}

			if (!Has)
			{
				GNewMailItem *New = new GNewMailItem(this, m);
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

int GNewMailDlg::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDC_OPEN:
		{
			LList *l;
			if (GetViewById(IDC_NEW_MAIL, l))
			{
				List<GNewMailItem> a;
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

	
