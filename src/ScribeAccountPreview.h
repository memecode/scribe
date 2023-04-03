#ifndef __SCRIBE_ACCOUNT_PREVIEW_H
#define __SCRIBE_ACCOUNT_PREVIEW_H

#include "lgi/common/ListItemCheckBox.h"

class AccountMessage : public LListItem
{
public:
	// Data
	ScribeAccount *To;
	int Index;
	bool New;
	int64 Size;
	char *From;
	char *Subject;
	char *ServerUid;
	LDateTime Date;
	bool Attachments;

	LListItemCheckBox *Download;
	LListItemCheckBox *Delete;

	// Constructor
	AccountMessage(ScribeAccount *to);
	~AccountMessage();

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

	int OnNotify(LViewI *Ctrl, LNotification n);
	void OnPulse();
	void OnPaint(LSurface *pDC);
};

#endif
