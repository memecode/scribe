#ifndef _MANAGE_MAIL_STORES_H_
#define _MANAGE_MAIL_STORES_H_

#include "lgi/common/XmlTreeUi.h"

class ManageMailStores : public LDialog, public LXmlTreeUi
{
	ScribeWnd *App;
	class LList *Lst = nullptr;
	
	LMailStore *GetCurrentMailStore();

public:
	LXmlTag Options;

	ManageMailStores(ScribeWnd *app);
	~ManageMailStores();

	void OnItemSelect();
	int OnNotify(LViewI *c, const LNotification &n) override;
};


#endif