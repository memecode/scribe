#include "lgi/common/Lgi.h"

#include "context.h"

FolderMeta::FolderMeta(LString fullPath, LStream *logger) :
	log(logger),
	path(fullPath),
	uidMap(0, INVALID)
{
}

FolderMeta::~FolderMeta()
{
	save();
}

void FolderMeta::save()
{
	if (dirty)
	{
		if (serialize(true))
			dirty = false;
	}
}

bool FolderMeta::serialize(bool write)
{
	LFile f(path, write ? O_WRITE : O_READ);
	if (write)
	{
		for (auto p: uidMap)
			f.Print("uid,%s,%i\n", p.key, p.value);
		log->Print("Wrote %i msg->uid maps to '%s'\n", (int)uidMap.Length(), path.Get());
	}
	else
	{
		uidMap.Empty();
		nextUid = 0;

		if (!f)
			return false;

		auto lines = f.Read().SplitDelimit("\n");
		for (auto &l: lines)
		{
			auto p = l.SplitDelimit(",");
			if (p[0].Equals("uid"))
			{
				if (p.Length() == 3)
				{
					auto uid = (int)p[2].Int();
					uidMap.Add(p[1], uid);
					nextUid = MAX(uid, nextUid);
				}
				else LAssert(!"invalid token count");
			}
		}

		log->Print("Read %i msg->uid maps from '%s'\n", (int)uidMap.Length(), path.Get());
	}
	return true;
}

int FolderMeta::getUid(const char *msgId)
{
	auto uid = uidMap.Find(msgId);
	if (uid == INVALID)
	{
		uid = ++nextUid;
		uidMap.Add(msgId, uid);
		dirty = true;
	}
	return uid;
}
