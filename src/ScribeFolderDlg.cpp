
#include "Scribe.h"
#include "ScribeFolderDlg.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

////////////////////////////////////////////////////////////////////////////
class RecentItem : public LListItem
{
	ScribeFolderDlg *Dlg;
public:
	RecentItem(ScribeFolderDlg *dlg, char *FileName)
	{
		Dlg = dlg;
		SetText(FileName);
	}

	void OnSelect()
	{
		Dlg->SetCtrlName(IDC_EXISTING_FOLDER, GetText(0));
	}

	void OnMouseClick(LMouse &m)
	{
		if (Dlg && m.Double())
		{
			Dlg->PostEvent(M_CHANGE, IDOK);
		}
	}
};

////////////////////////////////////////////////////////////////////////////
ScribeFolderDlg::ScribeFolderDlg(ScribeWnd *app)
{
	SetParent(App = app);
	Create = false;

	if (LoadFromResource(IDD_NO_FOLDER))
	{
		// Load MRU
		LList *Recent;
		if (GetViewById(IDC_FOLDER_HISTORY, Recent))
		{
			for (int i=0; i<10; i++)
			{
				char Key[32];
				LVariant f;
				sprintf_s(Key, sizeof(Key), "FolderMru.%i", i);
				if (App->GetOptions()->GetValue(Key, f))
				{
					Recent->Insert(new RecentItem(this, f.Str()));
				}
			}
		}

		// Look at folders...
		LArray<char*> Current;
		int Exists = 0;
		LXmlTag *MailStores = App->GetOptions()->LockTag(OPT_MailStores, _FL);
		if (MailStores)
		{
			for (auto t: MailStores->Children)
			{
				char *File = t->GetAttr(OPT_MailStoreLocation);
				if (ValidStr(File))
				{
					Current.Add(NewStr(File));
					Exists += LFileExists(File) ? 1 : 0;
				}
			}
			App->GetOptions()->Unlock();
		}

		if (Current.Length())
		{
			char Msg[256];
			if (Exists)
			{
				// another app has it open
				SetCtrlValue(IDC_ACTION, 1);

				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ERROR_CANT_OPEN_FOLDERS), Current[0]);
				SetCtrlName(IDC_MESSAGE, Msg);
			}
			else
			{
				// doesn't exist...
				SetCtrlValue(IDC_ACTION, 0);

				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ERROR_FOLDERS_DONT_EXIST), Current[0]);
				SetCtrlName(IDC_MESSAGE, Msg);
				SetCtrlName(IDC_NEW_FOLDER, Current[0]);
			}
		}
		else
		{
			// no file actually specified... this is most likely because it's
			// a new installation.
			char Def[MAX_PATH_LEN] = "";
			LAutoString Base = App->GetDataFolder();
			if (LMakePath(Def, sizeof(Def), Base, "Folders.mail3"))
				SetCtrlName(IDC_NEW_FOLDER, Def);
			else
				LgiTrace("%s:%i - LMakePath failed.\n", _FL);				

			SetCtrlValue(IDC_ACTION, 0);
			SetCtrlName(IDC_MESSAGE, LLoadString(IDS_ENTER_FILE_NAME_FOR_FOLDERS));
		}
		Current.DeleteArrays();

		LViewI *w = FindControl(IDC_ACTION);
		LNotification note(LNotifyValueChanged);
		if (w) OnNotify(w, note);
	}

	MoveToCenter();
}

int ScribeFolderDlg::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDC_BROWSE_NEW:
		{
			auto Select = new LFileSelect(this);
			auto Txt = GetCtrlName(IDC_NEW_FOLDER);
			if (Txt)
			{
				char Def[MAX_PATH_LEN];
				strcpy_s(Def, sizeof(Def), Txt);
				LTrimDir(Def);
				Select->InitialDir(Def);
			}
			else
			{
				Select->InitialDir(LGetExePath());
			}
			

			Select->Type("v3 Mail Store", "*.sqlite");
			Select->Type("All Files", LGI_ALL_FILES);

			Select->Save([this](auto s, auto ok)
			{
				if (ok)
				{
					char Def[MAX_PATH_LEN];
					strcpy_s(Def, sizeof(Def), s->Name());
					char *d = strrchr(Def, DIR_CHAR);
					if (d)
					{
						char *e = strrchr(d, '.');
						if (!e)
						{
							if (s->SelectedType() == 0)
								strcat(d, ".mail3");
						}

						SetCtrlName(IDC_NEW_FOLDER, Def);
					}
					else LgiMsg(this, "Error: Invalid path.", AppName);
				}
			});
			break;
		}
		case IDC_BROWSE_EXISTING:
		{
			auto Select = new LFileSelect(this);
			char Def[300];
			auto Txt = GetCtrlName(IDC_EXISTING_FOLDER);
			if (Txt)
			{
				strcpy_s(Def, sizeof(Def), Txt);
				LTrimDir(Def);
				Select->InitialDir(Def);
			}
			else
			{
				Select->InitialDir(LGetExePath());
			}

			Select->Type("v3 Mail Store", "*.sqlite");
			Select->Type("All Files", LGI_ALL_FILES);

			Select->Open([this](auto dlg, auto id)
			{
				if (id)
				{
					if (LFileExists(dlg->Name()) || LDirExists(dlg->Name()))
						SetCtrlName(IDC_EXISTING_FOLDER, dlg->Name());
					else
						LgiMsg(this, LLoadString(IDS_ERROR_FOLDERS_DONT_EXIST), AppName, MB_OK, dlg->Name());
				}
			});
			break;
		}
		// case IDC_ACTION:
		case IDC_CREATE_FOLDER:
		case IDC_EXISTING:
		{
			Create = GetCtrlValue(IDC_CREATE_FOLDER) != 0;

			SetCtrlEnabled(IDC_NEW_FOLDER, Create);
			SetCtrlEnabled(IDC_BROWSE_NEW, Create);

			SetCtrlEnabled(IDC_EXISTING_FOLDER, !Create);
			SetCtrlEnabled(IDC_BROWSE_EXISTING, !Create);
			SetCtrlEnabled(IDC_FOLDER_HISTORY, !Create);
			break;
		}
		case IDOK:
		{
			Create = GetCtrlValue(IDC_CREATE_FOLDER) != 0;
			FolderFile.Reset(NewStr(GetCtrlName(Create ? IDC_NEW_FOLDER : IDC_EXISTING_FOLDER)));
			LgiTrace("In IDOK, FolderFile=%s\n", FolderFile.Get());
			// fall thru
		}
		case IDCANCEL:
		{
			EndModal(Ctrl->GetId());
			break;
		}
	}

	return 0;
}
