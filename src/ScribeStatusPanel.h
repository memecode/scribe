
#ifndef __STATUSPANEL_H
#define __STATUSPANEL_H

#include "lgi/common/FileTransferProgress.h"
#include "lgi/common/TableLayout.h"

// Status item
class AccountStatusItem : public LListItem
{
	char Buf[32];

public:
	AccountStatusPanel *Panel;
	ScribeAccount *Account;
	LImageList *ImgLst;
	int State;

	AccountStatusItem(AccountStatusPanel *panel, ScribeAccount *account, LImageList *imglst);
	~AccountStatusItem();

	const char *GetText(int Col);
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c);
	void OnPulse();
	void OnMouseClick(LMouse &m);
	int Compare(LListItem *To, ssize_t Field = 0);
};

// Reports the status of client transaction, SMTP, POP3, IMAP4 etc
class AccountStatusPanel :
	public ScribePanel,
	public LResourceLoad
{
	friend class AccountStatusItem;

	// Data
	LImageList *ImgLst;
	List<ScribeAccount> *Accounts;
	size_t PrevAccounts;
	ScribeAccount *Current;
	AccountStatusItem *CurStatusItem;

	// Controls
	LViewI *AccountTbl;
	LList *Lst;

	LTableLayout *ProgressTbl;
	LProgressView *Total;
	LProgressView *Sub;

	class LTabView *Log;

	// Methods
	int AccountStatus(Accountlet *Acc);
	int CalcWidth();
	bool _Lock();
	void _Unlock();

public:
	AccountStatusPanel(ScribeWnd *app, LImageList *imglst);
	~AccountStatusPanel();

	// Impl
	void OnAccountSelect(AccountStatusItem *Item);
	void OnAccountListChange();
	void Empty();
	LXmlTag *GetOptions();
	void SetDataRate(int Percent);
	void OnPosChange();

	// Window
	void OnPaint(LSurface *pDC);
	void OnPulse();
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
};

#endif
