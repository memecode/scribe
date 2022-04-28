#include "lgi/common/Lgi.h"
#include "lgi/common/Store3.h"
#include "UnitTest.h"
#include "ScribeDefs.h"

class Printf : public LStream
{
public:
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		if (Flags == 0)
			printf("%.*s", (int)Size, (char*)Buffer);
		return Size;
	}
};

struct ImapTestPriv : public LDataEventsI
{
	LViewI *App;
	LDataStoreI *Imap;
	Printf Log;
	bool Status;
	LDataFolderI *Inbox, *Test;

	ImapTestPriv(LViewI *app)
	{
		App = app;
		Status = true;
		Imap = 0;
		Inbox = Test = 0;
	}

	void Shutdown()
	{
		LDataStoreI *Temp = Imap;
		Imap = 0;
		int64 Start = LCurrentTime();
		DeleteObj(Temp);
		int64 Time = LCurrentTime() - Start;
		if (Time > 1000)
		{
			Status = false;
			printf("%s:%i - Error: took to long to shutdown IMAP store (" LPrintfInt64 "ms)\n", _FL, Time);
		}
	}

	void Post(LDataStoreI *store, void *Param)
	{
		App->PostEvent(M_STORAGE_EVENT, (LMessage::Param)store, (LMessage::Param)Param);
	}

	bool GetSystemPath(int Folder, LVariant &Path)
	{
		return false;
	}

	LOptionsFile *GetOptions(bool Create = false)
	{
		return 0;
	}

	void OnNew(LDataFolderI *parent, LDataI *new_item, int pos)
	{
	}

	bool OnDelete(LDataFolderI *parent, LDataI *del_item)
	{
		return false;
	}

	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LDataI *item)
	{
		return false;
	}

	bool OnChange(LDataI *update_item)
	{
		return false;
	}

	void OnIntPropChange(LDataStoreI *store, int prop)
	{
	}

	void OnStrPropChange(LDataStoreI *store, int prop)
	{
	}

	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
	{
	}

	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items)
	{
		return true;
	}

	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
	{
		return true;
	}

	bool OnChange(LArray<LDataI*> &items, int FieldHint)
	{
		return true;
	}
};

ImapTest::ImapTest(LViewI *app) : UnitTest(app, "Imap Store3")
{
	d = new ImapTestPriv(app);
}

ImapTest::~ImapTest()
{
	DeleteObj(d);
}

bool ImapTest::OnIdle()
{
	return d->Imap ? d->Imap->OnIdle() : false;
}

void ImapTest::OnEvent(LMessage *m)
{
	switch (m->Msg())
	{
		case M_STORAGE_EVENT:
		{
			if (d->Imap)
			{
				LDataStoreI *Store = (LDataStoreI*)m->A();
				if (Store)
				{
					Store->OnEvent((void*)m->B());
				}
			}
			break;
		}
	}
}

void ImapTest::ClearCache()
{
	// Clear any previous cache
	char c[MAX_PATH_LEN];
	if (LGetSystemPath(LSP_APP_INSTALL, c, sizeof(c)))
	{
		LMakePath(c, sizeof(c), c, "ImapCache");
		FileDev->RemoveFolder(c, true);
	}
}

bool ImapTest::Login()
{
	char *User = 0, *Pass = 0, *Host = 0;
	if (LDirExists("h:\\Code"))
	{
		Host = "localhost";
		User = "fret@memecode.dyndns.org";
		Pass = "_groon";
	}
	else
	{
		Host = "imap";
		User = "matthew";
		Pass = "_black6";
	}

	// Create Imap store...
	LAutoPtr<ProtocolSettingStore> store;
	if (d->Imap = OpenImap(Host, 0, User, Pass, 0, d, NULL, NULL, &d->Log, 1234, store))
	{
		// Wait for it to connect and go online
		int64 Start = LCurrentTime();
		while (!d->Imap->GetInt(FIELD_IS_ONLINE) &&
			(LCurrentTime() - Start) < 5000)
		{
			LSleep(100);
		}
		int64 Time = LCurrentTime() - Start;
		if (Time >= 5000)
		{
			printf("\t%s:%i - Error: IMAP store took too long to go online (%ims).\n", _FL, (int)Time);
			DeleteObj(d->Imap);
			return false;
		}

		return true;
	}

	return false;
}

bool ImapTest::Test01_LoadInbox()
{
	// Create imap store...
	d->Log.Print("	Logging in.\n");
	if (Login())
	{
		LDataFolderI *r = d->Imap->GetRoot();
		if (r)
		{
			if (r->SubFolders().Length() == 0)
			{
				printf("%s:%i - Error: IMAP root folder has no sub-folders.\n", _FL);
				d->Status = false;
			}
			else
			{
				d->Log.Print("\tGetting sub-folders...\n");
				d->Log.Print("\tRoot folder has %i sub-folders\n", r->SubFolders().Length());
				for (LDataFolderI *c = r->SubFolders().First(); c; c = r->SubFolders().Next())
				{
					auto Name = c->GetStr(FIELD_FOLDER_NAME);
					if (!Name)
					{
						printf("\t%s:%i - Error: IMAP folder has no name.\n", _FL);
						d->Status = false;
						break;
					}
					else
					{
						printf("\t\t%s\n", Name);
						if (stricmp(Name, "inbox") == 0)
							d->Inbox = c;
					}
				}

				if (!d->Inbox)
				{
					printf("\t%s:%i - Error: No IMAP inbox.\n", _FL);
					d->Status = false;
				}
				else
				{
					int Len = d->Inbox->Children().Length();
					printf("Inbox currently has %i email.\n", Len);
					if (!Len)
					{
						int64 Start = LCurrentTime(), Last = 0;
						while (!d->Inbox->GetInt(FIELD_LOADED))
						{
							if (!Last || LCurrentTime() - Last > 15000)
							{
								printf("\tWaiting folder load... %" LPrintfSizeT "\n", d->Inbox->Children().Length());
								Last = LCurrentTime();
							}
							LSleep(1000);
						}
						printf("\tLoad took %i\n", (int)(LCurrentTime()-Start));
					}

					printf("\tInbox now has %" LPrintfSizeT " email.\n", d->Inbox->Children().Length());
					/*
					int n = 0;
					for (LDataI *m = d->Inbox->Children().First(); m; m = d->Inbox->Children().Next(), n++)
					{
						if (n < 20)
						{
							printf("\t[%i] %.90s\n\t\tTo: ", n, m->GetStr(FIELD_SUBJECT));
							GDataIt It = m->GetList(FIELD_TO);
							int k = 0;
							for (LDataPropI *i=It->First(); i && k < 3; i=It->Next(), k++)
							{
								if (i->GetStr(FIELD_NAME))
									printf("%s <%s>, ", i->GetStr(FIELD_NAME), i->GetStr(FIELD_EMAIL));
								else
									printf("<%s>, ", i->GetStr(FIELD_EMAIL));
							}
							printf("\n\t\tFlags: %s\n", m->GetStr(FIELD_CACHE_FLAGS));
						}
						else if (n == 20)
							printf("\t.......snip..........\n");
					}
					*/
				}
			}
		}
		else
		{
			printf("%s:%i - Error: Failed to get IMAP root folder.\n", _FL);
			d->Status = false;
		}

		printf("Profile: %.1f listing, %.1f select\n",
				(double)d->Imap->GetInt(FIELD_PROFILE_IMAP_LISTING)/1000.0,
				(double)d->Imap->GetInt(FIELD_PROFILE_IMAP_SELECT)/1000.0);

		// Shut it down
		d->Shutdown();
	}
	else d->Status = false;

	return d->Status;
}

bool ImapTest::Test()
{
	return Test01_LoadInbox();
}

