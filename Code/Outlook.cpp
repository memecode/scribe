#include <stdio.h>

#include "defs.h"
#include "memdev.h"
#include "file.h"
#include "gdc2.h"
#include "gui.h"

#include "scribe.h"
#include "mapix.h"

#define PR_SMTP_ADDRESS				(PROP_TAG(PT_STRING8, 0x39fe))

SPropValue *MapiGetField(SRow *Row, int Field)
{
	SPropValue *v = 0;
	if (Row)
	{
		for (int i=0; i<Row->cValues; i++)
		{
			if (PROP_ID(Row->lpProps[i].ulPropTag) == PROP_ID(Field))
			{
				return Row->lpProps + i;
			}
		}
	}
	return v;
}

/*
class GRow : public LListItem
{
	SRow *Row;
	char **Data;

public:
	GRow(SRow *row)
	{
		Row = row;
		if (Row)
		{
			Cols = Row->cValues;
		}
		Data = new char*[Cols];
		memset(Data, 0, sizeof(*Data)*Cols);
	}

	char *GetText(int i)
	{
		SPropValue *Value = (Row)?Row->lpProps+i:0;
		if (Value)
		{
			char Str[256] = "";

			DeleteArray(Data[i]);
			switch (PROP_TYPE(Value->ulPropTag))
			{
				case PT_I2:
				{
					sprintf(Str, "%i", Value->Value.i);
					break;
				}
				case PT_I4:
				{
					sprintf(Str, "%i", Value->Value.l);
					break;
				}
				case PT_R8:
				{
					sprintf(Str, "%f", Value->Value.dbl);
					break;
				}
				case PT_BOOLEAN:
				{
					sprintf(Str, "%s", (Value->Value.b)?"true":"false");
					break;
				}
				case PT_STRING8:
				{
					sprintf(Str, "%s", Value->Value.lpszA);
					break;
				}
				case PT_ERROR:
				{
					sprintf(Str, "e(%08.8X)", Value->Value.err);
					break;
				}
				case PT_BINARY:
				{
					sprintf(Str, "Bin(%i)", Value->Value.bin.cb);
					break;
				}
				default:
				{
					sprintf(Str, "#TYPE(%04.4X)", PROP_TYPE(Value->ulPropTag));
					break;
				}
			}

			if (strlen(Str)>0)
			{
				Data[i] = NewStr(Str);
			}

			return (Data[i])?Data[i]:"";
		}
		return "";
	}
};

class LShowTable : public LDialog
{
	SRowSet *Table;
	GList *List;

public:
	LShowTable(LView *parent, SRowSet *table)
	{
		Pos.ZOff(660, 490);
		MoveToCenter();
		Parent = parent;
		Name("Show Table");
		Table = table;

		Children.Insert(new LButton(101, 550, 420, 60, 20, "Close"));
		Children.Insert(List = new GList(100, 10, 10, 600, 400));
		if (List AND Table)
		{
			if (Table->cRows > 0)
			{
				SRow *First = Table->aRow;
				for (int i=0; i<First->cValues; i++)
				{
					char Str[256];
					sprintf(Str, "%08.8X", First->lpProps[i].ulPropTag);
					List->AddColumn(Str, 80);
				}

				for (i=0; i<Table->cRows; i++)
				{
					List->Insert(new GRow(Table->aRow+i));
				}
			}

			DoModal();
		}
	}

	int OnNotify(LViewI *Ctrl, int Flags)
	{
		if (Ctrl->GetId() == 101)
		{
			EndModal(0);
		}
		return 0;
	}

};
*/

void OutlookTest(LView *Wnd)
{
	int NewContacts = 0;
	ULONG Ui = (ULONG) ((Wnd) ? Wnd->Handle() : 0);
	HRESULT Error = S_OK;

	HINSTANCE hMapi = LoadLibrary("MAPI32.DLL");
	if (hMapi)
	{
		typedef HRESULT (__stdcall *MAPIInitializeProc)(LPVOID lpMapiInit);
		typedef HRESULT (__stdcall *MAPILogonExProc)(ULONG ulUIParam, LPTSTR lpszProfileName, LPTSTR lpszPassword, FLAGS flFlags, LPMAPISESSION FAR *lppSession);
		typedef void (__stdcall *MAPIUninitializeProc)();

		MAPIInitializeProc pMAPIInitialize = (MAPIInitializeProc) GetProcAddress(hMapi, "MAPIInitialize");
		MAPILogonExProc pMAPILogonEx = (MAPILogonExProc) GetProcAddress(hMapi, "MAPILogonEx");
		MAPIUninitializeProc pMAPIUninitialize = (MAPIUninitializeProc) GetProcAddress(hMapi, "MAPIUninitialize");

		if (pMAPIInitialize AND
			pMAPILogonEx AND
			pMAPIUninitialize AND
			pMAPIInitialize(NULL) == S_OK)
		{
			LPMAPISESSION Session = NULL;

			if (pMAPILogonEx(Ui,
							NULL,
							NULL,
							MAPI_LOGON_UI,
							&Session) == SUCCESS_SUCCESS)
			{
				IAddrBook *AddrBook = 0;

				if ((Error = Session->OpenAddressBook(Ui, NULL, 0, &AddrBook)) == S_OK AND
					AddrBook)
				{
					ULONG EntryIDSize = 0;
					ENTRYID *PersonalAddrBook = 0;
					if (AddrBook->GetPAB(&EntryIDSize, &PersonalAddrBook) == S_OK AND
						PersonalAddrBook)
					{
						ULONG Type = 0;
						IDistList *DistList = 0;
						if (AddrBook->OpenEntry(EntryIDSize,
												PersonalAddrBook,
												NULL,
												0,
												&Type,
												(IUnknown**)&DistList) == S_OK AND
							DistList)
						{
							LPMAPITABLE DistContents = 0;
							if (DistList->GetContentsTable(0, &DistContents) == S_OK AND
								DistContents)
							{
								// How many rows are there
								ULONG Rows = 0;
								DistContents->GetRowCount(0, &Rows);

								// Seek to the beginning
								if (DistContents->SeekRow(BOOKMARK_BEGINNING, 0, NULL) == S_OK)
								{
									SRowSet *Row = 0;
									if (DistContents->QueryRows(Rows, 0, &Row) == S_OK AND
										Row)
									{
										// LShowTable Tbl(Wnd, Row);

										for (int i=0; i<Row->cRows; i++)
										{
											SPropValue *v = MapiGetField(Row->aRow + i, PR_ENTRYID);
											if (v)
											{
												ULONG type = 0;
												IMailUser *User = 0;
												if (DistList->OpenEntry(	v->Value.bin.cb,
																			(LPENTRYID) v->Value.bin.lpb,
																			0,
																			0,
																			&type,
																			(IUnknown**)&User) == S_OK AND
													User)
												{
													SPropTagArray Props[1];
													SPropValue *Array = 0;
													ULONG Values = 0;

													Props[0].cValues = 2;
													Props[0].aulPropTag[0] = PR_DISPLAY_NAME;
													Props[0].aulPropTag[1] = PR_EMAIL_ADDRESS;

													if (User->GetProps(	NULL, // Props
																		0,
																		&Values,
																		&Array) == S_OK AND
														Array)
													{
														Contact *Person = (Contact*)MainWnd->CreateItem(MAGIC_CONTACT, 0, false);
														if (Person)
														{
															for (int n=0; n<Values; n++)
															{
																switch (Array[n].ulPropTag)
																{
																	case PR_DISPLAY_NAME:
																	{
																		char *Name = NewStr(Array[n].Value.lpszA);
																		if (Name)
																		{
																			int Spaces = 0;
																			for (int k=0; Name[k]; k++)
																			{
																				if (Name[k] == ' ') Spaces++;
																			}

																			if (Spaces == 1)
																			{
																				char *Space = strchr(Name, ' ');
																				*Space = 0;
																				Person->Set(OPT_First, Name);
																				Person->Set(OPT_Last, Space+1);

																				char *Last = 0;
																				Person->Get(OPT_Last, Last);
																				int n=0;
																			}
																			else
																			{
																				Person->Set(OPT_First, Name);
																			}

																			DeleteArray(Name);
																		}
																		break;
																	}
																	case PR_EMAIL_ADDRESS:
																	case PR_SMTP_ADDRESS:
																	{
																		char *Addr = Array[n].Value.lpszA;
																		if (strchr(Addr, '@'))
																		{
																			Person->Set(OPT_Email, Array[n].Value.lpszA);
																		}
																		break;
																	}
																}

																/*
																if (PROP_TYPE(Array[n].ulPropTag) == PT_STRING8)
																{
																	char *s = Array[n].Value.lpszA;
																	int k=0;
																}
																*/
															}

															Person->Save();
															NewContacts++;
														}
													}
												}
											}
										}
									}
								}
							}

							char *Msg = new char[256];
							if (Msg)
							{
								sprintf(Msg, "%i contacts imported from Outlook.", NewContacts);
							}
							LgiMsg(Wnd, (Msg)?Msg:"Error", AppName, MB_OK);
							DeleteArray(Msg);
						}
						else
						{
							LgiMsg(Wnd, "Couldn't open the personal address book.", "Error", MB_OK);
						}
					}
					else
					{
						LgiMsg(Wnd, "No personal address book.", "Error", MB_OK);
					}
				}
				else
				{
					LgiMsg(Wnd, "Couldn't open the address book.", "Error", MB_OK);
				}

				Session->Logoff(0, 0, 0);
				Session->Release();
			}
			else
			{
				LgiMsg(Wnd, "Couldn't log into to MAPI", "Error", MB_OK);
			}

			pMAPIUninitialize();
		}
		else
		{
			LgiMsg(Wnd, "Couldn't initialize MAPI.", "Error", MB_OK);
		}

		FreeLibrary(hMapi);
	}
	else
	{
		LgiMsg(Wnd, "MAPI32.DLL not installed", "Error", MB_OK);
	}
}
