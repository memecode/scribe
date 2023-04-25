#include "Scribe.h"
#include "lgi/common/TextView3.h"
#include "resdefs.h"
#include "ScribeListAddr.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"
#include "AddressSelect.h"

ItemFieldDef GroupFieldDefs[] = {
	{ContactGroupName, SdName, GV_STRING, FIELD_GROUP_NAME, IDC_GROUP_NAME},
	{ContactGroupList, SdList, GV_STRING, FIELD_GROUP_LIST, IDC_GROUP_LIST},
	{ContactGroupDateModified, SdDateModified, GV_DATETIME, FIELD_DATE_MODIFIED, 0},
	{0}
};

int DefaultGroupFields[] =
{
	FIELD_GROUP_NAME,
	FIELD_DATE_MODIFIED,
	0,
	0
};

////////////////////////////////////////////////////////////////////////////////////////
class LAddressEdit;

struct AddressMeta
{
	int Id;
	bool Referenced;
	LAddressEdit *Edit;
	ListAddr Addr;
	LString Text;

	AddressMeta(LAddressEdit *edit, int id = -1);
	~AddressMeta();

	bool Matched()
	{
		return Addr.Length() == 1;
	}

	void Ins();
	bool IsResolved();
	bool IsUnique();
	void OpenContact();
};

class LAddressEdit : public LTextView3
{
	friend struct AddressMeta;
	LHashTbl<IntKey<int>,AddressMeta*> Map;
	LArray<BrowseItem*> Items;

	int CreateId()
	{
		int Id;
		while (Map.Find(Id = LRand(1000)))
			;
		return Id;
	}

	AddressMeta *GetMeta(LStyle &s)
	{
		return Map.Find(s.Data.CastInt32());
	}

	bool UpdateMeta(LStyle &Style)
	{
		auto m = GetMeta(Style);
		if (!m)
			return false;

		m->Addr.SetText(m->Text);

		// LgiTrace("UpdateMeta '%s' -> %i\n", m->Text.Get(), m->Addr.Length());

		if (m->Addr.Length() > 1)
		{
			// Matched more than one recip
			Style.Fore.Rgb(255, 0, 0);
			Style.Font = Underline;
		}
		else if (m->Addr.Length())
		{
			// Matched a single recip
			Style.Fore = LColour(L_BLACK);
			Style.Font = Underline;
		}
		else
		{
			// No match
			Style.Fore.Rgb(0xb0, 0xb0, 0xb0);
			Style.Font = GetFont();
			Style.Decor = LCss::TextDecorSquiggle;
			Style.DecorColour = LColour::Red;
		}

		return true;
	}

public:
	bool AllowPour;
	ScribeWnd *App;

	LAddressEdit(LRect *p, const char *t);

	void OnPaint(LSurface *pDC);
	void PourStyle(size_t Start, ssize_t Length);
	bool Insert(size_t At, const char16 *Data, ssize_t Len);
	bool Delete(size_t At, ssize_t Len);
	bool GetStyles(List<AddressMeta> &Styles);

	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);
	bool OnStyleClick(LStyle *style, LMouse *m);
	bool OnStyleMenu(LStyle *style, LSubMenu *m);
	void OnStyleMenuClick(LStyle *style, int i);
};

class GroupUi :
	public ThingUi,
	public LResourceLoad
{
	ContactGroup *Item;
	LAddressEdit *Edit;

public:
	GroupUi(ContactGroup *item);
	~GroupUi();
	int OnNotify(LViewI *c, LNotification n);

	void ResolveAll();
	void OnDirty(bool Dirty) {}
	void OnLoad();
	void OnSave();
	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState);
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState);
	void Add(char *Email);
};

////////////////////////////////////////////////////////////////////////////////////////
class ContactGroupPrivate
{
public:
};

ContactGroup::ContactGroup(ScribeWnd *app, LDataI *object) : Thing(app, object)
{
	DefaultObject(object);
	d = new ContactGroupPrivate;
	Ui = 0;
}

ContactGroup::~ContactGroup()
{
	DeleteObj(d);
}

Thing &ContactGroup::operator =(Thing &c)
{
	LAssert(0);
	return *this;
}

char *ContactGroup::GetDropFileName()
{
	if (!DropFileName)
	{
		auto Name = GetName();
		DropFileName.Reset(MakeFileName(Name ? Name : "group", "xml"));
	}
	
	return DropFileName;
}

bool ContactGroup::GetDropFiles(LString::Array &Files)
{
	auto fn = GetDropFileName();
	if (!fn)
		return false;

	if (!LFileExists(fn))
	{
		LAutoPtr<LFile> F(new LFile);
		if (F->Open(fn, O_WRITE))
		{
			F->SetSize(0);
			Export(AutoCast(F), sMimeXml);
		}
	}

	if (!LFileExists(DropFileName))
		return false;

	Files.Add(DropFileName.Get());
	
	return true;
}

bool ContactGroup::GetFormats(bool Export, LString::Array &MimeTypes)
{
	MimeTypes.Add(sTextXml);
	return MimeTypes.Length() > 0;
}

Thing::IoProgress ContactGroup::Import(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sTextXml) &&
	    Stricmp(mimeType, sMimeXml))
	    IoProgressNotImpl();

	LXmlTree Tree;
	LXmlTag r, *t;
	if (!Tree.Read(&r, stream))
		IoProgressError("Xml parse error.");

	if (!r.IsTag(ContactGroupObj))
		IoProgressError("No ContactGroup tag.");

	if ((t = r.GetChildTag(ContactGroupName)))
		GetObject()->SetStr(FIELD_GROUP_NAME, t->GetContent());
	else
		IoProgressError("No Name tag.");

	if ((t = r.GetChildTag(ContactGroupList)))
		GetObject()->SetStr(FIELD_GROUP_LIST, t->GetContent());
	else
		IoProgressError("No List tag.");

	if ((t = r.GetChildTag(ContactGroupDateModified)))
	{
		LDateTime dt;
		if (dt.Set(t->GetContent()))
			GetObject()->SetDate(FIELD_DATE_MODIFIED, &dt);
	}

	IoProgressSuccess();
}

Thing::IoProgress ContactGroup::Export(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeXml))
		IoProgressNotImpl();

	auto Name = GetName();
	LVariant Addr;
	GetVariant(ContactGroupList, Addr);
	auto Modified = GetObject()->GetDate(FIELD_DATE_MODIFIED);

	LXmlTag r(ContactGroupObj), *t;

	if ((t = r.CreateTag(ContactGroupName)))
		t->SetContent(Name);
	if ((t = r.CreateTag(ContactGroupList)))
		t->SetContent(Addr.Str());
	if (Modified &&
		Modified->IsValid() &&
		(t = r.CreateTag(ContactGroupDateModified)))
		t->SetContent(Modified->Get());
	
	LXmlTree tree;
	if (tree.Write(&r, stream))
		IoProgressError("Failed to write xml.");
	
	IoProgressSuccess();
}

LString::Array ContactGroup::GetAddresses()
{
	LString::Array Addrs;
	
	LVariant l;
	if (GetVariant(ContactGroupList, l))
	{
		auto t = l.LStr().SplitDelimit();
		for (unsigned i=0; i<t.Length(); i++)
		{
			Addrs.Add(t[i]);
		}
	}
	
	return Addrs;
}

bool ContactGroup::GetAddresses(List<char> &a)
{
	bool Status = false;
	
	LVariant l;
	if (GetVariant(ContactGroupList, l))
	{
		auto t = l.LStr().SplitDelimit();
		for (unsigned i=0; i<t.Length(); i++)
		{
			a.Insert(NewStr(t[i]));
			Status = true;
		}
	}
	
	return Status;
}

static const char *ListDelimiters = ", \r\n";

bool ContactGroup::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdName:
		{
			GetObject()->SetStr(FIELD_GROUP_NAME, Value.Str());
			break;
		}
		case SdList: // Type: String[]
		{
			if (Array)
			{
				auto Idx = Atoi(Array);
				auto t = LString(GetObject()->GetStr(FIELD_GROUP_LIST)).SplitDelimit(ListDelimiters);
				if (t.IdxCheck(Idx))
					t[Idx] = Value.Str();
				else
					t.New() = Value.Str();
				GetObject()->SetStr(FIELD_GROUP_LIST, LString("\n").Join(t));
			}
			else
			{
				GetObject()->SetStr(FIELD_GROUP_LIST, Value.Str());
			}
			break;
		}
		case SdDateModified:
		{
			return SetDateField(FIELD_DATE_MODIFIED, Value);
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool ContactGroup::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdName: // Type: String
		{
			Value = GetObject()->GetStr(FIELD_GROUP_NAME);
			break;
		}
		case SdList: // Type: String[]
		{
			if (Array)
			{
				auto t = LString(GetObject()->GetStr(FIELD_GROUP_LIST)).SplitDelimit(", \r\n");
				int Idx = atoi(Array);
				if (Idx >= 0 && Idx < (int)t.Length())
				{
					Value = t[Idx];
				}
			}
			else
			{
				Value = GetObject()->GetStr(FIELD_GROUP_LIST);
			}
			break;
		}
		case SdType: // Type: Int32
		{
			Value = GetObject()->Type();
			break;
		}
		case SdDateModified:
		{
			return GetDateField(FIELD_DATE_MODIFIED, Value);
		}
		case SdUsedTs:
		{
			Value = &UsedTs;
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool ContactGroup::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	if (GetObject() && !Stricmp((char*)MethodName, (char*)"AddAddress"))
	{
		int Added = 0;
		
		LString Addrs = GetObject()->GetStr(FIELD_GROUP_LIST);
		LString::Array a = Addrs.SplitDelimit(WhiteSpace);

		for (unsigned i=0; i<Args.Length(); i++)
		{
			char *AddrToAdd = Args[i]->Str();
			if (AddrToAdd)
			{
				bool HasAddr = false;
				for (unsigned n=0; n<a.Length(); n++)
				{
					if (!Stricmp(AddrToAdd, a[n].Get()))
					{
						HasAddr = true;
						break;
					}
				}
				if (!HasAddr)
				{
					a.SetFixedLength(false);
					a.New() = AddrToAdd;
					Added++;
				}
			}
		}
		
		if (Added)
		{
			LString Sep = "\n";
			LString Lst = Sep.Join(a);
			GetObject()->SetStr(FIELD_GROUP_LIST, Lst);
			
			SetDirty();
			
			UsedTs.SetNow();
			*Args.GetReturn() = true;
		}
		else
		{
			*Args.GetReturn() = false;
		}
			
		return true;
	}

	return Thing::CallMethod(MethodName, Args);
}

bool ConvertList(ContactGroup *g, LArray<ListAddr*> &a)
{
	List<char> Addrs;
	if (g->GetAddresses(Addrs))
	{
		for (auto e: Addrs)
		{
			ListAddr *la = new ListAddr(g->App, e, (char*)0);
			if (la)
			{
				la->OnFind();
				a.Add(la);
			}
		}
		Addrs.DeleteArrays();
	}
	
	return a.Length() > 0;
}

void ContactGroup::OnMouseClick(LMouse &m)
{
	LListItem::OnMouseClick(m);

	if (m.IsContextMenu())
	{
		// open the right click menu
		LScriptUi s(new LSubMenu);
		if (s.Sub)
		{
			List<Mail> Templates;

			s.Sub->AppendItem(LLoadString(IDS_OPEN), IDM_OPEN, true);
			s.Sub->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);

			ScribeFolder *f = App->GetFolder(FOLDER_TEMPLATES);
			if (f)
			{
				auto Merge = s.Sub->AppendSub(LLoadString(IDS_MERGE_TEMPLATE));
				if (Merge)
				{
					int n = 0;
					for (auto t: f->Items)
					{
						Mail *m = t->IsMail();
						if (m)
						{
							Templates.Insert(m);
							Merge->AppendItem(m->GetSubject()?m->GetSubject():(char*)"(no subject)", IDM_MERGE_TEMPLATE_BASE+n++, true);
						}
					}
				}
			}
			else
			{
				s.Sub->AppendItem(LLoadString(IDS_MERGE_TEMPLATE), 0, false);
			}

			s.Sub->AppendItem(LLoadString(IDS_MERGE_FILE), IDM_MERGE_FILE, true);

			if (GetList()->GetMouse(m, true))
			{
				LArray<LScriptCallback*> Callbacks;
				if (App->GetScriptCallbacks(LThingContextMenu, Callbacks))
				{
					LScriptArguments Args(NULL);
					Args[0] = new LVariant(App);
					Args[1] = new LVariant(this);
					Args[2] = new LVariant(&s);
					for (unsigned i=0; i<Callbacks.Length(); i++)
					{
						App->ExecuteScriptCallback(*Callbacks[i], Args);
					}
				}

				int Msg;
				switch (Msg = s.Sub->Float(GetList(), m.x, m.y))
				{
					case IDM_OPEN:
					{
						DoUI();
						break;
					}
					case IDM_DELETE:
					{
						LVariant ConfirmDelete;
						App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

						if (!ConfirmDelete.CastInt32() ||
							LgiMsg(GetList(), LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
						{
							List<LListItem> Del;
							LList *ParentList = LListItem::Parent;
							if (ParentList && ParentList->GetSelection(Del))
							{
								for (auto i: Del)
								{
									auto obj = dynamic_cast<ContactGroup*>(i);
									if (obj)
										obj->OnDelete();
									else
										LAssert(!"What type of object is this?");
								}
							}
						}
						break;
					}
					case IDM_MERGE_FILE:
					{
						auto s = new LFileSelect(App);
						s->Type("Email Template", "*.txt;*.eml");
						s->Open([this](auto dlg, auto status)
						{
							if (status)
							{
								LArray<ListAddr*> Recip;
								if (ConvertList(this, Recip))
									App->MailMerge(Recip, dlg->Name(), 0);
								Recip.DeleteObjects();
							}
							delete dlg;
						});
						break;
					}
					default:
					{
						if (Msg >= IDM_MERGE_TEMPLATE_BASE && Msg - IDM_MERGE_TEMPLATE_BASE < (ssize_t)Templates.Length())
						{
							Mail *Template = Templates[Msg - IDM_MERGE_TEMPLATE_BASE];
							if (Template)
							{
								LArray<ListAddr*> Recip;
								if (ConvertList(this, Recip))
									App->MailMerge(Recip, 0, Template);
								Recip.DeleteObjects();
							}
						}
						else
						{
							// Handle any installed callbacks for menu items
							for (unsigned i=0; i<s.Callbacks.Length(); i++)
							{
								LScriptCallback &Cb = s.Callbacks[i];
								if (Cb.Param == Msg)
								{
									LScriptArguments Args(NULL);
									Args[0] = new LVariant(App);
									Args[1] = new LVariant(this);
									Args[2] = new LVariant(Cb.Param);

									App->ExecuteScriptCallback(Cb, Args);
								}
							}
						}
						break;
					}
				}
			}

			DeleteObj(s.Sub);
		}
	}
	else if (m.Down() && m.Double())
	{
		if (!Ui)
		{
			Ui = new GroupUi(this);
		}
	}

}

void ContactGroup::OnSerialize(bool Write)
{
	if (Write)
	{
		UsedTs.SetNow();
	}
	else if (!UsedTs.IsValid())
	{
		auto m = GetObject()->GetDate(FIELD_DATE_MODIFIED);
		if (m)
			UsedTs = *m;
	}
}

ThingUi *ContactGroup::DoUI(MailContainer *c)
{
	if (!Ui)
		Ui = new GroupUi(this);
	else
		Ui->Visible(true);

	return Ui;
}

int ContactGroup::Compare(LListItem *Arg, ssize_t Field)
{
	ContactGroup *cg = dynamic_cast<ContactGroup*>(Arg);
	if (!cg)
		return 0;

	switch (Field)
	{
		case FIELD_DATE_MODIFIED:
		{
			auto a = GetObject()->GetDate((int)Field);
			auto b = cg->GetObject()->GetDate((int)Field);
			if (a && b)
				return a->Compare(b);
			break;
		}
		default:
		{
			auto a = GetFieldText((int)Field);
			auto b = cg->GetFieldText((int)Field);
			if (a && b)
				return _stricmp(a, b);
			break;
		}
	}

	return 0;
}

bool ContactGroup::Save(ScribeFolder *Into)
{
	if (!GetFolder())
	{
		if (Into)
			SetParentFolder(Into);
		else if (App)
			SetParentFolder(App->GetFolder(FOLDER_GROUPS));
	}

	if (!GetFolder())
		return false;

	LDateTime Now;
	GetObject()->SetDate(FIELD_DATE_MODIFIED, &Now.SetNow());

	auto Status = GetFolder()->WriteThing(this) != Store3Error;
	if (Status)
		SetDirty(false);

	return Status;
}

int *ContactGroup::GetDefaultFields()
{
	return DefaultGroupFields;
}

const char *ContactGroup::GetFieldText(int Field)
{
	switch (Field)
	{
		case FIELD_DATE_MODIFIED:
		{
			auto d = GetObject()->GetDate(Field);
			if (d)
				DateCache = d->Local().Get();
			else
				DateCache = LLoadString(IDS_NONE);
			return DateCache;
		}
	}

	return GetObject() ? GetObject()->GetStr(Field) : 0;
}

const char *ContactGroup::GetText(int i)
{
	if (FieldArray.Length())
	{
		if (i >= 0 && i < (int)FieldArray.Length())
			return GetFieldText(FieldArray[i]);
	}
	else if (i < CountOf(DefaultGroupFields))
	{
		return GetFieldText(DefaultGroupFields[i]);
	}
	
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
GroupUi::GroupUi(ContactGroup *item) : ThingUi(item, "Contact Group")
{
	Item = item;
	Edit = 0;

	LRect p;
	LAutoString n;
	if (LoadFromResource(IDD_GROUP, this, &p, &n))
	{
		SetPos(p);
		Name(n);
		MoveSameScreen(App);
		if (GetViewById(IDC_GROUP_LIST, Edit))
		{
			Edit->App = App;
		}

		if (Attach(0))
		{
			AttachChildren();
			OnLoad();
			Visible(true);

			SetWindow(this);
		}
	}
}

GroupUi::~GroupUi()
{
	Item->Ui = 0;
}

extern bool SerializeUi(ItemFieldDef *Defs, LDataI *Object, LViewI *View, bool ToUi);

void GroupUi::OnLoad()
{
	SerializeUi(GroupFieldDefs, Item->GetObject(), this, true);

	auto GrpName = Item->GetObject()->GetStr(FIELD_GROUP_NAME);
	if (ValidStr(GrpName))
	{
		LString s;
		s.Printf("%s - %s", LLoadString(IDD_GROUP), GrpName);
		Name(s);
	}
}

void GroupUi::ResolveAll()
{
	// Turn all the references into email addresses
	List<AddressMeta> Styles;
	if (Edit && Edit->GetStyles(Styles))
	{
		for (auto a: Styles)
		{
			if (!a->IsResolved() &&
				a->IsUnique())
			{
				a->Ins();
			}
		}
	}
}

void GroupUi::OnSave()
{
	ResolveAll();
	
	// Save the group of contacts
	Item->SetDirty();
	SerializeUi(GroupFieldDefs, Item->GetObject(), this, false);
	Item->Save();
	Item->Update();
}

int GroupUi::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDOK:
		{
			OnSave();			
			Quit();
			break;
		}
	}

	return 0;
}

int GroupUi::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	Formats.Supports(ScribeThingList);
	Formats.SupportsFileDrops();	
	return Formats.GetSupported().Length() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

void GroupUi::Add(char *Email)
{
	ResolveAll();
	
	if (Edit)
	{	
		List<AddressMeta> Styles;
		Edit->GetStyles(Styles);
		for (auto a: Styles)
		{
			if (a->Addr.sAddr &&
				_stricmp(a->Addr.sAddr, Email) == 0)
			{
				LgiTrace("%s:%i - '%s' already in group\n", _FL, Email);
				return;
			}
		}
		
		char16 NewLine[] = { '\n', 0 };
		char16 *e = Utf8ToWide(Email);
		if (e)
		{
			auto Len = StrlenW(Edit->NameW());
			if (Len) Edit->Insert(Len, NewLine, 1);
			Edit->Insert(Len + 1, e, StrlenW(e));
		}
	}	
}

int GroupUi::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;

	for (unsigned n=0; n<Data.Length(); n++)
	{
		LDragData &dd = Data[n];
		if (dd.IsFormat(ScribeThingList))
		{
			for (unsigned k=0; k<dd.Data.Length(); k++)
			{
				LVariant *Data = &dd.Data[k];
				if (Data->IsBinary() &&
					ScribeClipboardFmt::IsThing(Data->Value.Binary.Data, Data->Value.Binary.Length))
				{
					ScribeClipboardFmt *Fmt = (ScribeClipboardFmt*)Data->Value.Binary.Data;
					
					for (unsigned i=0; i<Fmt->Length(); i++)
					{
						Contact *c = Fmt->ThingAt(i) ? Fmt->ThingAt(i)->IsContact() : NULL;
						if (c)
						{
							auto Email = c->GetAddrAt(0);
							if (Email)
							{
								Add(Email);
								Status = DROPEFFECT_COPY;
							}
							else LgiTrace("%s:%i - No addr\n", _FL);
						}
						else LgiTrace("%s:%i - Not a contact\n", _FL);
					}
				}
			}
		}
		else if (dd.IsFileDrop())
		{
			LgiTrace("%s:%i - Impl file drop here\n", _FL);			
		}
	}

	return Status;
}

////////////////////////////////////////////////////////////////////////////////////////
#define EDIT_OPEN_CONTACT		100
#define EDIT_DELETE				101
#define EDIT_ADDR_BASE			1000

#define GEDIT 'GEDT'

AddressMeta::AddressMeta(LAddressEdit *edit, int id) :
	Addr(edit->App)
{
	Edit = edit;
	Id = id < 0 ? edit->CreateId() : id;
	Referenced = false;

	LAssert(!Edit->Map.Find(Id));
	Edit->Map.Add(Id, this);
}

AddressMeta::~AddressMeta()
{
	LAssert(Edit->Map.Find(Id));
	Edit->Map.Delete(Id, true);
}

bool AddressMeta::IsUnique()
{
	return Addr.Length() == 1;
}

bool AddressMeta::IsResolved()
{
	bool Status = false;

	if (IsUnique())
	{
		RecipientItem *r = Addr[0];
		if (r && ValidStr(r->GetEmail()))
		{
			Contact *c = r->GetContact();
			if (c)
			{
				auto Emails = c->GetEmails();
				for (auto e: Emails)
				{
					if (e.Equals(Text))
					{
						Status = true;
						break;
					}
				}
			}
			else
			{
				Status = _stricmp(Text, r->GetEmail()) == 0;
			}
		}
	}
	
	return Status;
}

void AddressMeta::Ins()
{
	RecipientItem *r = Addr[0];
	if (r)
	{
		const char *n = Addr.sAddr;
		if (!n)
			n = r->GetEmail();

		LAutoWString Name(Utf8ToWide(n));
		if (Name)
		{
			/*
			auto v = Style->View;

			Edit->AllowPour = false;
			v->Delete(Style->Start, Style->Len);
			Edit->AllowPour = true;

			Style->Len = StrlenW(Name);
			auto sl = Style->End();
				
			// This may delete this object... so don't use anything local afterwards
			v->Insert(Style->Start, Name, Style->Len);
				
			// No local!
			v->Invalidate();
			v->SetCaret(sl, false, false);
			*/
		}
	}
}

void AddressMeta::OpenContact()
{
	if (Addr.Length() == 1)
	{
		RecipientItem *r = Addr[0];
		if (r && r->GetContact())
		{
			r->GetContact()->DoUI();
		}
	}
}

LAddressEdit::LAddressEdit(LRect *p, const char *t) : LTextView3(-1, 0, 0, 100, 100, 0)
{
	App = NULL;
	AllowPour = true;
	if (p) SetPos(*p);
	Sunken(true);
}

void LAddressEdit::OnPaint(LSurface *pDC)
{
	LTextView3::OnPaint(pDC);

	#if 0// def _DEBUG
	pDC->ClipRgn(0);

	char s[256];
	sprintf_s(s, sizeof(s), "%i styles", Style.Length());
	LSysFont->Colour(Rgb24(0, 0, 255), 24);
	LSysFont->Transparent(true);
	LDisplayString ds(LSysFont, s);
	ds.Draw(pDC, 2, Y()-20);
	#endif
}

void LAddressEdit::PourStyle(size_t Start, ssize_t Length)
{
	if (AllowPour)
	{
		const char *Delim = " \t\r\n,;";

		LUnrolledList<LStyle> Old, New;
		Old.Swap(Style);

		for (auto i : Map)
		{
			i.value->Referenced = false;
		}

		LHashTbl<IntKey<ssize_t>,LStyle*> Hash;
		for (auto &i : Old)
		{
			LAssert(i.Data.Type != GV_NULL);
			Hash.Add(i.Start, &i);
		}

		// Generate the new list...
		for (int i=0; i<Size;)
		{
			// Match word
			while (i<Size && strchr(Delim, Text[i])) i++;
			int Start = i;
			while (i<Size && !strchr(Delim , Text[i])) i++;
			int Len = i - Start;

			if (Len > 0)
			{
				// Insert new style
				LAssert(App != NULL);
				auto &s = New.New().Construct(this, STYLE_ADDRESS);
				s.Start = Start;
				s.Len = Len;
			}
		}

		// Match the old and new lists, merging unchanged entries from
		// the old list and taking changed entries from the new list.
		AddressMeta *m;
		for (auto &n : New)
		{
			// Find matching old entry
			LString s(Text + n.Start, n.Len);
			auto o = Hash.Find(n.Start);
			if (o && o->Len == n.Len)
			{
				// Carry over the data ref id
				LAssert(o->Data.Type != GV_NULL);
				n.Data = o->Data;
				n.Font = o->Font;
				n.Fore = o->Fore;
				n.Back = o->Back;
				n.Decor = o->Decor;
				n.DecorColour = o->DecorColour;
				
				auto m = GetMeta(n);
				if (m)
				{
					// LgiTrace("Existing meta @ %i '%s'\n", n.Start, m->Text.Get());
					m->Referenced = true;
				}
			}
			else
			{
				// Create a new entry
				if ((m = new AddressMeta(this)))
				{
					n.Data = m->Id;
					m->Referenced = true;
					m->Text.SetW(Text + n.Start, n.Len);
					UpdateMeta(n);

					// LgiTrace("Creating meta @ %i '%s' with id=%i\n", n.Start, m->Text.Get(), m->Id);
				}
			}

		}

		// Clear out unreferenced meta objects
		for (auto i : Map)
		{
			if (i.value->Referenced == false)
			{
				// LgiTrace("Deleting unreferenced meta '%s'\n", i.value->Text.Get());
				delete i.value;
			}
		}

		#ifdef _DEBUG
		for (auto &s : New)
		{
			LAssert(s.Data.Type != GV_NULL);
		}
		#endif

		Style.Swap(New);

		// Update
		LRect r(0, Y()-20, 100, Y());
		Invalidate(&r);
		
		#if 0
		printf("Styles:\n");
		for (GEditAddress *e=(GEditAddress*)Style.First(); e; e=(GEditAddress*)Style.Next())
		{
			printf("\tStart=%i Len=%i Text=%.*S\n", e->Start, e->Len, e->Len, Text+e->Start);
		}
		#endif
	}
}

bool LAddressEdit::Insert(size_t At, const char16 *Data, ssize_t Len)
{
	for (auto &s : Style)
	{
		LAssert(s.Data.Type != GV_NULL);
		if (s.Start > (ssize_t)At)
		{
			/*auto m =*/ GetMeta(s);
			// LgiTrace("Insert adding to start: %i->%i '%s'\n", s.Start, s.Start + Len, m ? m->Text.Get() : NULL);
			s.Start += Len;
		}
	}

	return LTextView3::Insert(At, Data, Len);
}

bool LAddressEdit::Delete(size_t at, ssize_t Len)
{
	ssize_t At = (ssize_t)at;
	for (auto &s : Style)
	{
		LAssert(s.Data.Type != GV_NULL);
		AddressMeta *m = GetMeta(s);

		if (s.Overlap(At, Len) &&
			m &&
			m->Matched())
		{
			// extend delete region to include the whole styled addr
			if (At > s.Start)
			{
				Len += At - s.Start;
				At = s.Start;
			}
			if (At + Len < s.Start + s.Len)
			{
				Len = s.Start + s.Len - At;
			}

			// LgiTrace("Delete len change: %i:%i '%s'\n", s.Start, s.Len, m ? m->Text.Get() : NULL);
		}

		if (s.Start > At)
		{
			s.Start -= Len;
			// LgiTrace("Delete start change: %i:%i '%s'\n", s.Start, s.Len, m ? m->Text.Get() : NULL);
		}
	}

	return LTextView3::Delete(At, Len);
}

bool LAddressEdit::GetStyles(List<AddressMeta> &Styles)
{
	/*
	Styles.Empty();
	for (GEditAddress *s = (GEditAddress*) Style.First(); s; s = (GEditAddress*) Style.Next())
	{
		Styles.Insert(s);
	}
	return Styles.First() != 0;	
	*/

	return false;
}

int LAddressEdit::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	LWindow *w = GetWindow();
	LDragDropTarget *t = dynamic_cast<LDragDropTarget*>(w);
	if (t)
	{
		return t->WillAccept(Formats, Pt, KeyState);
	}
	return DROPEFFECT_NONE;
}

bool LAddressEdit::OnStyleClick(LStyle *style, LMouse *ms)
{
	auto m = GetMeta(*style);
	if (!m)
		return false;

	if (ms->Double() && ms->Left())
	{
		m->OpenContact();
		return true;
	}
	
	return false;
}

bool LAddressEdit::OnStyleMenu(LStyle *style, LSubMenu *sub)
{
	auto m = GetMeta(*style);
	if (!m)
		return false;

	if (m->Addr.Length() == 1)
	{
		sub->AppendItem("Open Contact", EDIT_OPEN_CONTACT, true);
		sub->AppendItem("Delete", EDIT_DELETE, true);
	}
	else
	{
		Items.DeleteObjects();
		AddressBrowseLookup(App, Items, m->Text);

		if (Items.Length())
		{
			int Idx = 0;
			for (auto i : Items)
			{
				LString n;
				n.Printf("%s %s <%s>", i->First.Get(), i->Last.Get(), i->Email.Get());
				sub->AppendItem(n, EDIT_ADDR_BASE + Idx++);
			}
		}
		else
		{
			sub->AppendItem("No results", -1, false);
		}
	}

	sub->AppendSeparator();

	/*
	if (IsOk() && Addr.Length() > 0)
	{
		if (Addr.Length() == 1)
		{
			m->AppendItem("Open Contact", EDIT_OPEN_CONTACT, true);
			m->AppendItem("Delete", EDIT_DELETE, true);
			m->AppendSeparator();
		}

		char s[256];
		for (int i=0; i<Addr.Length(); i++)
		{
			Contact *c = Addr[i]->GetContact();
			if (c)
			{
				LAutoString Email;
				for (int n=0; (Email = c->GetAddrAt(n)); n++)
				{
					char *First = 0, *Last = 0;

					s[0] = 0;
					c->Get(OPT_First, First);
					c->Get(OPT_Last, Last);
					
					if (First)
					{
						if (*s) strcat(s, " ");
						strcat(s, First);
					}
					if (Last)
					{
						if (*s) strcat(s, " ");
						strcat(s, Last);
					}
					int len = (int)strlen(s);
					sprintf_s(s+len, sizeof(s)-len, " <%s>", (char*)Email);
					int k = (i << 8) + n;
					m->AppendItem(	s,
									EDIT_ADDR_BASE + k,
									ValidStr(Email));
					LgiTrace("Adding contact '%s' %i:%i (%k)\n", s, i, n, k);
				}
			}
			else
			{
				char *Name = Addr[i]->GetName();
				char *Email = Addr[i]->GetEmail();
				sprintf_s(s, sizeof(s), "%s <%s>", Name, Email);
				m->AppendItem(	s,
								EDIT_ADDR_BASE + (i << 8),
								ValidStr(Email));
			}
		}

		return true;
	}
	*/

	return false;
}

void LAddressEdit::OnStyleMenuClick(LStyle *style, int i)
{
	auto m = GetMeta(*style);
	if (!m)
		return;

	switch (i)
	{
		case EDIT_OPEN_CONTACT:
		{
			m->OpenContact();
			break;
		}
		case EDIT_DELETE:
		{
			// This will delete us, don't access anything local afterwards
			Delete(style->Start, style->Len);
				
			// Update the edit and get outta here
			Invalidate();
			return;
			break;
		}
		default:
		{
			BrowseItem *r = Items[i - EDIT_ADDR_BASE];
			if (r)
			{
				Contact *c = Contact::LookupEmail(r->Email);
				if (c)
				{
					style->Fore = LColour(L_TEXT);
					m->Addr.SetWho(new RecipientItem(c), -1);
					
					LAutoWString wEmail(Utf8ToWide(r->Email));

					auto Start = style->Start;
					auto Len = style->Len;
					Delete(Start, Len);
					Insert(Start, wEmail, Strlen(wEmail.Get()));
				}
			}
		}
	}
}

int LAddressEdit::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	LWindow *w = GetWindow();
	LDragDropTarget *t = dynamic_cast<LDragDropTarget*>(w);
	if (t)
	{
		return t->OnDrop(Data, Pt, KeyState);
	}
	return DROPEFFECT_NONE;
}

class LAddressEditFactory : public LViewFactory
{
public:
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (strcmp(Class, "LAddressEdit") == 0)
		{
			return new LAddressEdit(Pos, Text);
		}

		return 0;
	}
}	AddressEditFactory;

//////////////////////////////////////////////////////////////////////////////
LGroupMap::LGroupMap(ScribeWnd *app) : App(app)
{
	auto srcs = App->GetThingSources(MAGIC_GROUP);
	for (auto s: srcs)
	{
		s->LoadThings();
		for (auto i: s->Items)
			Index(i->IsGroup());
	}		
}

LGroupMap::~LGroupMap()
{
	DeleteObjects();
}

void LGroupMap::Index(ContactGroup *grp)
{
	if (!grp)
		return;

	for (auto email: grp->GetAddresses())
	{
		auto a = Find(email);
		if (!a)
		{
			a = new LGroupMapArray;
			Add(email, a);
		}
		a->Add(grp);
	}
}
