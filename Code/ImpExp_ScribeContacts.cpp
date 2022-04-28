#include "Scribe.h"
#include "GToken.h"

void Import_ScribeContacts(ScribeWnd *Parent)
{
	if (Parent)
	{
		LFileSelect Select;

		Select.Parent(Parent);
		Select.Type("Binary contacts files", "*.contacts");
		Select.Type("Csv contacts files", "*.csv");

		if (Select.Open())
		{
			bool Csv = false;
			GFileType *Type = Select.TypeAt(Select.SelectedType());
			if (Type)
			{
				Csv = stristr(Type->Extension(), ".csv") != 0;
			}

			LFile f;
			if (f.Open(Select.Name(), O_READ))
			{
				ScribeFolder *Contacts = Parent->GetFolder(FOLDER_CONTACTS);
				if (!Contacts AND Parent->GetMailbox())
				{
					// create the folder if it doesn't exist
					Contacts = Parent->GetMailbox()->CreateSubDirectory("Contacts", MAGIC_CONTACT);
				}

				if (Contacts AND Contacts->Store)
				{
					if (Csv)
					{
						// read in csv contacts
						int Len = f.GetSize();
						char *Buf = new char[Len+1];
						if (Buf AND
							f.Read(Buf, Len) == Len)
						{
							Buf[Len] = 0;
							GToken Lines(Buf, "\r\n");
							if (Lines.Length() > 1)
							{
								// process fields
								GToken Fields(Lines[0], "\",");
								if (Fields.Length() > 0)
								{
									// read in all the contact records
									for (int i=1; i<Lines.Length(); i++)
									{
										Contact *c = new Contact;
										if (c)
										{
											int Col = 0;
											int FieldsProcessed = 0;

											char *s = Lines[i];
											while (s AND *s)
											{
												char Delim = ',';
												if (*s == '\"')
												{
													Delim = *s++;
												}

												char *e = s;
												while (*e AND *e != Delim) e++;

												char *FieldName = Fields[Col];
												int DataLen = (int)e-(int)s;
												if (DataLen > 0 AND
													FieldName)
												{
													char *Data = NewStr(s, DataLen);
													if (Data)
													{
														ItemFieldDef *Field = 0;
														for (Field=ContactFieldDefs; Field->Id(); Field++)
														{
															if (stricmp(Field->Option(), FieldName) == 0)
															{
																break;
															}
														}

														if (Field->Id())
														{
															// found the field
															c->Set(Field->Option(), Data);
															FieldsProcessed++;
														}
														else
														{
															// field name not recongnised
														}
													}
												}

												s = e + 1;
												if (Delim == '\"') s++;
												Col++;
											}

											// Add contact to database
											if (FieldsProcessed > 0)
											{
												StorageItem *New = Contacts->Store->CreateSub(c);
												if (New)
												{
													c->Window = Parent;
													New->Object = c;
													c->Store = New;
												}
											}
											else
											{
												DeleteObj(c);
											}
										}
									}
								}
							}
						}

						DeleteArray(Buf);
					}
					else
					{
						// read in binary contacts
						bool Done = false;
						Contact *c = new Contact;
						while (c AND NOT Done)
						{
							if (c->Serialize(f, false))
							{
								StorageItem *New = Contacts->Store->CreateSub(c);
								if (New)
								{
									c->Window = Parent;
									New->Object = c;
									c->Store = New;
								}
								else
								{
									Done = true;
								}
							}
							else
							{
								Done = true;
							}

							c = new Contact;
						}

						DeleteObj(c);
					}
				}
			}
			else
			{
				Error(LLoadString(IDS_COULDNT_OPEN), Select.Name());
			}
		}
	}
}

void Export_ScribeContacts(ScribeWnd *Parent)
{
	if (Parent)
	{
		List<Contact> Contacts;
		Parent->GetContacts(Contacts);
		if (Contacts.GetItems() > 0)
		{
			LFileSelect Select;

			Select.Parent(Parent);
			Select.Type("Binary contacts files", "*.contacts");
			Select.Type("Csv contacts files", "*.csv");

			if (Select.Save())
			{
				// Extension check on file name
				GFileType *Type = Select.TypeAt(Select.SelectedType());
				char FileName[256];
				strsafecpy(FileName, Select.Name(), sizeof(FileName));
				char *Dot = strrchr(FileName, '.');
				if ((NOT Dot OR
					strchr(Dot, DIR_CHAR)) AND
					Type)
				{
					strcat(FileName, Type->Extension() + 1);
				}

				if (NOT FileExists(FileName) OR
					LgiMsg(Parent, LLoadString(IDS_ERROR_FILE_EXISTS), AppName, MB_YESNO, FileName) == IDYES)
				{
					// open file
					LFile f;
					if (f.Open(FileName, O_WRITE))
					{
						bool Csv = false;

						f.SetSize(0);

						if (Type)
						{
							Csv = stristr(Type->Extension(), ".csv") != 0;
						}

						if (Csv)
						{
							// Write out in CSV format

							// header line
							for (ItemFieldDef *Field=ContactFieldDefs; Field->Id(); )
							{
								f.Print("\"%s\"", Field->Option());
								Field++;
								if (Field->Id())
								{
									f.Write((char*)",", 1);
								}
							}
							f.Write((char*)"\r\n", 2);

							// write data
							for (Contact *c = Contacts.First(); c; c = Contacts.Next())
							{
								for (ItemFieldDef *Field=ContactFieldDefs; Field->Id(); )
								{
									char *s;
									if (c->Get(Field->Option(), s) AND s)
									{
										f.Print((char*)"\"%s\"", s);
									}
									Field++;
									if (Field->Id())
									{
										f.Write((char*)",", 1);
									}
								}

								f.Write((char*)"\r\n", 2);
							}
						}
						else
						{
							// Write out in a binary format
							for (Contact *c = Contacts.First(); c; c = Contacts.Next())
							{
								c->Serialize(f, true);
							}
						}
					}
					else
					{
						Error(LLoadString(IDS_COULDNT_OPEN), Select.Name());
					}
				}
			}
		}
	}
}
