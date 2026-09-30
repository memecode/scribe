#include "Scribe.h"

#include "lgi/common/Store3Defs.h"
#include "lgi/common/PopupNotification.h"

#include "resdefs.h"

///////////////////////////////////////////////////////////////////////
class BayesFolderItem : public LTreeItem
{
	enum MenuId
	{
		MenuHam = 1,
		MenuSpam,
		MenuUnknown
	};

	ScribeFolder *Folder;
	LString Type;
	LColour Colour;

	LString HandlePath(LString inPaths, ScribeMailType type, LString path)
	{
		LString sep = "\n";
		auto paths = inPaths.SplitDelimit(sep);
		paths.SetFixedLength(false);
		
		if (type == ScribeMailType::BayesMailSpam)
		{
			// If the folder path is not already in the list, add it.
			if (!paths.HasItem(path))
				paths.Add(path);
		}
		else
		{
			// Remove it..
			for (ssize_t i = (ssize_t)paths.Length() - 1; i >= 0; i--)
			{
				if (paths[i] == path)
					paths.DeleteAt(i, true);
			}
		}

		auto newPaths = sep.Join(paths);
		printf("New paths: %s\n", newPaths.Get());
		return newPaths;
	}

	bool WriteOpts(ScribeMailType cat)
	{
		if (!Folder)
			return false;

		auto app = Folder->App;
		if (!app)
			return false;

		auto opts = app->GetOptions();
		if (!opts)
			return false;

		auto obj = Folder->GetFldObj();
		if (!obj)
			return false;

		auto type = (Store3Backend) obj->GetInt(FIELD_STORE_TYPE);
		printf("%s:%i - type=%i\n", _FL, type);
		switch (type)
		{
			case Store3Sqlite:
			{
				// Write to global option OPT_SpamFolder.
				LVariant v;
				opts->GetValue(OPT_SpamFolder, v);
				auto newPaths = HandlePath(v.Str(), cat, Folder->GetPath());
				if (!opts->SetValue
					(
						OPT_SpamFolder,
						v = newPaths.Get()
					))
					printf("Failed to set new spam folder paths\n");
				opts->SetValue(OPT_HasSpam, v = newPaths.Length() > 0);

				// Also need to set the IDC_SPAM_FOLDER in the parent dialog..
				if (auto t = GetTree())
					if (auto w = t->GetWindow())
						w->SetCtrlName(IDC_SPAM_FOLDER, newPaths);
					else printf("%s:%i - no wnd?\n", _FL);
				else printf("%s:%i - no tree?\n", _FL);

				break;
			}
			case Store3Imap:
			{
				LString folderSep = "/";

				// Write to OPT_SpamFolder for IMAP store.
				auto accountId = obj->GetInt(FIELD_ACCOUNT_ID);
				if (auto account = app->GetAccountById(accountId))
				{
					auto opt = account->Receive.OptionName(OPT_ReceiveSubFolders);
					if (auto tag = opts->LockTag(opt, _FL))
					{
						// This will have the mail store as the first path segment...
						auto path = Folder->GetPath();

						// Strip that out...
						auto parts = path.SplitDelimit(folderSep);
						path = folderSep.Join(parts.Slice(1));

						auto oldPaths = tag->GetAttr(OPT_SpamFolder);
						auto newPaths = HandlePath(oldPaths, cat, path);
						printf("%s:%i - newPaths=%s\n", _FL, newPaths.Get());
						tag->SetAttr(OPT_SpamFolder, newPaths);
						tag->SetAttr(OPT_HasSpam, newPaths.Length() > 0);
						
						opts->Unlock();
					}
				}
				else LPopupNotification::Message(GetTree()->GetWindow(), "Couldn't get account by id.");
				break;
			}
			default:
			{
				LAssert(!"Unknown store type");
				return false;
			}
		}

		return opts->SerializeFile(true);
	}

	bool IsMailFolder()
	{
		return Folder && MAGIC_MAIL == Folder->GetItemType();
	}

public:
	BayesFolderItem(ScribeFolder *folder) : Folder(folder)
	{
		if (IsMailFolder())
			SetType(Folder->App->BayesTypeFromPath(Folder->GetPath()), false);
	}

	bool SetType(ScribeMailType cat, bool writeOpts)
	{
		if (writeOpts && !WriteOpts(cat))
			return false;

		// Update UI
		switch (cat)
		{
			case ScribeMailType::BayesMailHam:
				Type = "Ham";
				Colour = LColour(0, 0, 255);
				break;
			case ScribeMailType::BayesMailSpam:
				Type = "Spam";
				Colour = LColour(224, 96, 85); // Pink for Spam
				break;
			default:
			case ScribeMailType::BayesMailUnknown:
				Type = "Unknown";
				Colour = LColour(128, 128, 128); // Gray for Unknown
				break;
		}

		// Child folder types depend on whether an ancestor is a spam folder.
		if (writeOpts)
			UpdateChildTypes();

		return true;
	}

	void UpdateChildTypes()
	{
		for (auto c = GetChild(); c; c = c->GetNext())
		{
			if (auto item = dynamic_cast<BayesFolderItem*>(c))
			{
				if (item->IsMailFolder())
					item->SetType(item->Folder->App->BayesTypeFromPath(item->Folder->GetPath()), false);
				item->UpdateChildTypes();
			}
		}
	}

	const char *GetText(int i = 0) override
	{
		return i ? Type.Get() : Folder->GetText();
	}

	int GetImage(int flags = 0) override
	{
		return Folder->GetImage(flags);
	}

	constexpr static float BackMix = 0.05f;

	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c) override
	{
		LColour old = Ctx.Fore;
		LColour oldBack = Ctx.Back;
		LColour oldTxtBack = Ctx.TxtBack;
		if (i == 1 && IsMailFolder())
		{
			Ctx.Fore = Colour;
			Ctx.Back = Ctx.Back.Mix(Colour, BackMix);
			Ctx.TxtBack = Ctx.TxtBack.Mix(Colour, BackMix);
		}
		LTreeItem::OnPaintColumn(Ctx, i, c);
		Ctx.Fore = old;
		Ctx.Back = oldBack;
		Ctx.TxtBack = oldTxtBack;
	}

	void OnMouseClick(LMouse &m) override
	{
		if (!IsMailFolder())
			return;

		if (!m.Down() || !m.IsContextMenu())
			return;

		LSubMenu menu;
		menu.AppendItem("Ham", MenuHam, true);
		menu.AppendItem("Spam", MenuSpam, true);
		menu.AppendItem("Unknown", MenuUnknown, true);

		auto menuMouse = m;
		if (auto tree = GetTree())
			menuMouse -= tree->ScrollPxPos();

		switch (menu.Float(GetTree(), menuMouse))
		{
			case MenuHam:
				SetType(ScribeMailType::BayesMailHam, true);
				break;
			case MenuSpam:
				SetType(ScribeMailType::BayesMailSpam, true);
				break;
			case MenuUnknown:
				SetType(ScribeMailType::BayesMailUnknown, true);
				break;
			default:
				return;
		}

		if (auto tree = GetTree())
		{
			tree->UpdateAllItems();
			tree->Invalidate();
		}
	}
};

///////////////////////////////////////////////////////////////////////
class BayesDlgPrivate
{
public:
	ScribeWnd *App;
	bool foldersLoaded = false;

	void AddFolders(LTreeItem *parent, ScribeFolder *folder)
	{
		for (auto child = folder->GetChildFolder(); child; child = child->GetNextFolder())
		{
			auto item = new BayesFolderItem(child);
			if (!item)
				continue;

			parent->Insert(item);
			AddFolders(item, child);
			item->Expanded(true);
		}
	}

	void LoadFolders(LTree *folderTree)
	{
		if (foldersLoaded || !folderTree)
			return;

		folderTree->ColumnHeaders(true);
		folderTree->AddColumn("Path", 230);
		folderTree->AddColumn("Type", 90);
 
		for (auto &store: App->GetStorageFolders())
		{
			if (auto root = store.GetRoot())
			{
				auto item = new BayesFolderItem(root);
				if (!item)
					continue;

				folderTree->Insert(item);
				AddFolders(item, root);
				item->Expanded(true);
			}
		}

		for (auto account: *App->GetAccounts())
		{
			if (auto root = account->Receive.GetRootFolder())
			{
				auto item = new BayesFolderItem(root);
				if (!item)
					continue;

				folderTree->Insert(item);
				AddFolders(item, root);
				item->Expanded(true);
			}
		}

		folderTree->UpdateAllItems();
		foldersLoaded = true;
	}
};

///////////////////////////////////////////////////////////////////////
BayesDlg::BayesDlg(ScribeWnd *app, int tab) // : TabDialog(IDC_TABS, IDC_LAUNCH_HELP)
{
	d = new BayesDlgPrivate;
	d->App = app;
	SetParent(d->App);

	Map(OPT_BayesFilterMode, IDC_BAYES_MODE, GV_INT32);
	Map(OPT_BayesMoveTo, IDC_SUSPECT_FOLDER, GV_STRING);

	Map(OPT_BayesDeleteAttachments, IDC_BAYES_DELETE_ATTACHMENTS, GV_BOOL);
	Map(OPT_BayesDeleteOnServer, IDC_BAYES_DELETE_ON_SERVER, GV_BOOL);

	Map(OPT_BayesUserWhiteList, IDC_WHITELIST, GV_STRING);
	Map(OPT_BayesThreshold, IDC_BAYES_THRESHOLD, GV_STRING);
	Map(OPT_BayesIncremental, IDC_BAYES_INCREMENTAL, GV_BOOL);
	Map(OPT_BayesDebug, IDC_BAYES_DEBUG, GV_BOOL);
	Map(OPT_BayesHam, IDC_HAM);
	Map(OPT_BayesSpam, IDC_SPAM);
	Map(OPT_BayesFalsePositives, IDC_FALSE_POS);
	Map(OPT_BayesSetRead, IDC_BAYES_READ);

	Map(OPT_SpamFolder, IDC_SPAM_FOLDER);

	if (LoadFromResource(IDD_BAYES_SETTINGS))
	{
		MoveToCenter();
		Convert(app->GetOptions(), this, true);
		SetCtrlValue(IDC_TABS, tab);

		int Spam = (int) GetCtrlValue(IDC_SPAM);
		int FalseNeg = (int) GetCtrlValue(IDC_FALSE_NEG);
		
		char s[256];
		int Total = Spam + FalseNeg;
		if (Total)
		{
			sprintf_s(s, sizeof(s), "%.1f%%", (double)Spam*100/Total);
			SetCtrlName(IDC_EFFICIENCY, s);
		}
		else
		{
			SetCtrlName(IDC_EFFICIENCY, "n/a");
		}
	}
}

BayesDlg::~BayesDlg()
{
	DeleteObj(d);
}

int BayesDlg::OnNotify(LViewI *c, const LNotification &n)
{
	switch (c->GetId())
	{
		case IDC_LAUNCH_HELP:
		{
			d->App->LaunchHelp("filters.html#bayes");
			break;
		}
		case IDC_SET_SUSPECT_FOLDER:
		{
			auto fd = new FolderDlg(this, d->App, MAGIC_MAIL);
			fd->DoModal([this, fd](auto dlg, auto ok)
			{
				if (ok)
					SetCtrlName(IDC_SUSPECT_FOLDER, fd->Get());
			});
			break;
		}
		case IDC_SET_SPAM_FOLDER:
		{
			auto fd = new FolderDlg(this, d->App, MAGIC_MAIL);
			fd->DoModal([this, fd](auto dlg, auto ok)
			{
				if (!ok)
					return;

				LString in = GetCtrlName(IDC_SPAM_FOLDER);
				auto paths = in.SplitDelimit("\n");
				paths.SetFixedLength(false);
				if (!paths.HasItem(fd->Get()))
					paths.Add(fd->Get());
				SetCtrlName(IDC_SPAM_FOLDER, LString("\n").Join(paths));
			});
			break;
		}
		case IDC_TABS:
		{
			switch (n.Type)
			{
				case LNotifyValueChanged:
				{
					if (1 == n.Int[0])
					{
						// 'Check Folders' tab
						LTree *folderTree = nullptr;
						if (GetViewById(ID_FOLDER_CAT, folderTree))
							d->LoadFolders(folderTree);
					}
					break;
				}
				default:
					break;
			}
			break;
		}
		case IDOK:
		{
			Convert(d->App->GetOptions(), this, false);
			// fall thru
		}
		case IDCANCEL:
		{
			EndModal(c->GetId() == IDOK);
			break;
		}
	}

	return 0;
}

