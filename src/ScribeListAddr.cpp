#include "Scribe.h"
#include "resdefs.h"
#include "lgi/common/ClipBoard.h"
#include "ScribeListAddr.h"
#include "lgi/common/LgiRes.h"

#define IDM_ALT_EMAIL_BASE					100000

////////////////////////////////////////////////////////////////////////////
void RecipientItem::_New()
{
	c = 0;
	g = 0;
	// e = 0;
	Buf[0] = 0;
}

RecipientItem::RecipientItem(RecipientItem *item)
{
	_New();
	c = item->c;
	// e = item->e;
	g = item->g;
}

RecipientItem::RecipientItem(Contact *contact)
{
	_New();
	c = contact;
}

RecipientItem::RecipientItem(ContactGroup *group)
{
	_New();
	g = group;
}

const char *RecipientItem::GetName()
{
	if (c)
	{
		if (!Buf[0])
		{
			const char *First = 0, *Last = 0;
			c->Get(OPT_First, First);
			c->Get(OPT_Last, Last);
			if (First)
				strcpy_s(Buf, sizeof(Buf), First);
			if (Last)
			{
				if (Buf[0]) strcat(Buf, " ");
				strcat(Buf, Last);
			}
		}

		return Buf;
	}

	/*
	if (e)
	{
		e->GetValue(PropToStr(PropName), Name);
		return Name.Str();
	}
	*/

	return 0;
}

const char *RecipientItem::GetFirst()
{
	const char *s = 0;

	if (c)
	{
		c->Get(OPT_First, s);
	}
	else if (g)
	{
		LVariant Name;
		if (g->GetVariant(ContactGroupName, Name) &&
			Name.Str())
		{
			static char Buf[64];
			strcpy_s(Buf, sizeof(Buf), Name.Str());
			s = Buf;
		}
	}

	return s;
}

const char *RecipientItem::GetLast()
{
	const char *s = 0;
	if (c)
		c->Get(OPT_Last, s);

	return s;
}

const char *RecipientItem::GetEmail()
{
	const char *s = 0;
	if (c)
		c->Get(OPT_Email, s);
	
	return s;
}

const char *RecipientItem::GetAttribute(char *Attr)
{
	const char *s = 0;
	if (c)
		c->Get(Attr, s);

	return s;
}

void RecipientItem::DoUI(LView *Parent)
{
	if (c)
		c->DoUI();
	else if (g)
		g->DoUI();
}

////////////////////////////////////////////////////////////////////////////
#define IDM_WHO					1000

ListAddr::ListAddr(ScribeWnd *app)
{
	App = app;
	Loaded = false;
}

ListAddr::ListAddr(ScribeWnd *app, LDataPropI *Prop)
{
	App = app;
	Loaded = true;
	if (Prop)
	{
		sName = Prop->GetStr(FIELD_NAME);
		sAddr = Prop->GetStr(FIELD_EMAIL);
		CC = (EmailAddressType)Prop->GetInt(FIELD_CC);
		Status = (uint8_t) Prop->GetInt(FIELD_STATUS);

		OnFind();
	}
}

ListAddr::ListAddr(Contact *c)
{
	Loaded = true;
	App = NULL;
	if (c)
	{
		App = c->App;
		Who.Insert(new RecipientItem(c));
		OnFound();
	}
	else LAssert(0);
}

ListAddr::ListAddr(ContactGroup *g)
{
	Loaded = true;
	App = NULL;
	if (g)
	{
		App = g->App;
		Who.Insert(new RecipientItem(g));
		OnFound();
	}
}

ListAddr::ListAddr(ScribeWnd *app, RecipientItem *c)
{
	Loaded = true;
	App = app;
	if (c)
	{
		Who.Insert(c);
		OnFound();
	}
}

ListAddr::ListAddr(ScribeWnd *app, AddressDescriptor *a, List<Contact> *Cache)
{
	Loaded = true;
	App = app;
	
	ListAddr *la = dynamic_cast<ListAddr*>(a);
	if (la)
	{
		sName = la->sName;
		sAddr = la->sAddr;
		Status = la->Status;
		CC = la->CC;

		for (auto r: *la)
		{
			Who.Insert(new RecipientItem(r));
		}
	}
	else if (a)
	{
		sName = a->sName;
		sAddr = a->sAddr;
		CC = a->CC;
		Status = a->Status;

		OnFind(Cache);
	}
}

ListAddr::ListAddr(ScribeWnd *app, const char *Email, const char *Nm, List<Contact> *Cache)
{
	Loaded = true;
	App = app;
	sName = Nm;
	sAddr = Email;

	OnFind(Cache);
}

ListAddr::~ListAddr()
{
	_Delete();
}

const char *ListAddr::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
			return sName;
		case FIELD_EMAIL:
			return sAddr;
	}

	LAssert(0);
	return 0;
}

Store3Status ListAddr::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_NAME:
			if (sName.Get() != str)
				sName = str;
			break;
		case FIELD_EMAIL:
			if (sAddr.Get() != str)
				sAddr = str;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 ListAddr::GetInt(int id)
{
	switch (id)
	{
		case FIELD_CC:
			return CC;
		case FIELD_STATUS:
			return Status;
		case FIELD_COLOUR:
		{
			LCss *Css = GetCss();
			if (!Css)
				return -1;

			LCss::ColorDef c = Css->Color();
			return c.Type == LCss::ColorRgb ? c.Rgb32 : -1;
		}
	}

	LAssert(0);
	return -1;
}

Store3Status ListAddr::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_COLOUR:
		{
			LCss *Css = GetCss(true);
			if (!Css)
				return Store3Error;
			
			if (i)
				Css->Color(LColour((uint32_t)i, 32));
			else
				Css->Color(LCss::ColorDef(LCss::ColorInherit));
			break;
		}
		default:
		{
			LAssert(0);
			return Store3Error;
		}
	}

	return Store3Success;
}

LDataPropI &ListAddr::operator =(LDataPropI &p)
{
	AddressDescriptor *a = dynamic_cast<AddressDescriptor*>(&p);
	if (a)
	{
		CopyFrom(*a);
	}
	else LAssert(0);
	
	return *this;
}

void ListAddr::CopyFrom(AddressDescriptor &a)
{
	_Delete();

	sName = a.sName;
	sAddr = a.sAddr;
	CC = a.CC;
	Status = a.Status;
}

int ListAddr::Length()
{
	if (Loaded)
	{
		OnFind(0, true);
		Loaded = true;
	}

	#if 0
	if (Who.Length() > 1)
	{
		LgiTrace("name=%s,%s\n", Name, Addr);
		for (RecipientItem *ri=Who.First();  ri; ri=Who.Next())
		{
			Contact *c = ri->GetContact();
			if (c)
			{
				ScribeFolder *t = c->GetFolder();
				if (t)
				{
					LAutoString p = t->GetPath();
					LgiTrace("p=%s\n", p.Get());
				}
				else LgiTrace("t=0\n");
			}
			else LgiTrace("c=0\n");
		}
	}
	#endif

	return (int)Who.Length();
}

void ListAddr::Delete(RecipientItem *i)
{
	Who.Delete(i);
}

void ListAddr::SetWho(RecipientItem *i, int Idx)
{
	Who.Delete(i);
	Who.DeleteObjects();
	Who.Insert(i);

	if (Idx >= 0)
	{
		Contact *c = Who[0]->GetContact();
		if (c)
		{
			auto a = c->GetAddrAt(Idx);
			sAddr = a.Get();
		}
	}

	OnFound();

	Loaded = true;
}

bool ListAddr::Serialize(LString &s, bool write)
{
	Store3Status Status = Store3Error;
	static const char *Delim = "\'\"<>,";

	if (write)
	{
		// ListAddr -> String
		LString name = LUrlEncode(sName, Delim);
		LString email = LUrlEncode(sAddr, Delim);
		
		if (name && email)
			s.Printf("'%s' <%s>", name.Get(), email.Get());
		else if (name)
			s.Printf("'%s'", name.Get());
		else if (email)
			s.Printf("<%s>", email.Get());
		else
			return false;

		Status = Store3Success;
	}
	else
	{
		// String -> ListAddr
		char *c = s;
		while (*c)
		{
			while (*c && strchr(LWhiteSpace, *c))
				c++;
			
			if (*c == '\'' || *c == '<')
			{
				char del = *c++;
				char *e = strchr(c, del=='<'?'>':del);
				if (!e)
					break;
				*e = 0;
				LString d = LUrlDecode(c);
				if (d)
				{
					if (del == '<')
					{
						if (SetStr(FIELD_EMAIL, d))
							Status = Store3Success;
					}
					else
					{
						if (SetStr(FIELD_NAME, d))
							Status = Store3Success;
					}
				}
				c = e + 1;
			}
			else break;
		}
	}
	
	return Status >= Store3Delayed;
}

RecipientItem *ListAddr::operator [](int i)
{
	if (!Loaded)
	{
		Loaded = true;
		OnFind(0, true);
	}

	return Who[i];
}

List<RecipientItem>::I ListAddr::begin()
{
	if (!Loaded)
	{
		Loaded = true;
		OnFind(0, true);
	}

	return Who.begin();
}

List<RecipientItem>::I ListAddr::end()
{
	return Who.end();
}

LString ListAddr::MakeName(const char *Delim, bool LocalTime)
{
	LString name;
	if (sAddr && sName)
	{
		RecipientItem *i = Who[0];
		Contact *c = i ? i->GetContact() : 0;
		LString Local;
		if (LocalTime && c)
			Local = c->GetLocalTime();
		if (Local)
			name.Printf("%s%s%s <%s> (%s)", Delim, sName.Get(), Delim, sAddr.Get(), Local.Get());
		else
			name.Printf("%s%s%s <%s>", Delim, sName.Get(), Delim, sAddr.Get());
	}
	else if (sAddr)
	{
		name.Printf("<%s>", sAddr.Get());
	}
	else if (sName)
	{
		name.Printf("%s%s%s", Delim, sName.Get(), Delim);
	}
	
	return name;
}

const char *ListAddr::GetText(int i)
{
	AddressList *al = dynamic_cast<AddressList*>(Parent);
	if (al)
	{
		switch (i)
		{
			case 0:
			{
				switch (CC)
				{
					case MAIL_ADDR_TO:
						return "To:";
					case MAIL_ADDR_CC:
						return "Cc:";
					case MAIL_ADDR_BCC:
						return "Bcc:";
					case MAIL_ADDR_FROM:
						return NULL;
					default:
						return "#ErrUnknownCC";
				}
				break;
			}
			case 1:
			{
				sNameCache = MakeName();
				return sNameCache;
			}
		}
	}
	else
	{
		switch (i)
		{
			case 0:
				return sAddr;
			case 1:
				return sName;
		}
	}

	return LListItem::GetText(i);
}

bool ListAddr::SetText(const char *s, int i)
{
	if (s && !i)
	{
		sAddr = s;
		OnFind();
	}
	else
	{
		return LListItem::SetText(s, i);
	}

	return true;
}

int ListAddr::GetImage(int Flags)
{
	auto Whose = Who.Length();
	if (Whose == 1)
	{
		RecipientItem *i = Who[0];
		if (i && i->GetGroup())
		{
			return ICON_CLOSED_FOLDER;
		}

		return ICON_CONTACT;
	}

	if (Whose > 1)
		return ICON_UNKNOWN;

	return -1;
}

void ListAddr::OnFound(bool Persist)
{
	if (Who.Length() == 1)
	{
		RecipientItem *c = Who[0];
		if (c)
		{
			auto First = c->GetFirst();
			auto Last = c->GetLast();

			bool ValidEmail = false;
			if (sAddr)
			{
				Contact *Con = c->GetContact();
				if (Con)
				{
					ValidEmail = Con->HasEmail(sAddr);
				}
				else
				{
					auto Email = c->GetEmail();
					if (Email)
						ValidEmail = _stricmp(Email, sAddr) == 0;
				}
			}

			if (c->GetGroup())
			{
				sName.Empty();
				sAddr = First;
			}
			else
			{
				if (!ValidEmail)
				{
					sAddr = c->GetEmail();
				}

				if (First || Last)
				{
					auto first = (First) ? First : "";
					auto last = (Last) ? Last : "";

					if (!sName || !Persist)
						sName.Printf("%s %s", first, last);
				}
			}

			Update();
		}
	}
}

void ListAddr::OnFind(List<Contact> *Cache, bool Persist)
{
	if (sAddr)
	{
		// Strip off mailto...
		LString s = sAddr.Strip();
		if (s.Find(MailToStr) == 0)
			sAddr = s(7,-1);
	}

	{
		List<RecipientItem> MatchLast;
		List<RecipientItem> MatchNick;

		if (!Cache)
		{
			Cache = App->GetEveryone();
		}

		Who.DeleteObjects();
		
		// search scribe's contact database for matches
		auto Lst = Cache->begin();
		// ssize_t Len = Lst.Length();
		int Iters = 0;

		if (sAddr)
		{
			for (Contact *c = *Lst; c; c = *++Lst, Iters++)
			{
				if (strchr(sAddr, '@'))
				{
					if (c->HasEmail(sAddr))
						Who.Insert(new RecipientItem(c));
				}
				else
				{
					const char *First = 0;
					const char *Last = 0;
					const char *Nick = 0;
					
					c->Get(OPT_First, First);
					c->Get(OPT_Last, Last);
					c->Get(OPT_Nick, Nick);

					char *Space = strchr(sAddr, ' ');
					if (Space)
					{
						ssize_t Len = Space - sAddr.Get();
						if (First && Last)
						{
							if (_strnicmp(sAddr, First, Len) == 0 &&
								_stricmp(Space+1, Last) == 0)
							{
								Who.Insert(new RecipientItem(c));
							}
						}
						else if (First)
						{
							if (_stricmp(sAddr, First) == 0)
							{
								Who.Insert(new RecipientItem(c));
							}
						}
					}
					else
					{
						if (First && _stricmp(sAddr, First) == 0)
						{
							Who.Insert(new RecipientItem(c));
						}

						if (Last && _stricmp(sAddr, Last) == 0)
						{
							MatchLast.Insert(new RecipientItem(c));
						}

						if (Nick && _stricmp(sAddr, Nick) == 0)
						{
							MatchNick.Insert(new RecipientItem(c));
						}
					}
				}
			}
		}

		// search the groups for names
		if (sName)
		{
			ContactGroup *g = LookupContactGroup(App, sName);
			if (g)
				Who.Insert(new RecipientItem(g));
		}
	
		// look at results
		if (Who.Length() == 0)
		{
			if (MatchLast.Length() > 0)
			{
				Who = MatchLast;
			}
			else if (MatchNick.Length() > 0)
			{
				Who = MatchNick;
			}
		}
	}

	OnFound(Persist);
}

void ListAddr::AddToContacts(bool Ui, ScribeFolder *Folder)
{
	Contact *c = (Contact*) App->CreateItem(MAGIC_CONTACT, Folder, false);
	if (c)
	{
		char *NameStr = 0;
		if (sAddr && strchr(sAddr, '@'))
		{
			c->Set(OPT_Email, sAddr);
			NameStr = sName;
		}
		else
		{
			NameStr = sAddr;
		}

		char Buffer[256];
		if (NameStr && strlen(NameStr) < sizeof(Buffer))
		{
			strcpy_s(Buffer, sizeof(Buffer), NameStr);
			char *Space = strchr(Buffer, ' ');
			if (Space)
			{
				// has first and last
				*Space++ = 0;
				c->Set(OPT_Last, Space);
			}
			// else just the first name

			c->Set(OPT_First, Buffer);
		}

		c->Save();
		if (Ui)
			c->DoUI();
			
		Who.Insert(new RecipientItem(c));
		Update();
		
		/*
		if (LListItem::Parent)
		{
			LListItem::Parent->Invalidate();
		}
		*/
	}
}

void ListAddr::OnMouseClick(LMouse &m)
{
	AddressList *al = dynamic_cast<AddressList*>(Parent);
	if (!al)
	{
		return;
	}

	if (m.IsContextMenu())
	{
		auto RClick = new LSubMenu;
		if (RClick)
		{
			RClick->SetImageList(App->GetIconImgList(), false);

			if (Who.Length() > 1)
			{
				int i=0;
				for (auto c: Who)
				{
					RClick->AppendItem(c->GetName(), IDM_WHO + i, true);
					i++;
				}
			}
			else
			{
				// Allow user to select amoungst multiple email addresses
				if (Who.Length() == 1)
				{
					Contact *c = Who[0]->GetContact();
					if (c && c->GetAddrCount() > 1)
					{
						auto Emails = c->GetEmails();
						int i = 0;
						for (auto e: Emails)
							RClick->AppendItem(e, IDM_ALT_EMAIL_BASE+i++, true);
						RClick->AppendSeparator();
					}
				}
				
				auto Item = RClick->AppendItem("&To:", IDM_TO, true);
				if (Item && CC == 0) Item->Checked(true);
				Item = RClick->AppendItem("&Cc:", IDM_CC, true);
				if (Item && CC == 1) Item->Checked(true);
				Item = RClick->AppendItem("&Bcc:", IDM_BCC, true);
				if (Item && CC == 2) Item->Checked(true);
			}
			RClick->AppendSeparator();

			RClick->AppendItem(LLoadString(IDS_COPY), IDM_COPY, true);
			RClick->AppendSeparator();

			RClick->AppendItem(LLoadString(IDS_EDIT), IDM_EDIT, true);
			RClick->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);

			if (sAddr)
			{
				if (Who.Length() == 1)
				{
					RecipientItem *r = Who[0];
					auto i = RClick->AppendItem(LLoadString(IDS_OPEN), IDM_OPEN, true);
					if (i)
					{
						if (r->GetContact())
						{
							i->Icon(ICON_CONTACT);
						}
						else if (r->GetGroup())
						{
							i->Icon(ICON_CLOSED_FOLDER);
						}
					}
				}
				else if (Who.Length() < 1)
				{
					RClick->AppendItem(LLoadString(IDS_ADD_CONTACTS), IDM_NEW_CONTACT, true);
				}
			}

			m.ToScreen();

			int Msg = RClick->Float(LListItem::Parent, m.x, m.y);
			switch (Msg)
			{
				case IDM_TO:
				case IDM_CC:
				case IDM_BCC:
				{
					List<LListItem> Sel;
					LList *ParentList = LListItem::Parent;
					if (ParentList && ParentList->GetSelection(Sel))
					{
						int Val = 0;
						switch (Msg)
						{
							case IDM_CC:
								Val = 1;
								break;
							case IDM_BCC:
								Val = 2;
								break;
						}

						for (auto It = Sel.rbegin(); It != Sel.end(); It--)
						{
							auto c = dynamic_cast<ListAddr*>(*It);
							if (c)
							{
								c->CC = (EmailAddressType)Val;
								c->Update();
							}
						}
					}
					break;
				}
				case IDM_COPY:
				{
					if (LListItem::GetList())
					{
						List<ListAddr> Sel;
						if (LListItem::GetList()->GetSelection(Sel))
						{						
							LStringPipe p(256);
							int i = 0;
							for (auto la: Sel)
							{
								p.Print("%s%s", i?EOL_SEQUENCE:"", la->MakeName("\"", false).Get());
								i++;
							}

							LAutoString s(p.NewStr());
							if (s)
							{
								LClipBoard Clip(Parent);
								Clip.Text(s);
								LAutoWString w(Utf8ToWide(s));
								if (w)
									Clip.TextW(w, false);
							}
						}
					}
					break;
				}
				case IDM_EDIT:
				{
					auto Dlg = new LInput(Parent, sAddr);
					Dlg->DoModal([this, Dlg](auto dlg, auto id)
					{
						if (id == IDOK)
						{
							sAddr = Dlg->GetStr();

							OnFind();
							Update();

							if (LListItem::GetList())
								LListItem::GetList()->OnNotify(LListItem::GetList(), LNotifyItemChange);
						}
						delete dlg;
					});
					break;
				}
				case IDM_DELETE:
				{
					List<LListItem> Del;
					LList *ParentList = LListItem::Parent;
					if (ParentList && ParentList->GetSelection(Del))
					{
						for (auto It = Del.rbegin(); It != Del.end(); It--)
						{
							auto c = dynamic_cast<ListAddr*>(*It);
							if (c)
								ParentList->Delete(c);
						}

						ParentList->Invalidate();
					}
					break;
				}
				case IDM_NEW_CONTACT:
				{
					AddToContacts(true);
					break;
				}
				case IDM_OPEN:
				{
					RecipientItem *c = Who[0];
					if (c)
					{
						c->DoUI(Parent);
					}
					break;
				}
				default:
				{
					if (Msg >= IDM_WHO && Msg < IDM_WHO + Who.Length())
					{
						RecipientItem *c = Who[Msg - IDM_WHO];
						Who.Delete(c);
						Who.DeleteObjects();
						Who.Insert(c);
						OnFound();

						if (LListItem::Parent) LListItem::Parent->Invalidate();
					}
					
					// Action the change address menu
					if (Msg >= IDM_ALT_EMAIL_BASE && Who.Length() == 1)
					{
						Contact *c = Who[0]->GetContact();
						if (c && c->GetAddrCount() > 1)
						{
							int Index = Msg - IDM_ALT_EMAIL_BASE;
							if (Index < c->GetAddrCount())
							{
								sAddr = c->GetAddrAt(Index);
								Update();
							}
						}
					}
					break;
				}
			}

			DeleteObj(RClick);
		}
	}
	else if (m.Double() && m.Left())
	{
		if (Who.Length() == 1)
		{
			RecipientItem *c = Who[0];
			if (c)
			{
				c->DoUI(Parent);
			}
		}
	}
}

bool ListAddr::SetVariant(const char *VarName, LVariant &Value, const char *Array)
{
	if (!VarName)
		return false;

	if (_stricmp(VarName, "Text") == 0)
	{
		LAutoString n, a;
		DecodeAddrName(Value.Str(), n, a, 0);
		sName = n.Get();
		sAddr = a.Get();

		return true;
	}
	return false;

	return true;
}

bool ListAddr::GetVariant(const char *VarName, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(VarName);
	switch (Fld)
	{
		case SdText: // Type: String
		{
			Value = Print().Get();
			break;
		}		
		case SdName: // Type: String
		{
			Value = sName;
			break;
		}
		case SdEmail: // Type: String
		{
			Value = sAddr;
			break;
		}
		case SdType: // Type: Int32
		{
			Value = (int)CC;
			break;
		}
		case SdContact: // Type: Contact
		{
			OnFind(0, true);
			RecipientItem *Ri = Who[0];
			if (Ri &&
				Ri->GetContact())
			{
				Value = Ri->GetContact();
			}
			else return false;
			break;
		}
		case SdGroups: // Type: String[]
		{
			OnFind(0, true);
			RecipientItem *Ri = Who[0];
			if (Ri &&
				Ri->GetContact())
			{
				return Ri->GetContact()->GetVariant(VarName, Value, Array);
			}
			else if (sAddr)
			{
				if (Value.SetList())
				{
					ScribeFolder *g = App->GetFolder(FOLDER_GROUPS);
					if (g)
					{
						for (auto t: g->Items)
						{
							ContactGroup *Grp = t->IsGroup();
							if (Grp)
							{
								List<char> GrpAddr;
								if (Grp->GetAddresses(GrpAddr))
								{
									for (auto a: GrpAddr)
									{
										if (_stricmp(a, sAddr) == 0)
										{
											auto Name = Grp->GetFieldText(FIELD_GROUP_NAME);
											if (Name)
												Value.Value.Lst->Insert(new LVariant(Name));
										}
									}

									GrpAddr.DeleteArrays();
								}						
							}
						}
					}
				}
			}
			else return false;
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

LString ListAddr::Copy()
{
	return MakeName("\"", false);
}

void ListAddr::Paste(const char *s)
{
	_Delete();
	DecodeAddrName(s, sName, sAddr, 0);
}

void ListAddr::_Delete()
{
	AddressDescriptor::_Delete();
	Who.DeleteObjects();
}
