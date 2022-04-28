#ifndef _REMOTE_H_
#define _REMOTE_H_

#define RNF_DIR			0x01

class RemoteNode;
class RemoteObjectStore;

//
// Client side object to receive event notifications
//
class ClientNodeEventSink
{
public:
	virtual void OnRemoteChange(RemoteNode *Node) {}
};

//
// There are 2 copies of this object, one owned by the local cache and
// one owned by the server. The replicate thread will create, delete and
// set the value of this to keep the 2 object stores in sync.
//
class RemoteNode
{
protected:
	ClientNodeEventSink *Es;
	RemoteObjectStore *Os;

public:
	RemoteNode();
	virtual ~RemoteNode();
	
	// Stats
	virtual int GetFlags() = 0;
	virtual bool SetFlags(int f) = 0;
	virtual char *GetUid() = 0;
	virtual bool SetUid(char *uid) = 0;
	virtual LDateTime &GetLastModified() = 0;
	
	// Heirarchy
	virtual int GetChildren() = 0;
	virtual RemoteNode *ChildAt(int i) = 0;
	virtual RemoteNode *ChildAt(char *Uid) = 0;
	virtual bool Insert(RemoteNode *n, int Index = -1) = 0;
	virtual bool Delete() = 0;
	
	// Data
	virtual int GetSize() = 0;
	virtual char *GetData() = 0;
	virtual bool SetData(char *Data, int Len = -1) = 0;
	
	// Events
	virtual void OnChange() { if (Es) Es->OnRemoteChange(this); }
	
	// Non-virtuals
	void SetClientNodeEventSink(ClientNodeEventSink *es) { Es = es; }
	RemoteObjectStore *GetObjectStore() { return Os; }
	bool IsDir() { return TestFlag(GetFlags(), RNF_DIR); }
	void IsDir(bool d) { SetFlags(GetFlags() | RNF_DIR); }
};

//
// The generalized object store API that both the local and remote parts
// implement.
//
class RemoteObjectStore
{
public:
	virtual ~RemoteObjectStore() {}
	
	virtual RemoteNode *GetRoot() = 0;
	virtual bool Serialize(ObjProperties *Props, bool Write) = 0;
	virtual bool GetDirtyNodes(List<RemoteNode> &Nodes) { return false; }
};

//
// The cache handles the storage of remote nodes while the server is offline.
// This speeds searching and provides and local copy of data.
//
class RemoteCache : public RemoteObjectStore
{
	class RemoteCachePrivate *d;

public:
	RemoteCache();
	~RemoteCache();

	RemoteNode *GetRoot();
	bool Serialize(ObjProperties *Props, bool Write);
};

//
// This returns the objects on the server when the replicator wants to sync
// the local contents to the remote contects.
//
class RemoteServer : public RemoteObjectStore
{
	class RemoteServerPrivate *d;

public:
	RemoteServer();
	~RemoteServer();

	RemoteNode *GetRoot();
	bool Serialize(ObjProperties *Props, bool Write);
};

//
// This thread constantly tries to keep the 2 object stores in sync. It will
// recover from any interruption and continue from where it last left off.
//
class RemoteReplicator : public LThread
{
	class RemoteReplicatorPrivate *d;

public:
	RemoteReplicator(RemoteObjectStore *a, RemoteObjectStore *b);
	~RemoteReplicator();

	int Main();
};

//
// This object mediates between Scribe and the RemoteCache.
// 
class RemoteFolder : public ScribeFolder
{
	class RemoteFolderPrivate *d;
	
public:
	RemoteFolder();
	~RemoteFolder();

	// ScribeFolder	
	bool ReadThing(Thing *t);
	bool WriteThing(Thing *t);
	bool DeleteThing(Thing *t);
	bool DeleteAllThings();
	bool LoadFolders();
	bool LoadThings(LViewI *Parent = 0);
	bool IsWriteable();

	void OnProperties(int Tab = -1);
	ScribeFolder *CreateSubDirectory(char *Name, int Type);
	void OnRename(char *NewName);
	void OnDelete();
	char *GetPath();
	ScribeFolder *GetSubFolder(char *Path);
	void Populate(ThingList *List);
	bool CanHaveSubFolders(ScribeItemTypes Type = MAGIC_MAIL);
};

#endif
