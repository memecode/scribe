#include "lgi/common/Lgi.h"
#include "lgi/common/WordStore.h"
#include "../../libs/Btree/gbtree.h"

#define GWordStore_AllItems		"__GWordStoreAllItems__"

class LWordStorePriv
{
public:
	LString File;
	LAutoPtr<GBTree> Tree;
};

LWordStore::LWordStore(const char *file)
{
	d = new LWordStorePriv;
	if (file)
		Serialize(file, true);
}

LWordStore::~LWordStore()
{
	DeleteObj(d);
}

unsigned long LWordStore::Length()
{
	return d->Tree->Length();
}

bool LWordStore::Serialize(const char *file, bool Load)
{
	bool Status = false;

	if (Load)
	{
		if (!d->File)
			d->File = NewStr(file);

		Status = d->Tree.Reset(new GBTree(d->File));
	}

	return Status;
}

long LWordStore::GetItems()
{
	if (d->Tree)
	{
		long Items;
		if (d->Tree->Find(GWordStore_AllItems, Items))
		{
			return Items;
		}
	}

	return 0;
}

void LWordStore::SetFile(const char *file)
{
	DeleteArray(d->File);
	d->File = NewStr(file);
}

bool LWordStore::SetItems(int s)
{
	return d->Tree && d->Tree->Insert(GWordStore_AllItems, s);
}

bool LWordStore::Insert(const char *Word)
{
	if (!d->Tree && d->File)
	{
		if (!d->Tree.Reset(new GBTree(d->File)))
			return false;
	}

	if (d->Tree)
	{
		size_t WordLen = strlen(Word);
		if (WordLen < 256)
		{
			long Count = 0;
			if (d->Tree->Find(Word, Count))
			{
				return d->Tree->Insert(Word, Count + 1);
			}
			else
			{
				return d->Tree->Insert(Word, 1);
			}
		}
	}

	return false;
}

int LWordStore::SetWordCount(const char *Word, ssize_t Count)
{
	if (d->Tree)
	{
		return d->Tree->Insert(Word, (long)Count);
	}

	return false;
}

bool LWordStore::DeleteWord(const char *Word)
{
	return d->Tree ? d->Tree->Delete(Word) : false;
}

long LWordStore::GetWordCount(const char *Word)
{
	if (d->Tree)
	{
		long Count = 0;
		if (d->Tree->Find(Word, Count))
		{
			return Count;
		}
	}

	return 0;
}

void LWordStore::Empty()
{
	d->Tree.Reset();
	if (d->File)
		FileDev->Delete(d->File, false);
}

char *LWordStore::GetFile()
{
	return d->File;
}

#ifdef _DEBUG
int64 LWordStore::Sizeof()
{
	return 0;
}
#endif

