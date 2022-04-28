

#ifndef __G_NEW_MAIL_DLG_H
#define __G_NEW_MAIL_DLG_H

class GNewMailDlg : public LDialog
{
	GNewMailDlg **Ptr;
	ScribeWnd *App;
	List<Thing> Things;

public:
	GNewMailDlg(ScribeWnd *app, GNewMailDlg **ptr);
	~GNewMailDlg();

	void AddThings(List<Mail> *NewThings);

	int OnNotify(LViewI *c, LNotification n);
};

#endif
