#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/Db.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

//////////////////////////////////////////////////////////////////////////
class LFieldMap : public LListItem
{
	LDbField &From;
	ItemFieldDef *To;
	LString Txt;

public:
	LFieldMap(LDbField &from) : From(from)
	{
		To = 0;

		#ifdef WIN32
		Txt = LFromNativeCp(From.Name());
		#else
		Txt = From.Name();
		#endif

		for (ItemFieldDef *f = ContactFieldDefs; f->Option; f++)
		{
			if (_stricmp(From.Name(), f->DisplayText) == 0)
			{
				To = f;
				break;
			}
		}
	}

	int FieldId()
	{
		return To ? To->FieldId : -1;
	}

	void FieldId(int i)
	{
		To = GetFieldDefById(i);
		Update();
	}

	const char *GetText(int c)
	{
		switch (c)
		{
			case 0:
			{
				return Txt;
			}
			case 1:
			{
				if (To)
				{
					const char *n = LLoadString(To->FieldId);
					return n ? n : To->DisplayText;
				}
				break;
			}
		}

		return 0;
	}

	void OnMouseClick(LMouse &m)
	{
		auto RClick = new LSubMenu;
		if (RClick)
		{
			int n=0;
			for (ItemFieldDef *f = ContactFieldDefs; f->Option; f++)
			{
				const char *Name = LLoadString(f->FieldId);
				RClick->AppendItem(Name ? Name : f->DisplayText, 1000+n++, true);
			}			

			if (Parent->GetMouse(m, true))
			{
				int i = RClick->Float(Parent, m.x, m.y, m.Right());
				if (i>=1000)
				{
					To = ContactFieldDefs + i - 1000;
					Update();
				}
			}

			DeleteObj(RClick);
		}
	}

	void Convert(Contact *c)
	{
		if (From && To)
		{
			LVariant v;
			if (From.Get(v))
			{
				char *s = v.Str();
				if (s)
				{
					if (To->FieldId != FIELD_NOTE)
					{
						char *Out = s;
						for (char *In=s; *In; In++)
						{
							if (*In != '\r' && *In != '\n')
							{
								*Out++ = *In;
							}
						}
						*Out++ = 0;
					}
					
					c->Set(To->Option, s);
				}
			}
		}
	}
};

class LImpCsv : public LDialog
{
	ScribeWnd *App;
	LList *Map;

public:
	List<LFieldMap> Mapping;
	ScribeFolder *Folder;
	bool Merge;

	LImpCsv(ScribeWnd *app, LDbRecordset *Rs)
	{
		Folder = 0;
		Map = 0;
		Merge = false;
		SetParent(App = app);
		if (LoadFromResource(IDD_CSV_IMPORT))
		{
			MoveToCenter();

			if (GetViewById(IDC_MAPPING, Map))
			{
				Map->DrawGridLines(true);
				for (int i=0; i<Rs->Fields(); i++)
				{
					LDbField &Fld = (*Rs)[i];
					LFieldMap *m = new LFieldMap(Fld);
					if (m)
					{
						Mapping.Insert(m);
						Map->Insert(m);
					}
				}
			}
		}

		Folder = App->GetCurrentFolder();
		if (Folder && Folder->GetItemType() != MAGIC_CONTACT)
		{
			Folder = App->GetFolder(FOLDER_CONTACTS);
		}

		if (Folder)
			SetCtrlName(IDC_FOLDER, Folder->GetPath());
	}

	void SaveMapping(const char *File)
	{
		LFile f;
		if (Map && f.Open(File, O_WRITE))
		{
			f.SetSize(0);
			
			LXmlTree Xml;
			LXmlTag Root;
			Root.SetTag("field-map");

			List<LFieldMap> All;
			Map->GetAll(All);
			for (auto i: All)
			{
				LXmlTag *c;
				Root.InsertTag(c = new LXmlTag);
				if (c)
				{
					c->SetTag("mapping");
					c->SetAttr("from", i->GetText(0));
					if (i->FieldId() >= 0)
					{
						char s[32];
						sprintf_s(s, sizeof(s), "%i", i->FieldId());					
						c->SetAttr("to", s);
					}
				}
			}			
			
			Xml.Write(&Root, &f);
		}
		else
		{
			LgiMsg(this, "Couldn't open '%s'\n", AppName, MB_OK, File);
		}
	}
	
	void LoadMapping(const char *File)
	{
		LFile f;
		if (Map && f.Open(File, O_READ))
		{
			LXmlTree Xml;
			LXmlTag Root;
			Root.SetTag("field-map");

			if (Xml.Read(&Root, &f, 0))
			{
				List<LFieldMap> All;
				Map->GetAll(All);
				for (auto i: All)
				{
					for (auto t: Root.Children)
					{
						char *From1 = t->GetAttr("from");
						const char *From2 = i->GetText(0);
						if (From1 && From2 && _stricmp(From1, From2) == 0)
						{
							char *Id;
							if ((Id = t->GetAttr("to")))
							{
								i->FieldId(atoi(Id));
							}
							break;
						}
					}
				}
			}			
		}
		else
		{
			LgiMsg(this, "Couldn't open '%s'\n", AppName, MB_OK, File);
		}
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_SAVE:
			{
				LFileSelect s;
				s.Parent(this);
				s.Type("XML", "*.xml");
				s.Type("All Files", LGI_ALL_FILES);
				if (s.Save())
				{
					SaveMapping(s.Name());
				}
				break;
			}
			case IDC_LOAD:
			{
				LFileSelect s;
				s.Parent(this);
				s.Type("XML", "*.xml");
				s.Type("All Files", LGI_ALL_FILES);
				if (s.Open())
				{
					LoadMapping(s.Name());					
				}
				break;
			}
			case IDC_BROWSE_FOLDER:
			{
				FolderDlg d(this, App, MAGIC_CONTACT);
				if (d.DoModal())
				{
					char *NewPath = d.Get();
					if (NewPath)
					{
						Folder = App->GetFolder(NewPath);
						if (Folder)
							SetCtrlName(IDC_FOLDER, Folder->GetPath());
					}
				}
				break;
			}
			case IDOK:
			{
				Merge = GetCtrlValue(IDC_MODE) == 0;
			}
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
				break;
			}
		}

		return 0;
	}
};

bool ImgCsvMatch(const char *a, const char *b)
{
	if (!a && !b)
	{
		return true;
	}
	
	if (a && b)
	{
		return _stricmp(a, b) == 0;
	}
	
	return false;
}

void ImportCsv(ScribeWnd *App)
{
	LFileSelect s;
	s.Parent(App);
	s.Type("Comma Separated Text", "*.csv;*.txt");
	s.Type("All Files", LGI_ALL_FILES);
	if (s.Open() && LFileExists(s.Name()))
	{
		LDb *Db = OpenCsvDatabase(s.Name());
		if (Db)
		{
			LDbRecordset *Rs = Db->TableAt(0);
			if (Rs)
			{
				LImpCsv Dlg(App, Rs);
				if (Dlg.DoModal())
				{
					if (Dlg.Folder)
					{
						for (bool b=Rs->MoveFirst(); b; b=Rs->MoveNext())
						{
							Contact *c = new Contact(App);
							if (c)
							{
								c->App = App;
								
								for (auto fm: Dlg.Mapping)
								{
									fm->Convert(c);
								}

								if (Dlg.Merge)
								{
									Contact *m = 0;
									const char *CFirst = 0, *CLast = 0;
									c->Get(OPT_First, CFirst);
									c->Get(OPT_Last, CLast);
									
									for (auto t: Dlg.Folder->Items)
									{
										Contact *i = t->IsContact();
										if (i)
										{
											const char *IFirst = 0, *ILast = 0;
											i->Get(OPT_First, IFirst);
											i->Get(OPT_Last, ILast);
											
											if (ImgCsvMatch(IFirst, CFirst) &&
												ImgCsvMatch(ILast, CLast))
											{
												m = i;
												break;
											}
										}
									}
									
									if (m)
									{
										// Convert across fields.
										for (ItemFieldDef *Def = ContactFieldDefs; Def->FieldId; Def++)
										{
											const char *s;
											if (c->Get(Def->DisplayText, s))
											{
												#ifdef WIN32
												auto t = LFromNativeCp(s);
												if (t)
													m->Set(Def->Option, t);
												#else
												m->Set(Def->Option, s);
												#endif
											}
										}
										
										m->Save();
										c->DecRef();
										c = NULL;
										m->Update();
									}
									else
									{
										// No match, new entry
										c->Save(Dlg.Folder);
									}
								}
								else
								{
									c->Save(Dlg.Folder);
								}
							}
						}
					}
				}
			}

			DeleteObj(Db);
		}
	}
}

class LExportCsv : public LDialog
{
	ScribeWnd *App;

public:
	char *Folder;
	bool SubFolders;

	LExportCsv(ScribeWnd *app)
	{
		Folder = 0;
		SubFolders = false;
		SetParent(App = app);
		if (LoadFromResource(IDD_CSV_EXPORT))
		{
			MoveToCenter();
			SetCtrlEnabled(IDC_FOLDERS, false);

			LString p;
			ScribeFolder *f = App->GetCurrentFolder();
			if (f && f->GetItemType() == MAGIC_CONTACT && (p = f->GetPath()) != 0)
			{
				SetCtrlName(IDC_FOLDERS, p);
			}
			else if ((f = App->GetFolder(FOLDER_CONTACTS)) != 0 &&
					 (p = f->GetPath()) != 0)
			{
				SetCtrlName(IDC_FOLDERS, p);
			}
		}
	}

	~LExportCsv()
	{
		DeleteArray(Folder);
	}

	int OnNotify(LViewI *v, LNotification n)
	{
		switch (v->GetId())
		{
			case IDC_SET_FOLDER:
			{
				FolderDlg d(this, App, MAGIC_CONTACT);
				if (d.DoModal())
				{
					char *NewPath = d.Get();
					if (NewPath)
					{
						SetCtrlName(IDC_FOLDERS, NewPath);
					}
				}
				break;
			}
			case IDOK:
			{
				Folder = NewStr(GetCtrlName(IDC_FOLDERS));
				SubFolders = GetCtrlValue(IDC_SUB_FOLDERS) != 0;
			}
			case IDCANCEL:
			{
				EndModal(v->GetId() == IDOK);
				break;
			}
		}

		return 0;
	}
};

void ExportCsv(ScribeWnd *App)
{
	LExportCsv Dlg(App);
	if (Dlg.DoModal())
	{
		ScribeFolder *Folder = App->GetFolder(Dlg.Folder);
		if (Folder)
		{
			LFileSelect s;
			s.Parent(App);
			s.Type("Comma Separated Text", "*.csv");
			s.Type("All Files", LGI_ALL_FILES);
			if (s.Save())
			{
				LString MsgStr = AskOverwriteMsg(s.Name());
				bool Exists = LFileExists(s.Name());
				if (!Exists || LgiMsg(App, MsgStr, AppName, MB_YESNO) == IDYES)
				{
					int Exported = 0;
					const char *Error = 0;

					if (Exists)
						FileDev->Delete(s.Name());

					LDb *Db = OpenCsvDatabase(s.Name());
					LAssert(Db != NULL);
					if (Db)
					{
						LDbRecordset *Rs = Db->TableAt(0);
						LAssert(Rs != NULL);
						if (Rs)
						{
							for (ItemFieldDef *f=ContactFieldDefs; f->Option && f->FieldId; f++)
							{
								Rs->InsertField(f->DisplayText, GV_STRING);
							}
							Rs->InsertField("AltEmail", GV_STRING);

							List<Contact> Contacts;
							App->GetContacts(Contacts, Folder, Dlg.SubFolders);
							if (Contacts[0])
							{
								for (auto c: Contacts)
								{
									if (Rs->AddNew())
									{
										int Index = 0;
										for (ItemFieldDef *f=ContactFieldDefs; f->Option && f->FieldId; f++, Index++)
										{
											const char *n;
											if (c->Get(f->Option, n))
											{
												LVariant v(n);
												(*Rs)[Index].Set(v);
											}
										}

										auto Emails = LString(",").Join(c->GetEmails().Slice(1));
										if (Emails)
											(*Rs)[Index] = Emails;

										if (Rs->Update())
											Exported++;
									}
								}
							}
							else Error = "No Contacts.";
						}
						else Error = "Couldn't open record set.";

						DeleteObj(Db);
					}
					else Error = "Couldn't open database.";

					LgiMsg(App, LLoadString(IDS_EXPORT_MSG), AppName, MB_OK, Exported, Error?Error:(char*)"");
				}
			}
		}
	}
}
