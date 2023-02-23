#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "Store3Mail3/Mail3.h"
#include "ManageMailStores.h"
#include "lgi/common/List.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/Edit.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"
#include "resdefs.h"

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

	int OnNotify(LViewI *Ctrl, LNotification n)
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
class SubFolderDlg : public LDialog, public LXmlTreeUi
{
	ScribeWnd *App;

	void FolderSelector(int OutputCtrl, int Limit)
	{
		auto Dlg = new FolderDlg(this, App, Limit);
		Dlg->DoModal([this, Dlg, OutputCtrl](auto dlg, auto id)
		{
			if (id)
			{
				LEdit *e;
				if (GetViewById(OutputCtrl, e))
					e->Name(Dlg->Get());
			}
			delete dlg;
		});
	}

public:
	SubFolderDlg(LView *parent, ScribeWnd *app)
	{
		App = app;
		SetParent(parent);

		if (App->GetOptions() && LoadFromResource(IDD_SUB_FOLDERS))
		{
			Map(OPT_Inbox, IDC_INBOX, GV_STRING);
			Map(OPT_Outbox, IDC_OUTBOX, GV_STRING);
			Map(OPT_Sent, IDC_SENT, GV_STRING);
			Map(OPT_Trash, IDC_TRASH, GV_STRING);
			Map(OPT_Contacts, IDC_CONTACT_FLD, GV_STRING);
			Map(OPT_Templates, IDC_TEMPLATES, GV_STRING);
			Map(OPT_Calendar, IDC_CALENDER, GV_STRING);
			Map(OPT_Filters, IDC_FILTERS_FLD, GV_STRING);
			Map(OPT_Groups, IDC_GROUPS_FLD, GV_STRING);
			Map(OPT_SpamFolder, IDC_SPAM_FLD, GV_STRING);
			
			Map(OPT_HasTemplates, IDC_HAS_TEMPLATES, GV_BOOL);
			Map(OPT_HasGroups, IDC_HAS_GROUPS, GV_BOOL);
			Map(OPT_HasCalendar, IDC_HAS_CAL_EVENTS, GV_BOOL);
			Map(OPT_HasFilters, IDC_HAS_FILTERS, GV_BOOL);
			Map(OPT_HasSpam, IDC_HAS_SPAM, GV_BOOL);
			
			Convert(App->GetOptions(), this, true);
			MoveToCenter();
		}
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
				Convert(App->GetOptions(), this, false);
				EndModal(1);
				break;
			case IDCANCEL:
				EndModal(0);
				break;
			case IDC_SET_INBOX:
				FolderSelector(IDC_INBOX, MAGIC_MAIL);
				break;
			case IDC_SET_OUTBOX:
				FolderSelector(IDC_OUTBOX, MAGIC_MAIL);
				break;
			case IDC_SET_SENT:
				FolderSelector(IDC_SENT, MAGIC_MAIL);
				break;
			case IDC_SET_TRASH:
				FolderSelector(IDC_TRASH, MAGIC_ANY);
				break;
			case IDC_SET_CONTACTS:
				FolderSelector(IDC_CONTACT_FLD, MAGIC_CONTACT);
				break;
			case IDC_SET_TEMPLATES:
				FolderSelector(IDC_TEMPLATES, MAGIC_MAIL);
				break;
			case IDC_SET_CALENDER:
				FolderSelector(IDC_CALENDER, MAGIC_CALENDAR);
				break;
			case IDC_SET_FILTERS:
				FolderSelector(IDC_FILTERS_FLD, MAGIC_FILTER);
				break;
			case IDC_SET_GROUPS:
				FolderSelector(IDC_GROUPS_FLD, MAGIC_GROUP);
				break;
			case IDC_SET_SPAM:
				FolderSelector(IDC_SPAM_FLD, MAGIC_MAIL);
				break;
		}

		return 0;
	}
};

////////////////////////////////////////////////////////////////////////////////////////
LVariant FolderFullPath(const char *Path)
{
	char f[MAX_PATH_LEN];
	if (LIsRelativePath(Path))
	{
		LMakePath(f, sizeof(f), LGetExePath(), Path);
		return f;
	}

	return Path;
}

int GetFolderVersion(const char *f)
{
	LVariant Path = FolderFullPath(f);
	if (LDirExists(Path.Str()))
	{
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Path.Str(), MAIL3_DB_FILE);
		if (LFileExists(p))
		{
			return 3;
		}
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
			callback(true);
		}
		else callback(false);
		delete dlg;
	});
}

class StoreItem : public LListItem
{
	ScribeWnd *App;
	LListItemCheckBox *Disable;

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
		EditWebdav(GetList(), &Tag, [&](auto status)
		{
			if (status)
				Update();
		});
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
					break;
				}
			}
		}
		
		LListItem::OnMouseClick(m);
	}
};

LListItem *CreateStoreItem(void *User)
{
	return new StoreItem((ScribeWnd*)User);
}

ManageMailStores::ManageMailStores(ScribeWnd *app)
{
	Lst = 0;
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

		Map(OPT_MailStores, IDC_MAIL_STORES, CreateStoreItem, OPT_MailStore, App);
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
	int Ver = s ? GetFolderVersion(s->GetText(1)) : 0;
	SetCtrlEnabled(IDC_COMPACT_MS, Ver > 0);
	SetCtrlEnabled(IDC_CONVERT_MS, Ver == 3);
	SetCtrlEnabled(IDC_REPAIR_MS, true);
}

LMailStore *ManageMailStores::GetCurrentMailStore()
{
	LListItem *s = Lst->GetSelected();
	if (s)
	{
		LVariant p = FolderFullPath(s->GetText(1));

		for (unsigned i=0; i<App->GetStorageFolders().Length(); i++)
		{
			LMailStore &s = App->GetStorageFolders()[i];

			if (s.Path && !_stricmp(s.Path, p.Str()))
			{
				if (s.Store)
					return &s;
			}
		}
	}

	return 0;
}

int ManageMailStores::OnNotify(LViewI *c, LNotification n)
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
						char *n = LGetExtension(b);
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
				delete dlg;
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
				s->Save([&](auto dlg, auto status)
				{
					if (status)
					{
						StoreItem *Si = new StoreItem(App);
						if (Si)
						{
							auto Rel = LMakeRelativePath(Opts, s->Name());
					
							Si->Tag.SetAttr(OPT_MailStoreLocation, Rel ? Rel.Get() : s->Name());
					
							Lst->Insert(Si);
							Lst->ResizeColumnsToContent();
						}
					}
					delete dlg;
				});
			}
			else if (Cmd == IDM_WEBDAV_FOLDER)
			{
				StoreItem *Si = new StoreItem(App);
				if (!Si)
					break;
				
				EditWebdav(this, &Si->Tag, [&](auto status)
				{
					if (status)
					{					
						Lst->Insert(Si);
						Lst->ResizeColumnsToContent();
					}
					else DeleteObj(Si);
				});
			}
			break;
		}
		case IDC_COMPACT_MS:
		{
			LMailStore *ms = GetCurrentMailStore();
			if (ms)
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
			LMailStore *ms = GetCurrentMailStore();
			if (ms)
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
					delete dlg;
				});
			}			
			break;
		}
		case IDC_REPAIR_MS:
		{
			LMailStore *ms = GetCurrentMailStore();
			if (ms)
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
			auto Dlg = new SubFolderDlg(this, App);
			Dlg->DoModal(NULL);
			break;
		}
		case IDC_SET_START_IN:
		{
			auto Dlg = new FolderDlg(this, App);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
					SetCtrlName(IDC_START_IN, Dlg->Get());
				delete dlg;
			});
			break;
		}
		case IDOK:
		{
			Convert(&Options, this, false);
		}
		case IDCANCEL:
		{
			EndModal(c->GetId() == IDOK);
			break;
		}
	}

	return 0;
}
