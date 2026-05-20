#include "lgi/common/Lgi.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Store3.h"

struct PrintLog : public LStream
{
	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0) override
	{
		printf("%.*s", (int)Size, (const char*)Ptr);
		return Size;
	}
};

class MapiConnector :
	public LDataEventsI,
	public LThread,
	public LCancel
{
	LAutoPtr<LDataStoreI> store;
	LDataFolderI *root = nullptr;
	PrintLog log;
	LArray<int> dirtyProps;

public:
	MapiConnector() :
		LThread("MapiConnector")
	{
		log.Print("MapiConnector starting...\n");

		// create the store and log in:
		if (!store.Reset(OpenMapiStore(	"Outlook",
										"matthew.allen@harman.com",
										"",
										0,
										this)))
		{
			log.Print("Error: alloc failed.\n");
			LCloseApp();
		}
		else
		{
			Run();
		}
	}

	~MapiConnector()
	{
		Cancel();
		WaitForExit();
	}

	void SetContext(const char *file, int line) override
	{
		LAssert(!"impl me");
	}
	
	void Post(LDataStoreI *store, void *Param) override
	{
		LAssert(!"impl me");
	}

	bool GetSystemPath(int Folder, LVariant &Path) override
	{
		LAssert(!"impl me");
		return false;
	}
	
	LOptionsFile *GetOptions(bool Create = false) override
	{
		LAssert(!"impl me");
		return nullptr;
	}
	
	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new, bool filter) override
	{
		LAssert(!"impl me");
	}

	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items) override
	{
		LAssert(!"impl me");
		return false;
	}

	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items) override
	{
		LAssert(!"impl me");
		return false;
	}

	bool OnChange(LArray<LDataI*> &items, int FieldHint) override
	{
		LAssert(!"impl me");
		return false;
	}

	void OnPropChange(LDataStoreI *Store, int Prop, LVariantType Type) override
	{
		switch (Prop)
		{
			case FIELD_MAPI_PROFILES:
			{
				log.Print("Got FIELD_MAPI_PROFILES change\n");
				dirtyProps.Add(Prop);
				return;
			}
		}

		LAssert(!"impl me");
	}

	LStreamI *GetLogger(LDataStoreI *store) override
	{
		return &log;
	}

	bool Match(LDataStoreI *store, LDataPropI *Addr, int ObjectType, LArray<LDom*> &Matches) override
	{
		LAssert(!"impl me");
		return false;
	}

	void ScanFolders(LDataFolderI *f, int depth = 0)
	{
		auto indent = LString("    ") * depth;
		log.Print("%s%s (%s)\n", indent.Get(),
			f->GetStr(FIELD_FOLDER_NAME),
			Store3ItemTypeName((Store3ItemTypes)f->GetInt(FIELD_FOLDER_TYPE)));

		auto &it = f->SubFolders();
		for (auto c = it.First(); c; c = it.Next())
		{
			if (auto cFolder = dynamic_cast<LDataFolderI*>(c))
				ScanFolders(cFolder, depth + 1);
		}
	}

	void ProcessDirtyProps()
	{
		for (auto prop: dirtyProps)
		{
			switch (prop)
			{
				case FIELD_MAPI_PROFILES:
				{
					if (auto profiles = store->GetStr(FIELD_MAPI_PROFILES))
					{
						for (auto p: LString(profiles).SplitDelimit(","))
							log.Print("Profile: %s\n", p.Get());
					}
					break;
				}
			}
		}
		dirtyProps.Empty();
	}

	int Main() override
	{
		// got log in status:
		auto online = store->GetInt(FIELD_IS_ONLINE);
		if (!online)
		{
			log.Print("Error: not online.\n");
			LCloseApp();
			return -1;
		}

		ProcessDirtyProps();

		// start enumerating the folders...
		if (!(root = store->GetRoot()))
		{
			log.Print("Error: no root folder?\n");
			LCloseApp();
			return -1;
		}

		ScanFolders(root);

		while (!IsCancelled())
		{
			LSleep(10);
		}

		return 0;
	}
};

int main(int args, const char **arg)
{
	OsAppArguments appArgs(args, arg);
	LApp app(appArgs, "mapiConnector");
	if (app)
	{
		MapiConnector connector;
		app.Run();
	}

	return 0;
}

