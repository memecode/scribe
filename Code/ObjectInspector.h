#ifndef _OBJECT_INSPECTOR_H_
#define _OBJECT_INSPECTOR_H_

#include "lgi/common/Box.h"
#include "lgi/common/Tree.h"
#include "lgi/common/List.h"
#include "lgi/common/TextLog.h"

class ObjectInspector : public LWindow
{
	LBox *Box;
	LTree *Tree;
	Thing *m;

public:
	LTextLog *Txt;
	LList *Lst;

	ObjectInspector(LViewI *Parent, Thing *obj);
	void OnPosChange();
};

#endif
