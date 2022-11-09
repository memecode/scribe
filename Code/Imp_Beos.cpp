
// Includes
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fs_attr.h>
#include <File.h>
#include <FindDirectory.h>
#include <Directory.h>

#include "Lgi.h"
#include "Scribe.h"

#define P_NAME				"META:name"
#define P_NICKNAME			"META:nickname"
#define P_COMPANY			"META:company"
#define P_ADDRESS			"META:address"
#define P_CITY				"META:city"
#define P_STATE				"META:state"
#define P_ZIP				"META:zip"
#define P_COUNTRY			"META:country"
#define P_HPHONE			"META:hphone"
#define P_WPHONE			"META:wphone"
#define P_FAX				"META:fax"
#define P_EMAIL				"META:email"
#define P_URL				"META:url"
#define P_GROUP				"META:group"

void MapField(Contact *c, char *Dest, BFile &f, char *Src)
{
	char Data[256];
	struct attr_info Info;
	if (f.GetAttrInfo(Src, &Info) == B_NO_ERROR)
	{
		f.ReadAttr(Src, B_STRING_TYPE, 0, Data, sizeof(Data));
		c->Set(Dest, Data);
	}
}

bool Import_BeosPeople(ScribeWnd *App)
{
	char Path[256];
	ScribeFolder *Contacts = App->GetFolder(FOLDER_CONTACTS);
	if (Contacts &&
		find_directory(B_USER_DIRECTORY, 0, false, Path, sizeof(Path)) == B_OK)
	{
		strcat(Path, "/people");
		BDirectory Dir(Path);
		if (Dir.InitCheck() == B_OK)
		{
			BEntry Entry;
			while (Dir.GetNextEntry(&Entry, true) == B_OK)
			{
				Contact *c = (Contact*) App->CreateItem(MAGIC_CONTACT, Contacts, false);;
				if (c)
				{
					char Str[256];
					struct attr_info Info;
	
					Entry.GetName(Str);
					BFile f(&Dir, Str, B_READ_ONLY);
					if (f.InitCheck() == B_OK)
					{
						char Data[256];
						
						if (f.GetAttrInfo(P_NAME, &Info) == B_NO_ERROR)
						{
							f.ReadAttr(P_NAME, B_STRING_TYPE, 0, Data, sizeof(Data));
							char *Space = strchr(Data, ' ');
							if (Space)
							{
								*Space = 0;
								c->Set(OPT_Last, Space+1);
							}
							c->Set(OPT_First, Data);
						}
	
						MapField(c, OPT_Nick, f, P_NICKNAME);
						// MapField(c, , f, P_COMPANY);
						MapField(c, OPT_HomeStreet, f, P_ADDRESS);
						MapField(c, OPT_HomeSuburb, f, P_CITY);
						MapField(c, OPT_HomeState, f, P_STATE);
						MapField(c, OPT_HomePostcode, f, P_ZIP);
						MapField(c, OPT_HomeCountry, f, P_COUNTRY);
						MapField(c, OPT_HomePhone, f, P_HPHONE);
						MapField(c, OPT_WorkPhone, f, P_WPHONE);
						MapField(c, OPT_WorkFax, f, P_FAX);
						MapField(c, OPT_Email, f, P_EMAIL);
						MapField(c, OPT_WorkWebPage, f, P_URL);
					}
				}
			}
		}
	}
	
	return false;
}

bool Import_MailDir(ScribeWnd *App, BDirectory *Dir, ScribeFolder *Folder)
{
	bool Status = false;
	
	if (App && Dir && Folder)
	{
		BEntry Entry;
		Status = true;
		while (Dir->GetNextEntry(&Entry, true) == B_OK)
		{
			char Str[256];
			Entry.GetName(Str);

			struct stat s;
			if (Entry.GetStat(&s) == B_OK)
			{
				if (S_ISREG(s.st_mode))
				{
					// Normal file
					BFile F(Dir, Str, B_READ_ONLY);
					if (F.InitCheck() == B_OK)
					{
						Mail *NewMsg = (Mail*) App->CreateItem(MAGIC_MAIL, Folder, false);
						if (NewMsg)
						{
							off_t Size = 0;
							F.GetSize(&Size);
							
							LMimeStream *Ms;
							LAutoPtr<LMime> Mime(Ms = new LMimeStream);
							if (Ms)
							{
								LAutoString Buf(new char[Size+1]);
								if (F.Read(Buf, Size) == Size)
								{
									Buf[Size] = 0;
									Ms->Parse();
									NewMsg->OnAfterReceive(Mime);
									Status = true;
								}
							}
						}
					}
				}
				else if (S_ISDIR(s.st_mode))
				{
					// Directory
					char FName[256];
					BDirectory D(Dir, Str);
					LAutoString Path = Folder->GetPath();					
					sprintf(FName, "%s/%s", Path.Get(), Str);
					ScribeFolder *F = App->GetFolder(FName);
					if (!F)
					{
						// folder doesn't exist... so create it
						F = Folder->CreateSubDirectory(Str, MAGIC_MAIL);
					}
					if (F)
					{
						// recurse down into that folder
						Status &= Import_MailDir(App, &D, F);
					}
				}
			}
		}
	}

	return Status;
}

bool Import_BeosMail(ScribeWnd *App)
{
	bool Status = false;
	char Path[256];
	ScribeFolder *Root = App->GetFolder("/");
	if (Root &&
		find_directory(B_USER_DIRECTORY, 0, false, Path, sizeof(Path)) == B_OK)
	{
		strcat(Path, "/mail");
		BDirectory Dir(Path);
		if (Dir.InitCheck() == B_OK)
		{
			Status = Import_MailDir(App, &Dir, Root);
		}
	}
	
	return Status;
}

bool Import_Beos(ScribeWnd *App, int Flags)
{
	bool Status = false;

	if (App)
	{
		if (Flags & IMP_BEOS_PEOPLE)
		{
			Status = Import_BeosPeople(App);
		}
		if (Flags & IMP_BEOS_MAIL)
		{
			Status = Import_BeosMail(App);
		}
	}

	return Status;
}
