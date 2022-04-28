#ifndef _ADDRESS_BROWSE_H_

#include "lgi/common/PopupList.h"

ScribeFunc bool AddressBrowseLookup(ScribeWnd *App, LArray<BrowseItem*> &Items, LString s);

class AddressBrowse : public GPopupList<BrowseItem>
{
	class AddressBrowsePrivate *d;

public:
	AddressBrowse(ScribeWnd *app, LView *target, LList *recip, LViewI *setto);
	~AddressBrowse();

	LString ToString(BrowseItem *Obj);
	void OnSelect(BrowseItem *Obj);
	int OnNotify(LViewI *c, LNotification n);
};


#endif