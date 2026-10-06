#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "lgi/common/XmlTree.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"

enum LIconIndex
{
	LIconEnglish = 0,
	LIconPortugueseBrazil = 1,
	LIconSpanish = 2,
	LIconSwedish = 3,
	LIconCzech = 4,
	LIconLithuanian = 5,
	LIconGerman = 6,
	LIconPortuguese = 8,
	LIconDutch = 9,
	LIconSerbian = 10,
	LIconPolish = 11,
	LIconNorwegian = 12,
	LIconJapanese = 13,
	LIconChineseTraditional = 14,
	LIconRussian = 15,
	LIconFrench = 16,
	LIconItalian = 17,
	LIconDanish = 18,
	LIconIndonesian = 19,
	LIconTurkish = 20,
	LIconVietnamese = 21,
	LIconUkraine = 22,
	LIconPersian = 23,
	LIconKorean = 24,
	LIconHungarian = 25,
	LIconLuganda = 26,
};

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
	LXmlTag *LangData = nullptr;
	if (auto Res = LgiGetResObj(false, NULL, false))
	{
		if (auto File = Res->GetFileName())
		{
			LFile f;
			if (f.Open(File, O_READ))
			{
				LXmlTree t;
				if ((LangData = new LXmlTag))
				{
					if (t.Read(LangData, &f, 0))
					{
						if (auto Lang = FindLangTag(LangData))
						{
							for (auto &a: Lang->Attr)
							{
								auto Name = a.GetName();
								if (Stricmp(Name, "Ref") != 0 &&
									Stricmp(Name, "Cid") != 0 &&
									Stricmp(Name, "Define") != 0)
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
						if (auto i = new LangItem)
						{
							auto Name = LangNames.Find(Lang->Id);

							i->SetText(Name ? Name : Lang->Name, 0);
							i->SetText(Lang->Id, 1);

							#define MatchIcon(lang, img) if (_stricmp(Lang->Id, lang) == 0) i->SetImage(img);
							MatchIcon("en", LIconEnglish)
							MatchIcon("pt_br", LIconPortugueseBrazil)
							MatchIcon("es", LIconSpanish)
							MatchIcon("sv", LIconSwedish)
							MatchIcon("cs", LIconCzech)
							MatchIcon("lt", LIconLithuanian)
							MatchIcon("de", LIconGerman)
							MatchIcon("pt", LIconPortuguese)
							MatchIcon("nl", LIconDutch)
							MatchIcon("sr", LIconSerbian)
							MatchIcon("pl", LIconPolish)
							MatchIcon("no", LIconNorwegian)
							MatchIcon("ja", LIconJapanese)
							MatchIcon("zh_tw", LIconChineseTraditional)
							MatchIcon("ru", LIconRussian)
							MatchIcon("fr", LIconFrench)
							MatchIcon("it", LIconItalian)
							MatchIcon("da", LIconDanish)
							MatchIcon("id", LIconIndonesian)
							MatchIcon("tr", LIconTurkish)
							MatchIcon("vi", LIconVietnamese)
							MatchIcon("uk", LIconUkraine)
							MatchIcon("fa", LIconPersian)
							MatchIcon("ko", LIconKorean)
							MatchIcon("hu", LIconHungarian)
							MatchIcon("lg", LIconLuganda)

							d->Lst->Insert(i);
						}
					}
				}
			}
			
			d->Lst->Sort([this](auto *a, auto *b)
				{
					return Stricmp(a->GetText(1), b->GetText(1));
				});
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

int LanguageDlg::OnNotify(LViewI *c, const LNotification &n)
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
