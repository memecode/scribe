#ifndef __SCRIBE_FOLDER_SELECT_H
#define __SCRIBE_FOLDER_SELECT_H

class ScribeClass FolderDlg : public LDialog
{
	class FolderDlgPriv *d;

public:
	FolderDlg(	LViewI *Parent,
				ScribeWnd *App,
				int LimitToType = MAGIC_ANY,
				ScribeFolder *Root = NULL,
				const char *InitialSelect = NULL,
				bool AllowCreate = true,
				char *DefaultNewFolderName = NULL,
				char *DialogMsg = NULL);
	~FolderDlg();
	
	char *Get();

	/// DoModal returns TRUE on select, FALSE on cancel
	int OnNotify(LViewI *Ctrl, LNotification n);
};

#endif
