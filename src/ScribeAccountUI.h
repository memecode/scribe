#ifndef __SCRIBE_ACCOUNT_DLG_H
#define __SCRIBE_ACCOUNT_DLG_H

class AccountDlg : public TabDialog
{
	ScribeWnd *App = NULL;
	ScribeAccount *Account = NULL;
	LList *Plugins = NULL;
	class LEdit *SendServer = NULL, *ReceiveServer = NULL;
	class LEdit *SendPort = NULL, *ReceivePort = NULL;
	bool UseGoogle = false;

	void UpdateDefaultPort(bool Send);
	void FillWithCharsets(int id, bool All);
	void OnUseGoogle();

public:
	AccountDlg(LView *p, ScribeWnd *app, ScribeAccount *a, int Tab);
	int OnNotify(LViewI *c, LNotification n);
	void OnCreate();
};

#endif

