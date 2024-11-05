

#ifndef __G_NEW_MAIL_DLG_H
#define __G_NEW_MAIL_DLG_H

class LNewMailDlg : public LDialog
{
	LNewMailDlg **Ptr;
	ScribeWnd *App;
	List<Thing> Things;

public:
	LNewMailDlg(ScribeWnd *app, LNewMailDlg **ptr);
	~LNewMailDlg();

	void AddThings(List<Mail> *NewThings);

	int OnNotify(LViewI *c, const LNotification &n) override;
};

#endif
