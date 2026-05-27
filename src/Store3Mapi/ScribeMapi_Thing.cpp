#include "ScribeMapi.h"

LMapiThing::LMapiThing(LMapiStore *store)
{
	Store = store;
}

LMapiThing::~LMapiThing()
{
	if (Store && IsDirty)
		Store->Dirty.Delete(this);
	ReleaseHandle();		
}

void LMapiThing::ReleaseHandle()
{
	if (MapiMsg)
	{
		MapiMsg->Release();
		MapiMsg = NULL;
	}
}

void LMapiThing::SetDirty()
{
	if (Store)
	{
		if (!IsDirty)
		{
			IsDirty = true;
			Store->Dirty.Add(this);
		}
	}
	else LAssert(0);
}
