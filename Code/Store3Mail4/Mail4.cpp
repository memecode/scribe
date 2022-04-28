//  Written by Matthew Allen, <fret@memecode.com>, Nov 2013.
#include "./leveldb/include/leveldb/db.h"

#include "Lgi.h"
#include "Mail4.h"

struct GMail4StorePriv
{
	LAutoString Folder;
	leveldb::DB *Db;
	
	GMail4StorePriv(const char *BaseFolder)
	{
		Folder.Reset(NewStr(BaseFolder));
		
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Folder, "Folders.mail4");
		
		leveldb::Options options;
		options.create_if_missing = true;
		leveldb::Status status = leveldb::DB::Open(options, p, &Db);
		LAssert(status.ok());
	}
};

GMail4Store::GMail4Store(char *Mail4Folder, LDataEventsI *Callback, bool Create)
{
	d = new GMail4StorePriv(Mail4Folder);
}

GMail4Store::~GMail4Store()
{
}

int64 GMail4Store::GetFolderId(char *Path)
{
	return -1;
}

LDataEventsI *GMail4Store::GetEvents()
{
	return NULL;
}

bool GMail4Store::OnIdle()
{
	return false;
}

bool GMail4Store::IsOk()
{
	return false;
}

int64 GMail4Store::GetInt(int id)
{
	return -1;
}

bool GMail4Store::SetInt(int id, int64 i)
{
	return false;
}

char *GMail4Store::GetStr(int id)
{
	return NULL;
}

uint64 GMail4Store::Size()
{
	return -1;
}

LDataI *GMail4Store::Create(int Type)
{
	return NULL;
}

LDataFolderI *GMail4Store::GetRoot(bool create)
{
	return NULL;
}

Store3Status GMail4Store::Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items)
{
	return Store3Error;
}

Store3Status GMail4Store::Delete(LArray<LDataI*> &Items, bool ToTrash)
{
	return Store3Error;
}

Store3Status GMail4Store::Change(LArray<LDataI*> &Items, int PropId, LVariant &Value)
{
	return Store3Error;
}

bool GMail4Store::Compact(LViewI *Parent, LDataPropI *Props)
{
	return false;
}

bool GMail4Store::Upgrade(LViewI *Parent, LDataPropI *Props)
{
	return false;
}

bool GMail4Store::SetFormat(LViewI *Parent, LDataPropI *Props)
{
	return false;
}

void GMail4Store::OnEvent(void *Param)
{
}

bool GMail4Store::Check(int Code, const char *Sql)
{
	return false;
}

LDataStoreI::StoreTrans GMail4Store::StartTransaction()
{
	return LDataStoreI::StoreTrans();
}

