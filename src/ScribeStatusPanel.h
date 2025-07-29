
#ifndef __STATUSPANEL_H
#define __STATUSPANEL_H

#include "lgi/common/FileTransferProgress.h"
#include "lgi/common/TableLayout.h"

// Status item
class AccountStatusItem : public LListItem
{
	char Buf[32];

public:
	AccountStatusPanel *Panel = nullptr;
	ScribeAccount *Account = nullptr;
	LImageList *ImgLst = nullptr;
	int State = 0;

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
	LImageList *ImgLst = nullptr;
	List<ScribeAccount> *Accounts = nullptr;
	size_t PrevAccounts = 0;
	ScribeAccount *Current = nullptr;
	AccountStatusItem *CurStatusItem = nullptr;

	// Controls
	LViewI *AccountTbl = nullptr;
	LList *Lst = nullptr;

	LTableLayout *ProgressTbl = nullptr;
	LProgressView *Total = nullptr;
	LProgressView *Sub = nullptr;

	class LTabView *Log = nullptr;

	// Methods
	int AccountStatus(Accountlet *Acc);
	int CalcWidth() override;
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
	void OnPosChange() override;

	// Window
	void OnPaint(LSurface *pDC) override;
	void OnPulse() override;
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
};

#endif
