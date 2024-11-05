#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"

///////////////////////////////////////////////////////////////////////
class SecurityDlgPrivate
{
public:
	ScribeWnd *App;
	ScribePassword *Pwd;
	bool HasPsw;

	SecurityDlgPrivate()
	{
		Pwd = 0;
		HasPsw = false;
	}

	~SecurityDlgPrivate()
	{
		DeleteObj(Pwd);
	}
};

///////////////////////////////////////////////////////////////////////
SecurityDlg::SecurityDlg(ScribeWnd *app)
{
	d = new SecurityDlgPrivate;
	d->App = app;
	SetParent(d->App);

	d->Pwd = new ScribePassword(d->App->GetOptions(),
								OPT_UserPermPassword,
								IDC_USER_LEVEL_PSW,
								IDC_USER_PASS,
								IDC_USER_PASS_CONFIRM);

	Map(OPT_AccPermRead, IDC_ACCOUNTS_READ, GV_INT32);
	Map(OPT_AccPermWrite, IDC_ACCOUNTS_WRITE, GV_INT32);

	LVariant v;
	if (LoadFromResource(IDD_SECURITY))
	{
		MoveToCenter();

		bool ReadAdmin = false;
		bool WriteAdmin = false;
		/*
		char *p = GetScribeAccountPerm("Read");
		bool ReadAdmin = p && _stricmp(p, "Admin") == 0;
		if (ReadAdmin)
		{
			d->App->GetOptions()->SetValue(OPT_AccPermRead, v = (int) PermRequireAdmin);
		}

		p = GetScribeAccountPerm("Write");
		bool WriteAdmin = p && _stricmp(p, "Admin") == 0;
		if (WriteAdmin)
		{
			d->App->GetOptions()->SetValue(OPT_AccPermWrite, v = (int) PermRequireAdmin);
		}
		*/

		Convert(d->App->GetOptions(), this, true);
		d->Pwd->Load(this);

		if (ReadAdmin)
		{
			SetCtrlEnabled(IDC_APR_NONE, false);
			SetCtrlEnabled(IDC_APR_USER, false);
			SetCtrlValue(IDC_APR_ADMIN, true);
		}
		else
		{
			SetCtrlEnabled(IDC_APR_ADMIN, false);
		}

		if (WriteAdmin)
		{
			SetCtrlEnabled(IDC_APW_NONE, false);
			SetCtrlEnabled(IDC_APW_USER, false);
			SetCtrlValue(IDC_APW_ADMIN, true);
		}
		else
		{
			SetCtrlEnabled(IDC_APW_ADMIN, false);
		}
		
		d->HasPsw = GetCtrlValue(IDC_USER_LEVEL_PSW) != 0;
	}
}

SecurityDlg::~SecurityDlg()
{
	DeleteObj(d);
}

int SecurityDlg::OnNotify(LViewI *c, const LNotification &n)
{
	d->Pwd->OnNotify(c, n);

	switch (c->GetId())
	{
		case IDOK:
		{
			// Warn about removing security
			if (d->HasPsw && !GetCtrlValue(IDC_USER_LEVEL_PSW))
			{
				if (LgiMsg(this, LLoadString(IDS_WARN_NO_SECURITY), AppName, MB_YESNO) == IDNO)
				{
					break;
				}
			}
			
			if (!d->Pwd->Save()) break;
			Convert(d->App->GetOptions(), this, false);
			// fall thru
		}
		case IDCANCEL:
		{
			EndModal(c->GetId() == IDOK);
			break;
		}
	}

	return 0;
}

