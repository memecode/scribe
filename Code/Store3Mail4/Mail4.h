//  Written by Matthew Allen, <fret@memecode.com>, Nov 2013.
#ifndef _MAIL4_H_
#define _MAIL4_H_

#include "Lgi.h"
#include "Store3.h"

class GMail4Store : public LDataStoreI
{
	struct GMail4StorePriv *d;

public:
	GMail4Store(char *Mail4Folder, LDataEventsI *Callback, bool Create);
	~GMail4Store();

	int64 GetFolderId(char *Path);
	LDataEventsI *GetEvents();
	bool OnIdle();
	bool IsOk();
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	char *GetStr(int id);
	uint64 Size();
	LDataI *Create(int Type);
	LDataFolderI *GetRoot(bool create = false);
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items);
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash);
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value);
	bool Compact(LViewI *Parent, LDataPropI *Props);
	bool Upgrade(LViewI *Parent, LDataPropI *Props);
	bool SetFormat(LViewI *Parent, LDataPropI *Props);
	void OnEvent(void *Param);
	bool Check(int Code, const char *Sql);
	// GMail3Def *GetFields(const char *t);

	StoreTrans StartTransaction();
};

#endif