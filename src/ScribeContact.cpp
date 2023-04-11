/*
**	FILE:		ScribeContact.cpp
**	AUTHOR:		Matthew Allen
**	DATE:		11/11/98
**	DESCRIPTION:	Scribe contact object and UI
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "ScribePageSetup.h"
#include "lgi/common/Map.h"
#include "lgi/common/vCard-vCal.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/TabView.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/Button.h"
#include "lgi/common/GdcTools.h"
#include "lgi/common/DropFiles.h"
#include "lgi/common/Http.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/List.h"
#include "lgi/common/Json.h"
#include "lgi/common/CssTools.h"
#include "lgi/common/FileSelect.h"
#include "PrintContext.h"
#include "resdefs.h"
#include "resource.h"

bool ConvertImageToContactSize(LAutoPtr<LSurface> &Img, int Px, LSurface *Raw)
{
	if (!Raw)
	{
		LAssert(!"No raw image?");
		return false;
	}
		
	if (!Img.Reset(new LMemDC(Px, Px, Raw->GetColourSpace())))
	{
		LAssert(!"Failed to create image?");
		return false;
	}

	LRect Src;
	if (Raw->X() > Raw->Y())
	{
		// Wider than high
		Src.ZOff(Raw->Y()-1, Raw->Y()-1);
		Src.Offset((Raw->X()-Raw->Y())>>1, 0);
	}
	else if (Raw->X() < Raw->Y())
	{
		Src.ZOff(Raw->X()-1, Raw->X()-1);
		Src.Offset(0, (Raw->Y()-Raw->X())>>1);
	}
	else
	{
		Src = Raw->Bounds();
	}
	
	ResampleDC(Img, Raw, &Src);

	return true;
}


class ContactPriv
{
public:
	Contact *Item;
	char *Scratch;
	List<char> Plugins;
	LAutoPtr<LSurface> Image;
	LString ImagePath;
	LString DateCache;
	LAutoPtr<LJson> CustomCache;

	ContactPriv(Contact *c) : Item(c)
	{
		Scratch = 0;
	}

	~ContactPriv()
	{
		DeleteArray(Scratch);
		Plugins.DeleteArrays();
	}

	LJson *GetJson()
	{
		auto Obj = Item->GetObject();
		if (!Obj)
			return NULL;

		if (!CustomCache)
		{
			auto json = Obj->GetStr(FIELD_CONTACT_JSON);
			CustomCache.Reset(new LJson(json));
		}

		return CustomCache;
	}
};

ItemFieldDef ContactFieldDefs[] = {

	// Standard fields
	{"Title", SdTitle,					GV_STRING, FIELD_TITLE,				IDC_TITLE,			OPT_Title},
	{"First", SdFirst,					GV_STRING, FIELD_FIRST_NAME,		IDC_FIRST,			OPT_First},
	{"Last", SdSurname,					GV_STRING, FIELD_LAST_NAME,			IDC_LAST,			OPT_Last},
	{"Email", SdEmail,					GV_STRING, FIELD_EMAIL,				IDC_EMAIL,			OPT_Email},
	{"Nickname", SdNickname,			GV_STRING, FIELD_NICK,				IDC_NICK,			OPT_Nick},
	{"Spouse", SdSpouse,				GV_STRING, FIELD_SPOUSE,			IDC_SPOUSE,			OPT_Spouse},
	{"Notes", SdNotes,					GV_STRING, FIELD_NOTE,				IDC_NOTE,			OPT_Note},
	{"Uid", SdUid,						GV_INT32,  FIELD_UID,				-1,					OPT_Uid},
	{"TimeZone", SdTimeZone,			GV_STRING, FIELD_TIMEZONE,			IDC_TIMEZONE,		OPT_TimeZone},

	// Home fields
	{"Home Street", SdHomeStreet,		GV_STRING, FIELD_HOME_STREET,		IDC_HOME_STREET,	OPT_HomeStreet},
	{"Home Suburb", SdHomeSuburb,		GV_STRING, FIELD_HOME_SUBURB,		IDC_HOME_SUBURB,	OPT_HomeSuburb},
	{"Home Postcode", SdHomePostcode,	GV_STRING, FIELD_HOME_POSTCODE,		IDC_HOME_POSTCODE,	OPT_HomePostcode},
	{"Home State", SdHomeState,			GV_STRING, FIELD_HOME_STATE,		IDC_HOME_STATE,		OPT_HomeState},
	{"Home Country", SdHomeCountry,		GV_STRING, FIELD_HOME_COUNTRY,		IDC_HOME_COUNTRY,	OPT_HomeCountry},
	{"Home Phone", SdHomePhone,			GV_STRING, FIELD_HOME_PHONE,		IDC_HOME_PHONE,		OPT_HomePhone},
	{"Home Mobile", SdHomeMobile,		GV_STRING, FIELD_HOME_MOBILE,		IDC_HOME_MOBILE,	OPT_HomeMobile},
	{"Home IM#", SdHomeIM,				GV_STRING, FIELD_HOME_IM,			IDC_HOME_IM,		OPT_HomeIM},
	{"Home Fax", SdHomeFax,				GV_STRING, FIELD_HOME_FAX,			IDC_HOME_FAX,		OPT_HomeFax},
	{"Home Webpage", SdHomeWebpage,		GV_STRING, FIELD_HOME_WEBPAGE,		IDC_HOME_WEBPAGE,	OPT_HomeWebPage},

	// Work fields
	{"Work Street", SdWorkStreet,		GV_STRING, FIELD_WORK_STREET,		IDC_WORK_STREET,	OPT_WorkStreet},
	{"Work Suburb", SdWorkSuburb,		GV_STRING, FIELD_WORK_SUBURB,		IDC_WORK_SUBURB,	OPT_WorkSuburb},
	{"Work Postcode", SdWorkPostcode,	GV_STRING, FIELD_WORK_POSTCODE,		IDC_WORK_POSTCODE,	OPT_WorkPostcode},
	{"Work State", SdWorkState,			GV_STRING, FIELD_WORK_STATE,		IDC_WORK_STATE,		OPT_WorkState},
	{"Work Country", SdWorkCountry,		GV_STRING, FIELD_WORK_COUNTRY,		IDC_WORK_COUNTRY,	OPT_WorkCountry},
	{"Work Phone", SdWorkPhone,			GV_STRING, FIELD_WORK_PHONE,		IDC_WORK_PHONE,		OPT_WorkPhone},
	{"Work Mobile", SdWorkMobile,		GV_STRING, FIELD_WORK_MOBILE,		IDC_WORK_MOBILE,	OPT_WorkMobile},
	{"Work IM#", SdWorkIM,				GV_STRING, FIELD_WORK_IM,			IDC_WORK_IM,		OPT_WorkIM},
	{"Work Fax", SdWorkFax,				GV_STRING, FIELD_WORK_FAX,			IDC_WORK_FAX,		OPT_WorkFax},
	{"Company", SdCompany,				GV_STRING, FIELD_COMPANY,			IDC_COMPANY,		OPT_Company},
	{"Work Webpage", SdWorkWebpage,		GV_STRING, FIELD_WORK_WEBPAGE,		IDC_WORK_WEBPAGE,	OPT_WorkWebPage},

	{"CustomFields", SdCustomFields,	GV_STRING, FIELD_CONTACT_JSON,		IDC_CUSTOM_FIELDS,	OPT_CustomFields},
	{"DateModified", SdDateModified,	GV_DATETIME, FIELD_DATE_MODIFIED,	-1,					NULL},
	{0}
};

#define ForAllContactFields(var) ItemFieldDef *var = 0; for (var = ContactFieldDefs; var->Option && var->FieldId; var++)

#define MacroAllFields(Macro)							\
	Macro(FIELD_TITLE, OPT_Title);						\
	Macro(FIELD_FIRST_NAME, OPT_First);					\
	Macro(FIELD_LAST_NAME, OPT_Last);					\
	Macro(FIELD_EMAIL, OPT_Email);						\
	Macro(FIELD_NICK, OPT_Nick);						\
	Macro(FIELD_SPOUSE, OPT_Spouse);					\
	Macro(FIELD_NOTE, OPT_Note);						\
	Macro(FIELD_TIMEZONE, OPT_TimeZone);				\
	Macro(FIELD_HOME_STREET, OPT_HomeStreet);			\
	Macro(FIELD_HOME_SUBURB, OPT_HomeSuburb);			\
	Macro(FIELD_HOME_POSTCODE, OPT_HomePostcode);		\
	Macro(FIELD_HOME_STATE, OPT_HomeState);				\
	Macro(FIELD_HOME_COUNTRY, OPT_HomeCountry);			\
	Macro(FIELD_HOME_PHONE, OPT_HomePhone);				\
	Macro(FIELD_HOME_MOBILE, OPT_HomeMobile);			\
	Macro(FIELD_HOME_IM, OPT_HomeIM);					\
	Macro(FIELD_HOME_FAX, OPT_HomeFax);					\
	Macro(FIELD_HOME_WEBPAGE, OPT_HomeWebPage);			\
	Macro(FIELD_WORK_STREET, OPT_WorkStreet);			\
	Macro(FIELD_WORK_SUBURB, OPT_WorkSuburb);			\
	Macro(FIELD_WORK_POSTCODE, OPT_WorkPostcode);		\
	Macro(FIELD_WORK_STATE, OPT_WorkState);				\
	Macro(FIELD_WORK_COUNTRY, OPT_WorkCountry);			\
	Macro(FIELD_WORK_PHONE, OPT_WorkPhone);				\
	Macro(FIELD_WORK_MOBILE, OPT_WorkMobile);			\
	Macro(FIELD_WORK_IM, OPT_WorkIM);					\
	Macro(FIELD_WORK_FAX, OPT_WorkFax);					\
	Macro(FIELD_WORK_WEBPAGE, OPT_WorkWebPage);			\
	Macro(FIELD_COMPANY, OPT_Company);					\
	Macro(FIELD_CONTACT_JSON, OPT_CustomFields);


//////////////////////////////////////////////////////////////////////////////
const char *WinFmtUrl = "UniformResourceLocatorW";
const char *FmtUrlList = "text/uri-list";
#include "lgi/common/SkinEngine.h"

class LContactBtn : public LView
{
	bool Down;
	bool Over;

public:
	LContactBtn(int id)
	{
		SetId(id);
		Over = false;
		Down = false;

		LRect r(0, 0, 23, 23);
		SetPos(r);
	}
	
	~LContactBtn()
	{
	}
	
	void OnMouseClick(LMouse &m)
	{
		Capture(Down = m.Down());
		Invalidate();
		if (m.Down())
			Over = true;
		else if (Over)
			SendNotify();
	}
	
	void OnMouseMove(LMouse &m)
	{
		if (IsCapturing())
		{
			bool o = GetClient().Overlap(m.x, m.y);
			if (o ^ Over)
			{
				Down = Over = o;
				Invalidate();
			}
		}
	}

	void OnPaint(LSurface *pDC)
	{
		LRect c = GetClient();
		if (LApp::SkinEngine &&
			c.X() &&
			c.Y())
		{
			LCssTools Tools(this);
			LColour Base = Tools.GetBack();
			LMemDC Mem(c.X(), c.Y(), System32BitColourSpace);
			Mem.Colour(0, 32);
			Mem.Rectangle();
			LRect r(0, 0, X()-1, Y()-1);
			LApp::SkinEngine->DrawBtn(&Mem, r, Base, Down, true);

			int Dot = 2;
			int Gap = 3;
			int Sz = (Dot * 3) + (Gap * 2);
			int x = ((X()-Sz) >> 1) + Down;
			int y = (Y() / 2) + Down;
			Mem.Colour(L_TEXT);
			for (int i=0; i<3; i++)
			{
				Mem.Rectangle(x, y, x + 1, y + 1);
				x += Dot + Gap;
			}
			
			pDC->Op(GDC_ALPHA);
			pDC->Blt(0, 0, &Mem);
		}
	}
};

class LContactImage :
	public LView,
	public ResObject,
	public LDragDropTarget
{
	friend class ContactUi;
	constexpr static int M_IMAGE_LOADED = M_USER + 100;

	ScribeWnd *App = NULL;
	Contact *c = NULL;
	LContactBtn *Btn = NULL;
	bool IsNoFace = false;

	// This is the uncompressed bitmap which is not saved.
	LAutoPtr<LSurface> Img;

	// This is a compressed version that is saved.
	LVariant CompressedImg;
	
	class UriLoader : public LThread, public LCancel
	{
		LContactImage *Ci;
		LString Uri;
	
	public:
		bool Status;
		LMemStream Data;
		LString Error;
	
		UriLoader(LContactImage *ci, LString uri) :
			LThread("LContactImage"),
			Data(1024)
		{
			Ci = ci;
			Uri = uri;
			Status = false;
			Run();
		}
		
		~UriLoader()
		{
			while (!IsExited())
				LSleep(1);
		}
		
		int Main()
		{
			Status = LgiGetUri(this, &Data, &Error, Uri, NULL, NULL);
			Ci->PostEvent(M_IMAGE_LOADED);
			return 0;
		}
	};
	LAutoPtr<UriLoader> Worker;
	
public:
	LContactImage() : ResObject(Res_Custom)
	{
		SetTabStop(true);
	}
	
	LVariant &GetImage()
	{
		if (!IsNoFace && CompressedImg.Type != GV_BINARY)
		{
			LMemStream m(4 << 10);
			if (GdcD->Save(&m, Img, "icon.jpg"))
			{
				CompressedImg.Type = GV_BINARY;
				auto &Bin = CompressedImg.Value.Binary;
				Bin.Length = (ssize_t) m.GetSize();
				Bin.Data = new char[Bin.Length];
				m.SetPos(0);
				m.Read(Bin.Data, Bin.Length);
			}
		}

		return CompressedImg;
	}

	bool OnKey(LKey &k)
	{
		switch (k.vkey)
		{
			case 'v':
			case 'V':
			{
				if (k.Ctrl())
				{
					// Paste shortcut handler:
					if (k.Down())
					{
						CompressedImg.Empty();

						LClipBoard c(this);
						IsNoFace = false;

						c.Bitmap([this](auto bmp, auto str)
						{
							SetImage(bmp);
						});
					}
					return true;
				}
				break;
			}
			case LK_DELETE:
			{
				// Delete the image...
				CompressedImg.Empty();
				SetDefaultNoFace();
				break;
			}
		}

		return false;
	}

	void OnMouseClick(LMouse &m)
	{
		if (m.Down())
			Focus(true);
	}
	
	void SetImage(LAutoPtr<LSurface> Raw)
	{
		if (Raw && (Raw->X() != 160 || Raw->Y() != 160))
			ConvertImageToContactSize(Img, 160, Raw);
		else
			Img = Raw;

		Invalidate();
	}
	
	void OnCreate()
	{
		SetWindow(this);
	}
	
	bool OnLayout(LViewLayoutInfo &Inf)
	{
		if (!Inf.Width.Min)
			Inf.Width.Min = Inf.Width.Max = 160;
		else
			Inf.Height.Min = Inf.Height.Max = 160;
		
		return true;
	}
	
	void OnPaint(LSurface *pDC)
	{
		LRegion rgn(GetClient());
		if (Img)
		{
			pDC->Blt(0, 0, Img);
			LRect Bnds = Img->Bounds();
			rgn.Subtract(&Bnds);
		}
		for (int i=0; i<rgn.Length(); i++)
		{
			pDC->Colour(L_LOW);
			pDC->Rectangle(rgn[i]);
		}
		
		if (Btn)
		{
			LRect c = GetClient();
			c.Inset(6, 6);
			LRect p = Btn->GetPos();
			p.Offset(c.x2 - p.X() - p.x1 + 1,
					 c.y1 - p.y1);
			Btn->SetPos(p);
		}

		if (Focus())
		{
			auto c = GetClient();
			PatternBox(pDC, LRect(c.x1, c.y1, c.x1+1, c.y2));
			PatternBox(pDC, LRect(c.x2-1, c.y1, c.x2, c.y2));
			PatternBox(pDC, LRect(c.x1+2, c.y1, c.x2-2, c.y1+1));
			PatternBox(pDC, LRect(c.x1+2, c.y2-1, c.x2-2, c.y2));
		}
	}

	void SetDefaultNoFace()
	{
		if (App)
		{
			LVariant NoFace;
			if (App->GetValue("NoContact.Image[160]", NoFace))
			{
				if (NoFace.Str())
				{
					IsNoFace = Img.Reset(GdcD->Load(NoFace.Str()));
					Invalidate();
				}
			}
		}
	}

	void SetContact(Contact *contact)
	{
		c = contact;
		if (c)
		{
			App = c->App;
			SetDefaultNoFace();
			
			if (!Btn)
			{
				Btn = new LContactBtn(100);
			}
			if (Btn)
			{
				#if 0
				Btn->GetCss(true)->NoPaintColor(LCss::ColorDef(LCss::ColorRgb, Rgb32(0xcb,0xcb,0xcb)));
				#else
				Btn->GetCss(true)->NoPaintColor(LCss::ColorDef(LCss::ColorTransparent));
				#endif
				
				AddView(Btn);
			}
		}
	}
	
	bool SetImage(const LVariant &v, const char *FileHint = NULL)
	{
		if (v.Type != GV_BINARY)
			return false;

		CompressedImg = v;
		LMemStream mem(	CompressedImg.Value.Binary.Data,
						CompressedImg.Value.Binary.Length,
						false);
		LAutoPtr<LSurface> i(GdcD->Load(&mem, FileHint));
		if (!i)
		{
			CompressedImg.Empty();
			return false;
		}

		SetImage(i);
		IsNoFace = false;
		return true;
	}
	
	bool Load(const char *File)
	{
		LFile f;
		if (!f.Open(File, O_READ))
		{
			LgiMsg(this, "Couldn't open image file.", AppName, MB_OK);
			return false;
		}

		int Sz = (int)f.GetSize();
		LAutoPtr<uint8_t> p(new uint8_t[Sz]);
		if (!p)
		{
			LgiMsg(this, "Couldn't alloc mem for image.", AppName, MB_OK);
			return false;
		}

		ssize_t rd = f.Read(p, Sz);
		if (rd <= 0)
		{
			LgiMsg(this, "Couldn't read from image file.", AppName, MB_OK);
			return false;
		}

		LVariant v;
		v.SetBinary(rd, p.Release(), true);
		if (!SetImage(v, File))
		{
			LgiMsg(this, "Couldn't decompress image.", AppName, MB_OK);
			return false;
		}

		IsNoFace = false;
		return true;
	}

	LMessage::Param OnEvent(LMessage *Msg)
	{
		if (Msg->Msg() == M_IMAGE_LOADED)
		{
			if (Worker &&
				Worker->Status)
			{
				LAutoPtr<LSurface> pDC(GdcD->Load(&Worker->Data, NULL));
				if (pDC)
				{
					IsNoFace = false;
					SetImage(pDC);
				}
			}
			Worker.Reset();
			return 0;
		}
		return LView::OnEvent(Msg);
	}
	
	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		if (Ctrl->GetId() == 100)
		{
			auto s = new LFileSelect(this);
			s->Open([this](auto dlg, auto status)
			{
				if (status)
					Load(dlg->Name());
				delete dlg;
			});
		}
		
		return 0;
	}

	int WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
	{
		if (Formats.HasFormat(LGI_FileDropFormat))
			Formats.SupportsFileDrops();
		else
		{
			#if WINDOWS
			Formats.Supports(WinFmtUrl);
			#endif
			Formats.Supports(FmtUrlList);
		}

		return Formats.GetSupported().Length() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	}
	
	int OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
	{
		for (unsigned i=0; i<Data.Length(); i++)
		{
			LDragData &dd = Data[i];
			if (dd.IsFileDrop())
			{
				LDropFiles files(dd);
				if (files.Length() > 0)
				{
					Load(files[0]);
					return DROPEFFECT_COPY;
				}
			}
			else if (dd.IsFormat(FmtUrlList) ||
					 dd.IsFormat(WinFmtUrl))
			{
				auto &v = dd.Data[0];
				if (v.Type == GV_BINARY)
				{
					LString uri = (char16*)v.Value.Binary.Data;
					if (uri)
						Worker.Reset(new UriLoader(this, uri));
				}
				else if (v.Type == GV_STRING)
				{
					Worker.Reset(new UriLoader(this, v.Str()));
				}
				else LAssert(!"Unexpected type.");
			}
		}
		return DROPEFFECT_NONE;
	}
};

class LContactImageFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (Class && !_stricmp(Class, "LContactImage"))
			return new LContactImage;
		return NULL;
	}
}	ContactImageFactory;

//////////////////////////////////////////////////////////////////////////////
class LFieldItem : public LListItem
{
public:
	LString Field, Value;

	const char *GetText(int i)
	{
		switch (i)
		{
			case 0: return Field;
			case 1: return Value;
		}
		return NULL;
	}

	bool SetText(const char *s, int i=0)
	{
		switch (i)
		{
			case 0: Field = s; break;
			case 1: Value = s; break;
			default: return false;
		}
		LListItem::SetText(s, i);
		auto Lst = GetList();
		Lst->ResizeColumnsToContent();
		Lst->OnNotify(Lst, LNotifyItemChange);
		return true;
	}

	void OnMouseClick(LMouse &m)
	{
		if (m.Down() && m.Double())
		{
			int Col = GetList()->ColumnAtX(m.x);
			if (Col >= 0 && Col <= 1)
				EditLabel(Col);
		}
	}
};

class LFieldEditor : public LList
{
	LString u;
	LAutoWString w;

public:
	LFieldEditor() : LList(IDC_STATIC)
	{
		SetObjectName(Res_Custom);
		DrawGridLines(true);
		AllowEditLabels(true);
		SetNotify(this);
		AddColumn("Field", 110);
		AddColumn("Value", 110);
		OnChange();
	}

	void OnChange()
	{
		LArray<LFieldItem*> a;
		GetAll(a);
		for (auto i: a)
			if (!i->Field && !i->Value)
				return;
		Insert(new LFieldItem);
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		if (Ctrl->GetId() == GetId())
		{
			switch (Flags)
			{
				case LNotifyItemChange:
				{
					OnChange();
					break;
				}
				case LNotifyDeleteKey:
				{
					LArray<LListItem*> a;
					GetSelection(a);
					a.DeleteObjects();
					OnChange();
					break;
				}
			}
		}

		return LList::OnNotify(Ctrl, n);
	}

	bool NameW(const char16 *n)
	{
		LAutoString a(WideToUtf8(n));
		return Name(a);
	}

	const char16 *NameW()
	{
		w.Reset(Utf8ToWide(Name()));
		return w;
	}

	bool Name(const char *n)
	{
		Empty();

		LJson j(n);
		LArray<LString> keys = j.GetKeys();
		for (auto k: keys)
		{
			LFieldItem *i = new LFieldItem;
			i->Field = k;
			i->Value = j.Get(k);
			Insert(i);
		}

		OnChange();
		// LgiTrace("SetText: %s\n", n);
		return true;
	}

	const char *Name()
	{
		LJson j;
		LArray<LFieldItem*> items;
		GetAll(items);
		for (auto i: items)
		{
			if (i->Field)
				j.Set(i->Field, i->Value);
		}
		u = j.GetJson();
		// LgiTrace("GetText: %s\n", u.Get());
		return u;
	}
};

class LFieldEditorFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (Class && !_stricmp(Class, "LFieldEditor"))
			return new LFieldEditor;
		return NULL;
	}
}	FieldEditorFactory;

//////////////////////////////////////////////////////////////////////////////
List<Contact> Contact::Everyone;
LHashTbl<ConstStrKey<char,false>, int> Contact::PropMap;

Contact::Contact(ScribeWnd *app, LDataI *object) : Thing(app, object)
{
	if (!PropMap.Length())
	{
		PropMap.Add("Type", FIELD_TYPE);
		ForAllContactFields(Fld)
		{
			LAssert(Fld->FieldId > 0 && Fld->FieldId < FIELD_MAX);
			// LgiTrace("AddProp %i, %s\n", Fld->FieldId, Fld->Option);
			PropMap.Add(Fld->Option, Fld->FieldId);
		}
	}

	DefaultObject(object);
	d = new ContactPriv(this);
	Everyone.Insert(this);
}

Contact::~Contact()
{
	Everyone.Delete(this);
	DeleteObj(Ui);
	DeleteObj(d);
}

Contact *Contact::LookupEmail(const char *Email)
{
	if (!Email)
		return NULL;

	for (auto c : Everyone)
		if (c->HasEmail(Email))
			return c;
	
	return NULL;
}

bool Contact::Get(const char *Opt, int &Value)
{
	if (GetObject())
	{
		int Id = PropMap.Find(Opt);
		if (Id)
		{
			int i = (int)GetObject()->GetInt(Id);
			if (i >= 0)
			{
				Value = i;
				return true;
			}
		}
	}

	return false;
}

bool Contact::Get(const char *Opt, const char *&Value)
{
	if (GetObject())
	{
		int Id = PropMap.Find(Opt);
		if (Id)
		{
			Value = GetObject()->GetStr(Id);
			return Value != 0;
		}
	}

	return false;
}

bool Contact::Set(const char *Opt, int Value)
{
	if (GetObject())
	{
		int Id = PropMap.Find(Opt);
		if (Id)
		{
			return GetObject()->SetInt(Id, Value) > Store3Error;
		}
	}

	return false;
}

bool Contact::Set(const char *Opt, const char *Value)
{
	auto o = GetObject();
	if (!o)
		return false;

	auto Id = PropMap.Find(Opt);
	if (Id <= 0 || Id >= FIELD_MAX)
	{
		LAssert(!"Invalid ID.");
		return false;
	}

	return o->SetStr(Id, Value) > Store3Error;
}

int Contact::GetAddrCount()
{
	if (!GetObject())
		return 0;

	auto DefEmail = GetObject()->GetStr(FIELD_EMAIL);
	auto AltEmail = GetObject()->GetStr(FIELD_ALT_EMAIL);
	int c = ValidStr(DefEmail) ? 1 : 0;
	if (AltEmail)
	{
		c++;
		for (auto s = AltEmail; s && *s; s++)
		{
			if (*s == ',') c++;
		}
	}
	return c;
}

LString::Array Contact::GetEmails()
{
	LString::Array e;
	const char *s;
	if (Get(OPT_Email, s) && s)
		e.New() = s;
	if (GetObject())
	{
		auto alt = LString(GetObject()->GetStr(FIELD_ALT_EMAIL)).Split(",");
		if (alt.Length())
			e += alt;
	}
	return e;
}

bool Contact::HasEmail(LString email)
{
	auto Emails = GetEmails();
	for (auto e: Emails)
		if (e.Equals(email))
			return true;
	return false;
}

LString Contact::GetAddrAt(int i)
{
	if (i == 0)
	{
		const char *e;
		if (Get(OPT_Email, e))
			return e;
	}
	else if (i > 0)
	{
		auto t = LString(GetObject()->GetStr(FIELD_ALT_EMAIL)).SplitDelimit(",");
		if (i <= (int)t.Length())
			return t[i-1];
	}
	
	return NULL;
}

bool Contact::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	auto Obj = GetObject();
	if (!Name || !Obj)
		return false;

	// Check normal fields..
	ScribeDomType Fld = StrToDom(Name);
	if (Fld == SdDateModified)
		return SetDateField(FIELD_DATE_MODIFIED, Value);

	int Id = PropMap.Find(Name);
	if (Id)
	{
		// Pre-defined field
		auto r = Obj->SetStr(Id, Value.CastString());
		if (r > Store3Error)
		{
			SetDirty();
			return true;
		}
	}
	else
	{
		// Custom field...
		auto j = d->GetJson();
		if (!j)
			return false;
		
		if (!j->Set(Name, Value.CastString()))
			return false;
		
		if (!Obj->SetStr(FIELD_CONTACT_JSON, j->GetJson()))
			return false;

		SetDirty();
		return true;
	}

	return false;
}

bool Contact::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	// Check scribe level fields...
	ScribeDomType Fld = StrToDom(Name);

	switch (Fld)
	{
		case SdScribe: // Type: ScribeWnd
		{
			Value = (LDom*)App;
			return true;
		}
		case SdFolder: // Type: ScribeFolder
		{
			ScribeFolder *f = GetFolder();
			if (!f)
				return false;
			Value = (LDom*)f;
			return true;
		}
		default:
			break;
	}

	auto Obj = GetObject();
	if (!Obj)
		return false;

	switch (Fld)
	{
		case SdEmail: // Type: String[]
		{
			if (!Value.SetList())
				return false;

			int c = GetAddrCount();				
			for (int i=0; i<c; i++)
			{
				auto e = GetAddrAt(i);
				Value.Value.Lst->Insert(new LVariant(e));
			}
			return true;
		}
		case SdType: // Type: Int32
		{
			Value = Obj->Type();
			return true;
		}
		case SdGroups: // Type: String[]
		{
			LHashTbl<StrKey<char,false>,bool> Emails;

			for (int i=0; i<GetAddrCount(); i++)
			{
				auto e = GetAddrAt(i);
				Emails.Add(e, true);
			}

			List<LVariant> Grps;

			auto Srcs = App->GetThingSources(MAGIC_GROUP);
			for (auto g: Srcs)
			{
				for (auto t: g->Items)
				{
					ContactGroup *Grp = t->IsGroup();
					if (Grp)
					{
						List<char> Addr;
						if (Grp->GetAddresses(Addr))
						{
							for (auto a: Addr)
							{
								if (Emails.Find(a))
								{
									auto Name = Grp->GetFieldText(FIELD_GROUP_NAME);
									if (Name)
									{
										Grps.Insert(new LVariant(Name));
									}
								}
							}

							Addr.DeleteArrays();
						}						
					}
				}
			}

			Value.SetList(&Grps);
			return true;
		}
		case SdImage: // Type: Image
		{
			LAssert(!"Impl.");
			return false;
		}
		case SdImageHtml: // Type: String
		{
			if (!d->ImagePath)
			{
				int Px = 80;
				if (Array)
				{
					int i = atoi(Array);
					if (i > 0)
						Px = i;
				}
				
				// Check to see if the contact has an image...
				const LVariant *Bin = Obj->GetVar(FIELD_CONTACT_IMAGE);
				if (Bin && Bin->Type != GV_NULL)
				{
					if (Bin->Type == GV_BINARY)
					{
						auto Png = LFilterFactory::New("name.png", O_WRITE, NULL);
						if (Png)
						{
							if (!d->Image)
							{
								LMemStream Mem(Bin->Value.Binary.Data, Bin->Value.Binary.Length, false);
								LAutoPtr<LSurface> Raw(GdcD->Load(&Mem));
								if (Raw)
								{
									ConvertImageToContactSize(d->Image, Px, Raw);
								}
							}
							if (d->Image)
							{
								LFile::Path p(ScribeTempPath());
								const char *First = Obj->GetStr(FIELD_FIRST_NAME);
								const char *Last = Obj->GetStr(FIELD_LAST_NAME);
								LString leaf;
								leaf.Printf("%s%sContact.png", First, Last);
								p += leaf;

								LFile f;
								if (f.Open(p, O_WRITE))
								{
									if (Png->WriteImage(&f, d->Image))
									{
										#if 0
										// We could save the resized version here?
										int64 NewSize = f.GetSize();
										if (Bin->Length() > (50 << 10))
										{
											int asd=0;
										}
										#endif
										
										d->ImagePath = p.GetFull();
									}
								}
							}
						}
					}
					else LAssert(0);
				}
			}
			
			if (d->ImagePath)
			{
				LString html;
				html.Printf("<img src='file://%s'>\n", d->ImagePath.Get());
				Value = html;
				return true;
			}
			
			return App->GetValue("NoContact.ImageHtml", Value);
		}
		case SdTitle: // Type: String
			Value = Obj->GetStr(FIELD_TITLE);
			return true;
		case SdFirst: // Type: String
			Value = Obj->GetStr(FIELD_FIRST_NAME);
			return true;
		case SdSurname: // Type: String
			Value = Obj->GetStr(FIELD_LAST_NAME);
			return true;
		case SdNickname: // Type: String
			Value = Obj->GetStr(FIELD_NICK);
			return true;
		case SdSpouse: // Type: String
			Value = Obj->GetStr(FIELD_SPOUSE);
			return true;
		case SdNotes: // Type: String
			Value = Obj->GetStr(FIELD_NOTE);
			return true;
		case SdUid: // Type: Int64
			Value = Obj->GetInt(FIELD_UID);
			return true;
		case SdTimeZone: // Type: String
			Value = Obj->GetStr(FIELD_TIMEZONE);
			return true;
		case SdHomeStreet: // Type: String
			Value = Obj->GetStr(FIELD_HOME_STREET);
			return true;
		case SdHomeSuburb: // Type: String
			Value = Obj->GetStr(FIELD_HOME_SUBURB);
			return true;
		case SdHomePostcode: // Type: String
			Value = Obj->GetStr(FIELD_HOME_POSTCODE);
			return true;
		case SdHomeState: // Type: String
			Value = Obj->GetStr(FIELD_HOME_STATE);
			return true;
		case SdHomeCountry: // Type: String
			Value = Obj->GetStr(FIELD_HOME_COUNTRY);
			return true;
		case SdHomePhone: // Type: String
			Value = Obj->GetStr(FIELD_HOME_PHONE);
			return true;
		case SdHomeMobile: // Type: String
			Value = Obj->GetStr(FIELD_HOME_MOBILE);
			return true;
		case SdHomeIM: // Type: String
			Value = Obj->GetStr(FIELD_HOME_IM);
			return true;
		case SdHomeFax: // Type: String
			Value = Obj->GetStr(FIELD_HOME_FAX);
			return true;
		case SdHomeWebpage: // Type: String
			Value = Obj->GetStr(FIELD_HOME_WEBPAGE);
			return true;
		case SdWorkStreet: // Type: String
			Value = Obj->GetStr(FIELD_WORK_STREET);
			return true;
		case SdWorkSuburb: // Type: String
			Value = Obj->GetStr(FIELD_WORK_SUBURB);
			return true;
		case SdWorkPostcode: // Type: String
			Value = Obj->GetStr(FIELD_WORK_POSTCODE);
			return true;
		case SdWorkState: // Type: String
			Value = Obj->GetStr(FIELD_WORK_STATE);
			return true;
		case SdWorkCountry: // Type: String
			Value = Obj->GetStr(FIELD_WORK_COUNTRY);
			return true;
		case SdWorkPhone: // Type: String
			Value = Obj->GetStr(FIELD_WORK_POSTCODE);
			return true;
		case SdWorkMobile: // Type: String
			Value = Obj->GetStr(FIELD_WORK_MOBILE);
			return true;
		case SdWorkIM: // Type: String
			Value = Obj->GetStr(FIELD_WORK_IM);
			return true;
		case SdWorkFax: // Type: String
			Value = Obj->GetStr(FIELD_WORK_FAX);
			return true;
		case SdCompany: // Type: String
			Value = Obj->GetStr(FIELD_COMPANY);
			return true;
		case SdWorkWebpage: // Type: String
			Value = Obj->GetStr(FIELD_WORK_WEBPAGE);
			return true;
		case SdDateModified: // Type: DateTime
			return GetDateField(FIELD_DATE_MODIFIED, Value);
		default:
		{
			// Check custom fields..
			if (!d->CustomCache)
			{
				auto json = Obj->GetStr(FIELD_CONTACT_JSON);
				d->CustomCache.Reset(new LJson(json));
			}
			if (!d->CustomCache)
				return false;

			Value = d->CustomCache->Get(Name);
			return true;
		}
	}

	return false;
}

bool Contact::CallMethod(const char *MethodName, LVariant *ReturnValue, LArray<LVariant*> &Args)
{
	// ScribeDomType Fld = StrToDom(MethodName);
	
	return Thing::CallMethod(MethodName, ReturnValue, Args);
}

Thing &Contact::operator =(Thing &c)
{
	Contact *Arg = c.IsContact();
	if (GetObject() && Arg && Arg->GetObject())
	{
		GetObject()->CopyProps(*Arg->GetObject());
	}

	return *this;
}

bool Contact::IsAssociatedWith(char *PluginName)
{
	if (PluginName)
	{
		for (auto p: d->Plugins)
		{
			if (_stricmp(p, PluginName) == 0)
			{
				return true;
			}
		}
	}

	return false;

}

int Contact::Compare(LListItem *Arg, ssize_t FieldId)
{
	Contact *c1 = this;
	Contact *c2 = dynamic_cast<Contact*>(Arg);
	if (c1 && c2)
	{
		static int Fields[] = {FIELD_FIRST_NAME, FIELD_LAST_NAME, FIELD_EMAIL};
		if (FieldId < 0)
		{
			int i = -(int)FieldId - 1;
			if (i >= 0 && i < CountOf(Fields))
			{
				FieldId = Fields[i];
			}
		}		
		
		auto s1 = c1->GetObject()->GetStr((int)FieldId);
		auto s2 = c2->GetObject()->GetStr((int)FieldId);
		const char *Empty = "";
		return _stricmp(s1?s1:Empty, s2?s2:Empty);
	}

	return 0;
}

ThingUi *Contact::DoUI(MailContainer *c)
{
	if (!Ui)
		Ui = new ContactUi(this);

	return Ui;
}

int Contact::DefaultContactFields[] = { FIELD_FIRST_NAME, FIELD_LAST_NAME, FIELD_EMAIL, 0 };

int *Contact::GetDefaultFields()
{
	return DefaultContactFields;
}

const char *Contact::GetFieldText(int Field)
{
	const char *Status = NULL;

	#define GetFldText(Id, Opt) \
		case Id: Get(Opt, Status); break;

	switch (Field)
	{
		MacroAllFields(GetFldText);

		case FIELD_DATE_MODIFIED:
		{
			auto Dt = GetObject()->GetDate(Field);
			if (Dt)
				d->DateCache = Dt->Local().Get();
			else
				d->DateCache = LLoadString(IDS_NONE);
			return d->DateCache;
			break;
		}
		case FIELD_PLUGIN_ASSOC:
		{
			static char Str[256];
			Str[0] = 0;
			for (auto p: d->Plugins)
			{
				if (Str[0]) strcat(Str, ", ");
				strcat(Str, p);
			}
			return Str;
			break;
		}
	}

	return Status;
}

const char *Contact::GetText(int i)
{
	if (FieldArray.Length())
		return GetFieldText(FieldArray[i]);
	
	return GetFieldText(DefaultContactFields[i]);
}

void Contact::OnMouseClick(LMouse &m)
{
	if (m.IsContextMenu())
	{
		LScriptUi s(new LSubMenu);
		if (s.Sub)
		{
			bool Sep = false;
			const char *Email;
			if (Get(OPT_Email, Email) &&
				ValidStr(Email))
			{
				char Str[256];
				const char *First = 0, *Last = 0;

				Get(OPT_First, First);
				Get(OPT_Last, Last);

				char *LiteralEmail = NewStr(AddAmp(LLoadString(IDS_EMAIL), 'e'));
				if (First || Last)
				{
					sprintf_s(Str, sizeof(Str), "%s %s %s", LiteralEmail, (First) ? First : "", (Last) ? Last : "");
				}
				else
				{
					sprintf_s(Str, sizeof(Str), "%s %s", LiteralEmail, Email);
				}
				DeleteArray(LiteralEmail);

				s.Sub->AppendItem(Str, IDM_NEW_EMAIL, true);
				Sep = true;
			}

			const char *Web = 0;
			if (Get(OPT_HomeWebPage, Web) &&
				ValidStr(Web))
			{
				s.Sub->AppendItem(Web, IDM_LOAD, true);
				Sep = true;
			}

			if (Sep)
			{
				s.Sub->AppendSeparator();
			}

			s.Sub->AppendItem(LLoadString(IDS_OPEN), IDM_OPEN);
			s.Sub->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE);
			s.Sub->AppendItem(LLoadString(IDS_EXPORT), IDM_EXPORT);

			LArray<LScriptCallback*> Callbacks;
			if (App->GetScriptCallbacks(LThingContextMenu, Callbacks))
			{
				LScriptArguments Args(NULL);
				Args[0] = new LVariant(App);
				Args[1] = new LVariant(this);
				Args[2] = new LVariant(&s);
				for (auto c: Callbacks)
				{
					App->ExecuteScriptCallback(*c, Args);
				}
				Args.DeleteObjects();
			}

			if (GetList()->GetMouse(m, true))
			{
				int Result; 
				switch (Result = s.Sub->Float(GetList(), m.x, m.y))
				{
					case IDM_NEW_EMAIL:
					{
						App->CreateMail(this);
						break;
					}
					case IDM_LOAD:
					{
						if (Web)
							LExecute(Web, 0, 0);
						break;
					}
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
								for (auto It = Del.rbegin(); It != Del.end(); It--)
								{
									auto c = dynamic_cast<Contact*>(*It);
									if (c)
										c->OnDelete();
								}

								ParentList->Invalidate();
							}
						}
						break;
					}
					case IDM_EXPORT:
					{
						ExportAll(GetList(), sMimeVCard, NULL);
						break;
					}
					default:
					{
						// Handle any installed callbacks for menu items
						for (unsigned i=0; i<s.Callbacks.Length(); i++)
						{
							LScriptCallback &Cb = s.Callbacks[i];
							if (Cb.Param == Result)
							{
								LScriptArguments Args(NULL);
								Args[0] = new LVariant(App);
								Args[1] = new LVariant(this);
								Args[2] = new LVariant(Cb.Param);
								
								App->ExecuteScriptCallback(Cb, Args);
							}
						}
						break;
					}
				}
			}

			DeleteObj(s.Sub);
		}
	}
	else if (m.Left())
	{
		if (m.Double())
		{
			DoUI();
		}
	}
}

bool Contact::Save(ScribeFolder *Folder)
{
	bool Status = false;

	if (!Folder)
	{
		Folder = GetFolder();
	}
	if (!Folder && App)
	{
		Folder = App->GetFolder(FOLDER_CONTACTS);
	}

	if (Folder)
	{
		LDateTime Now;
		GetObject()->SetDate(FIELD_DATE_MODIFIED, &Now.SetNow());

		Status = Folder->WriteThing(this) != Store3Error;
		if (Status)
			SetDirty(false);
	}

	return Status;
}

bool Contact::GetFormats(bool Export, LString::Array &MimeTypes)
{
	if (!Export)
		MimeTypes.Add(sMimeVCard);

	return MimeTypes.Length() > 0;
}

Thing::IoProgress Contact::Import(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeVCard))
		IoProgressNotImpl();
	
	VCard vCard;
	if (!vCard.Import(GetObject(), stream))
		IoProgressError("vCard import failed.");

	IoProgressSuccess();
}

Thing::IoProgress Contact::Export(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeVCard))
		IoProgressNotImpl();

	VCard vCard;
	if (!vCard.Export(GetObject(), stream))
		IoProgressError("vCard export failed.");

	IoProgressSuccess();
}

char *Contact::GetDropFileName()
{
	if (!DropFileName)
	{
		char File[64] = "Contact";
		const char *First = 0, *Last = 0;

		Get(OPT_First, First);
		Get(OPT_Last, Last);
		if (First && Last)
			sprintf_s(File, sizeof(File), "%s-%s", First, Last);
		else if (First)
			strcpy_s(File, sizeof(File), First);
		else if (Last)
			strcpy_s(File, sizeof(File), Last);

		DropFileName.Reset(MakeFileName(File, "vcf"));
	}
	return DropFileName;
}

bool Contact::GetDropFiles(LString::Array &Files)
{
	bool Status = false;

	if (GetDropFileName())
	{
		if (!LFileExists(DropFileName))
		{
			LAutoPtr<LFile> F(new LFile);
			if (F->Open(DropFileName, O_WRITE))
			{
				F->SetSize(0);
				Export(AutoCast(F), sMimeVCard);
			}
		}

		if (LFileExists(DropFileName))
		{
			Files.Add(DropFileName.Get());
			Status = true;
		}
	}

	return Status;
}

void Contact::OnPrintHeaders(struct ScribePrintContext &Context)
{
	LDisplayString *ds = Context.Text(LLoadString(IDS_CONTACT));
	LRect &r = Context.MarginPx;
	int Line = ds->Y();
	LDrawListSurface *Page = Context.Pages.Last();
	Page->Rectangle(r.x1, Context.CurrentY + (Line * 5 / 10), r.x2, Context.CurrentY + (Line * 6 / 10));
	Context.CurrentY += Line;
}

void Contact::OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages)
{
	// Print document
	ForAllContactFields(Fld)
	{
		const char *Value = 0;
		if (Get(Fld->Option, Value))
		{
			LString v = Value;
			LString::Array Lines = v.SplitDelimit("\n");
			int x = 0;
			for (unsigned i=0; i<Lines.Length(); i++)
			{
				if (i)
				{
					Context.Text(Lines[i], x);
				}
				else
				{
					LString f;
					const char *Name = LLoadString(Fld->FieldId);
					f.Printf("%s: ", Name ? Name : Fld->DisplayText);
					LDisplayString *ds = Context.Text(f);
					if (ds)
					{
						x = ds->X();
						Context.CurrentY -= ds->Y();
						Context.Text(Lines[i], x);
					}
				}
			}
		}
	}
}

const char *ToField(int f)
{
	ForAllContactFields(Fld)
	{
		if (Fld->FieldId == f)
		{
			return Fld->Option;
		}
	}

	if (f == FIELD_PERMISSIONS)
	{
		return "Perms";
	}

	LAssert(0);

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////
#define M_REMOVE_EMAIL_ADDR			(M_USER+0x400)

class EmailAddr : public LListItem
{
	Contact *c;
	LAutoString Email;
	bool EditOneShot;
	bool Default;
	const char *ClickToAdd;

public:
	EmailAddr(Contact *contact, const char *email = 0, bool def = false)
	{
		c = contact;
		EditOneShot = false;
		Default = def;
		Email.Reset(NewStr(email));
		ClickToAdd = LLoadString(IDS_CLICK_TO_ADD);
	}

	bool GetDefault()
	{
		return Default;
	}

	char *GetEmail()
	{
		return Email;
	}

	LFont *GetFont()
	{
		return Default ? c->App->GetBoldFont() : 0;
	}

	const char *GetText(int Col=0)
	{
		if (EditOneShot)
		{
			EditOneShot = false;
			return (char*)"";
		}

		return Email ? Email : (char*)ClickToAdd;
	}

	void Delete()
	{
		if (Parent->Length() > 1)
		{
            Parent->GetWindow()->PostEvent(M_REMOVE_EMAIL_ADDR, (LMessage::Param)this);

			if (Default)
			{
				// Set one of the other's to the default
				List<EmailAddr> All;
				Parent->GetAll(All);
				for (auto a: All)
				{
					if (a != this && a->Email)
					{
						a->Default = true;
						a->Update();
						break;
					}
				}
			}
		}
		else
		{
			Email.Reset();
		}
	}

	bool SetText(const char *s, int Col=0)
	{
		if (s && Col == 0)
		{
			if (ValidStr(s))
			{
				if (!Email)
				{
					Parent->Insert(new EmailAddr(c));
				}

				Email.Reset(NewStr(s));
				
				if (GetCss())
					GetCss()->DeleteProp(LCss::PropColor);
				
				Update();
			}
			else
			{
				Delete();
			}
		}

		return false;
	}

	/*
	void OnPaint(LItem::ItemPaintCtx &Ctx)
	{
		if (!Email)
		{
			LColour Back;
			if (Select())
				Back.Set(LC_FOCUS_SEL_BACK, 24);
			else
				Back.Set(LC_WORKSPACE, 24);

			COLOUR Fore24 = LC_LOW;
			COLOUR BackGrey = GdcGreyScale(Back.c24(), 24);
			COLOUR ForeGrey = GdcGreyScale(Fore24, 24);
			int Diff = abs((int)(BackGrey - ForeGrey));

			if (Diff < 94)
				Fore24 = LC_FOCUS_SEL_FORE;
			// LgiTrace("Fore=%i Back=%i Diff=%i Fore=%x\n", BackGrey, ForeGrey, Diff, Fore24);

			SetForegroundFill(new GViewFill(Fore24, 24));
		}
		else SetForegroundFill(0);

		LListItem::OnPaint(Ctx);
	}
	*/

	bool OnKey(LKey &k)
	{
		if (k.Down())
		{
			switch (k.c16)
			{
				default:
				{
					if
					(
						k.Down() &&
						(
							IsAlpha(k.c16) ||
							k.c16 == ' ' ||
							k.c16 == LK_F2
						)
					)
					{
						EditOneShot = !Email;

						LViewI *v = EditLabel(0);
						if (v && k.IsChar && IsAlpha(k.c16))
						{
							v->IterateViews().DeleteObjects();
							if (v)
							{
								LEdit *e = dynamic_cast<LEdit*>(v);
								if (e && !ValidStr(v->Name()))
								{
									LAutoString u(WideToUtf8(&k.c16, 1));
									if (u)
									{
										e->Name(u);
										e->SetCaret(1);
									}
								}

								v->Focus(true);
							}
							else
							{
								LgiTrace("%s:%i - no edit.\n", __FILE__, __LINE__);
							}
						}

						return true;
					}
					break;
				}
				case LK_DELETE:
				{
					Parent->Delete(this);
					return true;
					break;
				}
			}
		}
		
		return false;
	}

	void OnMouseClick(LMouse &m)
	{
		if (m.Down())
		{
			if (m.IsContextMenu())
			{
				auto RClick = new LSubMenu;
				if (RClick)
				{
					#define IDM_SET_DEFAULT 200
					RClick->AppendItem(LLoadString(IDS_SET_DEFAULT), IDM_SET_DEFAULT, !Default);
					RClick->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);

					// RClick->AppendSeparator();

					if (LListItem::Parent->GetMouse(m, true))
					{
						switch (RClick->Float(LListItem::Parent, m.x, m.y))
						{
							case IDM_SET_DEFAULT:
							{
								// Clear the old default
								List<EmailAddr> All;
								Parent->GetAll(All);
								for (auto a: All)
								{
									if (a->Default)
									{
										a->Default = false;
										a->Update();
										break;
									}
								}

								// Set the new default
								Default = true;
								Update();
								break;
							}
							case IDM_DELETE:
							{
								Delete();
								break;
							}
						}
					}

					DeleteObj(RClick);
				}				
			}
			else if (m.Left())
			{
				int c = Parent->ColumnAtX(m.x);
				if (c >= 0)
				{
					EditOneShot = !Email;
					EditLabel(c);
				}
			}
		}
	}
};

ContactUi::ContactUi(Contact *item) :
	ThingUi(item, "Contact")
{
	Lst = 0;
	ImgView = NULL;
	Item = item;
	if (!(Item && Item->App))
	{
		return;
	}

	#if WINNATIVE
	SetStyle(GetStyle() & ~WS_VISIBLE);
	CreateClassW32("Scribe::ContactUi", LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_CONTACT)));
	#endif

	if (Attach(0))
	{
		LRect p;
		LAutoString s(NewStr("Contact"));		
		if (LoadFromResource(IDD_CONTACT, this, &p, &s))
		{
			// size/position
			Name(s);
			SetPos(p);

			MoveSameScreen(App);
			// MoveToCenter();
			
			// list setup
			if (GetViewById(IDC_EMAIL, Lst))
			{
				Lst->ShowColumnHeader(false);
				Lst->AddColumn("Email", Lst->X());
				Lst->Insert(new EmailAddr(Item));
			}

			// Tz setup
			LCombo *c;
			if (GetViewById(IDC_PICK_TZ, c))
			{
				c->Sort(true);
				c->Sub(GV_DOUBLE);

				GTimeZone *Tz = GTimeZones;
				while (Tz->Text)
				{
					char s[256];
					sprintf_s(s, sizeof(s), "%.1f  %s", Tz->Offset, Tz->Text);
					c->Insert(s);
					Tz++;
				}
			}
			
			// Show buttons to toggle mode...
			LButton *Show;
			if (GetViewById(IDC_SHOW_ADDR, Show))
			{
				Show->SetIsToggle(true);
				Show->Value(1);
			}
			if (GetViewById(IDC_SHOW_EXTRA, Show))
				Show->SetIsToggle(true);

			// controls
			AttachChildren();

			// Image setup
			if (GetViewById(IDC_IMAGE, ImgView))
			{
				ImgView->SetContact(Item);
			}
			
			OnLoad();

			// show the window
			Visible(true);

			// set default button
			_Default = FindControl(IDOK);
			LViewI *f = FindControl(IDC_FIRST);
			if (f) f->Focus(true);
		}
	}

	SetPulse(1000);
}

ContactUi::~ContactUi()
{
	Item->Ui = 0;
}

void ContactUi::OnDestroy()
{
	if (Item)
	{
		Item->Ui = 0;
	}
}

bool ContactUi::InitField(int Id, const char *Name)
{
	if (Item)
	{
		const char *s;
		if (Item->Get(Name, s))
		{
			SetCtrlName(Id, s);
			return true;
		}

		int i;
		if (Item->Get(Name, i))
		{
			SetCtrlValue(Id, i);
			return true;
		}
	}

	return false;
}

bool ContactUi::SaveField(int Id, const char *Name)
{
	if (Item)
	{
		if (Id == FIELD_UID)
		{
			Item->Set(Name, (int)GetCtrlValue(Id));
			return true;
		}
		else
		{
			Item->Set(Name, GetCtrlName(Id));
			return true;
		}
	}
	return false;
}

void ContactUi::OnLoad()
{
	int Insert = 0;
	ForAllContactFields(f)
	{
		if (f->FieldId == FIELD_EMAIL)
		{
			const char *e;
			if (Item->Get(f->Option, e))
			{
				Lst->Insert(new EmailAddr(Item, e, true), Insert++);
			}
		}
		else if (f->CtrlId > 0)
		{
			InitField(f->CtrlId, f->Option);
		}
	}

	auto AltEmail = LString(Item->GetObject()->GetStr(FIELD_ALT_EMAIL)).SplitDelimit(",");
	for (auto e: AltEmail)
		Lst->Insert(new EmailAddr(Item, e), Insert++);
	
	if (ImgView)
	{
		const LVariant *v = Item->GetObject()->GetVar(FIELD_CONTACT_IMAGE);
		if (v && v->Type == GV_BINARY)
		{
			ImgView->IsNoFace = false;
			ImgView->SetImage(*v);
		}
	}

	auto FirstName = Item->GetObject()->GetStr(FIELD_FIRST_NAME);
	auto LastName = Item->GetObject()->GetStr(FIELD_LAST_NAME);
	if (ValidStr(FirstName)||ValidStr(LastName))
	{
		LString s;
		s.Printf("%s - %s%s%s", LLoadString(IDS_CONTACT), FirstName?FirstName:"", FirstName?" ":"", LastName?LastName:"");
		Name(s);
	}

	LViewI *c;
	if (GetViewById(IDC_TIMEZONE, c))
	{
		LNotification note(LNotifyValueChanged);
		OnNotify(c, note);
	}
}

void ContactUi::OnSave()
{
	auto Obj = Item->GetObject();
	EmailAddr *Def = NULL;
	List<EmailAddr> All;
	Lst->GetAll(All);

	ForAllContactFields(f)
	{
		if (f->FieldId == FIELD_EMAIL)
		{
			for (auto a: All)
			{
				if (a->GetDefault())
				{
					Def = a;
					break;
				}
			}

			if (!Def)
				Def = All[0];

			if (Def)
				Item->Set(f->Option, Def->GetEmail());
		}
		else if (f->CtrlId > 0)
		{
			SaveField(f->CtrlId, f->Option);
		}
	}

	LString::Array AltEmails;
	for (auto a: All)
	{
		if (a != Def && a->GetEmail())
			AltEmails.New() = a->GetEmail();
	}
	LString AltEmail = LString(",").Join(AltEmails);
	Obj->SetStr(FIELD_ALT_EMAIL, AltEmail);

	if (ImgView)
	{
		// Reset the cache...
		Item->d->Image.Reset();
		Item->d->ImagePath.Empty();

		// Set the image in the back end store...
		LVariant *Img = &ImgView->GetImage();
		Obj->SetVar(FIELD_CONTACT_IMAGE, Img);
	}
	
	if (Item && Item->App)
	{
		Item->Save();
		
		LArray<LDataI*> c;
		c.Add(Obj);
		Item->App->SetContext(_FL);
		Item->App->OnChange(c, 0);
	}
}

void ContactUi::OnPosChange()
{
}

LMessage::Result ContactUi::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_REMOVE_EMAIL_ADDR:
		{
			EmailAddr *Addr = (EmailAddr*)Msg->A();
			if (Addr)
			{
				Lst->Delete(Addr);
			}
			break;
		}
	}

	return ThingUi::OnEvent(Msg);
}

int ContactUi::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDOK:
		{
			OnSave();
			// fall thru
		}
		case IDCANCEL:
		{
			PostEvent(M_CLOSE);
			break;
		}
		case IDC_TIMEZONE:
		{
			LString v = Ctrl->Name();
			if (!v)
				break;
			
			auto dv = v.Float();
			
			LCombo *c;
			if (GetViewById(IDC_PICK_TZ, c))
			{
				for (size_t i=0; i<c->Length(); i++)
				{
					auto s = (*c)[i];
					if (s)
					{
						auto sval = atof(s);
						if (ABS(sval-dv) < 0.0001)
						{
							c->Value(i);
							return true;
						}
					}
				}
			}
			break;
		}
		case IDC_PICK_TZ:
		{
			if (Ctrl->Value() >= 0 &&
				Ctrl->Name())
			{
				double Tz = atof(Ctrl->Name());
				char s[32];
				sprintf_s(s, sizeof(s), "%.1f", Tz);
				SetCtrlName(IDC_TIMEZONE, s);
			}
			break;
		}
		case IDC_SHOW_ADDR:
		{
			LTableLayout *Tbl;
			if (GetViewById(IDC_TABLE, Tbl))
			{
				int64 Shown = Ctrl->Value();
				Ctrl->Name(Shown ? "-" : "+");
				int y = 2;
				
				auto *c = Tbl->GetCell(0, y);
				if (c) c->Display(Shown ? LCss::DispBlock : LCss::DispNone);
				
				c = Tbl->GetCell(1, y);
				if (c) c->Display(Shown ? LCss::DispBlock : LCss::DispNone);
				
				Tbl->InvalidateLayout();
			}
			break;
		}
		case IDC_SHOW_EXTRA:
		{
			LTableLayout *Tbl;
			if (GetViewById(IDC_TABLE, Tbl))
			{
				int64 Shown = Ctrl->Value();
				Ctrl->Name(Shown ? "-" : "+");
				int y = 4;
				
				auto *c = Tbl->GetCell(0, y);
				if (c) c->Display(Shown ? LCss::DispBlock : LCss::DispNone);
				
				c = Tbl->GetCell(1, y);
				if (c) c->Display(Shown ? LCss::DispBlock : LCss::DispNone);
				
				Tbl->InvalidateLayout();
			}
			break;
		}
	}

	return 0;
}

char *Contact::GetLocalTime(const char *TimeZone)
{
	char *Status = 0;

	if (!ValidStr(TimeZone))
	{
		Get(OPT_TimeZone, TimeZone);
	}

	if (ValidStr(TimeZone))
	{
		double TheirTz = atof(TimeZone);
		LDateTime d;
		d.SetNow();
		d.SetTimeZone((int)(TheirTz * 60), true);

		char s[256];
		d.Get(s, sizeof(s));
		Status = NewStr(s);
	}

	return Status;
}

void ContactUi::OnPulse()
{
	char *Local = Item->GetLocalTime(GetCtrlName(IDC_TIMEZONE));
	if (Local)
	{
		SetCtrlName(IDC_LOCALTIME, Local);
		DeleteArray(Local);
	}
}


