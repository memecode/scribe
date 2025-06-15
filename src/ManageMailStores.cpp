#include "lgi/common/Lgi.h"
#include "lgi/common/List.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/Edit.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

#include "Scribe.h"
#include "resdefs.h"
#include "Store3Mail3/Mail3.h"
#include "ManageMailStores.h"
#include "SubFolderDlg.h"

class FmtDlg : public LDialog
{
public:
	int Ver;

	FmtDlg(LViewI *p, int ver)
	{
		Ver = -1;
		SetParent(p);
		if (LoadFromResource(IDD_FOLDER_FORMAT))
		{
			MoveToCenter();
			SetCtrlValue(IDC_FORMAT, ver);
		}
	}

	int OnNotify(LViewI *Ctrl, const LNotification &n) override
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
			{
				Ver = (int)GetCtrlValue(IDC_FORMAT);
				// Fall through
			}
			case IDCANCEL:
			{
				EndModal(Ctrl->GetId() == IDOK);
				break;
			}
		}

		return 0;
	}
};

////////////////////////////////////////////////////////////////////////////////////////
LString FolderFullPath(const char *Path)
{
	char f[MAX_PATH_LEN];
	if (LIsRelativePath(Path))
	{
		LMakePath(f, sizeof(f), LGetExePath(), Path);
		Path = f;
	}
	
	auto Ext = LGetExtension(Path);
	if (!Stricmp(Ext, "sqlite"))
	{
		LString s = Path;
		LTrimDir(s);
		return s;
	}
	
	return Path;
}

int GetFolderVersion(const char *f)
{
	auto Path = FolderFullPath(f);
	if (LDirExists(Path))
	{
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Path, MAIL3_DB_FILE);
		if (LFileExists(p))
			Path = p;
	}
	
	if (LFileExists(Path))
	{
		auto Ext = LGetExtension(Path);
		if (!Stricmp(Ext, "sqlite"))
			return 3;
	}

	return 0;
}

void EditWebdav(LViewI *parent, LXmlTag *t, std::function<void(bool)> callback)
{
	auto dlg = new LDialog(parent);

	dlg->LoadFromResource(IDD_WEBDAV_PROPS);
	dlg->MoveSameScreen(parent);
	
	dlg->SetCtrlName(IDC_DESC, t->GetAttr(OPT_MailStoreName));
	dlg->SetCtrlName(IDC_CONTACTS_URL, t->GetAttr(OPT_MailStoreContactUrl));
	dlg->SetCtrlName(IDC_CALENDAR_URL, t->GetAttr(OPT_MailStoreCalendarUrl));
	dlg->SetCtrlName(IDC_USERNAME, t->GetAttr(OPT_MailStoreUserName));
	dlg->SetCtrlName(IDC_PASSWORD, t->GetAttr(OPT_MailStorePassword));

	dlg->DoModal([t, callback](auto dlg, auto res)
	{
		if (res == IDOK)
		{	
			t->SetAttr(OPT_MailStoreName, dlg->GetCtrlName(IDC_DESC));
			t->SetAttr(OPT_MailStoreContactUrl, dlg->GetCtrlName(IDC_CONTACTS_URL));
			t->SetAttr(OPT_MailStoreCalendarUrl, dlg->GetCtrlName(IDC_CALENDAR_URL));
			t->SetAttr(OPT_MailStoreUserName, dlg->GetCtrlName(IDC_USERNAME));
			t->SetAttr(OPT_MailStorePassword, dlg->GetCtrlName(IDC_PASSWORD));
			t->DelAttr(OPT_MailStoreLocation);
			callback(true);
		}
		else callback(false);
	});
}

class StoreItem : public LListItem
{
	ScribeWnd *App = NULL;
	LListItemCheckBox *Disable = NULL;

public:
	LXmlTag Tag;

	StoreItem(ScribeWnd *app) : Tag(OPT_MailStore)
	{
		App = app;
		Disable = new LListItemCheckBox(this, 2, false);
	}

	const char *GetText(int Col)
	{
		switch (Col)
		{
			case 0:
				return Tag.GetAttr(OPT_MailStoreName);
			case 1:
				return Tag.GetAttr(OPT_MailStoreLocation);
		}

		return 0;
	}

	bool SetText(const char *s, int Col)
	{
		switch (Col)
		{
			case 0:
				Tag.SetAttr(OPT_MailStoreName, s);
				break;
			case 1:
				Tag.SetAttr(OPT_MailStoreLocation, s);
				break;
		}

		Update();
		GetList()->ResizeColumnsToContent();
		return true;
	}

	bool XmlIo(LXmlTag *t, bool Write)
	{
		if (Write)
		{
			Tag.SetAttr(OPT_MailStoreDisable, (int)Disable->Value());
			*t = Tag;
		}
		else
		{
			Tag = *t;
			
			int i = Tag.GetAsInt(OPT_MailStoreDisable);
			if (i >= 0)
				Disable->Value(i);
		}

		return true;
	}


	bool IsWebdav()
	{
		return	Tag.GetAttr(OPT_MailStoreContactUrl) ||
				Tag.GetAttr(OPT_MailStoreCalendarUrl);
	}

	void Edit()
	{
		if (IsWebdav())
		{
			EditWebdav(GetList(), &Tag, [this](auto status)
			{
				if (status)
					Update();
			});
		}
	}

	void OnMouseClick(LMouse &m)
	{
		bool Wd = IsWebdav();
		if (m.IsContextMenu())
		{
			if (Wd)
			{
				LSubMenu s;
				s.AppendItem(LLoadString(IDS_EDIT), IDM_EDIT);
				if (s.Float(GetList(), m) == IDM_EDIT)
					Edit();
			}
		}
		else if (m.Left() && m.Double() && m.Down())
		{
			int Col = GetList()->ColumnAtX(m.x);
			switch (Col)
			{
				case 0:
				{
					EditLabel(Col);
					break;
				}
				case 1:
				{
					if (Wd)
						Edit();
					else
						EditLabel(Col);
					return;
				}
			}
		}
		
		LListItem::OnMouseClick(m);
	}
};

ManageMailStores::ManageMailStores(ScribeWnd *app)
{
	SetParent(App = app);
	if (LoadFromResource(IDD_MANAGE_FOLDERS))
	{
		GetViewById(IDC_MAIL_STORES, Lst);
		MoveToCenter();

		LXmlTag *Base = App->GetOptions()->LockTag(0, _FL);
		if (Base)
		{
			Options.Copy(*Base, false);

			LXmlTag *Ms = Base->GetChildTag(OPT_MailStores);
			if (Ms)
			{			
				LXmlTag *t = Options.CreateTag(OPT_MailStores);
				if (t)
				{
					t->Copy(*Ms, true);
				}
			}

			App->GetOptions()->Unlock();
		}

		Map(OPT_MailStores, IDC_MAIL_STORES, OPT_MailStore, [this]()
		{
			return new StoreItem(App);
		});
		Map(OPT_StartInFolder, IDC_START_IN, GV_STRING);

		Convert(&Options, this, true);

		Lst->ResizeColumnsToContent();
		OnItemSelect();
	}
}

ManageMailStores::~ManageMailStores()
{
}

void ManageMailStores::OnItemSelect()
{
	LListItem *s = Lst->GetSelected();
	auto Name = s ? s->GetText(1) : NULL;
	auto Ver = Name ? GetFolderVersion(Name) : 0;
	
	SetCtrlEnabled(IDC_COMPACT_MS, Ver > 0);
	SetCtrlEnabled(IDC_CONVERT_MS, Ver == 3);
	SetCtrlEnabled(IDC_REPAIR_MS, true);
}

LMailStore *ManageMailStores::GetCurrentMailStore()
{
	LListItem *s = Lst->GetSelected();
	if (s)
	{
		auto p = FolderFullPath(s->GetText(1));

		for (unsigned i=0; i<App->GetStorageFolders().Length(); i++)
		{
			LMailStore &s = App->GetStorageFolders()[i];

			if (s.Path && s.Path.Equals(p))
			{
				if (s.Store)
					return &s;
			}
		}
	}

	return 0;
}

int ManageMailStores::OnNotify(LViewI *c, const LNotification &n)
{
	if (!Lst)
		return 0;

	switch (c->GetId())
	{
		case IDC_MAIL_STORES:
		{
			if (n.Type == LNotifyItemSelect)
			{
				OnItemSelect();
			}
			else if (n.Type == LNotifyItemDoubleClick)
			{
				auto ms = n.GetMouseEvent();
				auto col = Lst->ColumnAtX(ms.x);
				if (col == 0)
				{
					// noop: name col
				}
				else if (auto si = dynamic_cast<StoreItem*>(Lst->GetSelected()))
				{
					si->Edit();
				}
			}
			break;
		}
		case IDC_OPEN_MS:
		{
			auto s = new LFileSelect(this);
			s->InitialDir(LGetExePath());
			s->Type("Mail Folders", "*.mail3;*.sqlite");
			s->Open([this](auto dlg, auto status)
			{
				if (status)
				{
					StoreItem *Si = new StoreItem(App);
					if (Si)
					{
						char b[MAX_PATH_LEN];
						strcpy_s(b, sizeof(b), dlg->Name());
						auto n = (char*)LGetExtension(b);
						if (n && !_stricmp(n, "sqlite"))
						{
							n = strrchr(b, DIR_CHAR);
							if (n) *n = 0;
						}

						Si->Tag.SetAttr(OPT_MailStoreLocation, b);
						Lst->Insert(Si);
						Lst->ResizeColumnsToContent();
					}
				}
			});
			break;
		}
		case IDC_CLOSE_MS:
		{
			List<LListItem> s;
			Lst->GetSelection(s);
			s.DeleteObjects();
			Lst->ResizeColumnsToContent();
			break;
		}
		case IDC_CREATE_MS:
		{
			char Opts[MAX_PATH_LEN];
			LMakePath(Opts, sizeof(Opts), App->GetOptions()->GetFile(), "..");

			LView *btn;
			if (!GetViewById(IDC_CREATE_MS, btn))
				break;

			LSubMenu s;
			s.AppendItem("Mail3 Local Folders", IDM_LOCAL_FOLDERS);
			s.AppendItem("Webdav Remote Folder", IDM_WEBDAV_FOLDER);
			
			LPoint pt(0, btn->Y());
			btn->PointToScreen(pt);
			int Cmd = s.Float(this, pt.x, pt.y, LSubMenu::BtnLeft);
			if (Cmd == IDM_LOCAL_FOLDERS)
			{
				auto s = new LFileSelect(this);
				s->InitialDir(Opts);
				s->Name((char*)"Folders.mail3");
				s->Save([this, Opts=LString(Opts)](auto s, auto ok)
				{
					if (ok)
					{
						if (auto Si = new StoreItem(App))
						{
							auto Rel = LMakeRelativePath(Opts, s->Name());
					
							Si->Tag.SetAttr(OPT_MailStoreLocation, Rel ? Rel.Get() : s->Name());
					
							Lst->Insert(Si);
							Lst->ResizeColumnsToContent();
						}
					}
				});
			}
			else if (Cmd == IDM_WEBDAV_FOLDER)
			{
				auto Si = new StoreItem(App);
				if (!Si)
					break;
				
				EditWebdav(this, &Si->Tag, [this, Si](auto status)
				{
					if (status)
					{					
						Lst->Insert(Si);
						Lst->ResizeColumnsToContent();
					}
					else delete Si;
				});
			}
			break;
		}
		case IDC_COMPACT_MS:
		{
			if (auto ms = GetCurrentMailStore())
			{
				App->CompactFolders(*ms);
			}
			else
			{
				LgiMsg(this, LLoadString(IDS_ERROR_FOLDER_NOT_LOADED), AppName);
			}			
			break;
		}
		case IDC_CONVERT_MS:
		{
			if (auto ms = GetCurrentMailStore())
			{
				auto Dlg = new FmtDlg(this, (int)ms->Store->GetInt(FIELD_FORMAT));
				Dlg->DoModal([this, Dlg, ms](auto dlg, auto id)
				{
					if (id)
					{
						Store3Progress Prog(App, true);
						Prog.SetInt(Store3UiNewFormat, Dlg->Ver);
						if (!ms->Store->SetFormat(this, &Prog))
							LgiMsg(this, "Set format failed.", AppName);
					}
				});
			}			
			break;
		}
		case IDC_REPAIR_MS:
		{
			if (auto ms = GetCurrentMailStore())
			{
				auto Prog = new Store3Progress(App, true);
				ms->Store->Repair(this, Prog, [this, Prog](auto status)
				{
					if (!status)
					{
						auto Err = Prog->GetStr(Store3UiError);
						LgiMsg(	this,
								"Repair failed: %s",
								AppName,
								MB_OK,
								Err?Err:"Unsupported method.");
					}

					delete Prog;
				});
			}
			else LgiMsg(this, "Error: No mail store selected.", AppName);
			break;
		}
		case IDC_SUB_FOLDERS:
		{
			if (auto Dlg = new SubFolderDlg
					(
						this,
						App,
						App->GetOptions(),
						[this](auto Parent, auto App, auto Limit, auto cb)
						{
							if (auto Dlg = new FolderDlg(Parent, App, Limit))
								Dlg->DoModal([this, Dlg, cb](auto dlg, auto id)
								{
									if (id)
										cb(Dlg->Get());
								});
						}
					))
				Dlg->DoModal(NULL);
			break;
		}
		case IDC_SET_START_IN:
		{
			if (auto Dlg = new FolderDlg(this, App))
				Dlg->DoModal([this, Dlg](auto dlg, auto id)
				{
					if (id)
						SetCtrlName(IDC_START_IN, Dlg->Get());
				});
			break;
		}
		case IDOK:
		{
			Convert(&Options, this, false);
			// Fall through
		}
		case IDCANCEL:
		{
			EndModal(c->GetId() == IDOK);
			break;
		}
	}

	return 0;
}
