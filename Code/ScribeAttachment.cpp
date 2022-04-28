/*
**	FILE:			ScribeAttachment.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			7/12/98
**	DESCRIPTION:	Scribe Attachments
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/NetTools.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/Tnef.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/TextConvert.h"
#include "lgi/common/FileSelect.h"

extern char *ExtractCodePage(char *ContentType);

#ifdef WIN32
char NotAllowed[] = "\\/*:?\"|<>";
#else
char NotAllowed[] = "\\/";
#endif

//////////////////////////////////////////////////////////////////////////////
char *StripPath(const char *Full)
{
	if (Full)
	{
		auto Dos = strrchr(Full, '\\');
		auto Unix = strrchr(Full, '/');
		if (Dos)
		{
			return NewStr(Dos+1);
		}
		else if (Unix)
		{
			return NewStr(Unix+1);
		}
	}

	return NewStr(Full);
}

void CleanFileName(char *i)
{
	if (i)
	{
		char *o = i;
		while (*i)
		{
			if ((uint8_t)*i >= ' ' && !strchr(NotAllowed, *i))
			{
				*o++ = *i;
			}
			i++;
		}
		*o++ = 0;
	}
}

//////////////////////////////////////////////////////////////////////////////
void Attachment::_New(LDataI *object)
{
	DefaultObject(object);
	LAssert(GetObject() != NULL);
	Owner = 0;
	Msg = 0;
	IsResizing = false;
}

Attachment::Attachment(ScribeWnd *App, Attachment *From) : Thing(App)
{
	_New(From &&
		From->GetObject() ?
		From->GetObject()->GetStore()->Create(MAGIC_ATTACHMENT) : 0);
	if (From)
	{
		char *Ptr = 0;
		int Len = 0;

		if (From->Get(&Ptr, &Len))
		{
			Set(Ptr, Len);
			SetName(From->GetName());
			SetMimeType(From->GetMimeType());
			SetContentId(From->GetContentId());
			SetCharset(From->GetCharset());
		}
	}
}

Attachment::Attachment(ScribeWnd *App, LDataI *object, const char *Import) : Thing(App)
{
	_New(object);	
	if (Import)
		ImportFile(Import);
}

Attachment::~Attachment()
{
	DeleteObj(Msg);

	if (Owner)
		Owner->Attachments.Delete(this);
}

bool Attachment::ImportFile(const char *FileName)
{
	LAutoPtr<LFile> f(new LFile);
	if (!f)
	{
		LAssert(!"Out of memory");
		return false;
	}

	LString Mime = ScribeGetFileMimeType(FileName);
	if (!f->Open(FileName, O_READ))
	{
		LAssert(!"Can't open file.");
		return false;
	}

	char *c = strrchr((char*)FileName, DIR_CHAR);
	if (c) SetName(c + 1);
	else SetName(FileName);

	if (Mime)
		SetMimeType(Mime);
	
	LAutoStreamI s(f.Release());
	GetObject()->SetStream(s);
	return true;
}

bool Attachment::ImportStream(const char *FileName, const char *MimeType, LAutoStreamI Stream)
{
	if (!FileName ||
		!MimeType ||
		!Stream)
	{
		LAssert(!"Parameter error");
		return false;
	}
	
	char *c = strrchr((char*)FileName, DIR_CHAR);
	if (c) SetName(c + 1);
	else SetName(FileName);

	SetMimeType(MimeType);

	GetObject()->SetStream(Stream);	
	return true;
}

void Attachment::SetOwner(Mail *msg)
{
	Owner = msg;
}

bool Attachment::CallMethod(const char *MethodName, LVariant *Ret, LArray<LVariant*> &Args)
{
	ScribeDomType Fld = StrToDom(MethodName);

	*Ret = false;
	switch (Fld)
	{
		case SdSave: // Type: (String FileName)
		{
			auto Fn = Args.Length() > 0 ? Args[0]->Str() : NULL;
			if (Fn)
				*Ret = SaveTo(Fn, true);
			return true;
		}
		default:
			break;
	}

	return Thing::CallMethod(MethodName, Ret, Args);
}

bool Attachment::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdLength: // Type: Int64
		{
			Value = GetSize();
			break;
		}
		case SdName: // Type: String
		{
			Value = GetName();
			break;
		}
		case SdMimeType: // Type: String
		{
			Value = GetMimeType();
			break;
		}
		case SdContentId: // Type: String
		{
			Value = GetContentId();
			break;
		}
		case SdData: // Type: Binary
		{
			LAutoPtr<LStreamI> s(GotoObject(_FL));
			if (!s)
				return false;

			Value.Empty();
			Value.Type = GV_BINARY;
			if ((Value.Value.Binary.Data = new char[Value.Value.Binary.Length = (int)s->GetSize()]))
				s->Read(Value.Value.Binary.Data, Value.Value.Binary.Length);
			break;
		}
		case SdType: // Type: Int32
		{
			Value = GetObject()->Type();
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

Thing &Attachment::operator =(Thing &t)
{
	LAssert(0);
	return *this;
}

void IncFileIndex(char *FilePath, size_t FilePathLen)
{
	char File[MAX_PATH_LEN];
	char Ext[256] = "";

	// Get the extension part
	char *Dot = strrchr(FilePath, '.');
	if (Dot)
	{
		strcpy_s(Ext, sizeof(Ext), Dot);
	}

	// Get the filename part
	ssize_t FileLen = (ssize_t)strlen(FilePath) - strlen(Ext);
	memcpy(File, FilePath, FileLen);
	File[FileLen] = 0;

	// Seek to start of digits
	char *Digits = File + strlen(File) - 1;
	while (IsDigit(*Digits) && Digits > File)
	{
		Digits--;
	}
	if (!IsDigit(*Digits)) Digits++;

	// Increment the index
	int Index = atoi(Digits);
	sprintf_s(Digits, sizeof(File)-(Digits-File), "%i", Index + 1);

	// Write the resulting filename
	sprintf_s(FilePath, FilePathLen, "%s%s", File, Ext);
}

void Attachment::SetMsg(Mail *m)
{
	Msg = m;
}

Mail *Attachment::GetMsg()
{
	if (!Msg && IsMailMessage())
	{
		// is an email
		LAutoStreamI f = GetObject()->GetStream(_FL);
		if (f)
		{
			if ((Msg = new Mail(App)))
			{
				Msg->SetWillDirty(false);
				Msg->App = App;
				Msg->ParentFile = this;

				Msg->OnAfterReceive(f);
			}
		}
	}

	return Msg;
}

bool Attachment::GetIsResizing()
{
	return IsResizing;
}

void Attachment::SetIsResizing(bool b)
{
	IsResizing = b;
	Update();
}

bool Attachment::IsMailMessage()
{
	return	GetMimeType() &&
			!_stricmp(GetMimeType(), sMimeMessage);
}

bool Attachment::IsVCalendar()
{
	return	GetMimeType()
			&&
			(
				!_stricmp(GetMimeType(), sMimeVCalendar)
				||
				!_stricmp(GetMimeType(), sMimeICalendar)
			);
}

bool Attachment::IsVCard()
{
	return	GetMimeType() &&
			!_stricmp(GetMimeType(), sMimeVCard);
}

char *Attachment::GetDropFileName()
{
	if (!DropFileName)
		DropFileName = MakeFileName();
	return DropFileName;
}

bool Attachment::GetDropFiles(LString::Array &Files)
{
	bool Status = false;

	if (GetDropFileName())
	{
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), ScribeTempPath(), DropFileName);
		if (SaveTo(p, true))
		{
			Files.Add(p);
			Status = true;
		}
	}

	return Status;
}

LAutoString Attachment::MakeFileName()
{
	auto Name = GetName();
	LAutoString CleanName;
	
	if (Name)
	{
		CleanName.Reset(StripPath(Name));
		CleanFileName(CleanName);
	}
	else
	{
		LArray<LString> Ext;
		char s[256] = "Attachment";
		auto MimeType = GetMimeType();

		LGetMimeTypeExtensions(MimeType, Ext);
		if (Ext.Length())
		{
			size_t len = strlen(s);
			sprintf_s(s+len, sizeof(s)-len, ".%s", Ext[0].Get());
		}
		CleanName.Reset(NewStr(s));
	}

	return CleanName;
}

void Attachment::OnOpen(LView *Parent, char *Dest)
{
	bool VCal;
	if (GetMsg())
	{
		// Open the mail message...
		Msg->DoUI(Owner);
	}
	else if ((VCal = IsVCalendar()) || IsVCard())
	{
		// Open the event or contact...
		Thing *c = App->CreateItem(VCal ? MAGIC_CALENDAR : MAGIC_CONTACT, 0, false);
		if (c)
		{
			LAutoPtr<LStreamI> f(GotoObject(_FL));
			if (f)
			{
				if (c->Import(*f, GetMimeType()))
				{
					c->DoUI();
				}
				else LgiMsg(Parent, "Failed to parse calendar.", "Error");
			}
			else LgiTrace("%s:%i - Failed to get attachment stream.\n", _FL);
		}
	}
	else // is some generic file
	{
		bool IsExe = false;
		int TnefSizeLimit = 8 << 20;
		LStream *TnefStream = 0;
		LArray<TnefFileInfo*> TnefIndex;

		int64 AttachPos = -1;
		LAutoStreamI f = GetObject()->GetStream(_FL);
		if (f)
		{
			IsExe = LIsFileExecutable(GetName(), f, AttachPos = f->GetPos(), f->GetSize());
			if (!IsExe &&
				f->GetSize() < TnefSizeLimit)
			{
				f->SetPos(AttachPos);
				if (!TnefReadIndex(f, TnefIndex))
				{
					DeleteObj(TnefStream);
				}
			}
			f.Reset();
		}

		if (IsExe)
		{
			LgiMsg(Parent, LLoadString(IDS_ERROR_EXE_FILE), AppName);
			return;
		}

		// Check for TNEF
		if (TnefStream)
		{
			LStringPipe p;
			for (unsigned n=0; n<TnefIndex.Length(); n++)
			{
				char Size[64];
				LFormatSize(Size, sizeof(Size), TnefIndex[n]->Size);
				p.Print("\t%s (%s)\n", TnefIndex[n]->Name, Size);
			}
			char *FileList = p.NewStr();
			if (Owner &&
				LgiMsg(Parent, LLoadString(IDS_ASK_TNEF_DECODE), AppName, MB_YESNO, FileList) == IDYES)
			{
				char *Tmp = ScribeTempPath();
				if (Tmp)
				{
					for (unsigned i=0; i<TnefIndex.Length(); i++)
					{
						char s[MAX_PATH_LEN];
						LMakePath(s, sizeof(s), Tmp, TnefIndex[i]->Name);
						
						LFile Out;
						if (Out.Open(s, O_WRITE))
						{
							Out.SetSize(0);
							if (TnefExtract(TnefStream, &Out, TnefIndex[i]))
							{
								Out.Close();

								Attachment *NewFile = 0;
								Owner->AttachFile(NewFile = new Attachment(App, GetObject()->GetStore()->Create(MAGIC_ATTACHMENT), s));
								if (NewFile && Owner->GetUI())
								{
									AttachmentList *Lst = Owner->GetUI()->GetAttachments();
									if (Lst)
									{
										Lst->Insert(NewFile);
										Lst->ResizeColumnsToContent();
									}
								}
							}
							Out.Close();
						}
					}
				}

				Owner->Save();
				OnDeleteAttachment(Parent, false);
			}
			DeleteObj(TnefStream);
			DeleteArray(FileList);
		}
		else
		{
			// Open file...
			LAutoString FileToExecute;
			char *Tmp = ScribeTempPath();
			if (!Tmp)
				return;
				
			// get the file name
			char FileName[MAX_PATH_LEN];
			LAutoString CleanName = MakeFileName();
			if (CleanName)
			{
				LMakePath(FileName, sizeof(FileName), Tmp, CleanName);

				while (LFileExists(FileName))
				{
					IncFileIndex(FileName, sizeof(FileName));
				}

				// write the file out
				if (SaveTo(FileName))
				{
					FileToExecute.Reset(NewStr(FileName));
				}
			}
			
			// open the file
			auto Mime = GetMimeType();
			if (FileToExecute)
			{
				LAutoString AssociatedApp;
				LXmlTag *FileTypes = App->GetOptions()->LockTag(OPT_FileTypes, _FL);
				if (FileTypes)
				{
					auto Mime = GetMimeType();
					auto FileName = GetName();
					
					for (auto t: FileTypes->Children)
					{
						char *mt = t->GetAttr("mime");
						char *ext = t->GetAttr("extension");
						bool MimeMatch = mt && Mime && !_stricmp(mt, Mime);
						bool ExtMatch = ext && FileName && MatchStr(ext, FileName);
						if (MimeMatch || ExtMatch)
						{
							AssociatedApp.Reset(TrimStr(t->GetContent()));
							break;
						}
					}
					
					App->GetOptions()->Unlock();
				}
				
				if (AssociatedApp)
				{
					const char *s = AssociatedApp;
					LAutoString Exe(LTokStr(s));
					if (Exe)
					{
						char Args[MAX_PATH_LEN+100];
						
						if (!strchr(s, '%') ||
							sprintf_s(Args, sizeof(Args), s, FileToExecute.Get()) < 0)
						{
							if (sprintf_s(Args, sizeof(Args), "\"%s\"", FileToExecute.Get()) < 0)
								Args[0] = 0;
						}
						
						if (Args[0] && LExecute(Exe, Args))
						{
							// Successful..
							return;
						}
					}
				}
				
				if (!LExecute(FileToExecute, 0, Tmp))
				{
					// if the default open fails.. open as text
					LString AppPath = LGetAppForMimeType(Mime ? Mime : sTextPlain);
					bool Status = false;
					if (AppPath)
					{
						char *s = strchr(AppPath, '%');
						if (s) s[0] = 0;
						Status = LExecute(AppPath, FileToExecute, Tmp);
					}

					if (!Status)
					{
						LgiMsg(Parent, "Couldn't open file.", AppName, MB_OK);
					}
				}
			}
		}
	}
}

void Attachment::OnDeleteAttachment(LView *Parent, bool Ask)
{
	if (Owner)
	{
		LVariant ConfirmDelete;
		App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

		if (!Ask ||
			!ConfirmDelete.CastInt32() ||
			LgiMsg(GetList(), LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
		{
			List<LListItem> Sel;
			LList *p = GetList();

			if (p)
			{
				p->GetSelection(Sel);
			}
			else
			{
				Sel.Insert(this);
			}

			for (LListItem *i: Sel)
			{
				Attachment *a = dynamic_cast<Attachment*>(i);
				if (a)
				{
					if (p)
					{
						p->Remove(i);
					}
					
					a->Owner->DeleteAttachment(a);
				}
			}

			if (p)
			{
				p->Invalidate();
			}
		}
	}
}

bool Attachment::SaveTo(char *FileName, bool Quite, LView *Parent)
{
	bool Status = false;
	if (FileName)
	{
		LFile Out;
		Status = true;

		if (LFileExists(FileName))
		{
			if (Quite)
			{
				return true;
			}
			else
			{
				Out.Close();
				LString Msg = AskOverwriteMsg(FileName);
				Status = LgiMsg(Parent ? Parent : App, Msg, AppName, MB_YESNO) == IDYES;
			}
		}

		if (!Out.Open(FileName, O_WRITE))
		{
			if (!Quite)
				LgiMsg(App, LLoadString(IDS_ERROR_CANT_WRITE), AppName, MB_OK, FileName);
			else
				LgiTrace("%s:%i - Can't open '%s' for writing (err=0x%x)\n", _FL, FileName, Out.GetError());
			Status = false;
		}

		if (Status)
		{
			Out.SetSize(0);

			if (GetObject())
			{
				int BufSize = 64 << 10;
				char *Buf = new char[BufSize];

				LStreamI *f = GotoObject(_FL);
				if (f && Buf)
				{
					f->SetPos(0);					
					Out.SetSize(0);
					int64 MySize = f->GetSize();
					int64 s = MySize;
					while (s > 0)
					{
						ssize_t r = (int)MIN(BufSize, s);
						r = f->Read(Buf, r);
						if (r > 0)
						{
							Out.Write(Buf, r);
							s -= r;
						}
						else break;
					}

					int64 OutPos = Out.GetPos();
					if (OutPos < MySize)
					{
						// Error writing to disk...
						Out.Close();
						FileDev->Delete(FileName, false);
						LAssert(!"Failed to write whole attachment to disk.");
						Status = false;
					}
				}
				else
				{
					LgiTrace("%s:%i - GotoObject failed.\n", _FL);
					Status = false;
				}

				DeleteObj(f);
				DeleteArray(Buf);
			}
			/*
			else if (Data)
			{
				int Written = Out.Write(Data, Size);
				Status = Written == Size;
			}
			*/
		}
	}

	return Status;
}

void Attachment::OnSaveAs(LView *Parent)
{
	auto Name = GetName();
	char *n = StripPath(Name);
	if (!n)
	{
		n = NewStr("untitled");
	}
	
	if (n)
	{
		CleanFileName(n);

		LFileSelect Select;

		Select.Parent(Parent);
		Select.Type("All files", LGI_ALL_FILES);
		Select.Name(n);

		List<LListItem> Files;

		if (LListItem::Parent)
		{
			LListItem::Parent->GetSelection(Files);
		}
		else
		{
			Files.Insert(this);
		}

		if (Files.Length() > 0)
		{
			bool Status = false;

			if (Files.Length() > 1)
			{
				// multiple files, ask which directory to write to
				Status = Select.OpenFolder();
			}
			else
			{
				// single file, ask for filename and path
				Status = Select.Save();
			}

			if (Status)
			{
				char Dir[MAX_PATH_LEN];
				strcpy_s(Dir, sizeof(Dir), Select.Name());

				if (Files.Length() > 1)
				{
					// Loop through all the files and write them to that directory
					for (LListItem *i: Files)
					{
						Attachment *a = dynamic_cast<Attachment*>(i);
						if (a)
						{
							char Path[MAX_PATH_LEN];
							auto d = StripPath(a->GetName());
							if (d)
							{
								sprintf_s(Path, sizeof(Path), "%s%s%s", Dir, DIR_STR, d);
								a->SaveTo(Path);
								DeleteArray(d);
							}
						}
					}
				}
				else
				{
					// Write the file
					Attachment *a = dynamic_cast<Attachment*>(Files[0]);
					if (a)
					{
						a->SaveTo(Dir, false, Parent);
					}
				}
			}
		}

		DeleteArray(n);
	}
}

bool Attachment::OnKey(LKey &k)
{
	if (k.vkey == LK_RETURN && k.IsChar)
	{
		if (k.Down())
		{
			OnOpen(GetList());
		}

		return true;
	}

	return false;
}

void Attachment::OnMouseClick(LMouse &m)
{
	auto mt = GetMimeType();
	bool OpenAttachment = false;

	if (m.IsContextMenu())
	{
		// open the right click menu
		LSubMenu RClick;
		LString MimeType = GetMimeType();

		RClick.AppendItem(LLoadString(IDS_ADD_TO_CAL), IDM_ADD_TO_CAL, IsVCalendar());
		RClick.AppendSeparator();
		RClick.AppendItem(LLoadString(IDS_OPEN), IDM_OPEN, true);
		RClick.AppendItem(LLoadString(IDS_SAVEAS), IDM_SAVEAS, true);
		RClick.AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);
		RClick.AppendSeparator();
		RClick.AppendItem(LLoadString(IDS_RESIZE), IDM_RESIZE, MimeType.Lower().Find("image/") >= 0);

		if (Parent->GetMouse(m, true))
		{
			switch (RClick.Float(Parent, m.x, m.y))
			{
				case IDM_OPEN:
				{
					OpenAttachment = true;
					break;
				}
				case IDM_DELETE:
				{
					OnDeleteAttachment(Parent, true);
					break;
				}
				case IDM_SAVEAS:
				{
					OnSaveAs(Parent);
					break;
				}
				case IDM_ADD_TO_CAL:
				{
					ScribeFolder *Cal = App->GetFolder(FOLDER_CALENDAR);
					if (!Cal)
						LgiMsg(Parent, "Can't find the calendar folder.", AppName);
					else
					{
						Thing *c = App->CreateItem(MAGIC_CALENDAR, 0, false);
						if (c)
						{
							LAutoPtr<LStreamI> f(GotoObject(_FL));
							if (f)
							{
								if (c->Import(*f, mt))
								{
									c->Save(Cal);
									c->DoUI();
								}
								else LgiTrace("%s:%i - Failed to import cal stream.\n", _FL);
							}
							else LgiTrace("%s:%i - Failed to get attachment stream.\n", _FL);
						}
						else LgiTrace("%s:%i - Failed to create calendar obj.\n", _FL);
					}
					break;
				}
				case IDM_RESIZE:
				{
					List<Attachment> Sel;
					if (GetList() && GetList()->GetSelection(Sel))
					{
						for (auto a: Sel)
						{
							if (!a->Owner)
							{
								LgiTrace("%s:%i - No owner?", _FL);
								break;
							}

							a->Owner->ResizeImage(a);
						}
						
						GetList()->ResizeColumnsToContent();
					}
					break;
				}
			}
		}
	}
	else if (m.Double())
	{
		OpenAttachment = true;
	}	
	
	if (OpenAttachment)
	{
		// open the attachment
		OnOpen(Parent);
	}
}

bool Attachment::GetFormats(LDragFormats &Formats)
{
	Formats.SupportsFileDrops();
	return Formats.Length() > 0;
}

bool Attachment::GetData(LArray<LDragData> &Data)
{
	int SetCount = 0;
	
	for (unsigned idx=0; idx<Data.Length(); idx++)
	{
		LDragData &dd = Data[idx];
		if (dd.IsFileDrop())
		{
			List<Attachment> Att;
			if (Parent->GetSelection(Att))
			{
				LString::Array Files;

				for (auto a: Att)
				{
					// char *Nm = a->GetName();

					if (!a->DropSourceFile || !LFileExists(a->DropSourceFile))
					{
						a->DropSourceFile.Reset();

						char p[MAX_PATH_LEN];
						LAutoString Clean = a->MakeFileName();
						LMakePath(p, sizeof(p), ScribeTempPath(), Clean);

						char Ext[256];
						char *d = strrchr(p, '.');
						if (!d) d = p + strlen(p);
						strcpy_s(Ext, sizeof(Ext), d);

						for (int i=1; LFileExists(p); i++)
						{
							sprintf_s(d, sizeof(p)-(d-p), "_%i%s", i, Ext);
						}

						if (a->SaveTo(p, true))
						{
							a->DropSourceFile.Reset(NewStr(p));
						}
					}

					if (a->DropSourceFile)
					{
						Files.Add(a->DropSourceFile.Get());
					}
					else LAssert(0);
				}

				if (Files.First())
				{
					LMouse m;
					App->GetMouse(m, true);
					if (CreateFileDrop(&dd, m, Files))
					{
						SetCount++;
					}
				}
			}
		}
	}

	return SetCount > 0;
}

LStreamI *Attachment::GotoObject(const char *file, int line)
{
	if (!GetObject())
		return 0;

	LAutoStreamI s = GetObject()->GetStream(file, line);
	return s.Release();
}

int Attachment::Sizeof()
{
	return 0;
}

bool Attachment::Serialize(LFile &f, bool Write)
{
	/*
	ulong Magic = MAGIC_ATTACHMENT;
	LView *Parent = Window;
	if (Owner && Owner->GetUi())
	{
		Parent = Owner->GetUi();
	}

	if (Write)
	{
		f << Magic;
		f << Content;

		// Check we have the data to write out, as it will effect the
		// size we write out before the data
		bool DataOk = false;
		LFile In;
		if (Data)
		{
			DataOk = true;
		}
		else if (ImportName)
		{
			// Check we can open the file...
			while (!In.Open(ImportName, O_READ))
			{
				char Msg[256];
				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ERROR_CANT_READ), ImportName);
				LAlert Dlg(	Parent,
							AppName,
							Msg,
							LLoadString(IDS_RETRY),
							LLoadString(IDS_CANCEL));
				int Result = Dlg.DoModal();
				if (Result == 2)
				{
					break;
				}
			}

			DataOk = In.IsOpen();
		}
		else
		{
			// We're skipping the data already on disk
			DataOk = true;
		}

		if (!DataOk)
		{
			// No point continuing
			return false;
		}

		f << Size;
		WriteStr(f, Name);

		if (Data)
		{
			// write the file itself
			f.Write(Data, Size);
		}
		else if (ImportName)
		{
			// import from the file
			uint64 Last = LCurrentTime();
			LProgressDlg *Prog = 0;

			int BufSize = 64 << 10;
			uchar *Buf = new uchar[BufSize];
			if (Buf)
			{
				int s = Size;
				while (s > 0 && f.GetStatus())
				{
					int r = min(s, BufSize);
					r = In.Read(Buf, r);
					f.Write(Buf, r);
					s -= r;
					
					uint64 Now = LCurrentTime();
					if (Prog)
					{
						if (Now - Last > 300)
						{
							Prog->Value((Size-s) >> 10);
							LYield();
							Last = Now;

							if (Prog->Cancel())
							{
								DeleteArray(Buf);
								DeleteObj(Prog);
								return false;
							}
						}
					}
					else if (Now - Last > 1000)
					{
						Prog = new LProgressDlg(Parent);
						if (Prog)
						{
							Prog->SetDescription("Importing file...");
							Prog->SetLimits(0, Size >> 10);
							Prog->SetType("K");
						}
						Last = Now;
					}
				}

				DeleteArray(ImportName);
				DeleteObj(Prog);
			}

			DeleteArray(Buf);
		}
		else
		{
			// skip over data on hard disk
			f.Seek(Size, SEEK_CUR);
		}

		// new style fields
		if (MimeType)
		{
			WriteStrField(FIELD_MIME_TYPE, MimeType);
		}
		if (ContentId)
		{
			WriteStrField(FIELD_CONTENT_ID, ContentId);
		}
	}
	else
	{
		f >> Magic;
		if (Magic == MAGIC_ATTACHMENT)	// The versions before v1.25 didn't
										// set this correctly, but that is so old
										// now, I've removed the hack to allow
										// the attachment in.
		{
			f >> Content;
			f >> Size;
			DeleteArray(Name);
			Name = ReadStr(f PassDebugArgs);

			// Skip over the data
			DeleteArray(Data);

			int StartPos = f.GetPos();

			f.Seek(Size, SEEK_CUR);
			
			int ExtraPos = f.GetPos();

			// read list of new-style fields
			bool Done = false;
			bool Eob = false;

			while (	!(Eob = Store->EndOfObj(f)) &&
					!Done)
			{
				short FieldId = 0;
				ulong FieldSize = 0;

				f >> FieldId;
				switch (FieldId)
				{
					ReadStrField(FIELD_MIME_TYPE, MimeType);
					ReadStrField(FIELD_CONTENT_ID, ContentId);
					default:
					{
						// Error: unknown chunk
						SetDirty();
						Done = true;
						break;
					}
				}
			}

			LFormatSize(SizeStr, Size);
		}
		else return false;
	}

	return f.GetStatus();
	*/

	return false;
}

char *GetSubField(char *s, char *Field, bool AllowConversion = true)
{
	char *Status = 0;

	if (s && Field)
	{
		s = strchr(s, ';');
		if (s)
		{
			s++;

			size_t FieldLen = strlen(Field);
			char White[] = " \t\r\n";
			while (*s)
			{
				// Skip leading whitespace
				while (*s && (strchr(White, *s) || *s == ';')) s++;

				// Parse field name
				if (IsAlpha(*s))
				{
					char *f = s;
					while (*s && (IsAlpha(*s) || *s == '-')) s++;
					bool HasField = ((s-f) == FieldLen) && (_strnicmp(Field, f, FieldLen) == 0);
					while (*s && strchr(White, *s)) s++;
					if (*s == '=')
					{
						s++;
						while (*s && strchr(White, *s)) s++;
						if (*s && strchr("\'\"", *s))
						{
							// Quote Delimited Field
							char d = *s++;
							char *e = strchr(s, d);
							if (e)
							{
								if (HasField)
								{
									if (AllowConversion)
									{
										Status = DecodeRfc2047(NewStr(s, e-s));
									}
									else
									{
										Status = NewStr(s, e-s);
									}
									break;
								}

								s = e + 1;
							}
							else break;
						}
						else
						{
							// Delimited Field
							char *e = s;
							while (*e && *e != ';') e++;

							if (HasField)
							{
								Status = DecodeRfc2047(NewStr(s, e-s));
								break;
							}

							s = e;
						}
					}
					else break;
				}
				else break;
			}
		}
	}

	return Status;
}

const char *Attachment::GetText(int i)
{
	if (FieldArray.Length())
	{
		return "This is an attachment!!!!!";
	}
	else
	{
		switch (i)
		{
			case 0:
			{
				auto Nm = GetName();

				if (IsResizing)
				{
					Buf.Printf("%s (%s)", Nm, LLoadString(IDS_RESIZING));
					return Buf;
				}

				return Nm;
			}
			case 1:
			{
				static char s[64];
				LFormatSize(s, sizeof(s), GetSize());
				return s;
			}
			case 2:
			{
				return GetMimeType();
			}
			case 3:
			{
				return GetContentId();
			}
		}
	}

	return 0;
}

bool Attachment::Get(char **ptr, int *size)
{
	if (ptr && size)
	{
		LStreamI *f = GotoObject(_FL);
		if (f)
		{
			*size = (int)f->GetSize();
			*ptr = new char[*size+1];
			if (*ptr)
			{
				ssize_t r = f->Read(*ptr, *size);
				(*ptr)[r] = 0;

			}

			DeleteObj(f);
			return true;
		}
	}

	return false;
}

bool Attachment::Set(LAutoStreamI Stream)
{
	if (!GetObject())
	{
		LAssert(0);
		return false;
	}

	if (!GetObject()->SetStream(Stream))
	{
		LAssert(0);
		return false;
	}
	
	return true;
}

bool Attachment::Set(char *ptr, int size)
{
	LAutoStreamI s(new LMemStream(ptr, size));
	return Set(s);
}
