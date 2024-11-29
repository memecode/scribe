/*
**	FILE:			ScribeFolderTree.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			17/1/2000
**	DESCRIPTION:	Scribe Folder Tree
**
**	Copyright (C) 2000-2002, Matthew Allen
**		fret@memecode.com
*/

// Includes
#include "Scribe.h"
#include "lgi/common/DropFiles.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/RtfHtml.h"
#include "resdefs.h"
#include "ScribeListAddr.h"
#include "lgi/common/ProgressDlg.h"
#if WINNATIVE
#include "OutlookDropSupport.cpp"
#endif
#include "lgi/common/LgiRes.h"

//////////////////////////////////////////////////////////////////////////////
int FolderSorter(ScribeFolder *a, ScribeFolder *b, int d)
{
	auto A = a->GetName(true);
	auto B = b->GetName(true);
	return A && B ? _stricmp(A, B) : 0;
}

//////////////////////////////////////////////////////////////////////////////
MailTree::MailTree(ScribeWnd *app) : LTree(100, 0, 0, 100, 100, "")
{
	App = app;
	LastHit = 0;
	LastWasRoot = -1;

	Sunken(false);
}

MailTree::~MailTree()
{
}

ssize_t MailTree::Sizeof()
{
	LAssert(0);
	return 0;
}

bool MailTree::Serialize(LFile &f, bool Write)
{
	bool Status = false;
	LAssert(0);
	return Status;
}

#ifdef _DEBUG
const char *GetCompareHdrs(LDataI *Mail, LDataI *a)
{
	auto s = a->GetStr(FIELD_INTERNET_HEADER);
	if (!s && Mail)
		s = Mail->GetStr(FIELD_INTERNET_HEADER);
	return s;
}

void CompareTrimWhite(LArray<char> &a)
{
	while (a.Length() > 0 && strchr(LWhiteSpace, a[a.Length()-1]))
		a.Length(a.Length()-1);

	a.Add(0);
}

char *CompareChar(char a)
{
	static char buf[4][16];
	static int next = 0;

	char *b = buf[next++];
	if (next >= 4) next = 0;
	if (a < ' ' || ((uint8_t)a) >= 0x80)
		sprintf_s(b, 16, "0x%2.2X", a);
	else
		sprintf_s(b, 16, "'%c'", a);
	return b;
}

struct SegMap
{
	LDataI *a, *b;
};

bool CompareSegments(LDataI *MailA, LDataI *MailB, LDataI *a, LDataI *b, LStream &p)
{
	const char *s1, *s2;

	// Check headers
	s1 = GetCompareHdrs(MailA, a);
	s2 = GetCompareHdrs(MailB, b);
	if (s1 && s2 && strcmp(s1, s2))
	{
		p.Print("Seg.Headers(%p,%p),", a, b);
		return false;
	}
	else if ((s1 != 0) ^ (s2 != 0))
	{
		p.Print("Seg.HeaderPtrs,");
		return false;		
	}

	// Check data
	LAutoStreamI da = a->GetStream(_FL);
	LAutoStreamI db = b->GetStream(_FL);
	if (da && db)
	{
		LAutoString TransferEncoding(s1?InetGetHeaderField(s1, "Content-Transfer-Encoding"):0);
		bool IsText = true;
		if (TransferEncoding && !_stricmp(TransferEncoding, "base64"))
			IsText = false;

		if (IsText)
		{
			// Text compare
			LArray<char> bufa, bufb;
			if (bufa.Length((uint32_t) da->GetSize()) &&
				bufb.Length((uint32_t) db->GetSize()))
			{
				da->Read(&bufa[0], bufa.Length());
				db->Read(&bufb[0], bufb.Length());
				CompareTrimWhite(bufa);
				CompareTrimWhite(bufb);
				char *ta = &bufa[0], *tb = &bufb[0];
				do
				{
					if (*ta != *tb)
					{
						p.Print("Seg.TextBody(%s != %s @ %i, %p, %p),", CompareChar(*ta), CompareChar(*tb), ta - &bufa[0], a, b);
						return false;
					}
					ta++;
					tb++;

					if (*ta == '\r') ta++;
					if (*tb == '\r') tb++;
				}
				while (*ta && *tb);
			}
			else
			{
				p.Print("Seg.TextMemAlloc(" LPrintfInt64 "," LPrintfInt64 "),", da->GetSize(), db->GetSize());
				return false;
			}
		}
		else
		{
			// Binary compare
			if (da->GetSize() != db->GetSize())
			{
				p.Print("Seg.DataSize(" LPrintfInt64 "," LPrintfInt64 "),", da->GetSize(), db->GetSize());
				return false;
			}
			else
			{
				char bufa[1024], bufb[1024];
				for (int64 i=0; i<da->GetSize(); )
				{
					ssize_t ra = da->Read(bufa, sizeof(bufa));
					ssize_t rb = db->Read(bufb, sizeof(bufb));
					if (ra == rb)
					{
						if (memcmp(bufa, bufb, ra))
						{
							p.Print("Seg.DataCmp,");
							return false;
						}

						i += ra;
					}
					else
					{
						p.Print("Seg.DataRead,");
						return false;
					}
				}
			}
		}
	}
	else if ((da != 0) ^ (db != 0))
	{
		p.Print("Seg.Data(%p[%p,%I64i],%p[%p,%I64i]),",
			a, da.Get(), da ? da->GetSize() : 0,
			b, db.Get(), db ? db->GetSize() : 0);
		return false;
	}

	LDataIt ac = a->GetList(FIELD_MIME_SEG);
	LDataIt bc = b->GetList(FIELD_MIME_SEG);
	if (ac && bc)
	{
		if (ac->Length() != bc->Length())
		{
			p.Print("Seg.ChildLength,");
			return false;
		}
		else
		{
			LArray<SegMap> Map;
			LDataI *Seg;
			for (Seg = dynamic_cast<LDataI*>(ac->First());
				Seg;
				Seg = dynamic_cast<LDataI*>(ac->Next()))
			{
				Map.New().a = Seg;
			}

			for (Seg = dynamic_cast<LDataI*>(bc->First());
				Seg;
				Seg = dynamic_cast<LDataI*>(bc->Next()))
			{
				auto hdr_b = Seg->GetStr(FIELD_INTERNET_HEADER);
				bool Matched = false;
				for (unsigned i=0; i<Map.Length(); i++)
				{
					SegMap &m = Map[i];
					if (m.b == 0)
					{
						// Compare on data size?
						bool Close = false;
						bool Exact = false;
						LAutoStreamI da = m.a->GetStream(_FL);
						LAutoStreamI db = Seg->GetStream(_FL);
						if (da && db)
						{
							int64 size_a = da->GetSize();
							int64 size_b = db->GetSize();
							int64 diff = size_a - size_b;
							if (diff < 0) diff = -diff;
							if (diff == 0)
								Exact = true;
							else if (diff < 8)
								Close = true;
						}

						// What about headers?
						auto hdr_a = m.a->GetStr(FIELD_INTERNET_HEADER);
						if (hdr_a && hdr_b)
						{
							if (_stricmp(hdr_a, hdr_b))
							{
								Close = false;
								Exact = false;
							}
						}

						if (Close || Exact)
						{
							// Match
							m.b = Seg;
							Matched = true;
							break;
						}
					}
				}

				if (!Matched)
				{
					for (unsigned i=0; i<Map.Length(); i++)
					{
						SegMap &m = Map[i];
						if (m.b == 0)
						{
							auto hdr_a = m.a->GetStr(FIELD_INTERNET_HEADER);
							if (hdr_a && hdr_b)
							{
								if (!_stricmp(hdr_a, hdr_b))
								{
									// Match
									m.b = Seg;
									break;
								}
							}
						}
					}
				}
			}

			unsigned i;
			for (i=0; i<Map.Length(); i++)
			{
				SegMap &m = Map[i];
				p.Print("Map[%i] = %p - %p\r\n", i, m.a, m.b);
			}

			for (i=0; i<Map.Length(); i++)
			{
				SegMap &m = Map[i];
				if (m.a && m.b)
				{
					if (!CompareSegments(0, 0, m.a, m.b, p))
						return false;
				}
			}
		}
	}
	else if ((ac != 0) ^ (bc != 0))
	{
		p.Print("Seg.Children,");
		return false;
	}

	return true;
}

void CompareDumpSegs(LStream &p, LDataI *mail, LDataI *s, int depth = 0)
{
	char sp[256];
	int i = depth * 3;
	memset(sp, ' ', i);
	sp[i] = 0;

	auto hdr = GetCompareHdrs(mail, s);
	LAutoString ContentType(hdr ? InetGetHeaderField(hdr, "Content-Type") : 0);
	if (ContentType)
	{
		char *Colon = strchr(ContentType, ';');
		if (Colon) *Colon = 0;
	}
	LAutoStreamI Data = s->GetStream(_FL);
	p.Print("%s%p - %s (%I64i)\r\n", sp, s, ContentType.Get(), Data?Data->GetSize():-1);

	LDataIt c = s->GetList(FIELD_MIME_SEG);
	if (c)
	{
		for (LDataI *a = dynamic_cast<LDataI*>(c->First()); a; a = dynamic_cast<LDataI*>(c->Next()))
		{
			CompareDumpSegs(p, 0, a, depth+1);
		}
	}

	if (mail)
		p.Print("\r\n");
}
#endif

void MailTree::OnItemClick(LTreeItem *Item, LMouse &m)
{
	if (m.Down() && m.IsContextMenu())
	{
		Select(Item);

		ScribeFolder *t = dynamic_cast<ScribeFolder*>(Item);
		if (t)
			t->DoContextMenu(m);
	}
}

void MailTree::OnItemSelect(LTreeItem *Item)
{
	if (LastWasRoot ^ (int8)Item->IsRoot())
	{
		if (Item->IsRoot())
		{
		    if (App->GetItemList())
		        App->GetItemList()->RemoveAll();

			LAutoPtr<LView> v(new DynamicHtml(App, "title.html"));
			App->SetListPane(v);
		}
		else
		{
			LAutoPtr<LView> v(new ThingList(App));
			App->SetListPane(v);
		}
	}

	if (Things() && !Item->IsRoot())
	{
		ScribeFolder *C = dynamic_cast<ScribeFolder*>(Item);
		if (C)
		{
			C->Populate(Things());
			App->SetCtrlValue(IDM_THREAD, C->GetThreaded());
			App->OnFolderSelect(C);
		}
	}
	else
	{
		App->SetCtrlValue(IDM_THREAD, false);
	}

	LastWasRoot = Item ? Item->IsRoot() : false;
}

void MailTree::OnCreateSubDirectory(ScribeFolder *Item)
{
	// setup type list...
	Store3ItemTypes Type[] = { MAGIC_MAIL, MAGIC_CONTACT, MAGIC_FILTER, MAGIC_CALENDAR, MAGIC_GROUP };
	int i=0;
	for (int n=0; n<CountOf(Type); n++)
	{
		if (Type[n] == Item->GetItemType())
		{
			i = n;
			break;
		}
	}

	// do ui..
	bool Enable[] =
	{
		Item->CanHaveSubFolders(MAGIC_MAIL),
		Item->CanHaveSubFolders(MAGIC_CONTACT),
		Item->CanHaveSubFolders(MAGIC_FILTER),
		Item->CanHaveSubFolders(MAGIC_CALENDAR),
		Item->CanHaveSubFolders(MAGIC_GROUP)
	};
	
	auto Dlg = new CreateSubFolderDlg(this, i, Enable);
	Dlg->DoModal([this, Dlg, Item, Type](auto dlg, auto code)
	{
		if (code &&
			ValidStr(Dlg->SubName) &&
			Dlg->SubType >= 0)
		{
			// check the name doesn't conflict..
			auto Path = Item->GetPath();
			if (Path)
			{
				LString s;
				s.Printf("%s/%s", Path.Get(), Dlg->SubName.Get());
				if (App->GetFolder(s))
				{
					LgiMsg(this, LLoadString(IDS_SUBFLD_NAME_CLASH), AppName, MB_OK);
					Dlg->SubName.Empty();
				}
			}

			if (Dlg->SubName)
			{
				// insert the folder...
				Item->CreateSubFolder(Dlg->SubName, Type[Dlg->SubType]);
			}
		}
	});
}

void MailTree::OnDelete(ScribeFolder *Item, bool Force)
{
	if (Item)
	{
		int FolderType = App->GetFolderType(Item);
		if (!Force && FolderType >= 0)
		{
			char Msg[256];
			sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_DELETE_SYS_FOLDER), DefaultFolderNames[FolderType]);
			LgiMsg(this, Msg, AppName, MB_OK);
		}
		else
		{
			Item->OnDelete();
		}
	}
}

void MailTree::OnProperties(ScribeFolder *Item)
{
	if (Item)
	{
		Item->OnProperties();
	}
}

LMessage::Result MailTree::OnEvent(LMessage *Msg)
{
	return LTree::OnEvent(Msg);
}

extern char ScribeFolderObject[];

int MailTree::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;

	LastHit = ItemAtPoint(Pt.x, Pt.y);
	
	if (LastHit)
	{
		List<char> Accepted;

		Formats.Supports(ScribeThingList);
		Formats.Supports(ScribeFolderObject);
		Formats.SupportsFileDrops();
		#if WINNATIVE
		Formats.Supports(CFSTR_FILEDESCRIPTOR);
		#endif
		#ifdef MAC
		Formats.Supports(LGI_StreamDropFormat);
		#endif

		if (Formats.GetSupported().Length())
		{
			SelectDropTarget(LastHit);
			Status = KeyState & LGI_EF_CTRL ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
		}
	}

	return Status;
}

static int FolderItemCmp(LTreeItem *a, LTreeItem *b, NativeInt UserData)
{
	ScribeFolder *ta = dynamic_cast<ScribeFolder*>(a);
	ScribeFolder *tb = dynamic_cast<ScribeFolder*>(b);
	if (ta && tb)
	{
		int64 IndexA = ta->GetObject()->GetInt(FIELD_FOLDER_INDEX);
		int64 IndexB = tb->GetObject()->GetInt(FIELD_FOLDER_INDEX);
		return (int)IndexA - (int)IndexB;
	}
	
	return 0;
}

int MailTree::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;
	ScribeFolder *Leaf = dynamic_cast<ScribeFolder*>(LastHit);
	#if WINNATIVE
	LString FileDescFmt = CFSTR_FILEDESCRIPTOR;
	#endif

	SelectDropTarget(0);
	if (App)
		App->SetLastDrop();

	for (unsigned idx=0; idx<Data.Length() && Leaf!=NULL; idx++)
	{
		LDragData &dd = Data[idx];
		bool ThingDrop = dd.IsFormat(ScribeThingList);
		bool FolderDrop = dd.IsFormat(ScribeFolderObject);
		if (ThingDrop || FolderDrop)
		{
			if (dd.Data.Length() == 0 ||
				dd.Data[0].Type != GV_BINARY)
			{
				continue;
			}

			ssize_t Errors = 0, Count = 0;
			LVariant &v = dd.Data.First();

			if (ScribeClipboardFmt::IsFolder(v.Value.Binary.Data, v.Value.Binary.Length))
			{
				ScribeClipboardFmt *Fmt = (ScribeClipboardFmt*) v.Value.Binary.Data;
				Count = Fmt->Length();

				ScribeFolder *Root = dynamic_cast<ScribeFolder*>(ItemAt(0));

				for (ssize_t i=0; i<Fmt->Length(); i++)
				{
					ScribeFolder *Folder = Fmt->FolderAt(i);
					if (Folder)
					{
						if (Folder == Leaf ||
							Folder == Root ||
							Leaf->GetItemType() == MAGIC_ANY)
						{
							// they're dragging onto themselves or
							// dragging the whole mail tree around or
							// dragging a folder into the trash
							// just quit out now...
							return DROPEFFECT_NONE;
						}

						bool CopyOp = (KeyState & LGI_EF_CTRL) != 0;

						auto FinishFolderOp = [this, Folder, Leaf, CopyOp](int Res)
						{
							int SystemFolder = App->GetFolderType(Folder);
							auto OldPath = Folder->GetPath();
							Store3Status Status = Store3Error;
							ScribeFolder *OldParent = Folder->GetFolder();
							ScribeFolder *NewParent = NULL;
							int Index = 0;

							if (Res == 1)
							{
								// Attach next
								NewParent = Leaf->GetFolder();
								if (NewParent)
								{
									// Work out the index
									for (LTreeItem *Item = NewParent->GetChild(); Item && Item!=Leaf; Item=Item->GetNext())
										Index++;
									Index++;
								}
							}
							else if (Res == 2)
							{
								// Attach child
								NewParent = Leaf;
							}
							else return;

							if (SystemFolder >= 0 &&
								NewParent->GetObject()->GetStore() != OldParent->GetObject()->GetStore())
							{
								LgiMsg(App, "Can't move system folders to a different mail store.", AppName, MB_OK);
								return;
							}

							if (CopyOp)
							{
								// Copy
								Status = Folder->CopyTo(NewParent, Index);
							}
							else
							{
								LDataFolderI *fo = Folder->GetFldObj();
								if (NewParent == OldParent)
								{
									// Re-index only...
									// int64 OldIndex = fo->GetInt(FIELD_FOLDER_INDEX);
									if (fo->SetInt(FIELD_FOLDER_INDEX, Index))
									{
										// Change the UI to match...
										NewParent->Sort(FolderItemCmp);
										Status = Store3Success;
									}
								}
								else
								{
									// Move
									#ifdef _MSC_VER
										#pragma message("Reimplement index and async handler here.")
									#else
										#warning("Reimplement index and async handler here.")
									#endif
									Folder->SetFolder(NewParent,
										[this, NewParent](auto Status)
										{
											if (Status == Store3Success)
												NewParent->Sort(FolderItemCmp);
										});
								}
							}
								
							if (Status == Store3Success &&
								SystemFolder >= 0 &&
								!CopyOp)
							{
								// Update the system path location if it's changed...
								auto NewPath = Folder->GetPath();
								if (OldPath &&
									NewPath &&
									strcmp(OldPath, NewPath) != 0)
								{
									LVariant v;
									v = NewPath;
									LString SysFolderName;
									SysFolderName.Printf("Folder-%i", SystemFolder);
									App->GetOptions()->SetValue(SysFolderName, v);
								}
							}
						};

						int Res = 2;
						if (Leaf->GetFolder() != NULL)
						{
							auto Dlg = new LAlert(	this,
													LLoadString(CopyOp ? IDS_COPY_FOLDER : IDS_MOVE_FOLDER),
													LLoadString(IDS_MOVE_ATTACH),
													LLoadString(IDS_NEXT_FOLDER),
													LLoadString(IDS_SUB_FOLDER),
													LLoadString(IDS_CANCEL));
							Dlg->DoModal([this, Dlg, FinishFolderOp](auto dlg, auto Res)
							{
								if (Res > 0)
									FinishFolderOp(Res);
							});
						}
						else FinishFolderOp(Res);
					}
				}
			}
			else if (ScribeClipboardFmt::IsThing(v.Value.Binary.Data, v.Value.Binary.Length))
			{
				ScribeClipboardFmt *Fmt = (ScribeClipboardFmt*) v.Value.Binary.Data;
				LDataStoreI::StoreTrans Trans = Leaf->GetObject()->GetStore()->StartTransaction();
				bool CopyOnly = (KeyState & LGI_EF_CTRL) != 0;
				Count = Fmt->Length();

				LArray<Thing*> Items;
				for (ssize_t i=0; i<Count; i++)
				{
					Thing *Thg = Fmt->ThingAt(i);
					if (Thg)
						Items.Add(Thg);
				}

				if (Items.Length())
				{
					LArray<Store3Status> ItemStatus;
					Leaf->MoveTo(Items, CopyOnly, NULL);
					// Fixme: Impl MoveTo Callback
					for (auto s: ItemStatus)
						if (s == Store3Error)
							Errors++;
				}
			}
			else LAssert(!"Unknown drop format.");

			App->Update();

			if (Errors)
			{
				LgiMsg(this, LLoadString(IDS_MOVE_ERROR), AppName, MB_OK, Errors, Count);
			}
			else
			{
				Status = DROPEFFECT_COPY;
			}
			
			// We don't need to process any other data types... we're done.
			break;
		}
		else if (dd.IsFileDrop())
		{
			if (dd.Data.Length() > 0)
			{
				LDropFiles Files(dd.Data[0]);
				Leaf->OnReceiveFiles(Files);
				Status = DROPEFFECT_COPY;
			}
		}
		#if WINNATIVE
		else if (_stricmp(dd.Format, FileDescFmt) == 0)
		{
			if (dd.Data.Length() == 0 ||
				dd.Data[0].Type != GV_BINARY)
				continue;
			
			// Get file list...
			LString::Array Files;
			FILEGROUPDESCRIPTOR *FileGroup = (FILEGROUPDESCRIPTOR*) dd.Data[0].Value.Binary.Data;

			if (OnDropFileGroupDescriptor(FileGroup, Files))
			{
				// Process dropped files
				LArray<const char*> FileLst;
				for (auto &f : Files)
					FileLst.Add(f.Get());
				Leaf->OnReceiveFiles(FileLst);

				// Clean up
				for (auto f : Files)
				{
					FileDev->Delete(f, false);
				}
			}
			else if (DataObject)
			{
				for (int i=0; true; i++)
				{
					FORMATETC Format;
					Format.cfFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);
					Format.dwAspect = DVASPECT_CONTENT;
					Format.lindex = i;
					Format.tymed = TYMED_ISTORAGE;
					Format.ptd = 0;

					STGMEDIUM Medium;
					ZeroObj(Medium);

					HRESULT res = DataObject->GetData(&Format, &Medium);
					if (SUCCEEDED(res))
					{
						if (Medium.tymed == TYMED_ISTORAGE)
						{
							OutlookIStorage Dec(App, Medium.pstg, false);

							int Type = Dec.GetType();
							if (Type == Leaf->GetItemType())
							{
								switch (Type)
								{
									case MAGIC_MAIL:
									{
										Mail *m = Dec.GetMail();
										if (m)
										{
											m->App = App;
											m->Save(Leaf);
											Status = DROPEFFECT_COPY;
										}
										break;
									}
									case MAGIC_CONTACT:
									{
										Contact *c = Dec.GetContact();
										if (c)
										{
											c->App = App;
											c->Save(Leaf);
											Status = DROPEFFECT_COPY;
										}
										break;
									}
								}
							}
						}

						ReleaseStgMedium(&Medium);
					}
					else break;
				}
			}
		}
		#endif
	}

	return Status;
}

