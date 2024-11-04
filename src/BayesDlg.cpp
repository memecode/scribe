#include "Scribe.h"
#include "resdefs.h"

///////////////////////////////////////////////////////////////////////
class BayesDlgPrivate
{
public:
	ScribeWnd *App;
};

///////////////////////////////////////////////////////////////////////
BayesDlg::BayesDlg(ScribeWnd *app) // : TabDialog(IDC_TABS, IDC_LAUNCH_HELP)
{
	d = new BayesDlgPrivate;
	d->App = app;
	SetParent(d->App);

	Map(OPT_BayesFilterMode, IDC_BAYES_MODE, GV_INT32);
	Map(OPT_BayesMoveTo, IDC_SUSPECT_FOLDER, GV_STRING);

	Map(OPT_BayesDeleteAttachments, IDC_BAYES_DELETE_ATTACHMENTS, GV_BOOL);
	Map(OPT_BayesDeleteOnServer, IDC_BAYES_DELETE_ON_SERVER, GV_BOOL);

	Map(OPT_BayesUserWhiteList, IDC_WHITELIST, GV_STRING);
	Map(OPT_BayesThreshold, IDC_BAYES_THRESHOLD, GV_STRING);
	Map(OPT_BayesIncremental, IDC_BAYES_INCREMENTAL, GV_BOOL);
	Map(OPT_BayesDebug, IDC_BAYES_DEBUG, GV_BOOL);
	Map(OPT_BayesHam, IDC_HAM);
	Map(OPT_BayesSpam, IDC_SPAM);
	Map(OPT_BayesFalsePositives, IDC_FALSE_POS);
	Map(OPT_BayesSetRead, IDC_BAYES_READ);

	Map(OPT_SpamFolder, IDC_SPAM_FOLDER);

	if (LoadFromResource(IDD_BAYES_SETTINGS))
	{
		MoveToCenter();
		Convert(app->GetOptions(), this, true);

		int Spam = (int) GetCtrlValue(IDC_SPAM);
		int FalseNeg = (int) GetCtrlValue(IDC_FALSE_NEG);
		
		char s[256];
		int Total = Spam + FalseNeg;
		if (Total)
		{
			sprintf_s(s, sizeof(s), "%.1f%%", (double)Spam*100/Total);
			SetCtrlName(IDC_EFFICIENCY, s);
		}
		else
		{
			SetCtrlName(IDC_EFFICIENCY, "n/a");
		}
	}
}

BayesDlg::~BayesDlg()
{
	DeleteObj(d);
}

int BayesDlg::OnNotify(LViewI *c, LNotification &n)
{
	switch (c->GetId())
	{
		case IDC_LAUNCH_HELP:
		{
			d->App->LaunchHelp("filters.html#bayes");
			break;
		}
		case IDC_SET_SUSPECT_FOLDER:
		{
			auto fd = new FolderDlg(this, d->App, MAGIC_MAIL);
			fd->DoModal([this, fd](auto dlg, auto ok)
			{
				if (ok)
					SetCtrlName(IDC_SUSPECT_FOLDER, fd->Get());
			});
			break;
		}
		case IDC_SET_SPAM_FOLDER:
		{
			auto fd = new FolderDlg(this, d->App, MAGIC_MAIL);
			fd->DoModal([this, fd](auto dlg, auto ok)
			{
				if (ok)
					SetCtrlName(IDC_SPAM_FOLDER, fd->Get());
			});
			break;
		}
		case IDOK:
		{
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

