#include "Scribe.h"

#define CHECK_OUTPUT   1

ScribeOptions::ScribeOptions(char *file) : GSemaphore("ScribeOptions")
{
	Error = 0;
	if (file)
	{
		char FullPath[MAX_PATH_LEN];
		if (LgiIsRelitivePath(file))
		{
			char Exe[256];
			LgiGetExePath(Exe, sizeof(Exe));
			LMakePath(FullPath, sizeof(FullPath), FullPath, file);
		}
		else
		{
			strsafecpy(FullPath, file, sizeof(FullPath));
		}
		File = FileExists(FullPath) ? NewStr(FullPath) : 0;
	}
	else
	{
		File = 0;
	}

	Tag = NewStr("ScribeOptions");
	Dirty = false;
	_Defaults();
}

ScribeOptions::~ScribeOptions()
{
	DeleteArray(File);
}

bool ScribeOptions::IsValid()
{
	bool Status = false;

	if (Lock())
	{
		if (Attr.Length() > 3 AND
			Children.Length() > 0)
		{
			Status = true;
		}

		Unlock();
	}

	return Status;
}

void ScribeOptions::_Defaults()
{
	if (Lock())
	{
		GetTag("Accounts", true);
		GetTag("CalendarUI", true);
		GetTag("CalendarUI.Sources", true);
		GetTag("MailUI", true);
		GetTag("ScribeUI", true);
		GetTag("Plugins", true);
		GetTag("Print", true);

		Unlock();
	}
}

void ScribeOptions::SetFile(char *f)
{
	DeleteArray(File);
	File = NewStr(f);
	Dirty = true;
}

bool ScribeOptions::_OnAccess(bool Start)
{
	if (Start)
	{
		return Lock();
	}

	Unlock();
	return true;
}

bool ScribeOptions::DeleteValue(char *Name)
{
	bool Status = false;

	if (Name AND Lock())
	{
		LVariant v;
		SetValue(Name, v);
		Unlock();
	}

	return Status;
}

LXmlTag *ScribeOptions::LockTag(char *Name)
{
	LXmlTag *t = 0;

	if (Lock())
	{
		t = GetTag(Name);
		if (NOT t)
		{
			Unlock();
		}
	}

	return t;
}

bool ScribeOptions::CreateTag(char *Name)
{
	bool Status = false;

	if (Name AND Lock())
	{
		Status = GetTag(Name, true) != 0;
		Unlock();
	}

	return Status;
}

bool ScribeOptions::DeleteTag(char *Name)
{
	bool Status = false;

	if (Name AND Lock())
	{
		LXmlTag *t = GetTag(Name);
		if (t)
		{
			t->RemoveTag();
			DeleteObj(t);
			Status = true;
		}
		Unlock();
	}

	return Status;
}

bool ScribeOptions::Serialize(bool Write)
{
	bool Status = false;

	if (File AND Lock())
	{
		char Backup[300];
		LMakePath(Backup, sizeof(Backup), File, "../ScribeOptions.bak");

		if (Write AND FileExists(File))
		{
			// Backup the old file... in case we have a corrupt copy in memory...
			FileDev->DeleteFile(Backup, false);
			FileDev->MoveFile(File, Backup);
		}

		LFile f;
		if (f.Open(File, Write?O_WRITE:O_READ))
		{
			LXmlTree Tree(GXT_PRETTY_WHITESPACE);
			if (Write)
			{
				f.SetSize(0);
				Status = Tree.Write(this, &f);

				#if CHECK_OUTPUT
				f.Close();
				if (f.Open(File, O_READ))
				{
					LXmlTag *r = new LXmlTag;
					if (r)
					{
						LXmlTree t(GXT_PRETTY_WHITESPACE);
						if (NOT t.Read(r, &f, 0))
						{
							#ifdef _DEBUG
							LAssert(0);
							#else
							if (FileExists(Backup) AND
								LgiMsg(	MainWnd,
										"Due to a bug in the software the options file has been corrupted.\n"
										"Would you like to revert to a backup? (.\\ScribeOptions.bak)\n"
										"\n"
										"XML Error Msg: %s",
										AppName,
										MB_YESNO,
										t.GetErrorMsg()) == IDYES)
							{
								FileDev->DeleteFile(File, false);
								FileDev->CopyFile(Backup, File);
							}
							else
							{
								LgiMsg(	MainWnd,
										"Due to a bug in the software the options file has been corrupted.\n"
										"You could try and edit 'ScribeOptions.xml' back to valid XML.\n"
										"Otherwise, delete 'ScribeOptions.xml' and re-enter your settings\n"
										"from scratch. We are sorry for the inconvenience.\n"
										"\n"
										"XML Error Msg: %s",
										AppName,
										MB_OK,
										t.GetErrorMsg());
							}

							LExitApp();
							#endif
						}

						DeleteObj(r);
					}
				}
				#endif
			}
			else
			{
				Empty();
				if (Status = Tree.Read(this, &f, 0))
				{
					_Defaults();
				}
			}

			if (NOT Status)
			{
				Error = NewStr(Tree.GetErrorMsg());
			}
		}
		Unlock();
	}

	return Status;
}

