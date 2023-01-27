/*
**	FILE:			Imp_OutlookExpress.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			4/2/2000
**	DESCRIPTION:	Scribe importer
**
**	Copyright (C) 2000, Matthew Allen
**		fret@memecode.com
*/

#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

bool ImportMBX(ScribeWnd *Parent, ScribeFolder *ParentFolder, char *FileName)
{
	LFile F;
	if (FileName &&
		ParentFolder &&
		Parent &&
		F.Open(FileName, O_READ))
	{
		char Magic[4] = {'J', 'M', 'F', '6'};
		char Buf[4];
		if (F.Read(Buf, 4) == 4 &&
			memcmp(Magic, Buf, 4) == 0)
		{
			int32 Msgs;

			F.Seek(4, SEEK_CUR);
			F >> Msgs;

			// Create folder for these messages
			char n[256];
			strcpy_s(n, sizeof(n), FileName);
			char *Ls = 0; // last slash
			for (Ls = n+strlen(n)-1; Ls > n; Ls--)
			{
				if (*Ls == DIR_CHAR)
				{
					Ls++;
					break;
				}
			}

			char *Dot = strchr(Ls, '.');
			if (Dot)
			{
				*Dot = 0;
			}

			ScribeFolder *Folder = ParentFolder->CreateSubDirectory(Ls, MAGIC_MAIL);
			if (Folder)
			{
				// Skip unknown feilds
				F.Seek(4 + 4 + 1 + 63, SEEK_CUR);

				// Read the messages
				for (int i=0; i<Msgs; i++)
				{
					int MsgStartPos = (int)F.GetPos();
					
					int MsgId = 0;
					F >> MsgId;
					int MsgNum = 0;
					F >> MsgNum;
					int MsgTotalSize = 0;
					F >> MsgTotalSize;
					int MsgTextSize;
					F >> MsgTextSize;

					// Create item
					Mail *m = dynamic_cast<Mail*>(Parent->CreateItem(MAGIC_MAIL, Folder, false));
					if (m)
					{
						LMemStream Text(&F, 0, MsgTextSize);

						// Decode email into fields
						m->OnAfterReceive(&Text);

						m->SetFlags(MAIL_RECEIVED |
									MAIL_READ |
									((m->HasAttachments()) ? MAIL_ATTACHMENTS : 0) );
					}

					// Seek to the next message
					F.Seek(MsgStartPos+MsgTotalSize, SEEK_SET);
				}
			}
			else
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

#ifdef WIN32
#pragma pack(push, 1)
#endif
class DbxMessagePtr
{
public:
	uint MessagePos;
	uint TablePos;
	char Reserved[4];
};

// #define DbxTableSize 0x27c
class DbxTable
{
public:
	// Header
	/*
	uint FilePos;
	char Reserved1[13];
	uint Msgs;
	char Reserved2[3];
	*/

	uint FilePos;
	char Reserved1[4];
	uint ListPtr;
	uint NextPtr;
	char Reserved2[8];
};

class DbxMsgTable
{
public:
	uint FilePos;
	uint MessageLength;
    char Reserved[17];
    uint FirstBlockPos;
};

class DbxMsgBlock
{
public:
	/*
	uint FilePos;
	uint Flags;
    uint BlockSize;
    uint NextBlockPos;
	*/

	uint FilePos;
	uint Increase;
    uint Include;
    uint Next;
	uint UseNet;
};

#ifdef WIN32
#pragma pack(pop)
#endif

class ImportDBX
{
private:
	ScribeWnd *Parent = NULL;
	ScribeFolder *ParentFolder = NULL;
	LFile F;
	uint64 FileSize = 0;
	List<ssize_t> Used;
	
	class MsgInfo
	{
	public:
		int Pos;
		bool IsNews;

		MsgInfo(int p, bool n)
		{
			Pos = p;
			IsNews = n;
		}
	};
	List<MsgInfo> MsgList;

	bool IsUsed(ssize_t i)
	{
		for (auto n: Used)
		{
			if (*n == i) return true;
		}
		return false;
	}

	void SetUsed(ssize_t i)
	{
		if (!IsUsed(i))
		{
			Used.Insert(new ssize_t(i));
		}
	}

	bool ReadMessage(ssize_t Pos, bool IsNews, ScribeFolder *Folder)
	{
		bool Status = false;
		if (!IsUsed(Pos) &&
			F.Seek(Pos, SEEK_SET))
		{
			LTempStream Text(ScribeTempPath());

			SetUsed(Pos);

			while (Pos > 0)
			{
				DbxMsgBlock Msg;
				ZeroObj(Msg);
				F.Read(&Msg, sizeof(Msg)-4);
				if (Pos != Msg.FilePos)
				{
					break;
				}

				Pos += sizeof(Msg)-4;
				// int Next = Pos + Msg.Next;
				auto End = Pos + Msg.Include;

				char Buf[1024];
				while (Pos < End)
				{
					ssize_t R = F.Read(Buf, MIN(End-Pos, sizeof(Buf)));
					Text.Write(Buf, R);
					Pos += R;
				}

				F.Seek(Msg.Next, SEEK_SET);
				Pos = Msg.Next;
			}

			Mail *m = new Mail(Parent);
			if (m)
			{
				m->App = Parent;

				m->OnAfterReceive(&Text);
				m->SetFlags(MAIL_RECEIVED |
							MAIL_READ |
							// ((MsgTable.Reserved[3] == 3) ? MAIL_READ : 0) |
							(m->GetAttachments(0) ? MAIL_ATTACHMENTS : 0));
				m->SetDirty();
				m->Save(Folder);
				Status = true;
			}
		}

		return Status;
	}

	bool ReadMsgHeader(int Pos)
	{
		bool Status = false;
		if (!IsUsed(Pos) &&
			F.Seek(Pos, SEEK_SET))
		{
			SetUsed(Pos);

			DbxMsgBlock Msg;
			F.Read(&Msg, sizeof(Msg));
			if (Msg.FilePos == Pos)
			{
				Pos += sizeof(Msg);

				int32 Self, MsgPos = 0, NewsPost = false, Count = 0;
				do
				{
					F.Read(&Self, sizeof(Self));
					if ((Self & 0xff) == 0x84)
					{
						if (!MsgPos)
						{
							MsgPos = Self >> 8;
						}
					}

					if ((Self & 0xff) == 0x83)
					{
						NewsPost = true;
					}
					Count++;
				}
				while ((Self & 0x7f) > 0);

				if (MsgPos)
				{
					MsgList.Insert(new MsgInfo(MsgPos, NewsPost != 0));
					Status |= true;
				}
				else
				{
					F.Read(&Self, sizeof(Self));
					F.Read(&MsgPos, sizeof(MsgPos));
					MsgList.Insert(new MsgInfo(MsgPos, NewsPost != 0));
					Status |= true;
				}
			}
		}

		return Status;
	}

	bool ReadTable(ssize_t Pos)
	{
		bool Status = false;

		if (Pos > 0 &&
			(uint64)Pos < FileSize &&
			!IsUsed(Pos))
		{
			DbxTable Tbl;

			F.Seek(Pos, SEEK_SET);
			SetUsed(Pos);
			F.Read(&Tbl, sizeof(Tbl));
			if (Tbl.FilePos == Pos)
			{
				Pos += sizeof(Tbl);
				Status |= ReadTable(Tbl.NextPtr);
				Status |= ReadTable(Tbl.ListPtr);
				F.Seek(Pos, SEEK_SET);

				while (true)
				{
					bool Actioned = false;

					DbxMessagePtr Node;
					F.Seek(Pos, SEEK_SET);
					Pos += F.Read(&Node, sizeof(Node));
					if (Node.MessagePos > 0 &&
						Node.MessagePos < FileSize &&
						Node.MessagePos != Node.TablePos)
					{
						Actioned |= ReadMsgHeader(Node.MessagePos);
					}

					if (Node.TablePos > 0 &&
						Node.TablePos < FileSize &&
						Node.TablePos != Node.MessagePos)
					{
						Actioned |= ReadTable(Node.TablePos);
					}

					if (Actioned)
					{
						Status = true;
					}
					else break;
				}
			}
		}

		return Status;
	}

	void Clean()
	{
		Used.DeleteObjects();
		MsgList.DeleteObjects();
	}

public:
	ImportDBX(ScribeWnd *parent)
	{
		Parent = parent;
		ParentFolder = 0;
	}

	~ImportDBX()
	{
		Clean();
	}

	bool Import(ScribeFolder *parentFolder, char *FileName)
	{
		Clean();
		ParentFolder = parentFolder;
		if (sizeof(DbxTable) != 24)
		{
			// packing is screwed
			LgiMsg(Parent, "Compiled object 'DbxTable' is the wrong size", AppName, MB_OK);
			return false;
		}

		LProgressDlg Dlg(Parent);
		Dlg.SetDescription("Reading tables...");
		Dlg.SetRange(1);
		LProgressPane *Import = Dlg.Push();
		if (Import)
		{
			Import->SetDescription("Importing messages...");
			LYield();
		}

		if (Parent &&
			ParentFolder &&
			FileName &&
			F.Open(FileName, O_READ))
		{
			FileSize = F.GetSize();

			// Create folder for these messages
			char n[256];
			strcpy_s(n, sizeof(n), FileName);
			char *Ls = strrchr(n, DIR_CHAR); // last slash
			if (Ls)
			{
				Ls++;
				char *Dot = strchr(Ls, '.');
				if (Dot)
				{
					*Dot = 0;
				}
				
				// Make sure the path is unique, no overwriting previous folders
				LStringPipe NewPath(256);
				auto ParentPath = ParentFolder->GetPath();
				NewPath.Print("/%s/%s", ParentPath.Get(), Ls);
				auto BaseNewPath = NewPath.NewGStr();
				LString NewPathStr = BaseNewPath.Get();

				while (Parent->GetFolder(NewPathStr))
				{
					char *Num = NewPathStr.Get() + BaseNewPath.Length() - 1;
					for (; Num>NewPathStr && IsDigit(*Num); Num--)
						;
					if (!IsDigit(*Num))
						Num++;

					int i = atoi(Num);
					NewPath.Print("%s%i", BaseNewPath.Get(), i+1);
					NewPathStr = NewPath.NewGStr();
				}

				// Create output folder
				ScribeFolder *Folder = ParentFolder->CreateSubDirectory(Ls, MAGIC_MAIL);
				if (Folder)
				{
					// Read header
					F.Seek(0x30, SEEK_SET);
					uint TableLoc;
					F >> TableLoc;
					if (!TableLoc) TableLoc = 0x1e254;
					ReadTable(TableLoc);

					// Import msg list
					Dlg.Value(1);
					if (Import)
					{
						Import->SetRange(MsgList.Length());
						Import->SetType("Mail");
					}
					for (auto i: MsgList)
					{
						ReadMessage(i->Pos, i->IsNews, Folder);
						if (Import)
						{
							Import->Value(Import->Value()+1);
							LYield();
						}
					}

					/* 
					List<int> Msgs;
					while (Tbl)
					{
						int Pos = F.GetPosition();
						F.Read(Tbl, DbxTableSize);
						if (Tbl->FilePos == Pos)
						{
							for (int i=0; i<Tbl->Msgs; i++)
							{
								Msgs.Insert(new int(Tbl->Msg[i].MessagePos));
							}
						}			
						if (Tbl->Msgs < 76 ||
							Tbl->FilePos != Pos)
						{
							DeleteArray(Tbl);
						}
					}
					*/

					// Read in messages
					/*
					for (int *MsgPos = Msgs.First(); MsgPos; MsgPos = Msgs.Next())
					{
						bool Read = true;

						DbxMsgTable MsgTable;
						F.Seek(*MsgPos, SEEK_SET);
						F.Read(&MsgTable, sizeof(MsgTable));
						if (MsgTable.FilePos == *MsgPos)
						{
							// Read in blocks
							GBytePipe Msg;
							DbxMsgBlock MsgBlock;

							MsgBlock.NextBlockPos = MsgTable.FirstBlockPos;
							while (MsgBlock.NextBlockPos)
							{
								int SeekTo = MsgBlock.NextBlockPos & 0xFFFFFF;
								F.Seek(SeekTo, SEEK_SET);
								F.Read(&MsgBlock, sizeof(MsgBlock));

								if (MsgBlock.FilePos == SeekTo)
								{
									uchar *Data = new uchar[MsgBlock.BlockSize];
									if (Data)
									{
										F.Read(Data, MsgBlock.BlockSize);
										Msg.Push(Data, MsgBlock.BlockSize);
										DeleteArray(Data);
									}
								}
							}

							// Msg contains email...
							int Size = Msg.Sizeof();
							if (Size > 0)
							{
								Mail *m = dynamic_cast<Mail*>(Parent->CreateItem(MAGIC_MAIL, Folder, false));
								if (m)
								{
									m->Text = new char[Size+1];
									if (m->Text)
									{
										// Read data into item
										Msg.Pop((uchar*) m->Text, Size);
										m->Text[Size] = 0;

										// Decode email into fields
										m->OnAfterReceive();

										m->SetFlags(MAIL_RECEIVED |
													((MsgTable.Reserved[3] == 3) ? MAIL_READ : 0) |
													((m->Store->GetChild() != 0) ? MAIL_ATTACHMENTS : 0));
										
										m->StoreDirty = true;
									}
								}
							}
						}
					}
					*/

					return true;
				}
			}
		}

		return false;
	}

};


void Import_OutlookExpress(ScribeWnd *Parent, bool v5)
{
	// Get the base directory
	char Dir[256] = "";
	if (!LGetSystemPath(LSP_LOCAL_APP_DATA, Dir, sizeof(Dir)))
	{
		// Just in case
		LGetSystemPath(LSP_OS, Dir, sizeof(Dir));
	}
	
	// Search for the folder files
DoFileSearch:
	LArray<char*> Files;
	LArray<const char*> Ext;
	if (v5)
		Ext.Add("*.dbx");
	else
		Ext.Add("*.mbx");

	LRecursiveFileSearch(Dir, &Ext, &Files);

	if (Files.Length() == 0)
	{
		if (LgiMsg(	Parent,
					"%i outlook express data files found in:\n"
					"%s\n"
					"Do you want to select a different directory to search?",
					AppName,
					MB_YESNO,
					Files.Length(),
					Dir) == IDYES)
		{
			LFileSelect Select;
			Select.Parent(Parent);
			if (Select.OpenFolder())
			{
				strcpy_s(Dir, sizeof(Dir), Select.Name());
				goto DoFileSearch;
			}
		}
	}

	if (Files.Length() > 0)
	{
		// Strip files
		for (unsigned i=0; i<Files.Length(); i++)
		{
			char *f = Files[i];

			if (stristr(f, "Folders.") ||
				stristr(f, "Pop3uidl.") ||
				stristr(f, "Deleted Items."))
			{
				Files.DeleteAt(i);
				DeleteArray(f);
				i--;
			}
		}

		// Ask the user where to put them..
		ScribeFolder *Current = Parent->GetCurrentFolder();
		LString CurrentPath;
		if (Current)
		    CurrentPath = Current->GetPath();
		ChooseFolderDlg Dlg(Parent,
							false,
							AppName,
							LLoadString(IDS_OE_IMPORT),
							CurrentPath,
							MAGIC_MAIL,
							&Files);
		if (Dlg.DoModal())
		{
			ScribeFolder *Folder = Parent->GetFolder(Dlg.DestFolder);
			if (Folder)
			{
				LProgressDlg PrgDlg(Parent);
				PrgDlg.SetDescription("Importing folders...");
				PrgDlg.SetRange(Files.Length());
				LYield();

				int Imported = 0;
				int i=1;
				for (auto FileName: Dlg.SrcFiles)
				{
					if (v5)
					{
						ImportDBX Filter(Parent);
						if (Filter.Import(Folder, FileName))
						{
							Imported++;
						}
					}
					else
					{
						if (ImportMBX(Parent, Folder, FileName))
						{
							Imported++;
						}
					}

					PrgDlg.Value(i);
					LYield();
					i++;
				}

				PrgDlg.Visible(false);
				char *FileType = (v5) ? (char*)"DBX" : (char*)"MBX";
				LgiMsg(Parent, "%i of %i %s files imported successfully.", AppName, MB_OK, Imported, Files.Length(), FileType);
			}
			else
			{
				LgiMsg(Parent, "Error locating that folder.", AppName, MB_OK);
			}
		}
	}
}
