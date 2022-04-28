#include "Scribe.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "CalendarView.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

//////////////////////////////////////////////////////////////////////
WebdavThread::WebdavThread(WebdavStore *src, WebdavFolder *fld, LString Url) : LThread("RemoteCal.Thread"), LMutex("RemoteCal.Mutex")
{
	Src = src;
	Folder = fld;
	Remote = src->Remote;
	Remote.Url = Url;

	Run();
}

WebdavThread::~WebdavThread()
{
	Cancel();
	while (!IsExited())
		LSleep(1);
}

void WebdavThread::PostStore(void *Param)
{
	if (Src->Callback)
		Src->Callback->Post(Src, Param);
	else
		LAssert(0);
}

Store3Status WebdavThread::Save(LString Href, LString Data)
{
	if (!Lock(_FL))
		return Store3Error;
		
	WebdavEvent c;
	c.Type = CmdSave;
	c.Href = Href.Get(); // Copy over thread boundary
	c.Data = Data.Get();

	Cmds.AddAt(0, c); // Add at the start to get done earlier than later...

	Unlock();

	return Store3Delayed;
}

Store3Status WebdavThread::Delete(LString Href)
{
	if (!Lock(_FL))
		return Store3Error;
		
	auto &c = Cmds.New();
	c.Type = CmdDelete;
	c.Href = Href.Get(); // Copy over thread boundary
	Unlock();

	return Store3Delayed;
}

void WebdavThread::Refresh()
{
	if (!Lock(_FL))
		return;		
	auto &c = Cmds.New();
	c.Type = CmdRefresh;
	Unlock();
}

void WebdavThread::ReadDirectory(LWebdav &Wd)
{
	if (Lock(_FL))
	{
		Files.Empty();
		Unlock();
	}

	// Read the directory listing...
	LArray<LWebdav::FileProps> Local;
	if (Wd.PropFind(Local, "/"))
	{
		// Read all the file contents
		for (auto f: Local)
		{
			if (IsCancelled())
				break;

			auto Ct = f.GetContentType();
			if (Ct.Equals("text/calendar") ||
				Ct.Equals("text/vcard"))
			{
				// Put a bunch of tasks into the queue to download them all...
				if (Lock(_FL))
				{
					// Create a new download cmd...
					auto &c = Cmds.New();
					c.Type = CmdLoad;
					c.Href = f.Href;
					c.Data = f.Data;

					// Copy out to the main store..
					Files.New() = f;

					Unlock();
				}
				else LgiTrace("%s:%i - Can't lock.\n", _FL);
			}
			else LgiTrace("%s:%i - Unknown mime '%s'\n", _FL, Ct.Get());
		}
	}
}

int WebdavThread::Main()
{
	LWebdav Wd(Remote.Url, Remote.User, Remote.Pass, this);
	// LString::Array Opts = Wd.GetOptions("/");
		
	ReadDirectory(Wd);
	
	while (!IsCancelled())
	{
		LArray<WebdavEvent> Process;
		if (Lock(_FL))
		{
			Process.Swap(Cmds);
			Unlock();
		}
		for (auto &c: Process)
		{
			int Status = -1;

			switch (c.Type)
			{
				case CmdDelete:
					Status = Wd.Delete(c.Href);
					break;
				case CmdSave:
					Status = Wd.Put(c.Href, c.Data);
					break;
				case CmdRefresh:
					ReadDirectory(Wd);
					break;
				case CmdLoad:
				{
					Wd.Get(c.Href, c.Data);

					auto *e = new WebdavEvent(CmdFile, Folder, c.Href);
					if (e)
					{
						e->Data = c.Data.Get();
						e->Status = true;
						PostStore(e);
					}
					break;
				}
				default:
					LAssert(!"Not impl.");
					break;
			}

			if (Status >= 0)
			{
				auto e = new WebdavEvent(c.Type, Folder, c.Href);
				e->Status = Status > 0;
				PostStore(e);
			}
			if (IsCancelled())
				break;
		}

		LSleep(50);
	}
	return 0;
}

