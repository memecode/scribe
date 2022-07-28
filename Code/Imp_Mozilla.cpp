// #include <stdlib.h>

#include "Scribe.h"
#include "lgi/common/Map.h"
#include "resdefs.h"
#include "lgi/common/TextFile.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"
#include "v3.6.14/sqlite3.h"

void ToRecord(GMap<int, int> &r, char *Str)
{
	char *n;
	for (char *s=strchr(Str, '('); s && *s; s=n)
	{
		n = strchr(s, ')');
		if (n)
		{
			s++;
			*n++ = 0;

			if (*s++ == '^')
			{
				int a = htoi(s);
				s = strchr(s, '^');
				if (s)
				{
					int b = htoi(++s);
					r[a] = b;
				}
			}

			n = strchr(n, '(');
		}
	}
}

void ToMap(GMap<int, char*> &m, char *Str)
{
	char *n;
	for (char *s=strchr(Str, '('); s && *s; s=n)
	{
		n = strchr(s, ')');
		if (n)
		{
			s++;
			*n++ = 0;
			char *e = strchr(s, '=');
			if (e)
			{
				*e++ = 0;
				int Var = htoi(s);
				if (Var > 0)
				{
					m[Var] = e;
				}
			}

			n = strchr(n, '(');
		}
	}
}

char *Decode(char *s)
{
	// static const char *Cp = "iso-8859-1";

	if (s && strchr(s, '$'))
	{
		char Hex[3] = {0, 0, 0};
		LStringPipe p(4 << 10);

		char *b = s;
		while (*s)
		{
			if (*s == '$')
			{
				if (b < s)
				{
					p.Push(b, (int) (s - b));
				}

				s++;
				if (s[0] && s[1])
				{
					Hex[0] = *s++;
					Hex[1] = *s++;

					char c = htoi(Hex);
					p.Push(&c, 1);

					/*
					if (*s == '$')
					{
						s++;
						if (s[0] && s[1])
						{
							Hex[0] = *s++;
							Hex[1] = *s++;
							char16 c = c1 | (htoi(Hex) << 8);
							char *Utf8 = WideToUtf8(&c, sizeof(c));
							if (Utf8)
							{
								p.Push(Utf8);
								DeleteArray(Utf8);
							}
						}
					}
					*/

					b = s;
				}
				else break;
			}
			else s++;
		}
		if (b < s)
		{
			p.Push(b, (int) (s - b));
		}

		return p.NewStr();
	}

	return NewStr(s);
}

struct Sqlite
{
	bool Open;
	sqlite3 *Db;

	Sqlite(const char *File)
	{	
		Open = Check(sqlite3_open(File, &Db));
	}
	
	~Sqlite()
	{
		if (Open)
			sqlite3_close(Db);
	}
	
	bool Check(int Code, const char *Sql = NULL)
	{
		if (Code == SQLITE_OK ||
			Code == SQLITE_DONE)
			return true;

		const char *Err = sqlite3_errmsg(Db);
		LgiTrace("%s:%i - Sqlite error %i: %s\n%s",
			_FL,
			Code, Err,
			Sql?Sql:(char*)"",
			Sql?"\n":"");
		LAssert(!"Db Error");

		return false;
	}
};

bool ImportMozillaAddresss(ScribeWnd *App, ScribeFolder *Folder, char *File)
{
	if (!App || !Folder || !File)
	{
		LgiTrace("%s:%i - Param error.\n", _FL);
		return false;
	}

	auto Ext = LGetExtension(File);
	if (!Ext)
	{
		LgiTrace("%s:%i - No extension '%s'.\n", _FL, File);
		return false;
	}

	bool Status = false;
	if (!stricmp(Ext, "mab"))
	{
		char *Text = LReadTextFile(File);
		if (Text)
		{
			int Angle = 0;
			int Square = 0;
			List<char> Blocks;
			List<char> Records;
			char *StartAngle = 0;
			char *StartSquare = 0;

			// Parse MAB...
			for (char *s=Text; s && *s; s++)
			{
				if (*s == '<')
				{
					if (_strnicmp(s+1, "!--", 3) == 0)
					{
						char *e = strstr(s, "-->");
						if (e) s = e + 3;
						else break;
					}
					else
					{
						Angle++;

						if (Angle == 1)
							StartAngle = s + 1;
					}
				}
				else if (*s == '>')
				{
					if (Angle > 0)
					{
						if (StartAngle && Angle == 1)
							Blocks.Insert(NewStr(StartAngle, s - StartAngle));

						Angle--;
					}
				}
				else if (Angle == 0)
				{
					if (*s == '[')
					{
						if (++Square == 1)
							StartSquare = s + 1;
					}
					else if (*s == ']')
					{
						if (StartSquare && Square == 1)
							Records.Insert(NewStr(StartSquare, s - StartSquare));

						Square--;
					}
				}
			}

			// Parse blocks..
			bool Fields = true;
			GMap<int, char*> Field;
			GMap<int, char*> Data;

			for (auto b: Blocks)
			{
				if (Fields)
				{
					ToMap(Field, b);
					Fields = false;
				}
				else
				{
					ToMap(Data, b);
				}
			}

			for (auto r: Records)
			{
				GMap<int, int> Record;
				ToRecord(Record, r);

				int Flds = 0;
				Contact *c = new Contact(App);
				if (c)
				{
					c->App = App;

					#define MapField(From, To) \
					{ \
						char *d = Data[Record[Field.Reverse((char*)From)]]; \
						if (d) \
						{ \
							char *s = Decode(d); \
							if (s) \
							{ \
								c->Set(To, s); \
								Flds++; \
								DeleteArray(s); \
							} \
						} \
					}

					MapField("FirstName", OPT_First);
					MapField("LastName", OPT_Last);
					MapField("NickName", OPT_Nick);
					MapField("PrimaryEmail", OPT_Email);
					MapField("WorkPhone", OPT_WorkPhone);
					MapField("HomePhone", OPT_HomePhone);
					MapField("FaxNumber", OPT_HomeFax);
					MapField("CellularNumber", OPT_HomeMobile);
					MapField("HomeAddress", OPT_HomeStreet);
					MapField("HomeAddress2", OPT_HomeSuburb);
					MapField("HomeState", OPT_HomeState);
					MapField("HomeZipCode", OPT_HomePostcode);
					MapField("HomeCountry", OPT_HomeCountry);
					MapField("WorkAddress", OPT_WorkStreet);
					MapField("WorkAddress2", OPT_WorkSuburb);
					MapField("WorkState", OPT_WorkState);
					MapField("WorkZipCode", OPT_WorkPostcode);
					MapField("WorkCountry", OPT_WorkCountry);
					MapField("Company", OPT_Company);
					MapField("WebPage1", OPT_WorkWebPage);
					MapField("WebPage2", OPT_HomeWebPage);
					MapField("CustomFields", OPT_CustomFields);
					MapField("Notes", OPT_Note);
					// HomeCity
					// WorkCity
					// JobTitle
					// Department
					// BirthYear
					// BirthMonth
					// BirthDay
					// LastModifiedDate
					// RecordKey
					// AddrCharSet
					// LastRecordKey
					// ListName
					// ListNickName
					// ListDescription
					// ListTotalAddresses
					// LowercaseListName
					// SecondEmail
					// PreferMailFormat
					// PagerNumber
					
					#undef MapField

					if (Flds > 0)
					{
						Folder->WriteThing(c, NULL);
						Status = true;
					}
					else
					{
						DeleteObj(c);
					}
				}
			}
		}
		else
		{
			LgiMsg(	App,
					"Couldn't read from '%s'\n"
					"Is Mozilla still open?",
					AppName,
					MB_OK,
					File);
		}
	}
	else if (!stricmp(Ext, "sqlite"))
	{
		Sqlite s(File);
		if (!s.Open)
		{
			LgiTrace("%s:%i - Failed to open '%s'\n", _FL, File);
			return false;
		}
		
		LString Sql = "select * from properties";
		sqlite3_stmt *Stmt = NULL;
		if (!s.Check(sqlite3_prepare_v2(s.Db, Sql, -1, &Stmt, 0), Sql))
			return false;
		
		LHashTbl<ConstStrKey<char,false>, Contact*> Map;
		int r;
		while ((r = sqlite3_step(Stmt)) == SQLITE_ROW)
		{
			const char *card = (const char *)sqlite3_column_text(Stmt, 0);
			auto name = sqlite3_column_text(Stmt, 1);
			auto value = sqlite3_column_text(Stmt, 2);
			
			Contact *c = Map.Find(card);
			if (!c)
			{
				if ((c = new Contact(App)))
					Map.Add(card, c);
			}
			if (!c || !name || !value)
				continue;
			
			printf("%s, %s, %s\n", card, name, value);
			
			#define MapField(src, dst) \
				if (!stricmp((const char*)name, src)) c->Set(dst, (char*)value)
			MapField("FirstName", OPT_First);
			MapField("LastName", OPT_Last);
			MapField("NickName", OPT_Nick);
			MapField("PrimaryEmail", OPT_Email);
			MapField("WorkPhone", OPT_WorkPhone);
			MapField("HomePhone", OPT_HomePhone);
			MapField("FaxNumber", OPT_HomeFax);
			MapField("CellularNumber", OPT_HomeMobile);
			MapField("HomeAddress", OPT_HomeStreet);
			MapField("HomeAddress2", OPT_HomeSuburb);
			MapField("HomeState", OPT_HomeState);
			MapField("HomeZipCode", OPT_HomePostcode);
			MapField("HomeCountry", OPT_HomeCountry);
			MapField("WorkAddress", OPT_WorkStreet);
			MapField("WorkAddress2", OPT_WorkSuburb);
			MapField("WorkState", OPT_WorkState);
			MapField("WorkZipCode", OPT_WorkPostcode);
			MapField("WorkCountry", OPT_WorkCountry);
			MapField("Company", OPT_Company);
			MapField("WebPage1", OPT_WorkWebPage);
			MapField("WebPage2", OPT_HomeWebPage);
			MapField("CustomFields", OPT_CustomFields);
			MapField("Notes", OPT_Note);
			#undef MapField
		}
		
		s.Check(sqlite3_finalize(Stmt), 0);
		
		Status = Map.Length() > 0;
		for (auto p: Map)
			Folder->WriteThing(p.value, NULL);
	}
	else
	{
		LgiTrace("%s:%i - Unsupported address book format '%s'\n", _FL, File);
	}

	return Status;
}

void Import_MozillaAddressBook(ScribeWnd *App)
{
	LFileSelect Select;
	LArray<char*> FindFiles;
	LArray<const char*> Ext;

	LFile::Path Str(
		#ifdef LINUX
		LSP_HOME
		#else
		LSP_USER_APP_DATA
		#endif
		);
	#ifdef LINUX
	Str += ".thunderbird";
	#endif
	Ext.Add("abook.mab");
	Ext.Add("abook.sqlite");
	LgiTrace("Searching '%s' for thunderbird address book files.\n", Str.GetFull().Get());
	LRecursiveFileSearch(Str, &Ext, &FindFiles);

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
			strcpy_s(DefaultFolder, sizeof(DefaultFolder), p);
	}

	LString::Array Files;
	for (auto f: FindFiles)
		Files.Add(f);
	FindFiles.DeleteArrays();

	// Ask user...
	auto Dlg = new ChooseFolderDlg(App,
						false,
						AppName,
						"Select input files and destination directory",
						DefaultFolder,
						MAGIC_CONTACT,
						&Files);
	Dlg->DoModal([App, Dlg](auto dlg, auto id)
	{
		if (id && Dlg->SrcFiles[0])
			ImportMozillaAddresss(App, App->GetFolder(Dlg->DestFolder), Dlg->SrcFiles[0]);
		delete dlg;
	});
}

#ifndef WIN32
int GetPrivateProfileStringA(const char *lpAppName,
							const char *lpKeyName,
							const char *lpDefault,
							char *lpReturnedString,
							int nSize,
							const char *lpFileName)
{
	// FIXME
	LAssert(0);
	return 0;
}
#endif

void Import_MozillaMail(ScribeWnd *App)
{
	char Path[MAX_PATH_LEN];
	if (!LGetSystemPath(LSP_USER_APP_DATA, Path, sizeof(Path)))
		return;

	LMakePath(Path, sizeof(Path), Path, "Thunderbird\\profiles.ini");
	if (!LFileExists(Path))
	{
		LgiMsg(App, LLoadString(IDS_ERROR_FILE_DOESNT_EXIST), AppName, MB_OK, Path);
		return;
	}

	char s[128];
	if (GetPrivateProfileStringA("Profile0", "Path", "", s, sizeof(s), Path) <= 0)
		return;

	if (LIsRelativePath(s))
	{
		LTrimDir(Path);
		LMakePath(Path, sizeof(Path), Path, s);
	}
	else
	{
		strcpy_s(Path, sizeof(Path), s);
	}
	LMakePath(Path, sizeof(Path), Path, "Mail");
	if (!LDirExists(Path))
	{
		LgiMsg(App, LLoadString(IDS_ERROR_FOLDER_DOESNT_EXIST), AppName, MB_OK, Path);
		return;
	}
		
	LArray<char*> FindFiles;
	if (!LRecursiveFileSearch(Path, 0, &FindFiles))
		return;

	// Clear out index files...
	for (unsigned i=0; i<FindFiles.Length(); i++)
	{
		auto f = FindFiles[i];
		int64 Size = LFileSize(f);
		char *Ext = strrchr(f, '.');
		if
		(
			Size == 0
			||
			(
				Ext
				&&
				(
					_stricmp(Ext, ".msf") == 0
					||
					_stricmp(Ext, ".dat") == 0
				)
			)
		)
		{
			FindFiles.DeleteAt(i);
			DeleteArray(f);
			i--;
		}
	}

	LString::Array Files;
	for (auto f: FindFiles)
		Files.New() = f;
	FindFiles.DeleteArrays();

	// Do UI
	ScribeFolder *Cur = App->GetCurrentFolder();
	LString CurPath;
	if (Cur)
		CurPath = Cur->GetPath();
	auto Dlg = new ChooseFolderDlg(App,
						false,
						"Mozilla/Thunderbird",
						LLoadString(IDS_IMPORT),
						CurPath,
						MAGIC_MAIL,
						&Files);
	Dlg->DoModal([App, Dlg](auto dlg, auto id)
	{
		if (id && Dlg->DestFolder)
		{
			ScribeFolder *Dest = App->GetFolder(Dlg->DestFolder);
			if (Dest)
			{
				for (auto Src: Dlg->SrcFiles)
				{
					char *Name = strrchr(Src, DIR_CHAR);
					if (!Name) Name = Src;
					else Name++;

					ScribeFolder *Child = Dest->CreateSubDirectory(Name, MAGIC_MAIL);
					if (Child)
					{
						GTextFile f;
						if (f.Open(Src, O_READ))
						{
							Child->Import(f, sMimeMbox);
						}
					}
				}

				Dest->Expanded(true);
			}
			else LgiMsg(App, LLoadString(IDS_ERROR_FOLDER_DOESNT_EXIST), AppName, MB_OK, Dlg->DestFolder);
		}

		delete dlg;
	});
}
