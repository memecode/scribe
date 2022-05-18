#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "lgi/common/XmlTree.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"

class LanguageDlgPrivate
{
public:
	ScribeWnd *App;
	LList *Lst;
};

class LangItem : public LListItem
{
	bool Init;
	LAutoPtr<LFont> f;

public:
	LangItem()
	{
		Init = false;
	}

	void OnMeasure(LPoint *Info)
	{
		LListItem::OnMeasure(Info);
		if (f)
		{
			Info->y = f->GetHeight() + 3;
		}
	}

	LFont *GetFont()
	{
		if (!Init)
		{
			Init = true;

			const char *Id = GetText(1);
			if (_stricmp(Id, "ja") == 0 ||
				_stricmp(Id, "zh_tw") == 0)
			{
				if (f.Reset(new LFont))
				{
					*f = *LSysFont;
					f->PointSize(f->PointSize() + 2);
					f->Create();
				}
			}
		}

		return f;
	}
};

int StringItemSort(LListItem *a, LListItem *b, NativeInt d)
{
	return Stricmp(a->GetText((int)d), b->GetText((int)d));
}

LXmlTag *FindLangTag(LXmlTag *t)
{
	if (t->IsTag("string"))
	{
		char *Defn = t->GetAttr("Define");
		if (Defn && _stricmp(Defn, "IDS_LANGUAGE") == 0)
		{
			return t;
		}
	}

	for (auto c: t->Children)
	{
		LXmlTag *n = FindLangTag(c);
		if (n) return n;
	}

	return 0;
}

LanguageDlg::LanguageDlg(ScribeWnd *app)
{
	Ok = false;
	d = new LanguageDlgPrivate;
	d->App = app;

	LHashTbl<ConstStrKey<char,false>,char*> LangNames;
	LXmlTag *LangData = 0;
	LResources *Res = LgiGetResObj(false, NULL, false);
	if (Res)
	{
		char *File = Res->GetFileName();
		if (File)
		{
			LFile f;
			if (f.Open(File, O_READ))
			{
				LXmlTree t;
				LangData = new LXmlTag;
				if (LangData)
				{
					if (t.Read(LangData, &f, 0))
					{
						LXmlTag *Lang = FindLangTag(LangData);
						if (Lang)
						{
							for (unsigned i=0; i<Lang->Attr.Length(); i++)
							{
								LXmlAttr &a = Lang->Attr[i];

								char *Name = a.GetName();
								if (_stricmp(Name, "Ref") != 0 &&
									_stricmp(Name, "Cid") != 0 &&
									_stricmp(Name, "Define") != 0)
								{
									LangNames.Add(Name, a.GetValue());
								}
							}
						}
					}
					else LgiTrace("%s:%i - error\n", _FL);

				}
			}
			else LgiTrace("%s:%i - error\n", _FL);
		}
		else LgiTrace("%s:%i - error\n", _FL);
	}
	else LgiTrace("%s:%i - error\n", _FL);

	if ((Ok = LoadFromResource(IDD_LANG)))
	{
		MoveToCenter();
		LRect p = GetPos();
		p.x2 -= 5;
		p.y2 -= 5;
		SetPos(p);

		GetViewById(IDC_LANG, d->Lst);
		
		if (d->Lst)
		{
			d->Lst->SetImageList(LLoadImageList("Flags.png", 16, 16), true);
			LResources *Res = LgiGetResObj();
			if (Res && Res->GetLanguages())
			{
				LArray<LLanguageId> *InLangs = Res->GetLanguages();
				for (unsigned n=0; n<InLangs->Length(); n++)
				{
					LLanguageId i = (*InLangs)[n];
					LLanguage *Lang = LFindLang(i);
					if (Lang)
					{
						LListItem *i = new LangItem;
						if (i)
						{
							char *Name = LangNames.Find(Lang->Id);

							i->SetText(Name ? Name : Lang->Name, 0);
							i->SetText(Lang->Id, 1);

							#define MatchIcon(lang, img) if (_stricmp(Lang->Id, lang) == 0) i->SetImage(img);
							MatchIcon("en", 0);
							MatchIcon("pt_br", 1);
							MatchIcon("es", 2);
							MatchIcon("sv", 3);
							MatchIcon("cs", 4);
							MatchIcon("lt", 5);
							MatchIcon("de", 6);
							
							MatchIcon("pt", 8);
							MatchIcon("nl", 9);
							MatchIcon("sr", 10);
							MatchIcon("pl", 11);
							MatchIcon("no", 12);
							MatchIcon("ja", 13);
							MatchIcon("zh_tw", 14);
							MatchIcon("ru", 15);
							MatchIcon("fr", 16);
							MatchIcon("it", 17);
							MatchIcon("da", 18);
							MatchIcon("id", 19);
							MatchIcon("tr", 20);
							MatchIcon("vi", 21);

							d->Lst->Insert(i);
						}
					}
				}
			}
			
			d->Lst->Sort<NativeInt>(StringItemSort, 1);
			d->Lst->ResizeColumnsToContent();
		}
		else printf("%s:%i - error\n", _FL);
	}
	else printf("%s:%i - error\n", _FL);

	DeleteObj(LangData);
}

LanguageDlg::~LanguageDlg()
{
	DeleteObj(d);
}

int LanguageDlg::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDC_LANG:
		{
			if (n.Type != LNotifyItemDoubleClick)
			{
				break;
			}
			// else fall thru
		}
		case IDOK:
		{
			if (d->Lst)
			{
				LListItem *s = d->Lst->GetSelected();
				if (s && ValidStr(s->GetText(1)))
				{
					Lang.Reset(NewStr(s->GetText(1)));
				}
			}
			
			EndModal(true);
			break;
		}
	}
	
	return false;
}
