#include "Scribe.h"
#include "Remote.h"
#include "StoreCommon.h"

/////////////////////////////////////////////////////////////////////////////////////
#define MAGIC_CACHE_NODE		0xaaee0001

#define FIELD_NODE_FLAGS		1
#define FIELD_NODE_UID			2
#define FIELD_NODE_MIMETYPE		3
#define FIELD_NODE_LASTMOD		4

class RemoteCacheNode :
	public RemoteNode,
	public StorageObj
{
	int DataPos;

	int Flags;
	int64 Size;
	char *Data;
	char *Uid;
	char *MimeType;
	LDateTime LastMod;

public:
	RemoteCacheNode()
	{
		Flags = 0;
		Size = 0;
		Uid = 0;
		Data = 0;
		DataPos = 0;
	}

	~RemoteCacheNode()
	{
		DeleteArray(Uid);
		DeleteArray(Data);
	}

	// StorageObj Impl
	int Type()
	{
		return MAGIC_CACHE_NODE;
	}
	
	int Sizeof()
	{
		return	sizeof(int32) +	// magic
				sizeof(Size) +
				Size +
				SizeIntField(Flags) +
				SizeObjField(LastMod) +
				(Uid ? SizeStrField(Uid) : 0) +
				(MimeType ? SizeStrField(MimeType) : 0);
	}
	
	bool Serialize(LFile &f, bool Write)
	{
		f.SetStatus(true);

		if (Write)
		{
			int32 Magic = Type();
			f << Magic;
			f << Size;
			if (Data)
			{
				f.Write(Data, Size);
			}
			else
			{
				f.Seek(Size, SEEK_CUR);
			}
			WriteIntField(FIELD_NODE_FLAGS, Flags);
			WriteObjField(FIELD_NODE_LASTMOD, LastMod);
			if (Uid) WriteStrField(FIELD_NODE_UID, Uid);
			if (MimeType) WriteStrField(FIELD_NODE_MIMETYPE, MimeType);
		}
		else
		{
			DeleteArray(Uid);
			DeleteArray(MimeType);

			int32 Magic;
			f >> Magic;
			if (Magic == Type())
			{
				f >> Size;
				DataPos = f.GetPos();
				f.Seek(Size, SEEK_CUR);
				while (!f.Eof())
				{
					int16 Id;
					f >> Id;
					switch (Id)
					{
						ReadIntField(FIELD_NODE_FLAGS, Flags);
						ReadObjField(FIELD_NODE_LASTMOD, LastMod);
						ReadStrField(FIELD_NODE_UID, Uid);
						ReadStrField(FIELD_NODE_MIMETYPE, MimeType);
					}
				}
			}
			else return false;
		}
		
		return f.GetStatus();
	}	
	
	// RemoteNode Impl
	int GetFlags()
	{
		return Flags;
	}
	
	bool SetFlags(int f)
	{
		SetDirty(f != Flags);
		Flags = f;
		return true;
	}
	
	char *GetUid()
	{
		return Uid;
	}
	
	bool SetUid(char *uid)
	{
		if (uid AND Uid)
		{
			SetDirty(strcmp(uid, Uid) != 0);
		}
		else
		{
			SetDirty((Uid != 0) ^ (uid != 0));
		}

		DeleteArray(Uid);
		Uid = NewStr(uid);
		return true;
	}
	
	LDateTime &GetLastModified()
	{
		return LastMod;
	}
	
	int GetChildren()
	{
		return 0;
	}
	
	RemoteNode *ChildAt(int i)
	{
		return 0;
	}
	
	RemoteNode *ChildAt(char *Uid)
	{
		return 0;
	}
	
	bool Insert(RemoteNode *n, int Index = -1)
	{
		return false;
	}
	
	bool Delete()
	{
		return false;
	}
	
	int GetSize()
	{
		return Size;
	}
	
	char *GetData()
	{
		return Data;
	}
	
	bool SetData(char *data, int size = -1)
	{
		DeleteArray(Data);
		Size = size < 0 AND data ? strlen(Data) : max(size, 0);
		Data = Size ? new char[Size+1] : 0;
		if (Data AND data)
		{
			memcpy(Data, data, Size);
		}

		return true;
	}
};

/////////////////////////////////////////////////////////////////////////////////////
class RemoteCachePrivate
{
public:
	RemoteCacheNode *Root;
	
	RemoteCachePrivate()
	{
		Root = 0;
	}
	
	~RemoteCachePrivate()
	{
		DeleteObj(Root);
	}
};

/////////////////////////////////////////////////////////////////////////////////////
RemoteCache::RemoteCache()
{
	d = new RemoteCachePrivate;
}

RemoteCache::~RemoteCache()
{
	DeleteObj(d);
}

RemoteNode *RemoteCache::GetRoot()
{
	return d->Root;
}

bool RemoteCache::Serialize(ObjProperties *Props, bool Write)
{
	return false;
}

