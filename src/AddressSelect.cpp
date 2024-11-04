#include "Scribe.h"
#include "lgi/common/Edit.h"
#include "resdefs.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/DisplayString.h"
#include "ScribeListAddr.h"
#include "lgi/common/LgiRes.h"
#include "AddressSelect.h"

///////////////////////////////////////////////////////////////////////////////////////
static const char *Empty = "";

AddressList::AddressList(ScribeWnd *app, int id, int x, int y, int cx, int cy, const char *name)
	: LList(id, x, y, cx, cy, name)
{
	App = app;
	SetObjectName(Res_Custom);
	ColumnHeaders(false);
}

int AddressList::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	if (Formats.HasFormat(ScribeThingList))
		Formats.Supports(ScribeThingList);
	else
		Formats.SupportsFileDrops();
	return Formats.GetSupported().Length() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

int AddressList::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;
	
	for (unsigned idx=0; idx<Data.Length(); idx++)
	{
		LDragData &dd = Data[idx];
		if (dd.IsFormat(ScribeThingList))
		{
			if (dd.Data.Length() > 0 &&
				dd.Data[0].Type == GV_BINARY &&
				ScribeClipboardFmt::IsThing(dd.Data[0].Value.Binary.Data, dd.Data[0].Value.Binary.Length))
			{
				ScribeClipboardFmt *tl = (ScribeClipboardFmt*)dd.Data[0].Value.Binary.Data;
				for (uint32_t i=0; i<tl->Length(); i++)
				{
					Contact *c = tl->ThingAt(i)->IsContact();
					if (c)
					{
						ListAddr *New = new ListAddr(c);
						if (New)
							Insert(New, -1, false);
					}
				}

				Invalidate();				
				Status = DROPEFFECT_COPY;
			}
		}
		else if (dd.IsFileDrop())
		{
			LAssert(!"Impl contact drop here...");
		}
	}

	return DROPEFFECT_NONE;
}

void AddressList::OnCreate()
{
	LDisplayString ds(LSysFont, "Bcc:");
	
	AddColumn("To", ds.X() + 2);
	AddColumn("Info", 1000);

	SetWindow(this);
}

void AddressList::OnInit(LDataIt l)
{
	Empty();

	for (LDataPropI *a = l->First(); a; a = l->Next())
	{
		ListAddr *New = new ListAddr(App, a);
		if (New)
		{
			New->begin();
			Insert(New, -1, false);
		}
	}

	Invalidate();
}

void AddressList::OnSave(LDataStoreI *store, LDataIt l)
{
	l->DeleteObjects();

	List<ListAddr> a;
	GetAll(a);
	for (auto i: a)
	{
		LDataPropI *n = l->Create(store);
		if (n)
		{
			n->CopyProps(*i);
			l->Insert(n);
		}
	}
}

void AddressList::OnItemClick(LListItem *Item, LMouse &m)
{
	LList::OnItemClick(Item, m);

	// Do the right click menu
	if (!Item && m.Right())
	{
		LSubMenu *RClick = new LSubMenu;
		if (RClick)
		{
			RClick->AppendItem(LLoadString(IDS_ADD), IDM_NEW_CONTACT, true);

			if (GetMouse(m, true))
			{
				switch (RClick->Float(this, m.x, m.y))
				{
					case IDM_NEW_CONTACT:
					{
						LViewI *v = GetWindow()->FindControl(IDC_ADD);
						if (v)
						{
							LNotification note(LNotifyValueChanged);
							GetWindow()->OnNotify(v, note);
						}
						break;
					}
				}
			}

			DeleteObj(RClick);
		}
	}
}

void AddressList::Copy()
{
	List<LListItem> Sel;
	if (GetSelection(Sel))
	{
		LStringPipe p;

		for (auto i: Sel)
		{
			ListAddr *La = dynamic_cast<ListAddr*>(i);
			if (La)
			{
				auto s = La->Copy();
				if (s)
					p.Print("%s\r\n", s.Get());
			}
		}

		auto t = p.NewLStr();
		if (t)
		{
			LClipBoard Clip(this);
			Clip.Text(t);
			LAutoWString w(Utf8ToWide(t));
			if (w)
				Clip.TextW(w, false);
		}
	}
}

void AddressList::Paste()
{
	LClipBoard Clip(this);
	LString Txt = Clip.Text();
	if (Txt)
	{
		auto Lines = Txt.SplitDelimit("\r\n");
		for (auto &ln: Lines)
		{
			ListAddr *La = new ListAddr(App);
			if (La)
			{
				La->Paste(ln);
				Insert(La);
			}
		}
	}
}

bool AddressList::OnKey(LKey &k)
{
	bool Status = false;
	
	if (k.Down())
	{
		switch (k.vkey)
		{
			case LK_DELETE:
			{
				if (!k.IsChar)
				{
					List<LListItem> Sel;
					LList::GetSelection(Sel);

					for (auto i: Sel)
					{
						Delete(i);
					}
					Status = true;
				}
				break;
			}
			default:
			{
				switch (k.c16)
				{
					case 'c':
					case 'C':
					{
						Copy();
						Status = true;
						break;
					}
					case 'v':
					case 'V':
					{
						Paste();
						Status = true;
						break;
					}
				}
				break;
			}
		}
	}
	
	return LList::OnKey(k) || Status;
}


////////////////////////////////////////////////////////////////////////////
int AddrBrItemCmp(BrowseItem **a, BrowseItem **b)
{
	if ((*a)->Score != (*b)->Score)
	{
		return (*b)->Score - (*a)->Score;
	}
	else
	{
		char *A = (*a)->First, *B = (*b)->First;
		if (A && B)
		{
			return _stricmp(A, B);
		}
	}
	
	return 0;
}

class AddressBrowsePluginResults : public LDom, public LMutex
{
	LArray<LDom*> Results;
	LAutoString Msg;

public:
	bool Dirty;

	AddressBrowsePluginResults() : LMutex("AddressBrowsePluginResults")
	{
		Dirty = false;
	}
	
	const char *GetClass() override { return "AddressBrowsePluginResults"; }
	
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override
	{
		for (unsigned i=0; i<Args.Length(); i++)
		{
			LVariant *v = Args[i];
			if (v->Type == GV_DOM)
			{
				Results.Add(v->Value.Dom);
				Dirty = true;
			}
			else if (v->Type == GV_STRING)
			{
				Msg.Reset(v->ReleaseStr());
				Dirty = true;
			}
		}
		return true;
	}
}
PluginResults;

class AddressBrowsePrivate
{
public:
	AddressBrowse *Ad = NULL;
	ScribeWnd *App = NULL;
	LEdit *Target = NULL;
	LList *Recip = NULL;
	LViewI *SetTo = NULL;
	LArray<BrowseItem*> Items;
	LHashTbl<StrKey<char,false>, BrowseItem*> Has;
	ssize_t WordStart = 0, WordEnd = 0;

	AddressBrowsePrivate(AddressBrowse *ad)
	{
		Ad = ad;
	}

	void Test(LString::Array &Txt, const char *First, const char *Last, LString::Array &Email, const char *Nick)
	{
		if (Txt.Length() < 1)
			return;

		LAutoString TxtEmail;
		ssize_t i, EmailIdx = -1;
		for (i=0; i<(int)Txt.Length(); i++)
		{
			if (strchr(Txt[i], '@'))
			{
				TxtEmail.Reset(TrimStr(Txt[i], "{}[]<>()"));
				EmailIdx = i;
				break;
			}
		}

		#define ScoreText(Str, Search, Complete, First, Nth) \
			if (Str) \
			{ \
				char *s = Search; \
				if (s) \
				{ \
					size_t Len = strlen(s); \
					if (_stricmp(Str, s) == 0) Score += Complete; \
					if (_strnicmp(Str, s, Len) == 0) Score += First; \
					else if (stristr(Str, s)) Score += Nth; \
				} \
			}

		for (i=0; i<(ssize_t)Email.Length(); i++)
		{
			char *Match = NULL;
			int Score = 0;
			
			if (Txt.Length() == 1)
			{
				if (EmailIdx != 0)
				{
					ScoreText(First, Txt[0], 6, 5, 4);
					ScoreText(Last, Txt[0], 5, 4, 3);
				}
			}
			else if (Txt.Length() == 2)
			{
				if (EmailIdx != 0)
					ScoreText(First, Txt[0], 6, 5, 4);
				if (EmailIdx != 1)
					ScoreText(Last, Txt[1], 6, 5, 4);
			}
			
			// Full email match?
			if (TxtEmail)
			{
				if (stristr(Email[i], TxtEmail))
				{
					Match = Email[i];
					Score += 7;
				}
			}
			else
			{
				// Partial email match?
				if (stristr(Email[i], Txt[0]))
				{
					Match = Email[i];
					Score += 2;
				}
			}

			if (TxtEmail && !Match)
			{
				// if they specify an email address and it not
				// found at all then this can't be the contact
				// they are looking for
				return;
			}
			
			Score += stristr(Nick, Txt[0]) ? 1 : 0;
			if (Score)
			{
				char *Addr = Match ? Match : Email[i].Get();
				BrowseItem *i;

				if (!Has.Find(Addr))
				{
					Items.Add(i = new BrowseItem(First, Last, Addr, Score));
					if (i)
						Has.Add(Addr, i);
				}
			}
		}
	}
	
	void Update()
	{
		Items.DeleteObjects();
		Has.Empty();
			
		char *Txt = NULL;
		char *RawTxt = (char*)Target->Name();
		if (RawTxt)
		{
			ssize_t CharPos = Target->GetCaret();
			ssize_t BytePos = LSeekUtf8(RawTxt, CharPos) - RawTxt;

			Txt = RawTxt + BytePos;
			if (Txt)
			{
				auto s = Txt;

				// Seek to the start of the name
				while (Txt > RawTxt && !strchr(MailAddressDelimiters, Txt[-1]))
				{
					Txt = LSeekUtf8(Txt, -1, RawTxt);
				}
				WordStart = Txt - RawTxt;

				// Seek to the end of the name
				Txt = s;
				while (Txt[0] && !strchr(MailAddressDelimiters, Txt[0]))
					Txt = LSeekUtf8(Txt, 1);
				WordEnd = Txt - RawTxt;

				// Store the sub-string
				Txt = NewStr(RawTxt + WordStart, WordEnd - WordStart);
			}
		}

		// printf("%s:%i - onupdate '%s'\n", _FL, Txt);
		if (ValidStr(Txt))
		{
			Scan(Txt);

			// Put in the UI
			if (Ad)
			{
				// printf("Items=%i\n", (int)Items.Length());
				Ad->SetItems(Items);
			}

			DeleteArray(Txt);
		}
	}

	bool Scan(LString Txt)
	{
		if (!App)
			return false;

		auto t = Txt.SplitDelimit(" ");
		LHashTbl<StrKey<char,false>,Contact*> Contacts;
		App->HashContacts(Contacts);

		for (auto c: Contacts)
		{
			const char *First = 0, *Last = 0, *Nick = 0;
			c.value->Get(OPT_First, First);
			c.value->Get(OPT_Last, Last);
			c.value->Get(OPT_Nick, Nick);

			auto Emails = c.value->GetEmails();
			Test(t, First, Last, Emails, Nick);
		}
			
		auto GrpSrcs = App->GetThingSources(MAGIC_GROUP);
		for (auto Groups: GrpSrcs)
		{
			if (!Groups->IsLoaded())
				Groups->LoadThings(); // FIXME... this should refresh the browse list after load

			for (auto t : Groups->Items)
			{
				ContactGroup *Grp = t->IsGroup();
				if (Grp)
				{
					LVariant Name;
					LArray<char*> Empty;
					if (Grp->GetVariant("Name", Name) && Name.Str())
					{
						if (stristr(Name.Str(), Txt) &&
							!Has.Find(Name.Str()))
						{
							BrowseItem *i;
							Items.Add(i = new BrowseItem(Name.Str(), 0, Name.Str(), 1));
							if (i)
							{
								Has.Add(Name.Str(), i);
							}
						}
					}
				}
			}
		}
			
		// Sort
		Items.Sort(AddrBrItemCmp);

		return true;
	}
};

bool AddressBrowseLookup(ScribeWnd *App, LArray<BrowseItem*> &Items, LString s)
{
	AddressBrowsePrivate d(NULL);
	
	d.App = App;
	if (!d.Scan(s))
		return false;

	d.Items.Swap(Items);
	return true;	
}

AddressBrowse::AddressBrowse(ScribeWnd *app, LView *target, LList *recip, LViewI *setto) :
	LPopupList<BrowseItem>(target, PopupBelow, target->X(), 150)
{
	d = new AddressBrowsePrivate(this);
	d->App = app;
	d->Recip = recip;
	d->SetTo = setto;
	d->Target = dynamic_cast<LEdit*>(target);
	
	Name("AddressBrowse");
}

AddressBrowse::~AddressBrowse()
{
	DeleteObj(d);
}

LString AddressBrowse::ToString(BrowseItem *Obj)
{
	LString s;
	s.Printf("%s %s <%s>",
			Obj->First ? Obj->First.Get() : Empty,
			Obj->Last ? Obj->Last.Get() : Empty,
			Obj->Email.Get());
	return s;
}

void AddressBrowse::OnSelect(BrowseItem *a)
{
	if (!a || !a->Email)
		return;

	LStringPipe p;
	const char *Cur = d->Target->Name();
	if (Cur)
	{
		p.Push(Cur, d->WordStart);
		p.Push(Cur + d->WordEnd);
	}

	d->Target->Name(p.NewLStr());
	d->Target->SetCaret(d->WordStart);

	if (d->Recip)
	{
		char FullName[300], *Name = 0;
		if (a->First && a->Last)
			sprintf_s(Name = FullName, sizeof(FullName), "%s %s", a->First.Get(), a->Last.Get());
		else if (a->First)
			Name = a->First;
		else if (a->Last)
			Name = a->Last;

		if (Name || a->Email)
		{
			ListAddr *n = new ListAddr(d->App, a->Email, Name);
			if (n)
			{
				if (d->SetTo)
					n->CC = (EmailAddressType) d->SetTo->Value();
				d->Recip->Insert(n);
			}
		}
	}

	Visible(false);
}


int AddressBrowse::OnNotify(LViewI *c, LNotification &n)
{
	if
	(
		c == d->Target &&
		(
			n.Type == LNotifyValueChanged ||
			n.Type == LNotifyDocChanged
		)
	)
	{
		d->Update();
	}
	else
	{
		// printf("%s:%i - no update %i %i\n", _FL, c == d->Target, !f);
	}

	return LPopupList<BrowseItem>::OnNotify(c, n);
}

