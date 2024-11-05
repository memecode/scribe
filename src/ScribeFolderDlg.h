#ifndef __SCRIBE_FOLDER_DLG
#define __SCRIBE_FOLDER_DLG

class ScribeFolderDlg : public LDialog
{
	ScribeWnd *App;

public:
	bool Create;
	LAutoString FolderFile;

	ScribeFolderDlg(ScribeWnd *app);
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
};

#endif
