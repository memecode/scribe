#ifndef __SCRIBE_FOLDER_SELECT_H
#define __SCRIBE_FOLDER_SELECT_H

// Dialog to select a folder
class ScribeClass FolderDlg : public LDialog
{
	class FolderDlgPriv *d;

public:
	FolderDlg(	LViewI *Parent,
				ScribeWnd *App,
				int LimitToType = MAGIC_ANY,
				ScribeFolder *Root = nullptr,
				const char *InitialSelect = nullptr,
				bool AllowCreate = true,
				char *DefaultNewFolderName = nullptr,
				char *DialogMsg = nullptr);
	~FolderDlg();
	
	LString Get();

	/// DoModal returns TRUE on select, FALSE on cancel
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
	bool OnViewKey(LView *v, LKey &k);
};

#endif
