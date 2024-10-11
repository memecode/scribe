#pragma once

#include "Store3Webdav/WebdavStore.h"
#include "MailStoreUpgrade.h"
#include "ScribeWndPrivate.h"

extern bool HasMailStore(LXmlTag *MailStores, char *Name);

struct LoadMailStoreState : public LView::ViewEventTarget
{
	typedef std::function<void(bool)> BoolCb;
	typedef std::function<void(int)>  IntCb;

	ScribeWnd *App;
	BoolCb Callback; // Final cb for entire LoadMailStoreState execution
	LOptionsFile *Options = NULL;
	LXmlTag *MailStores = NULL;
	LArray<LXmlTag*> Que;
	int StoreIdx = 0;
	bool OptionsDirty = false;
	bool Status = false;
	std::function<bool(bool)> ReturnWithEvent;
	std::function<bool(bool)> ReturnOnDialog;
	std::function<void(const char *folderPath,const char *details,IntCb callback)> AskStoreUpgrade;

	LoadMailStoreState(ScribeWnd *app, std::function<void(bool)> callback) :
		ViewEventTarget(app, M_LOAD_NEXT_MAIL_STORE),
		App(app),
		Callback(callback)
	{
		Options = App->GetOptions();

		ReturnWithEvent = [this](bool s)
		{
			PostEvent(M_LOAD_NEXT_MAIL_STORE);
			return s;
		};

		ReturnOnDialog = [this](bool s)
		{
			return s;
		};

		AskStoreUpgrade = [this](auto FolderPath, auto Details, auto cb)
		{
			auto result = LgiMsg(App,
				LLoadString(IDS_MAILSTORE_UPGRADE_Q),
				AppName,
				MB_YESNO,
				FolderPath,
				ValidStr(Details) ? Details : "n/a");
			cb(result);
		};

		MailStores = Options->LockTag(OPT_MailStores, _FL);

		// Load up some work in the queue and start the iteration...
		if (MailStores)
			Que = MailStores->Children;
	}
	
	~LoadMailStoreState()
	{
		if (MailStores)
		{
			Options->Unlock();
			MailStores = NULL;
		}
	}

	void Start()
	{
		PostEvent(M_LOAD_NEXT_MAIL_STORE);
	}

	bool OnStatus(bool b)
	{
		if (MailStores)
		{
			Options->Unlock();
			MailStores = NULL;
		}

		if (OptionsDirty)
			App->SaveOptions();

		if (Callback)
			Callback(b);

		delete this;

		return b;
	}

	LMessage::Result OnEvent(LMessage *Msg) override
	{
		if (Msg->Msg() == M_LOAD_NEXT_MAIL_STORE)
		{
			if (!MailStores)
				OnStatus(false);
			else
				Iterate();
		}

		return 0;
	}

	// This will process one item off the Que,
	// Or if no work is available call PostIterate to 
	// finish the process.
	//
	// In most cases this function should complete with
	// ReturnWithEvent to trigger the next iteration...
	bool Iterate()
	{
		// No more work, so do the completion step:
		if (Que.Length() == 0)
			return PostIterate();

		// There are 2 exits modes for this function:
		//
		// 1) Normal exit where we should post a M_LOAD_NEXT_MAIL_STORE to ourselves to
		// start the next iteration by calling ReturnWithEvent.
		//
		// 2) A dialog was launched and we should NOT post a M_LOAD_NEXT_MAIL_STORE event
		// by calling ReturnOnDialog. The dialog's callback will do that later.

		// Get the next Xml tag off the queue:
		auto MailStore = Que[0];
		Que.DeleteAt(0, true);

		if (!MailStore->IsTag(OPT_MailStore))
			return ReturnWithEvent(false);

		// Read the folders..
		auto Path       = MailStore->GetAttr(OPT_MailStoreLocation);
		auto ContactUrl = MailStore->GetAttr(OPT_MailStoreContactUrl);
		auto CalUrl     = MailStore->GetAttr(OPT_MailStoreCalendarUrl);
		if (!Path && !ContactUrl && !CalUrl)
		{
			LgiTrace("%s:%i - No mail store path (%i).\n", _FL, StoreIdx);
			return ReturnWithEvent(false);
		}

		// If disabled, skip:
		if (MailStore->GetAsInt(OPT_MailStoreDisable) > 0)
			return ReturnWithEvent(false);

		// Check and validate the folder name:
		auto StoreName = MailStore->GetAttr(OPT_MailStoreName);
		if (!StoreName)
		{
			char Tmp[256];
			for (int i=1; true; i++)
			{
				sprintf_s(Tmp, sizeof(Tmp), "Folders%i", i);
				if (!HasMailStore(MailStores, Tmp))
					break;
			}

			MailStore->SetAttr(OPT_MailStoreName, Tmp);
			StoreName = MailStore->GetAttr(OPT_MailStoreName);
			OptionsDirty = true;
		}

		// Build a LMailStore entry in App->Folders:
		auto &Folder = App->Folders[StoreIdx];
		Folder.Name = StoreName;

		if (ValidStr(Path))
		{
			// Mail3 folders on disk...
			LFile::Path p;
			if (LIsRelativePath(Path))
			{
				p = Options->GetFile();
				p = p / ".." / Path;
			}
			else p = Path;
			auto Full = p.Absolute().GetFull();

			LVariant CreateFoldersIfMissing;
			Options->GetValue(OPT_CreateFoldersIfMissing, CreateFoldersIfMissing);

			// Sniff type...
			auto Ext = LGetExtension(Full);
			if (!Ext)
				return ReturnWithEvent(false);

			if (!Folder.Store)
				Folder.Store.Reset(App->CreateDataStore(Full, CreateFoldersIfMissing.CastInt32() != 0));
			if (!Folder.Store)
			{
				LgiTrace("%s:%i - Failed to create data store for '%s'\n", _FL, Full.Get());
				return ReturnWithEvent(false);
			}

			Folder.Path = Full;
		}
		else if (ContactUrl || CalUrl)
		{
			// Remove Webdav folders...
			Folder.Store.Reset(new WebdavStore(App, App, StoreName));
		}
		else
		{
			return ReturnWithEvent(false);
		}

		auto ex = MailStore->GetAsInt(OPT_MailStoreExpanded);
		if (ex >= 0)
			Folder.Expanded = ex != 0;

		// Check if the mail store requires upgrading...
		auto MsState = (Store3Status)Folder.Store->GetInt(FIELD_STATUS);
		if (MsState == Store3UpgradeRequired)
		{
			LgiTrace("%s:%i - this=%p\n", _FL, this);
			auto Details = Folder.Store->GetStr(FIELD_STATUS);
			AskStoreUpgrade(Folder.Path.Get(),
				ValidStr(Details) ? Details : "n/a",
				[this, Store=Folder.Store.Get(), MailStore](auto result)
				{
					if (result == IDYES)
					{
						auto Prog = new MailStoreUpgrade(App, Store);
						Prog->DoModal([this, MailStore](auto dlg, auto code)
							{
								// Upgrade complete, finish the iteration..
								Iterate2(MailStore);
							});
						return;
					}

					// Upgrade not allowed, so do next iteration...			
					ReturnWithEvent(false);
				});

			return ReturnOnDialog(true);
		}
		else if (MsState == Store3Error)
		{
			auto ErrMsg = Folder.Store->GetStr(FIELD_ERROR);
			auto a = new LAlert(App,
				AppName, ErrMsg ? ErrMsg : LLoadString(IDS_ERROR_FOLDERS_STATUS),
				LLoadString(IDS_EDIT_MAIL_STORES),
				LLoadString(IDS_OK));			
			a->DoModal([this](auto dlg, auto code)
				{
					delete dlg;

					if (code == 1)
					{
						App->PostEvent(M_COMMAND, IDM_MANAGE_MAIL_STORES);

						// This fails the whole LoadMailStores process, because the user
						// is going to edit the mail store list via the dialog;
						OnStatus(false);
					}
					else
					{
						// We can't load this mail store, so do the next iteration...
						ReturnWithEvent(true);
					}
				});

			return ReturnOnDialog(false);
		}

		// No upgrade or error, just keep going.
		return Iterate2(MailStore);
	}

	// This also should complete by calling ReturnWithEvent/ReturnOnDialog.
	bool Iterate2(LXmlTag *MailStore)
	{
		auto &Folder = App->Folders[StoreIdx];
		auto StoreName = MailStore->GetAttr(OPT_MailStoreName);

		// check password
		LString FolderPsw;
		if ((FolderPsw = Folder.Store->GetStr(FIELD_STORE_PASSWORD)))
		{
			bool Verified = false;

			if (ValidStr(App->d->MulPassword))
			{
				Verified = App->d->MulPassword.Equals(FolderPsw, false);
				App->d->MulPassword.Empty();
			}

			if (!Verified)
			{
				auto Dlg = new LInput(App, "", LLoadString(IDS_ASK_FOLDER_PASS), AppName, true);
				Dlg->DoModal([this, Dlg, FolderPsw, StoreName](auto dlg, auto id)
					{
						auto psw = Dlg->GetStr();

						if (id == IDOK)
						{
							auto &Folder = App->Folders[StoreIdx];
							if (psw == FolderPsw)
							{
								Status |= App->ProcessFolder(Folder.Store, StoreIdx, StoreName);
							}
							else
							{
								// Clear the folder and don't increment the StoreIdx...
								Folder.Empty();
								App->Folders.PopLast();
								ReturnWithEvent(false);
								return;
							}
						}

						Iterate3();
					});

				return ReturnOnDialog(true);
			}
		}
		else
		{
			Status |= App->ProcessFolder(Folder.Store, StoreIdx, StoreName);
		}

		return Iterate3();
	}

	bool Iterate3()
	{
		StoreIdx++;
		return ReturnWithEvent(true);
	}

	bool PostIterate()
	{
		if (Status)
		{
			// Force load some folders...
			ScribeFolder *Folder = App->GetFolder(FOLDER_CALENDAR);
			if (Folder)
				Folder->LoadThings();
			Folder = App->GetFolder(FOLDER_FILTERS);
			if (Folder)
				Folder->LoadThings();
			for (auto &ms: App->Folders)
			{
				if (!ms.GetRoot())
					continue;
				for (auto c = ms.GetRoot()->GetChildFolder(); c; c = c->GetNextFolder())
				{
					if (c->GetItemType() == MAGIC_CONTACT ||
						c->GetItemType() == MAGIC_FILTER)
						c->LoadThings();
				}
			}

			List<Contact> c;
			App->GetContacts(c);

			// Set selected folder to Inbox by default
			// if the user hasn't selected a folder already
			if (App->ScribeState != ScribeWnd::ScribeExiting && App->Tree && !App->Tree->Selection())
			{
				LVariant StartInFolder;
				Options->GetValue(OPT_StartInFolder, StartInFolder);

				ScribeFolder *Start = NULL;
				if (ValidStr(StartInFolder.Str()))
				{
					Start = App->GetFolder(StartInFolder.Str());
				}
				if (!Start)
				{
					Start = App->GetFolder(FOLDER_INBOX);
				}
				if (Start && App->Tree)
				{
					App->Tree->Select(Start);
				}
			}
		}

		Options->DeleteValue(OPT_CreateFoldersIfMissing);

		// Set system folders
		ScribeFolder *f = App->GetFolder(FOLDER_INBOX);
		if (f) f->SetSystemFolderType(Store3SystemInbox);
		f = App->GetFolder(FOLDER_OUTBOX);
		if (f) f->SetSystemFolderType(Store3SystemOutbox);
		f = App->GetFolder(FOLDER_SENT);
		if (f) f->SetSystemFolderType(Store3SystemSent);
		f = App->GetFolder(FOLDER_SPAM);
		if (f) f->SetSystemFolderType(Store3SystemSpam);

		return OnStatus(Status);
	}
};

