#ifndef _WEBDAV_STORE_PRIV_H_
#define _WEBDAV_STORE_PRIV_H_

#include "lgi/common/WebDav.h"

extern void SaveAttr(LXmlTag *t, const char *attr, const char *val);

//////////////////////////////////////////////////////////////////////
class WebdavStore;

enum CmdType
{
	CmdNone,
	CmdFile,
	CmdSave,
	CmdDelete,
	CmdRefresh,
	CmdLoad,
};

struct WebdavEvent
{
	CmdType Type;
	LString Href, Data;
	WebdavFolder *Folder;
	bool Status;

	WebdavEvent()
	{
		Type = CmdNone;
		Folder = NULL;
		Status = false;
	}

	WebdavEvent(CmdType t, WebdavFolder *f, const char *h)
	{
		Type = t;
		Folder = f;
		Href = h;
		Status = false;
	}

	void Set(CmdType t, WebdavFolder *f, const char *h)
	{
		Type = t;
		Folder = f;
		Href = h;
		Status = false;
	}
};

class WebdavThread : public LThread, public LCancel, public LMutex
{
	WebdavStore *Src;
	WebdavFolder *Folder;
	WebdavStore::LRemote Remote;

	// Lock before access...
	LArray<WebdavEvent> Cmds;

public:
	// Lock before access...
	LArray<LWebdav::FileProps> Files;

	WebdavThread(WebdavStore *src, WebdavFolder *fld, LString Url);
	~WebdavThread();

	void PostStore(void *Param);
	Store3Status Save(LString Href, LString Data);
	Store3Status Delete(LString Href);
	void Refresh();
	void ReadDirectory(LWebdav &Wd);
	int Main();
};


#endif