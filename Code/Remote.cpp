#include "Scribe.h"
#include "Remote.h"

///////////////////////////////////////////////////////////////////////////////////////////
class RemoteReplicatorPrivate
{
public:
	RemoteObjectStore *a;
	RemoteObjectStore *b;
	bool Loop;
	
	RemoteReplicatorPrivate()
	{
		a = b = 0;
		Loop = true;
	}
	
	~RemoteReplicatorPrivate()
	{
	}
};

RemoteReplicator::RemoteReplicator(RemoteObjectStore *a, RemoteObjectStore *b)
{
	d = new RemoteReplicatorPrivate;
	d->a = a;
	d->b = b;
}

RemoteReplicator::~RemoteReplicator()
{
	// Shut the thread down safely
	d->Loop = false;
	while (!IsExited())
	{
		LSleep(5);
	}
	
	DeleteObj(d);
}

int RemoteReplicator::Main()
{
	while (d->Loop)
	{
		LSleep(100);
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////
class RemoteFolderPrivate
{
public:
};

RemoteFolder::RemoteFolder() : ScribeFolder()
{
	d = new RemoteFolderPrivate;
}

RemoteFolder::~RemoteFolder()
{
	DeleteObj(d);
}

bool RemoteFolder::ReadThing(Thing *t)
{
	return false;
}

bool RemoteFolder::WriteThing(Thing *t)
{
	return false;
}

bool RemoteFolder::DeleteThing(Thing *t)
{
	return false;
}

bool RemoteFolder::DeleteAllThings()
{
	return false;
}

bool RemoteFolder::LoadFolders()
{
	return false;
}

bool RemoteFolder::LoadThings(LViewI *Parent)
{
	return false;
}

bool RemoteFolder::IsWriteable()
{
	return false;
}

void RemoteFolder::OnProperties(int Tab)
{
}

ScribeFolder *RemoteFolder::CreateSubDirectory(char *Name, int Type)
{
	return 0;
}

void RemoteFolder::OnRename(char *NewName)
{
}

void RemoteFolder::OnDelete()
{
}

char *RemoteFolder::GetPath()
{
	return 0;
}

ScribeFolder *RemoteFolder::GetSubFolder(char *Path)
{
	return 0;
}

void RemoteFolder::Populate(ThingList *l)
{
}

bool RemoteFolder::CanHaveSubFolders(ScribeItemTypes Type)
{
	return false;
}

