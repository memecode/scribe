#include "ScribeMapi.h"

GMapiThing::GMapiThing(GMapiStore *store)
{
	Store = store;
	MapiMsg = NULL;
	Parent = NULL;
	IsDirty = false;
}

GMapiThing::~GMapiThing()
{
	if (Store && IsDirty)
		Store->Dirty.Delete(this);
	ReleaseHandle();		
}

void GMapiThing::ReleaseHandle()
{
	if (MapiMsg)
	{
		MapiMsg->Release();
		MapiMsg = NULL;
	}
}

void GMapiThing::SetDirty()
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
