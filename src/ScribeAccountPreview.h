#ifndef __SCRIBE_ACCOUNT_PREVIEW_H
#define __SCRIBE_ACCOUNT_PREVIEW_H

#include "lgi/common/ListItemCheckBox.h"

class AccountMessage : public LListItem
{
public:
	// Data
	ScribeAccount *To = NULL;
	int Index = -1;
	bool New = true;
	int64 Size = 0;
	LString From;
	LString Subject;
	LString ServerUid;
	LDateTime Date;
	bool Attachments = false;

	LListItemCheckBox *Download = NULL;
	LListItemCheckBox *Delete = NULL;

	// Constructor
	AccountMessage(ScribeAccount *to);

	// Item
	const char *GetText(int i);
	int GetImage(int Flags = 0);

	// Methods
	ReceiveAction GetAction();
};

class ScribeAccountPreview :
	public LWindow,
	public LResourceLoad
{
	class ScribeAccountPreviewPrivate *d;
	
	void SetSort(int s);
	void GetMsgs(List<AccountMessage> &All);
	void Enable(bool e);

public:
	ScribeAccountPreview(ScribeWnd *app, LArray<ScribeAccount*> &Lst);
	~ScribeAccountPreview();

	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
	void OnPulse();
	void OnPaint(LSurface *pDC);
};

#endif
