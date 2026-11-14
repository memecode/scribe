#include "Scribe.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "CalendarView.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

//////////////////////////////////////////////////////////////////////
WebdavThread::WebdavThread(WebdavStore *src, WebdavFolder *fld, LString Url) :
	LThread("RemoteCal.Th"),
	LMutex("RemoteCal.Mutex")
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
	WaitForExit();
}

void WebdavThread::PostStore(void *Param)
{
	if (Src)
		Src->Post(Src, Param);
	else
		LAssert(0);
}

Store3Status WebdavThread::Save(LString Href, LString Data)
{
	if (!Href)
	{
		LAssert(!"No address.");
		return Store3Error;
	}

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

bool WebdavThread::IsItem(const LWebdav::FileProps &f)
{
	auto Ct = f.GetContentType();
	if (Ct.Equals("text/calendar") || Ct.Equals("text/vcard"))
		return true;

	// Not every server reports the content type for each item...
	return Folder->Extension && f.Href.EndsWith(Folder->Extension);
}

const char *WebdavThread::Multiget()
{
	return Folder->GetInt(FIELD_FOLDER_TYPE) == MAGIC_CONTACT
		? "addressbook-multiget"
		: "calendar-multiget";
}

bool WebdavThread::Unchanged(LArray<LWebdav::FileProps> &Known, const LWebdav::FileProps &f)
{
	for (auto &k: Known)
		if (k.Href.Equals(f.Href))
			// Without an etag there is no way to tell if the content changed, and
			// anything we have not fetched yet has to be fetched now...
			return k.Data && f.Tag && k.Tag.Equals(f.Tag);

	return false;
}

void WebdavThread::ReadDirectory(LWebdav &Wd)
{
	LArray<LWebdav::FileProps> Local;
	if (!Wd.PropFind(Local, "/"))
		return;

	LArray<LWebdav::FileProps> Known;
	if (!Lock(_FL))
		return;

	for (auto &k: Files)
		Known.New() = k.Copy();

	Unlock();

	// Work out which files we need the content for...
	LArray<LWebdav::FileProps> Next;
	LArray<size_t> Need;

	for (auto &f: Local)
	{
		if (!IsItem(f))
		{
			LgiTrace("%s:%i - Unknown mime '%s'\n", _FL, f.GetContentType().Get());
			continue;
		}

		auto &e = Next.New();
		e = f.Copy();

		if (Unchanged(Known, f))
		{
			// Carry over the content we already have...
			for (auto &k: Known)
				if (k.Href.Equals(e.Href))
				{
					e.Data = k.Data.Get();
					break;
				}
		}
		else Need.Add(Next.Length() - 1);
	}

	// The bulk fetch: ask for all the content we need with a single REPORT
	// request rather than a GET request per file...
	LString::Array Hrefs;
	for (auto idx: Need)
		Hrefs.Add(Next[idx].Href.Get());

	LArray<LWebdav::FileProps> Bulk;
	if (Hrefs.Length() && Wd.SupportsReport("/"))
		if (!Wd.Report(Bulk, "/", Multiget(), Hrefs))
			LgiTrace("%s:%i - Bulk fetch returned no content.\n", _FL);

	for (auto idx: Need)
	{
		if (IsCancelled())
			break;

		auto &e = Next[idx];

		for (auto &b: Bulk)
			if (b.Href.Equals(e.Href))
			{
				e.Data = b.Data.Get();
				break;
			}

		// The server can not bulk fetch, so get this file on its own...
		if (!e.Data && !Wd.Get(e.Href, e.Data))
			LgiTrace("%s:%i - Failed to load '%s'\n", _FL, e.Href.Get());
	}

	// Anything the server no longer has...
	LString::Array Gone;
	for (auto &k: Known)
	{
		bool Present = false;
		for (auto &f: Local)
			if (f.Href.Equals(k.Href))
			{
				Present = true;
				break;
			}
		if (!Present)
			Gone.Add(k.Href.Get());
	}

	for (auto idx: Need)
	{
		auto &e = Next[idx];
		if (!e.Data)
			continue;

		auto *ev = new WebdavEvent(CmdFile, Folder, e.Href);
		if (ev)
		{
			ev->Data = e.Data.Get();
			ev->Status = true;
			PostStore(ev);
		}
	}

	for (auto &h: Gone)
	{
		auto *ev = new WebdavEvent(CmdDelete, Folder, h);
		if (ev)
		{
			ev->Status = true;
			PostStore(ev);
		}
	}

	// Publish the listing once the events are posted, as the event posting uses
	// the indexes into Next...
	if (Lock(_FL))
	{
		Files.Swap(Next);
		Unlock();
	}
}

int WebdavThread::Main()
{
	LWebdav Wd(Remote.Url, Remote.User, Remote.Pass, this);
		
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

