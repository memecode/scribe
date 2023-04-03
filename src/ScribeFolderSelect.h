#ifndef __SCRIBE_FOLDER_SELECT_H
#define __SCRIBE_FOLDER_SELECT_H

class ScribeClass FolderDlg : public LDialog
{
	class FolderDlgPriv *d;

public:
	FolderDlg(	LViewI *Parent,
				ScribeWnd *App,
				int LimitToType = MAGIC_ANY,
				ScribeFolder *Root = 0,
				const char *InitialSelect = 0,
				bool AllowCreate = true,
				char *DefaultNewFolderName = 0,
				char *DialogMsg = 0);
	~FolderDlg();
	
	char *Get();

	/// DoModal returns TRUE on select, FALSE on cancel
	int OnNotify(LViewI *Ctrl, LNotification n);
};

#endif
