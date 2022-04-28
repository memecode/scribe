#ifndef __SCRIBE_ACCOUNT_DLG_H
#define __SCRIBE_ACCOUNT_DLG_H

class AccountDlg : public TabDialog
{
	ScribeWnd *App;
	ScribeAccount *Account;
	LList *Plugins;
	class LEdit *SendServer, *ReceiveServer;
	class LEdit *SendPort, *ReceivePort;

	void UpdateDefaultPort(bool Send);

public:
	AccountDlg(LView *p, ScribeWnd *app, ScribeAccount *a, int Tab);
	int OnNotify(LViewI *c, LNotification n);
	void OnCreate();
};

#endif

