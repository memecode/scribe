#ifndef _ADDRESS_BROWSE_H_

#include "lgi/common/PopupList.h"

ScribeFunc bool AddressBrowseLookup(ScribeWnd *App, LArray<BrowseItem*> &Items, LString s);

class AddressBrowse : public LPopupList<BrowseItem>
{
	class AddressBrowsePrivate *d;

public:
	AddressBrowse(ScribeWnd *app, LView *target, LList *recip, LViewI *setto);
	~AddressBrowse();

	LString ToString(BrowseItem *Obj) override;
	void OnSelect(BrowseItem *Obj) override;
	int OnNotify(LViewI *c, const LNotification &n) override;
};


#endif
