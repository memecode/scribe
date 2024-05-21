#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"

char EudoraPathKey[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Eudora.exe";

bool ImportEudoraAddresss(ScribeWnd *App, ScribeFolder *Folder, char *File)
{
	if (!App || !Folder || !File)
		return false;

	LFile f(File);
	if (!f)
		return false;

	auto Text = f.Read();
	if (!Text)
		return false;

	bool Status = false;
	char *Alias = 0;
	char *Address = 0;

	auto L = Text.SplitDelimit("\r\n");
	for (unsigned i=0; i<L.Length(); i++)
	{
		auto S = L[i].SplitDelimit(" ");
		if (S.Length() > 2)
		{
			if (_stricmp(S[0], "alias") == 0)
			{
				DeleteArray(Alias);
				Alias = NewStr(S[1]);
				DeleteArray(Address);
				Address = NewStr(S[2]);
			}
			else if (_stricmp(S[0], "note") == 0)
			{
				if (Alias && Address)
				{
					Contact *c = new Contact(App);
					if (c)
					{
						c->App = App;

						// set basic information
						c->Set(OPT_Nick, Alias);
						c->Set(OPT_Email, Address);

						// parse through all the tags..
						char *n = 0;
						for (char *s = strchr(L[i], '<'); s && *s; s = n)
						{
							char *Var = ++s;
							n = strchr(Var, '>');
							if (n)
							{
								*n++ = 0;

								char *Val = strchr(Var, ':');
								if (Val)
								{
									*Val++ = 0;

									#define Map(From, To) else if (_stricmp(Var, From) == 0) c->Set(To, Val)
									if (_stricmp(Var, "first") == 0)
									{
										c->Set(OPT_First, Val);
									}
									Map("last", OPT_Last);
									Map("address", OPT_HomeStreet);
									Map("city", OPT_HomeSuburb);
									Map("state", OPT_HomeState);
									Map("country", OPT_HomeCountry);
									Map("zip", OPT_HomePostcode);
									Map("phone", OPT_HomePhone);
									Map("fax", OPT_HomeFax);
									Map("mobile", OPT_HomeMobile);
									Map("web", OPT_HomeWebPage);
									// Map("title", );
									Map("company", OPT_Company);
									Map("address2", OPT_WorkStreet);
									Map("city2", OPT_WorkSuburb);
									Map("country2", OPT_WorkCountry);
									Map("zip2", OPT_WorkPostcode);
									Map("phone2", OPT_WorkPhone);
									Map("fax2", OPT_WorkFax);
									Map("web2", OPT_WorkWebPage);
									else if (_stricmp(Var, "name") == 0)
									{
										char *Sp = strchr(Val, ' ');
										if (Sp)
										{
											*Sp++ = 0;
											c->Set(OPT_First, Val);
											c->Set(OPT_Last, Sp);
										}
										else
										{
											c->Set(OPT_First, Val);
										}
									}
									else if (_stricmp(Var, "otheremail") == 0)
									{
												
									}
								}
							}

							char *Next = strchr(n, '<');
							if (!Next && strlen(n) > 0)
							{
								c->Set(OPT_Note, n);
								n = 0;
							}
							else n = Next;
						}

						Status |= (Folder->WriteThing(c, NULL) != Store3Error);
					}
				}
			}
		}
	}

	return Status;
}

void Import_EudoraAddressBook(ScribeWnd *App)
{
	char Str[256] = "/";
	#ifdef WIN32
		HKEY hKey;
		if (RegOpenKeyA(HKEY_LOCAL_MACHINE, EudoraPathKey, &hKey) == ERROR_SUCCESS)
		{
			DWORD Type = 0;
			DWORD Size = sizeof(Str);
			RegQueryValueExA(hKey, "Path", 0, &Type, (uchar*)Str, &Size);
			RegCloseKey(hKey);
		}
	#endif

	LString::Array SrcFiles;
	LMakePath(Str, sizeof(Str), Str, "NNdbase.txt");
	if (LFileExists(Str))
		SrcFiles.Add(Str);

	// Get default path..
	char DefaultFolder[256] = "/Contacts";
	ScribeFolder *f = App->GetCurrentFolder();
	if (!f || f->GetItemType() != MAGIC_CONTACT)
	{
		f = App->GetFolder(FOLDER_CONTACTS);
	}
	if (f && f->GetItemType() == MAGIC_CONTACT)
	{
		auto p = f->GetPath();
		if (p)
		{
			strcpy_s(DefaultFolder, sizeof(DefaultFolder), p);
		}
	}

	// Ask user...
	auto Dlg = new ImportExportDlg(	App,
									false,
									"Eudora",
									LLoadString(IDS_SELECT_IO),
									&SrcFiles,
									DefaultFolder,
									MAGIC_CONTACT);
	Dlg->DoModal([App, Dlg](auto dlg, auto id)
	{
		if (id && Dlg->SrcFiles[0])
			ImportEudoraAddresss(App, App->GetFolder(Dlg->DestFolder), Dlg->SrcFiles[0]);
	});
}
