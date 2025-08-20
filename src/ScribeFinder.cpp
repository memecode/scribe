/*
**	FILE:			ScribeFinder.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			3/6/1999
**	DESCRIPTION:	Scribe finder tool
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/Combo.h"
#include "resdefs.h"
#include "lgi/common/DragAndDrop.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/LgiRes.h"

#define IDC_RESULTS 90

enum FindMsgs {
	M_END_SEARCH = M_USER + 0x400
};

//////////////////////////////////////////////////////////////////////////////////
#include "LMarkColourSelect.h"

LMarkColourSelect::LMarkColourSelect() : ResObject(Res_Custom)
{
}

LArray<uint32_t> LMarkColourSelect::GetSelected()
{
	LArray<uint32_t> a;
	for (int i=0; i<IDM_MARK_MAX; i++)
	{
		if (ColSel[i])
			a.Add(MarkColours32[i]);
	}
	return a;
}

bool LMarkColourSelect::OnLayout(LViewLayoutInfo &Inf)
{
	if (Inf.Width.Max)
	{
		Inf.Height.Min = 
			Inf.Height.Max = ColPx + (Pad * 2) + 2;
	}
	else
	{
		LFont *f = GetFont();
		None.Reset(new LDisplayString(f, LLoadString(IDS_NONE)));
		AnyTxt.Reset(new LDisplayString(f, LLoadString(IDS_ANY)));
			
		if (None && AnyTxt)
		{
			Inf.Width.Max = None->X() + AnyTxt->X() + (IDM_MARK_MAX * (ColPx + Pad)) + (Pad * 3) + 2;
			Inf.Width.Min = None->X() + AnyTxt->X() + IDM_MARK_MAX + (Pad * 4) + 2;
		}
		else
		{
			Inf.Width.Max =
				Inf.Width.Min = -1;
		}
	}

	return true;
}

void LMarkColourSelect::OnPressColour(size_t i)
{
	ColSel[i] = !ColSel[i];
	if (!ColSel[i])
		Any = false;

	Invalidate();
	SendNotify(LNotifyValueChanged);
}

void LMarkColourSelect::SelectNone()
{
	Any = false;
	memset(ColSel, 0, sizeof(ColSel));
	Invalidate();
	SendNotify(LNotifyValueChanged);
}

void LMarkColourSelect::SelectAll()
{
	Any = true;
	for (int i=0; i<IDM_MARK_MAX; i++)
		ColSel[i] = true;
	Invalidate();
	SendNotify(LNotifyValueChanged);
}

void LMarkColourSelect::OnMouseClick(LMouse &m)
{
	if (m.IsContextMenu())
	{
	}
	else if (m.Left())
	{
		if (m.Down())
		{
			if (NoneRc.Overlap(m.x, m.y))
			{
				SelectNone();
			}
			else if (AnyRc.Overlap(m.x, m.y))
			{
				SelectAll();
			}
			else
			{
				for (size_t i=0; i<ColRc.Length(); i++)
				{
					if (ColRc[i].Overlap(m.x, m.y))
						OnPressColour(i);
				}
			}
		}
	}
}

void LMarkColourSelect::OnPaint(LSurface *pDC)
{
	LColour low = LColour(L_LOW);
	LColour txt = LColour(L_TEXT);
	LColour bk = LColour(L_MED).Mix(low, 0.2f);
	LRect cli = GetClient();
	pDC->Colour(low);
	pDC->Box(&cli);
	cli.Inset(1, 1);
	pDC->Colour(bk);
	pDC->Rectangle(&cli);

	int x = Pad;
	if (None)
	{
		None->GetFont()->Colour(txt, bk);
		NoneRc.ZOff(None->X()-1, None->Y()-1);
		NoneRc.Offset(cli.x1+x, cli.y1+Pad); 
		None->Draw(pDC, NoneRc.x1, NoneRc.y1);
		NoneRc.Inset(-Pad, -Pad);
		x += None->X() + Pad;
	}
	for (int i=0; i<IDM_MARK_MAX; i++)
	{
		LRect &r = ColRc[i];
		r.Set(x, Pad, x + 15, Pad + 15);
		r.Offset(cli.x1, cli.y1);
		pDC->Colour(MarkColours32[i], 32);
		if (ColSel[i])
			pDC->Rectangle(&r);
		else
		{
			LRect t = r;
			pDC->Box(&t);
			t.Inset(1, 1);
			pDC->Box(&t);
		}
		x += r.X() + Pad;
	}
	if (AnyTxt)
	{
		AnyTxt->GetFont()->Colour(txt, bk);
		AnyRc.ZOff(AnyTxt->X()-1, AnyTxt->Y()-1);
		AnyRc.Offset(cli.x1+x, cli.y1+Pad); 
		AnyTxt->Draw(pDC, cli.x1 + x, cli.x1 + Pad);
		AnyRc.Inset(-Pad, -Pad);
		x += AnyTxt->X() + Pad;
	}
}

class LMarkColourSelectFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (!stricmp(Class, "LMarkColourSelect"))
			return new LMarkColourSelect();
		return NULL;
	}

}	MarkColourSelectFactory;

//////////////////////////////////////////////////////////////////////////////
class ResultItem : public LListItem
{
	friend int FindCompare(LListItem *a, LListItem *b, NativeInt Data);
	friend class ResultList;

	LString Path;
	Thing *T;

	bool GetData(LArray<LDragData> &Data)
	{
		bool Status = false;
		
		List<LListItem> Objs;
		LList *ParentList = LListItem::Parent;
		if (ParentList &&
			ParentList->GetSelection(Objs))
		{
			for (unsigned di=0; di<Data.Length(); di++)
			{
				LDragData &dd = Data[di];
				
				if (dd.IsFormat(ScribeThingList))
				{
					if (auto Fmt = ScribeClipboardFmt::Alloc(false, Objs.Length()))
					{
						int n = 0;
						for (auto i: Objs)
						{
							auto Ri = dynamic_cast<ResultItem*>(i);
							if (Ri && Ri->T)
								Fmt->ThingAt(n++, Ri->T);
						}
						Status = dd.Data[0].SetBinary(Fmt->Sizeof(), Fmt);
						free(Fmt);
					}
				}
				else if (dd.IsFileDrop())
				{
					LMouse m;
					GetList()->GetMouse(m, true);
					LDragDropSource *Src = dynamic_cast<LDragDropSource*>(GetList());
					if (!Src)
						return false;

					LString::Array Files;
					for (auto i: Objs)
					{
						ResultItem *Ri = dynamic_cast<ResultItem*>(i);
						if (Ri)
						{
							Status |= Ri->T->GetDropFiles(Files);
						}
					}

					if (Status)
					{
						Status = Src->CreateFileDrop(&dd, m, Files);
					}
				}
			}
		}

		return Status;
	}

	int Type() { return T ? T->Type() : 0; }
	int Sizeof() { LAssert(0); return 0; }
	bool Serialize(LFile &f, bool Write) { LAssert(0); return 0; }

public:
	ResultItem(ScribeWnd *App, Thing *t)
	{
		T = t;
		LAssert(T != NULL);
		
		auto m = t->IsMail();
		if (m)
		{
			auto colour = m->GetMarkColour();
			if (colour > 0)
			{
				LColour c((uint32_t)colour, 32);
				GetCss(true)->BackgroundColor(c.Mix(L_WORKSPACE, Mail::MarkColourMix));
			}
		}
	}

	Thing *GetThing()
	{
		return T;
	}

	const char *GetText(int i = 0)
	{
		const char *Status = 0;

		int *DefFlds = T->GetDefaultFields();
		if (i < 4)
		{
			Status = DefFlds ? T->GetFieldText(DefFlds[i]) : (char*)"<error>";
		}
		else if (i == 4)
		{
			ScribeFolder *f = T->GetFolder();
			if (f)
			{
				Path = f->GetPath();
			}
			Status = Path;
		}

		return Status;
	}

	int GetImage(int Flags = 0)
	{
		return T->GetImage(Flags);
	}

	LView *DoUI()
	{
		return T->DoUI();
	}
};

class ResultList : public LList
{
	friend int FindCompare(LListItem *a, LListItem *b, NativeInt Data);

	int Col;
	bool Ascend;

public:
	ResultList(int id, int x, int y, int cx, int cy, const char *name = "") :
		LList(id, x, y, cx, cy, name)
	{
		Col = -1;
		Ascend = true;
		MultiSelect(true);
	}

	void OnItemBeginDrag(LListItem *Item, LMouse &m)
	{
		Drag(this, m.Event, DROPEFFECT_MOVE);
	}

	bool GetFormats(LDragFormats &Formats)
	{
		Formats.Supports(ScribeThingList);
		Formats.SupportsFileDrops();
		return true;
	}
	
	bool GetData(LArray<LDragData> &Data)
	{
		if (!Data.Length())
			return false;

		List<ResultItem> Sel;
		if (!GetSelection(Sel))
			return false;

		return Sel[0]->GetData(Data);
	}
		
	void OnItemClick(LListItem *Item, LMouse &m)
	{
		ResultItem *i = dynamic_cast<ResultItem*>(Item);
		if (i)
		{
			if (m.Down() && m.Double())
			{
				i->DoUI();
			}
			/*
			else if (i->T && m.Right())
			{
				i->T->DoContextMenu(this);
			}
			*/
		}
	}

	void SetSort(int col, int ascend)
	{
		Col = col;
		Ascend = ascend != 0;
		Sort([this](auto *a, auto *b)
			{
				auto t1 = dynamic_cast<ResultItem*>(a);
				auto t2 = dynamic_cast<ResultItem*>(b);
				if (t1 && t2)
				{
					// do specific compare on mail items..
					auto m1 = t1->T->IsMail();
					auto m2 = t2->T->IsMail();
					int Mul = Ascend ? 1 : -1;
					if (m1 && m2)
					{
						switch (Col)
						{
							case 2:
							{
								// size
								return Mul * (int)(m1->TotalSizeof() - m2->TotalSizeof());
								break;
							}
							case 3:
							{
								// date
								int Sort = 0;
								auto d1 = m1->GetDateSent();
								auto d2 = m2->GetDateSent();
								if (d1 && d2)
								{
									if (*d1 < *d2)
										Sort = -1;
									if (*d1 > *d2)
										Sort = 1;
									return Mul * Sort;
								}
								break;
							}
						}
					}

					// default back to a string compare
					return Mul * Stricmp(a->GetText(Col), b->GetText(Col));
				}

				return 0;
			});
		SetSortingMark(Col, !Ascend);
	}

	void OnColumnClick(int col, LMouse &m)
	{
		if (Col != col)
		{
			SetSort(col, true);
		}
		else
		{
			SetSort(Col, !Ascend);
		}
	}
};

///////////////////////////////////////////////////////////////////////
class FindTask
{
	// Work vars
	ScribeWnd *App = NULL;
	ResultList *Results = NULL;
	bool InMail;
	int MailF;
	bool InContacts;
	int ContactF;
	LArray<char*> SearchText;
	bool Deep;
	bool CaseSensitive;
	bool MatchWord;
	bool Loop = true;
	LArray<uint32_t> Colours;
	LAutoPtr<LGroupMap> GroupMap;

	// Iteration control
	ssize_t CurFolder = -1;
	LArray<ScribeFolder*> Folders;
	size_t CurItem = 0;

	// Status
	uint64 StartTime = 0;
	int ItemsSearched = 0;
	LString CurFolderPath;

	// Thread vars
	LViewI *Notify;

	bool MatchString(const char *String, char *Pattern)
	{
		if (String && Pattern)
		{
			size_t Len = strlen(Pattern);
			auto s = String;
			do
			{
				auto c = (CaseSensitive)?strstr(s, Pattern):stristr(s, Pattern);
				
				if (c)
				{
					if (MatchWord)
					{
						bool Before = c <= String || IsWordBoundry(c[-1]);
						bool After = c[Len] == 0 || IsWordBoundry(c[Len]);
						if (Before && After)
							return true;
					}
					else
					{
						return true;
					}
				}
				else break;

				s = c + Len;
			}
			while (s && *s);
		}

		return false;
	}

	bool MatchAddress(LDataPropI *Addr, char *Pattern, int CC = -1)
	{
		bool Status = false;

		if (Addr && Pattern && (CC < 0 || Addr->GetInt(FIELD_CC) == CC))
		{
			if (Addr->GetStr(FIELD_NAME))
			{
				Status |= MatchString(Addr->GetStr(FIELD_NAME), Pattern);
			}
			if (Addr->GetStr(FIELD_EMAIL))
			{
				Status |= MatchString(Addr->GetStr(FIELD_EMAIL), Pattern);
			}

			// Status |= (Addr->Name) ? stristr(Addr->Name, Text) != 0 : 0;
			// Status |= (Addr->Addr) ? stristr(Addr->Addr, Text) != 0 : 0;
		}

		return Status;
	}

	bool MatchMail(Mail *mail, int Field = -1)
	{
		if (!mail)
			return false;

		#define StrField(fld, id) if (id == Field || Field < 0) Status |= (MatchString(fld, Text) != 0);

		bool Result = true;
		for (unsigned i=0; i<SearchText.Length(); i++)
		{
			bool Status = false;
			char *Text = SearchText[i];

			if (Field == FIELD_SIZE)
			{
				const char *Ops = "<>=!";
				const char *WhiteSpace = " \t\r\n";
				const char *Number = "0123456789.e-";

				char *s = Text;
				for (; *s && strchr(WhiteSpace, *s); s++);
				char *StartOp = s;
				for (; *s && strchr(Ops, *s); s++);
				char *Op = NewStr(StartOp, s-StartOp);
				if (Op)
				{
					for (; *s && strchr(WhiteSpace, *s); s++);
					char *StartNumber = s;
					for (; *s && strchr(Number, *s); s++);
					// char *EndNumber = s;
					double Scale = 1.0;
					for (; *s; s++)
					{
						if (*s == 'k' || *s == 'K')
						{
							Scale = 1 << 10;
							break;
						}
						else if (*s == 'm' || *s == 'M')
						{
							Scale = 1 << 20;
							break;
						}
						else if (*s == 'g' || *s == 'G')
						{
							Scale = 1 << 30;
							break;
						}
					}

					int64 Size = mail->TotalSizeof();
					int Value = (int) (atof(StartNumber) * Scale);

					if (_stricmp(Op, "=") == 0)
					{
						Status = Size == Value;
					}
					else if (_stricmp(Op, "!=") == 0)
					{
						Status = Size != Value;
					}
					else if (_stricmp(Op, "<") == 0)
					{
						Status = Size < Value;
					}
					else if (_stricmp(Op, ">") == 0)
					{
						Status = Size > Value;
					}
					else if (_stricmp(Op, "<=") == 0)
					{
						Status = Size <= Value;
					}
					else if (_stricmp(Op, ">=") == 0)
					{
						Status = Size >= Value;
					}

					DeleteArray(Op);
				}
			}

			if (Field == FIELD_TEXT || Field < 0)
			{
				StrField(mail->GetBody(), FIELD_TEXT);
				StrField(mail->GetHtml(), FIELD_ALTERNATE_HTML);
			}

			StrField(mail->GetSubject(), FIELD_SUBJECT);
			// FIXME:
			// StrField(mail->GetMessageId(false), FIELD_MESSAGE_ID);
			StrField(mail->GetInternetHeader(), FIELD_INTERNET_HEADER);
			StrField(mail->GetLabel(), FIELD_LABEL);
			
			if (Field == FIELD_TO || Field == FIELD_CC || Field < 0)
			{
				LDataIt To = mail->GetTo();
				for (LDataPropI *a = To->First(); a; a = To->Next())
				{
					Status |= MatchAddress(a, Text, Field == FIELD_CC);
				}
			}
			if (Field == FIELD_FROM || Field < 0)
			{
				Status |= MatchAddress(mail->GetFrom(), Text);
			}
			if (Field == FIELD_REPLY || Field < 0)
			{
				Status |= MatchAddress(mail->GetObject()->GetObj(FIELD_REPLY), Text);
			}
			if (Field == FIELD_ATTACHMENTS_NAME || Field < 0)
			{
				List<Attachment> Files;
				if (mail->GetAttachments(&Files))
				{
					for (auto a: Files)
					{
						auto FileName = a->GetName();
						Status |= MatchString(FileName, Text) != 0;
					}
				}
			}
			if (Field == FIELD_MEMBER_OF_GROUP || Field < 0)
			{
				auto From = mail->GetFrom();
				if (From)
				{
					auto email = From->GetStr(FIELD_EMAIL);
					if (email && GroupMap)
					{
						auto grps = GroupMap->Find(email);
						if (grps)
							Status |= MatchString(grps->toString(), Text);
					}
				}
			}
			
			Result &= Status;
		}

		if (Colours.Length())
		{
			auto Col = mail->GetMarkColour();
			Result &= Colours.HasItem((uint32_t)Col);
		}

		return Result;
	}

	bool MatchFilter(Filter *filter)
	{
		if (!filter || !filter->GetObject() || SearchText.Length() == 0)
			return false;

		LArray<const char*> Strs;
		auto Obj = filter->GetObject();
		static int Fields[] = { FIELD_FILTER_NAME, FIELD_FILTER_SCRIPT, FIELD_FILTER_CONDITIONS_XML, FIELD_FILTER_ACTIONS_XML };
		for (int i=0; i<CountOf(Fields); i++)
		{
			auto s = Obj->GetStr(Fields[i]);
			if (s) Strs.Add(s);
		}

		for (auto t: SearchText)
		{
			for (auto s : Strs)
				if (MatchString(s, t))
					return true;
		}
		
		return false;
	}

	bool MatchContact(Contact *contact, int Field = -1)
	{
		if (!contact || !contact->GetObject() || SearchText.Length() == 0)
			return false;

		const char *v = 0;
		bool Status = true;

		for (unsigned i=0; i<SearchText.Length(); i++)
		{
			char *Text = SearchText[i];
			bool Found = false;
			
			if (Field >= 0)
			{
				ItemFieldDef *Def = GetFieldDefById(Field);
				if (Def)
				{
					if ((v = contact->GetObject()->GetStr(Def->FieldId)))
					{
						Found |= (stristr(v, Text) != 0);
					}
				}
			}
			else
			{
				for (ItemFieldDef *fd = ContactFieldDefs; !Found && fd->FieldId; fd++)
				{
					if ((v = contact->GetObject()->GetStr(fd->FieldId)))
					{
						Found |= stristr(v, Text) != 0;
					}
				}
			}
			
			Status &= Found;
		}
		
		return Status;
	}

	void AddSubFolders(ScribeFolder *f)
	{
		for (auto c = f->GetChildFolder(); c; c = c->GetNextFolder())
		{
			Folders.Add(c);
			AddSubFolders(c);
		}
	}

public:
	char Status[256];
	constexpr static int TIMESLICE  = 250; // ms - use this much time to do searching... then yeild
	constexpr static int PULSE_TIME = 500; // ms

	FindTask(	ScribeWnd *app,
				LView *notify,
				ScribeFolder *folder,
				ResultList *results,
				bool mail,
				int mailf,
				bool contact,
				int contactf,
				const char *searchText,
				bool deep,
				bool case_sensitive,
				bool match_word,
				LArray<uint32_t> colours,
				LAutoPtr<LGroupMap> groupMap)
	{
		App = app;
		Notify = notify;
		Results = results;
		InMail = mail;
		MailF = mailf;
		InContacts = contact;
		ContactF = contactf;
		Deep = deep;
		CaseSensitive = case_sensitive;
		MatchWord = match_word;
		Colours = colours;
		ItemsSearched = 0;
		GroupMap = groupMap;

		Folders.Add(folder);
		if (Deep)
			AddSubFolders(folder);

		if (searchText)
		{
			const char *White = " \t\r\n";
			
			for (auto s = searchText; s && *s; )
			{
				while (*s && strchr(White, *s)) s++;
				if (*s && strchr("\'\"", *s))
				{
					char Delim = *s++;
					char *e = strchr(s, Delim);
					if (e)
					{
						SearchText.Add( NewStr(s, e-s) );
						s = e + 1;
					}
					else
					{
						SearchText.Add( NewStr(s) );
						break;
					}
				}
				else
				{
					const char *e = s;
					while (*e && !strchr(White, *e)) e++;
					SearchText.Add( NewStr(s, e-s) );
					s = *e ? e + 1 : 0;
				}
			}
		}

		Loop = true;
		StartTime = LCurrentTime();
	}

	~FindTask()
	{
		Loop = false;
		SearchText.DeleteArrays();
	}

	void Search(bool i)
	{
		Loop = i;
	}

	void SetNotify(LViewI *w)
	{
		Notify = w;
	}

	bool AddThing(Thing *T)
	{
		ResultItem *Item = 0;
		if (T && Results)
		{
			Results->Insert(Item = new ResultItem(App, T));
		}
		return Item != 0;
	}

	void OnComplete()
	{
		Loop = false;

		if (Notify)
			Notify->PostEvent(M_END_SEARCH);
	}

	void SearchTimeslice()
	{
		auto StartTs = LCurrentTime();
		ScribeFolder *Folder = NULL;

		while (	Loop &&
				(LCurrentTime() - StartTs) < TIMESLICE)
		{
			if (!Folder && Folders.IdxCheck(CurFolder))
				Folder = Folders[CurFolder]; // Get the current folder...
			if (Folder && CurItem >= Folder->Items.Length())
				Folder = NULL; // Completed current folder...
			if (!Folder)
			{
				// Setup new folder...
				if (Folders.IdxCheck(++CurFolder))
				{
					if ((Folder = Folders[CurFolder]))
					{
						CurFolderPath = Folder->GetPath();
						CurItem = 0;
					}
					else return OnComplete();
				}
				else return OnComplete();
			}

			LAssert(Folder);
			LAssert(CurItem < Folder->Items.Length());

			auto Item = Folder->Items[CurItem++];
			LAssert(Item);

			switch ((uint32_t)Item->Type())
			{
				case MAGIC_MAIL:
				{
					if (InMail)
					{
						bool PreLoad = Item->GetObject() != 0;
						if (auto m = Item->IsMail())
						{
							if (MatchMail(m, MailF))
							{
								PreLoad = AddThing(m);
								break;
							}
							ItemsSearched++;
						}
					}
					break;
				}
				case MAGIC_CONTACT:
				{
					if (InContacts)
					{
						bool PreLoad = Item->GetObject() != 0;
						if (auto c = Item->IsContact())
						{
							if (MatchContact(c, ContactF))
							{
								PreLoad = AddThing(c);
								break;
							}
							ItemsSearched++;
						}
					}
					break;
				}
				case MAGIC_FILTER:
				{
					bool PreLoad = Item->GetObject() != 0;
					if (auto f = Item->IsFilter())
					{
						if (MatchFilter(f))
						{
							PreLoad = AddThing(f);
							break;
						}
						ItemsSearched++;
					}
					break;						
				}
			}
		}
	}
	
	LString GetStatus()
	{
		char s[256];
		double Sec = (double)(LCurrentTime() - StartTime) / 1000.0;
		double Rate = Sec != 0.0 ? ItemsSearched / Sec : 0.0;

		sprintf_s(s, sizeof(s), "Searching '%s', %.1f items/s.", CurFolderPath.Get(), Rate);
		
		return s;
	}
};

///////////////////////////////////////////////////////////////////////
class FindWnd :
	public LWindow,
	public LResourceLoad,
	public LDataEventsI
{
	ScribeWnd *App = NULL;
	LEdit *Text = NULL;
	LButton *Search = NULL;
	LEdit *Folder = NULL;
	LCheckBox *SearchSub = NULL;
	LCheckBox *SearchMail = NULL;
	LCombo *MailField = NULL;
	LCheckBox *SearchContact = NULL;
	LCombo *ContactField = NULL;
	ResultList *Results = NULL;
	LArray<int> MailFieldIds;

	char *SearchBtnText = NULL;
	FindTask *Task = NULL;

	void OnSearch(bool Searching);

public:
	FindWnd(ScribeWnd *app, ScribeFolder *folder);
	~FindWnd();

	int OnNotify(LViewI *Col, const LNotification &n) override;
	LMessage::Result OnEvent(LMessage *m) override;

	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new) override;
	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items) override;
	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items) override;
	bool OnChange(LArray<LDataI*> &items, int FieldHint) override;
	void OnPulse() override;
};


FindWnd::FindWnd(ScribeWnd *app, ScribeFolder *folder)
{
	App = app;
	SetQuitOnClose(false);

	// Setup controls
	LString ResName;
	LRect r;
	if (LoadFromResource(IDD_FIND, this, &r, &ResName))
	{
		SetPos(r);
		Name(ResName);

		GetViewById(IDC_TEXT, Text);
		GetViewById(IDOK, Search);
		GetViewById(IDC_FOLDER, Folder);
		GetViewById(IDC_SEARCH_SUB, SearchSub);
		GetViewById(IDC_MAIL, SearchMail);
		GetViewById(IDC_MAIL_FIELD, MailField);
		GetViewById(IDC_CONTACT, SearchContact);
		GetViewById(IDC_CONTACT_FIELD, ContactField);
		
		if (Search)
		{
			SearchBtnText = NewStr(Search->Name());
		}

		LTableLayout *Tbl;
		if (GetViewById(IDC_TABLE, Tbl))
		{
		    auto *c = Tbl->GetCell(0, Tbl->CellY()-1);
			c->Add(Results = new ResultList(IDC_RESULTS, 10, 260, 400, 300, ""));
		}

		// Initialize controls
		if (folder)
		{
			auto FolderPath = folder->GetPath();
			if (FolderPath)
				SetCtrlName(IDC_FOLDER, FolderPath);
		}
		else
		{
			SetCtrlName(IDC_FOLDER, "/");
		}

		char n[256];
		sprintf_s(n, sizeof(n), "(%s)", LLoadString(IDS_NONE));
		if (MailField)
		{
			MailField->Insert(n);
			for (ItemFieldDef *Def = MailFieldDefs; Def->FieldId; Def++)
			{
				const char *FName = LLoadString(Def->FieldId);
				if (FName)
				{
					MailField->Insert(FName);
					MailFieldIds.Add(Def->FieldId);
				}
			}

			/* Unsupported yet...
			MailField->Insert(LLoadString(IDS_ATTACHMENTS_DATA));
			MailFieldIds.Add(FIELD_ATTACHMENTS_DATA);
			*/

			MailField->Insert(LLoadString(IDS_ATTACHMENTS_NAME));
			MailFieldIds.Add(FIELD_ATTACHMENTS_NAME);

			MailField->Insert(LLoadString(IDS_MEMBER_OF_GROUP));
			MailFieldIds.Add(FIELD_MEMBER_OF_GROUP);
		}
		if (ContactField)
		{
			ContactField->Insert(n);
			for (ItemFieldDef *Def = ContactFieldDefs; Def->CtrlId; Def++)
			{
				ContactField->Insert(LLoadString(Def->FieldId));
			}
		}
		if (Results)
		{
			Results->AskText(true);
			Results->SetImageList(App->GetIconImgList(), false);
			switch ((uint32_t)folder->GetItemType())
			{
				case MAGIC_MAIL:
					Results->AddColumn(LLoadString(Mail::DefaultMailFields[0]), 100);
					Results->AddColumn(LLoadString(Mail::DefaultMailFields[1]), 100);
					Results->AddColumn(LLoadString(Mail::DefaultMailFields[2]), 100);
					Results->AddColumn(LLoadString(Mail::DefaultMailFields[3]), 100);
					break;
				case MAGIC_CONTACT:
					Results->AddColumn(LLoadString(Contact::DefaultContactFields[0]), 100);
					Results->AddColumn(LLoadString(Contact::DefaultContactFields[1]), 100);
					Results->AddColumn(LLoadString(Contact::DefaultContactFields[2]), 100);
					Results->AddColumn("", 100);
					break;
				default:
					Results->AddColumn("", 100);
					Results->AddColumn("", 100);
					Results->AddColumn("", 100);
					Results->AddColumn("", 100);
					break;
			}
			Results->AddColumn(LLoadString(IDS_1062), 200);
		}

		SetCtrlValue(IDC_SEARCH_SUB, false);
		switch ((uint32_t)folder->GetItemType())
		{
			case MAGIC_MAIL:
				SetCtrlValue(IDC_MAIL, true);
				SetCtrlValue(IDC_CONTACT, false);
				break;
			case MAGIC_CONTACT:
				SetCtrlValue(IDC_MAIL, false);
				SetCtrlValue(IDC_CONTACT, true);
				break;
			default:
				SetCtrlValue(IDC_MAIL, true);
				SetCtrlValue(IDC_CONTACT, true);
				break;
		}
		MoveSameScreen(App);

		// Create window
		if (Attach(0))
		{
			AttachChildren();

			if (Search)
				Search->Default(true);
			if (Folder)
				Folder->Enabled(false);

			Visible(true);
			Text->Focus(true);
		}
	}

	App->AddStore3EventHandler(this);
}

FindWnd::~FindWnd()
{
	App->RemoveStore3EventHandler(this);

	DeleteObj(Task);
	DeleteArray(SearchBtnText);
}

void FindWnd::OnSearch(bool Searching)
{
	Text->Enabled(!Searching);
	Folder->Enabled(!Searching);
	SearchSub->Enabled(!Searching);
	SearchMail->Enabled(!Searching);
	MailField->Enabled(!Searching);
	SearchContact->Enabled(!Searching);
	ContactField->Enabled(!Searching);
	// Results->Enabled(!Searching);

	MailField->Invalidate();
	ContactField->Invalidate();
	SetPulse(Searching ? FindTask::PULSE_TIME : -1);

	if (Searching)
	{
		Search->Name(LLoadString(IDS_CANCEL));
	}
	else
	{
		Search->Name(SearchBtnText ? SearchBtnText : (char*)"Search");
	}
}

int FindWnd::OnNotify(LViewI *Col, const LNotification &n)
{
	switch (Col->GetId())
	{
		case IDC_PICK_FOLDER:
		{
			if (Folder)
			{
				auto Dlg = new FolderDlg(this, App);
				Dlg->DoModal([this, Dlg](auto dlg, auto id)
				{
					if (id)
						this->Folder->Name(Dlg->Get());
				});
			}
			break;
		}
		case IDOK:
		{
			if (Results && Folder)
			{
				if (Task)
				{
					Task->Search(false);
					Search->Name("Ending...");
				}
				else
				{
					Results->Empty();

					int MailF = -1;
					int ContactF = -1;
					if (MailField)
					{
						int Index = (int)MailField->Value();
						if (Index > 0)
						{
							Index--;
							if (Index < (int)MailFieldIds.Length())
							{
								MailF = MailFieldIds[Index];
							}
						}
					}
					if (ContactField)
					{
						int Index = (int) ContactField->Value();
						if (Index > 0)
						{
							ContactF = ContactFieldDefs[Index-1].FieldId;
						}
					}
					
					ScribeFolder *Root = App->GetFolder(Folder->Name());
					if (Root)
					{
						auto it = Root->Items.begin();

						LProgressDlg prog(this, 1000);
						prog.SetDescription("Get message id's...");
						prog.SetRange(Root->Items.Length());
						int n = 0;
						for (Thing *t = *it; t; t = *++it, n++)
						{
							Mail *m = t->IsMail();
							if (m)
							{
								// This can't be done in the thread, so do it here
								m->GetMessageId();
							}
							prog.Value(n);
						}

						LArray<uint32_t> Colours;
						LMarkColourSelect *Mcs;
						if (GetViewById(IDC_COLOUR, Mcs))
						{
							for (int i=0; i<CountOf(Mcs->ColSel); i++)
							{
								if (Mcs->ColSel[i])
									Colours.Add(MarkColours32[i]);
							}
						}

						Task = new FindTask(App,
											this,
											Root,
											Results,
											SearchMail && SearchMail->Value() > 0,
											MailF,
											SearchContact && SearchContact->Value() > 0,
											ContactF,
											Text->Name(),
											SearchSub && SearchSub->Value(),
											GetCtrlValue(IDC_FIND_CASE) != 0,
											GetCtrlValue(IDC_FIND_WORD) != 0,
											Colours,
											LAutoPtr<LGroupMap>(new LGroupMap(App)));
						OnSearch(true);
					}
				}
			}
			break;
		}
	}

	return 0;
}

LMessage::Result FindWnd::OnEvent(LMessage *m)
{
	switch (m->Msg())
	{
		case M_END_SEARCH:
		{
			// thread is ended, it will delete itself
			OnSearch(false);
			DeleteObj(Task);
			break;
		}
	}

	return LWindow::OnEvent(m);
}

void FindWnd::OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
{
}

bool FindWnd::OnDelete(LDataFolderI *parent, LArray<LDataI*> &items)
{
	if (Results)
	{
		LHashTbl<PtrKey<LDataI*>, LListItem*> Map;
		List<ResultItem> a;
		if (Results->GetAll(a))
		{
			for (auto i: a)
			{
				Map.Add(i->GetThing()->GetObject(), i);
			}
		}

		for (unsigned i=0; i<items.Length(); i++)
		{
			LListItem *it = Map.Find(items[i]);
			DeleteObj(it);
		}
	}

	return true;
}

bool FindWnd::OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
{
	if (Results)
	{
		LHashTbl<PtrKey<LDataI*>, LListItem*> Map;
		List<ResultItem> a;
		if (Results->GetAll(a))
		{
			for (auto i: a)
			{
				Map.Add(i->GetThing()->GetObject(), i);
			}
		}

		for (unsigned i=0; i<items.Length(); i++)
		{
			LListItem *it = Map.Find(items[i]);
			if (it)
			{
				it->Update();
			}
		}
	}

	return true;
}

void FindWnd::OnPulse()
{
	LViewI *v;
	if (!Task || !GetViewById(IDC_STATUS, v))
		return;

	Task->SearchTimeslice();
	v->Name(Task->GetStatus());
	v->SendNotify(LNotifyTableLayoutRefresh);
}

bool FindWnd::OnChange(LArray<LDataI*> &items, int FieldHint)
{
	return true;
}

LView *OpenFinder(ScribeWnd *App, ScribeFolder *Folder)
{
	return new FindWnd(App, Folder);
}

