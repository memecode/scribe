// http://orig02.deviantart.net/42c2/f/2013/064/8/6/whatsapp_emoji_collection_by_lechuck80-d5x39i5.zip
// https://unicode.org/emoji/charts/full-emoji-list.html

// Subj: u+1f6cd u+fe0f u+20 u+43 u+79 u+62 u+65 u+72 u+20 u+4d u+6f u+6e u+64 u+61 u+79 u+20 u+69 u+73 u+20 u+48 u+65 u+72 u+65 u+21 u+1f6cd u+fe0f


#include "lgi/common/Lgi.h"
#include "lgi/common/OptionsFile.h"
#include "lgi/common/GdcTools.h"
#include "lgi/common/Base64.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Net.h"
#include "lgi/common/Http.h"
#include "lgi/common/Html.h"
#include "lgi/common/List.h"
#include "lgi/common/Button.h"
#include "lgi/common/ProgressDlg.h"

#include "resdefs.h"
// #include <cmph.h>

#define ICON_SIZE			32
#define ICONS_ACROSS		24

#define OUTPUT_ICON_SIZE	32

enum Messages
{
	M_PARSE_HTML = M_USER + 1000,
	M_EMOJI_LOADED,
	M_ICONS_DONE,
};

enum Controls
{
	IDC_LINK = 100,
	IDC_UNLINK,
	IDC_LIST,
	IDC_CREATE_IMG,
	IDC_CURRENT_CHAR,
	IDC_SEARCH
};

LColour Red(255, 222, 222);
LColour Green(0, 222, 0);

//////////////////////////////////////////////////////////////////
const char *AppName = "EmojiMapper";
const char *OPT_WndPos = "WndPos";

struct EmojiStr : public LArray<uint32_t>
{
	LString toString()
	{
		LString::Array s;
		for (auto ch: *this)
			s.New().Printf("0x%x", ch);
		return LString(",").Join(s);		
	}

	int operator-(const EmojiStr &b)
	{
		for (int i=0; i<Length()||i<b.Length(); i++)
		{
			auto ach = IdxCheck(i) ? (*this)[i] : 0;
			auto bch = b.IdxCheck(i) ? b.ItemAt(i) : 0;
			int64 d = (int64)ach - (int64)bch;
			if (d)
				return d < 0 ? -1 : 1;
		}

		return 0;
	}
};

struct Emoji
{
	EmojiStr Char;
	LString Path;
	bool Loading;
	bool Error;
	LAutoPtr<LSurface> Img;
	LAutoPtr<LSurface> Resize;
	
	Emoji()
	{
		Error = false;
		Loading = false;
	}
};

struct EmojiStore : public LMutex
{
	LArray<Emoji> Icons;
	unsigned Loaded;
	
	EmojiStore() : LMutex("EmojiStore")
	{
		Loaded = 0;
	}
	
	void Empty()
	{
		Icons.Length(0);
	}
};

int EmojiCmp(Emoji *a, Emoji *b)
{
	return a->Char - b->Char;
}

class ScanThread : public LThread
{
	LViewI *App;
	bool Loop;
	EmojiStore *Store;

public:
	ScanThread(LViewI *app, EmojiStore *store) :
		LThread("LoaderThread")		
	{
		App = app;
		Store = store;
		Loop = true;
		Run();
	}
	
	~ScanThread()
	{
		Loop = false;
		while (!IsExited())
			LSleep(1);
	}
	
	void Load(const char *p)
	{
		if (Store->Lock(_FL))
		{
			Emoji &e = Store->Icons.New();
			e.Path = p;
			Store->Unlock();
		}
	}
	
	void Scan(LString p)
	{
		LDirectory d;
		for (int b=d.First(p); b; b=d.Next())
		{
			LFile::Path sub = p.Get();
			sub += d.GetName();

			if (d.IsDir())
			{
				Scan(sub.GetFull());
			}
			else if (stristr(d.GetName(), ".png"))
			{
				Load(sub);
			}
		}
	}
	
	int Main()
	{
		LFile::Path p;
		p = LFile::Path::GetSystem(LSP_APP_INSTALL, 0);
		#ifdef MAC
		p--;
		#endif
		p += "Icons";
		LString full = p.GetFull();
		if (p.IsFolder())
			Scan(full);
		else
			printf("%s:%i - Error: path '%s' doesn't exist.\n", _FL, full.Get());
		
		App->PostEvent(M_EMOJI_LOADED);
		return 0;
	}
};

class LoaderThread : public LThread
{
	EmojiStore *Store;
	bool Loop;
	
public:
	LoaderThread(EmojiStore *s) : LThread("LoaderThread")
	{
		Store = s;
		Loop = true;
		Run();
	}
	
	~LoaderThread()
	{
		Loop = false;
		while (!IsExited())
			LSleep(1);
	}
	
	int Main()
	{
		while (Loop)
		{
			int Idx = -1;
			LString Path;
			if (Store->Lock(_FL))
			{
				for (unsigned i=0; i<Store->Icons.Length(); i++)
				{
					Emoji &e = Store->Icons[i];
					if (!e.Img && !e.Error && !e.Loading)
					{
						Idx = (int)i;
						Path = e.Path;
						e.Loading = true;						
						break;
					}
				}
				
				Store->Unlock();
			}
			
			if (Path)
			{
				LAutoPtr<LSurface> Img(GdcD->Load(Path));
				LAutoPtr<LSurface> Resize;
				if (Img)
				{
					LMemDC *r = new LMemDC(_FL, ICON_SIZE, ICON_SIZE, Img->GetColourSpace());
					if (r)
					{
						LRect rc = Img->Bounds();
						LFindBounds(Img, &rc);
						
						if (!ResampleDC(r, Img, &rc))
						{
							DeleteObj(r);
						}
						else
						{
							Resize.Reset(r);
						}
					}
				}
				
				if (Store->Lock(_FL))
				{
					Emoji &e = Store->Icons[Idx];
					if (Img)
					{
						e.Img = Img;
						e.Resize = Resize;
						Store->Loaded++;
					}
					else
					{
						e.Error = true;
					}
					e.Loading = false;
					Store->Unlock();
				}
			}
			else LSleep(10);
		}
		return 0;
	}
};

class HttpPool
{
public:
	struct Request
	{
		void *UserData;
		int Status;
		LString Src;
		LMemQueue Data;
		LString Error;
		
		Request() : Data(1024)
		{
			UserData = NULL;
			Status = -1;
		}
	};

	class HttpEvents
	{
	public:
		virtual ~HttpEvents() {}
		
		virtual void OnRequestFinished(LAutoPtr<Request> &r) = 0;
	};

private:
	HttpEvents *Events;
	
	struct HttpThread :
		public LThread,
		public LMutex,
		public LCancel
	{
		HttpEvents *Events;
		LArray<Request*> Req;
		bool Busy;
	
	public:
		HttpThread(HttpEvents *events) :
			LThread("HttpThread.Thread"),
			LMutex("HttpThread.Mutex")
		{
			Events = events;
			Busy = false;
			Run();
		}
		
		~HttpThread()
		{
			Cancel();
			while (!IsExited())
				LSleep(1);
		}
		
		size_t Length()
		{
			return Busy?1:0 + Req.Length();
		}
		
		bool Add(const char *Uri, void *UserData)
		{
			LAutoPtr<Request> r(new Request);
			if (!r || !Lock(_FL))
				return false;

			r->Src = Uri;
			r->UserData = UserData;
			Req.Add(r.Release());

			Unlock();
			return true;
		}
		
		int Main()
		{
			LProxyUri Proxy;
			while (!IsCancelled())
			{
				LAutoPtr<Request> r;
				if (LockWithTimeout(1000, _FL))
				{
					if (Req.Length())
					{
						r.Reset(Req[0]);
						Req.DeleteAt(0, true);
					}
					Unlock();
				}
				
				if (r)
				{
					Busy = true;
					r->Status = LGetUri(this, &r->Data, &r->Error, r->Src, NULL, &Proxy);
					Events->OnRequestFinished(r);
					Busy = false;
				}
				else LSleep(50);
			}
			
			return 0;
		}
	};

	LArray<HttpThread*> Threads;

public:
	HttpPool(HttpEvents *events)
	{
		Events = events;
	}
	
	~HttpPool()
	{
		Threads.DeleteObjects();
	}
	
	bool Submit(const char *Uri, void *UserData)
	{
		HttpThread *Best = NULL;
		size_t BestLen = 0;
		for (unsigned i=0; i<Threads.Length(); i++)
		{
			HttpThread *t = Threads[i];
			size_t tlen = t->Length();
			if (!Best || BestLen > tlen)
			{
				Best = t;
				BestLen = tlen;
			}
		}
		if (Threads.Length() >= 4 || (Best && BestLen < 3))
		{
			return Best->Add(Uri, UserData);
		}
		else
		{
			HttpThread *t = new HttpThread(Events);
			if (!t)
				return false;
			Threads.Add(t);
			return t->Add(Uri, UserData);
		}	
	}
};

struct HtmlElement : public LHtmlElement
{
	LHashTbl<ConstStrKey<char,false>, LString> Attr;
	LString s;

	HtmlElement(LHtmlElement *p) : LHtmlElement(p), Attr(16, false)
	{
	};

	bool Get(const char *attr, const char *&val)
	{
		s = Attr.Find(attr);
		if (!s.Get())
			return false;

		val = s;
		return true;
	}
	
	void Set(const char *attr, const char *val)
	{
		Attr.Add(attr, val);
	}
};

struct ListItem : public LListItem
{
	LAutoPtr<LSurface> Icon;
	LString IconData;
	EmojiStr Char;
	bool Linked = false;

	ListItem()
	{
		Linked = false;
	}

	void OnMeasure(LPoint *Info)
	{
		LListItem::OnMeasure(Info);
		if (Icon)
			Info->y = MAX(Info->y, Icon->Y());
	}
	
	void OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
	{
		LColour OldFore, OldBack;
		bool ReplaceCol = i == 3 && Linked;
		if (ReplaceCol)
		{
			OldFore = Ctx.Fore;
			OldBack = Ctx.Back;
			Ctx.Back = Green;
			Ctx.Fore = LColour(L_FOCUS_SEL_FORE);
		}
		
		LListItem::OnPaintColumn(Ctx, i, c);
		if (i == 2 && Icon)
		{
			int Op = Ctx.pDC->Op(GDC_ALPHA);
			Ctx.pDC->Blt(Ctx.x1, Ctx.y1, Icon);
			Ctx.pDC->Op(Op);
		}

		if (ReplaceCol)
		{
			Ctx.Back = OldBack;
			Ctx.Fore = OldFore;
		}
	}
};

struct ParserThread :
	public LThread,
	public LHtmlParser,
	public LHtmlStaticInst
{
	LHtmlElement *Html;
	LString File;
	LViewI *Wnd;
	LList *Lst;
	
	LHtmlElement *CreateElement(LHtmlElement *Parent)
	{
		return new HtmlElement(Parent);
	}
	
	ParserThread(LHtmlElement *html, LString file, LViewI *wnd, LList *lst) : LThread("HtmlParser"), LHtmlParser(NULL)
	{
		Html = html;
		File = file;
		Wnd = wnd;
		Lst = lst;
		
		Run();
	}
	
	~ParserThread()
	{
		while (!IsExited())
			LSleep(1);
	}
	
	LHtmlElement *FindElement(LHtmlElement *e, int TagId)
	{
		for (unsigned i=0; i<e->Children.Length(); i++)
		{
			LHtmlElement *c = e->Children[i];
			if (c->TagId == TagId)
				return c;
			
			LHtmlElement *f = FindElement(c, TagId);
			if (f)
				return f;
		}
		return NULL;
	}
	
	int Main()
	{
		auto Raw = LReadFile(File);
		if (Parse(Html, Raw))
		{
			List<LListItem> Items;
			
			LHtmlElement *t = FindElement(Html, TAG_TABLE);
			if (t &&
				t->Children.Length() == 1 &&
				t->Children[0]->TagId == TAG_TBODY)
			{
				t = t->Children.First();
			}

			if (t)
			{
				enum ColType {
					cNum,
					cCode,
					cBrowser,
					cApple,
					cGoogle,
					cFb,
					cWin,
					cTwitter,
					cJoy,
					cSamsung,
					cGmail,
					cSB,
					cDcm,
					cKDDI,
					cName,
				};
				LArray<LString> Headings;
				LAutoPtr<ListItem> Item;
				
				for (unsigned i=0; i<t->Children.Length(); i++)
				{
					LHtmlElement *Tr = t->Children[i];
					if (Tr->TagId == TAG_TR)
					{
						int Col = 0;
						Item.Reset(new ListItem);
						LString BackupIcon, BwIcon;
						
						for (unsigned n=0; n<Tr->Children.Length(); n++)
						{
							HtmlElement *Td = dynamic_cast<HtmlElement*>(Tr->Children[n]);
							if (Td->TagId == TAG_TH)
							{
								auto lnk = Td->Children.Length() ? Td->Children[0] : NULL;
								if (lnk)
									Headings[n] = lnk->GetText();
							}
							else if (Td->TagId == TAG_TD)
							{
								switch (Col)
								{
									case cNum:
									{
										LString Utf = Td->GetText();
										Item->SetText(Utf, 0);
										break;
									}
									case cCode:
									{
										if (!Td->Children.Length())
											break;

										LHtmlElement *c = Td->Children.First();
										if (!c)
											break;

										LString Utf = c->GetText();
										LString::Array a = Utf.SplitDelimit(" \r\r\n");
												
										Item->Char.Empty();
												
										for (auto s: a)
										{
											LString::Array tmp = s.Split("+");
											auto ch = tmp.Last().Int(16);
											LAssert(ch > 10);
											Item->Char.Add((uint32_t)ch);
										}

										Item->SetText(Utf, 1);
										break;
									}
									case cApple:
									{
										if (Td->Children.Length())
										{
											LHtmlElement *c = Td->Children.First();
											if (c && c->TagId == TAG_IMG)
											{
												const char *s;
												if (c->Get("src", s))
												{
													Item->IconData = s;
												}
											}
										}
										break;
									}
									case cName:
									{
										LString Utf = Td->GetText();
										Item->SetText(Utf, 4);
										break;
									}
								}

								const char *colSpan = NULL;
								if (Td->Get("colspan", colSpan))
									Col += (int)Atoi(colSpan);
								else
									Col++;
							}
						}
						
						if (Item->GetText(1))
						{
							if (!Item->IconData)
							{
								if (BackupIcon)
									Item->IconData = BackupIcon;
								else if (BwIcon)
									Item->IconData = BwIcon;
							}
							
							Items.Insert(Item.Release());
						}
					}
				}
			}				

			Lst->Insert(Items);

			Wnd->PostEvent(M_PARSE_HTML);

			Html->Children.DeleteObjects();
		}
		return 0;
	}
};

struct IconThread : public LThread
{
	LList *Lst;
	bool Loop;
	int Done = 0;
	
	IconThread(LList *lst) : LThread("IconThread")
	{
		Loop = true;
		Lst = lst;
		Run();
	}
	
	~IconThread()
	{
		Loop = false;
		while (!IsExited())
			LSleep(1);
	}
	
	int Main()
	{
		auto Png = LFilterFactory::New("file.png", FILTER_CAP_READ, NULL);
		while (Loop && Png)
		{
			ListItem *Item = NULL;
			if (Lst->Lock(_FL))
			{
				List<ListItem> a;
				if (Lst->GetAll(a))
				{
					for (auto i: a)
					{
						if (i->IconData.Get() && !i->Icon.Get())
						{
							Item = i;
							break;
						}
					}
				}
				Lst->Unlock();
			}
			
			if (Item)
			{
				const char *s = strchr(Item->IconData, ',');
				if (s)
				{
					ssize_t SLen = Strlen(++s);
					ssize_t BLen = BufferLen_64ToBin(SLen);
					LAutoPtr<uint8_t> Buf(new uint8_t[BLen]);
					ssize_t Ch = ConvertBase64ToBinary(Buf, BLen, (char*)s, SLen);
					if (Ch > 0)
					{
						LMemStream Str(Buf, BLen, false);
						LAutoPtr<LSurface> Img(new LMemDC(_FL));
						if (Png->ReadImage(Img, &Str) == LFilter::IoSuccess)
						{
							if (Img->GetColourSpace() != System32BitColourSpace)
							{
								LAutoPtr<LSurface> Img32(new LMemDC(_FL, Img->X(), Img->Y(), System32BitColourSpace));
								if (Img32)
								{
									Img32->Op(GDC_ALPHA);
									Img32->Blt(0, 0, Img);
									Img = Img32;
								}
							}
							
							if (Img->Y() > OUTPUT_ICON_SIZE)
							{
								LAutoPtr<LSurface> Resize(new LMemDC(_FL, OUTPUT_ICON_SIZE, OUTPUT_ICON_SIZE, System32BitColourSpace));
								if (Resize)
								{
									ResampleDC(Resize, Img);
									Img = Resize;
								}
							}
							
							// LgiTrace("Png=%ix%i, %s\n", Img->X(), Img->Y(), LColourSpaceToString(Img->GetColourSpace()));
							if (Lst->Lock(_FL))
							{
								Item->Icon = Img;
								Done++;
								Item->Update();
								Lst->Unlock();
							}
						}
					}					
				}
				
				Item->IconData.Empty();
			}
			else
			{
				if (Done > 100)
				{
					Lst->GetWindow()->PostEvent(M_ICONS_DONE);
					return 0;
				}
				LSleep(10);
			}
		}
		return 0;
	}
};

class App :
	public LWindow
{
	LOptionsFile Options;
	LAutoPtr<LThread> Scanner;
	LAutoPtr<LThread> Loader[12];
	LAutoPtr<LThread> IconCreator;
	LAutoPtr<LThread> Parser;

	EmojiStore Store;
	LRect Src;
	LRect Cursor;
	bool CursorBlink;
	int SrcSelect;
	ssize_t LastSize;
	unsigned LastLoaded;

	LRect Dst;
	LList *Lst;
	LTableLayout *Tbl;
	LHtmlElement Html;
	LHashTbl<ConstStrKey<char>, ListItem*> CharMap;
	
	bool EmojiLoaded;
	bool HtmlLoaded;

public:
	App() :
		Options(LOptionsFile::PortableMode, AppName),
		Html(NULL)
    {
		SrcSelect = 0;
		LastSize = 0;
		LastLoaded = 0;
		Cursor.ZOff(-1, -1);
		CursorBlink = 0;
		Lst = NULL;
		Tbl = NULL;
		EmojiLoaded = false;
		HtmlLoaded = false;
		
        Name(AppName);
        
        Options.SerializeFile(false);
        if (!SerializeState(&Options, OPT_WndPos, true))
        {        
			LRect r(0, 0, 1000, 800);
			SetPos(r);
			MoveToCenter();
		}
		
        SetQuitOnClose(true);

        if (Attach(0))
        {
			AddView(Tbl = new LTableLayout);
			int y = 0;
			auto *c = Tbl->GetCell(0, y++);
			c->Add(new LButton(IDC_LINK, 0, 0, -1, -1, "Link"));
			
			c = Tbl->GetCell(1, 0, true, 1, 5);
			c->Add(Lst = new LList(IDC_LIST, 0, 0, 100, 100));
			Lst->AddColumn("Id", 50);
			Lst->AddColumn("Char", 80);
			Lst->AddColumn("Img", 30);
			Lst->AddColumn("Link", 30);
			Lst->AddColumn("Name", 200);
			Lst->MultiSelect(false);

			c = Tbl->GetCell(0, y++);
			c->Add(new LButton(IDC_UNLINK, 0, 0, -1, -1, "Unlink"));

			c = Tbl->GetCell(0, y++);
			c->Add(new LButton(IDC_CREATE_IMG, 0, 0, -1, -1, "CreateImg"));
			
			c = Tbl->GetCell(0, y++);
			c->Add(new LTextLabel(IDC_CURRENT_CHAR, 0, 0, -1, -1, "..."));

			c = Tbl->GetCell(0, y++);
			LEdit *e = NULL;
			c->Add(e = new LEdit(IDC_SEARCH, 0, 0, 60, 20));
			e->GetCss(true)->Width(LCss::Len(LCss::LenPx, 50));
			
			AttachChildren();
            Visible(true);            
            
            Scanner.Reset(new ScanThread(this, &Store));
            
            int Cores = MIN(LAppInst->GetCpuCount(), CountOf(Loader));
            if (Cores < 0)
				Cores = 4;
            
			for (int i=0; i<Cores; i++)
		        Loader[i].Reset(new LoaderThread(&Store));
		       
			LFile::Path Local;
			Local = Local.GetSystem(LSP_APP_INSTALL, 0);
			#ifdef MAC
			Local--;
			#endif
			Local += "Unicode";
			Local += "Full Emoji List.html";
			if (Local.IsFile())
			{
				Parser.Reset(new ParserThread(&Html, Local.GetFull(), this, Lst));
			}

			SetCtrlEnabled(IDC_CREATE_IMG, false);
            SetPulse(100);
        }
    }
    
    ~App()
    {
		LProfile Prof("~App");

		Prof.Add("Storing Emoji to options");

		// Store all the emoji mappings into the options file...
		if (EmojiLoaded && HtmlLoaded)
		{
			Options.CreateTag("Emoji");
			LXmlTag *t = Options.LockTag("Emoji", _FL);
			if (t)
			{
				if (Store.Lock(_FL))
				{
					t->Children.DeleteObjects();
					
					for (unsigned i=0; i<Store.Icons.Length(); i++)
					{
						Emoji &e = Store.Icons[i];
						if (e.Char.Length())
						{
							ssize_t Pos = e.Path.RFind(DIR_STR);
							LString Leaf = e.Path(Pos + 1, -1);
							LAssert(Leaf.Get() != NULL);
							if (Leaf)
							{
								LXmlTag *c = new LXmlTag("Char");
								if (c)
								{
									c->SetAttr("char", e.Char.toString().Get());
									c->SetAttr("file", Leaf);
									t->InsertTag(c);
								}
							}
						}
					}
					
					Store.Unlock();
				}
				
				Options.Unlock();
			}
		}

		Prof.Add("Scanner");
		Scanner.Reset();
		
		Prof.Add("Loaders");
		for (unsigned i=0; i<CountOf(Loader); i++)
		{
			Loader[i].Reset();
		}

		Prof.Add("IconCreator");
		IconCreator.Reset();
		Prof.Add("Parser");
		Parser.Reset();

		Prof.Add("SerializeState");
		SerializeState(&Options, OPT_WndPos, false);

		Prof.Add("Serialize");
		Options.SerializeFile(true);

		Prof.Add("Elements");
		Html.Children.DeleteObjects();

		Prof.Add("LstEmpty");
		Lst->Empty();
		
		Prof.Add("EmojiStoreEmpty");
		Store.Empty();
    }
    
    void OnPulse()
    {
		bool Update = false;
		if (Store.Icons.Length() != LastSize)
		{
			LastSize = Store.Icons.Length();
			Update = true;
		}
			
		if (Store.Loaded != LastLoaded)
		{
			LastLoaded = Store.Loaded;
			Update = true;
		}
		
		bool Blink = (LCurrentTime() / 500) & 1;
		if (Blink ^ CursorBlink)
		{
			CursorBlink = Blink;
			if (!Update)
				Invalidate(&Cursor);
		}
		
		if (Update)
			Invalidate();
    }
    
    void OnPaint(LSurface *pScreen)
    {
		LRect c = GetClient();
		LMemDC Mem(_FL, c.X(), c.Y(), System32BitColourSpace);
		Mem.Colour(L_MED);
		Mem.Rectangle();
		LSurface *pDC = &Mem;
		
		int IconCell = ICON_SIZE + 2;
		int IconsDown = (int) ((Store.Icons.Length() + ICONS_ACROSS - 1) / ICONS_ACROSS);
		IconsDown = MAX(IconsDown, 1);
		
		Src = c;
		Src.Inset(10, 10);
		Src.x2 = Src.x1 + (ICONS_ACROSS * IconCell) - 1;
		Src.y2 = Src.y1 + (IconsDown * IconCell) - 1;
		
		Dst = c;
		Dst.Inset(10, 10);
		Dst.x1 = Src.x2 + 10;
		Tbl->SetPos(Dst);
		
		pDC->Colour(L_WORKSPACE);
		pDC->Rectangle(&Src);

		if (Store.Lock(_FL))
		{
			for (unsigned i=0; i<Store.Icons.Length(); i++)
			{
				Emoji &e = Store.Icons[i];
				LRect r(0, 0, IconCell-1, IconCell-1);
				int x = i % ICONS_ACROSS;
				int y = i / ICONS_ACROSS;
				r.Offset(Src.x1 + x * IconCell, Src.y1 + y * IconCell);
				
				if (e.Char.Length())
				{
					pDC->Colour(Green);
					pDC->Rectangle(&r);
				}
				else
				{
					pDC->Colour(Red);
					pDC->Box(&r);
				}
				r.Inset(1, 1);
				
				if (SrcSelect == i)
				{
					if (CursorBlink || !e.Char.Length())
					{
						pDC->Colour(CursorBlink ? L_FOCUS_SEL_BACK : L_WORKSPACE);
						pDC->Rectangle(&r);
					}
					Cursor = r;
				}
	
				if (e.Resize)
				{
					int Op = pDC->Op(GDC_ALPHA);
					pDC->Blt(r.x1, r.y1, e.Resize);
					pDC->Op(Op);
				}
				else
				{
					if (e.Loading)
						pDC->Colour(Green);
					else
						pDC->Colour(Red);
					pDC->Line(r.x1, r.y1, r.x2, r.y2);
					pDC->Line(r.x2, r.y1, r.x1, r.y2);
				}
			}

			if (Store.Icons.Length() == 0)
			{
				LDisplayString Ds(LSysFont, "No icons loaded.");
				LSysFont->Transparent(true);
				LSysFont->Colour(L_TEXT, L_WORKSPACE);
				Ds.Draw(&Mem, Src.x1 + 5, Src.y1 + 2);
			}
						
			Store.Unlock();
		}

		pScreen->Blt(0, 0, &Mem);
    }
    
    int HitText(int x, int y)
    {
		if (!Src.Overlap(x, y))
			return -1;
		
		int IconCell = ICON_SIZE + 2;
		int Sx = (x - Src.x1) / IconCell;
		int Sy = (y - Src.y1) / IconCell;
		int Index = (Sy * ICONS_ACROSS) + Sx;
		if (Index < 0 || Index >= (int)Store.Icons.Length())
			return -1;
		
		return Index;
    }
    
    void OnSelectEmoji()
    {
		if (Store.Lock(_FL))
		{
			Emoji &e = Store.Icons[SrcSelect];
			SetCtrlName(IDC_CURRENT_CHAR, e.Char.toString().Get());
			Store.Unlock();
			Tbl->InvalidateLayout();
		}

		Invalidate();
    }
    
    void OnMouseClick(LMouse &m)
    {
		if (m.IsContextMenu())
		{
		}
		else if (m.Down())
		{
			if (m.Left())
			{
				int Idx = HitText(m.x, m.y);
				if (Idx >= 0)
				{
					if (m.Double())
					{
						EmojiStr Ch;
						if (Store.Lock(_FL))
						{
							Emoji &e = Store.Icons[Idx];
							Ch = e.Char;
							Store.Unlock();
						}
						
						if (Ch.Length())
						{
							ListItem *li = CharMap.Find(Ch.toString());
							if (li)
							{
								li->Select(true);
								Lst->ScrollToSelection();
							}
						}
					}
					else
					{
						SrcSelect = Idx;
						CursorBlink = true;
						OnSelectEmoji();
					}
				}
			}
		}
    }

	struct Map
	{
		EmojiStr Char;
		uint16 Index;
		LSurface *Img;
		LString Desc;
	};
    
    bool CreateOutputImage()
    {
		LMutex::Auto Lock(&Store, _FL);
		
		Store.Icons.Sort(EmojiCmp);
		
		LFile Out;
		LFile::Path OutPath(LSP_APP_INSTALL);
		OutPath += "all-chars.csv";
		if (Out.Open(OutPath, O_WRITE))
			Out.SetSize(0);
		
		LArray<Map> m;
		int Next = 0;

		#if 1
			// Create map from list items...
			LArray<ListItem*> All;
			if (Lst->GetAll(All))
			{
				for (auto i: All)
				{
					if (i->Char.Length() && i->Icon)
					{
						auto &map = m.New();
						map.Char = i->Char;
						map.Index = Next++;
						map.Img = i->Icon;
						map.Desc = i->GetText(4);
						int asd=0;
					}
					else LgiTrace("%s:%i - Item missing data: %s, %p\n", _FL, i->Char.toString().Get(), i->Icon.Get());
				}
			}
		#else
			for (unsigned i=0; i<Store.Icons.Length(); i++)
			{
				Emoji &e = Store.Icons[i];
				if (e.Char)
				{
					Map &map = m.New();
					map.Char = e.Char;
					map.Index = Next++;
					map.Img = e.Img;
					LAssert(map.Img != NULL);
		
					if (Out.IsOpen())
						Out.Print("0x%I64x,%I64i,%i\n", map.Char, map.Char, map.Index);
				}
			}
		#endif		
		Out.Close();
		
		if (m.Length() == 0)
		{
			LgiMsg(this, "No mappings.", "Error");
			return false;
		}

		int Rows = (int) ((m.Length() + 15) / 16);
		LMemDC Icons(_FL, OUTPUT_ICON_SIZE * 16, OUTPUT_ICON_SIZE * Rows, System32BitColourSpace);
		Icons.Set(0, 32);
		Icons.Rectangle();
		
		LStringPipe Code(1024);
		Code.Print(	"// Generated by EmojiMapper\n"
					"// fret@memecode.com\n"
					"#include \"lgi/common/Lgi.h\"\n"
					"#include \"lgi/common/Emoji.h\"\n"
					"\n"
					"EmojiChar EmojiToIconIndex(const uint32_t *Str, ssize_t Len)\n"
					"{\n"
					"	switch (*Str)\n"
					"	{\n");

		LProgressDlg prog(this, 100);
		prog.SetDescription("Resizing icons...");
		prog.SetRange(m.Length());
		auto UpdateTs = LCurrentTime();

		auto Sorted = m;
		Sorted.Sort([](auto a, auto b) { return a->Char - b->Char; });

		LRange Ranges[2] = {
			LRange(0x203c),
			LRange(0x1f004)
		};

		uint64_t PrevChar = 0;
		for (unsigned i=0; i<Sorted.Length();)
		{
			Map &a = Sorted[i];
			int GrpSize = 1;
			while (i + GrpSize < Sorted.Length())
			{
				Map &b = Sorted[i + GrpSize];
				auto ach = a.Char[0];
				auto bch = b.Char[0];
				if (ach > 0 && ach == bch)
					GrpSize++;
				else
					break;
			}

			bool Grp = GrpSize > 1 || a.Char.Length() > 1;
			if (Grp)
				Code.Print(	"		case 0x%x:\n",
							a.Char[0]);

			for (int g=0; g<GrpSize; g++, i++)
			{
				a = Sorted[i];

				LString Desc = a.Desc.Replace("\xE2\x8A\x9B ");

				for (int r=CountOf(Ranges)-1; r>=0; r--)
				{
					if (a.Char[0] >= Ranges[r].Start)
					{
						auto Len = a.Char[0] - Ranges[r].Start + 1;
						if (Ranges[r].Len < Len)
							Ranges[r].Len = Len;
						break;
					}
				}

				LMemDC Resized(_FL, OUTPUT_ICON_SIZE, OUTPUT_ICON_SIZE, System32BitColourSpace);
				if (a.Img->X() != OUTPUT_ICON_SIZE ||
					a.Img->Y() != OUTPUT_ICON_SIZE)
				{
					ResampleDC(&Resized, a.Img);
				}
				else
				{
					Resized.Op(GDC_SET);
					Resized.Blt(0, 0, a.Img);
				}

				int x = a.Index % 16;
				int y = a.Index / 16;
				Icons.Blt(x * OUTPUT_ICON_SIZE, y * OUTPUT_ICON_SIZE, &Resized);

				if (Grp)
				{
					Code.Print("			if (Len >= %i", (int)a.Char.Length());
					for (int j=1; j<a.Char.Length(); j++)
						Code.Print(" && Str[%i]==0x%x", j, a.Char[j]);
					Code.Print(") return {%i, %i, \"%s\"};\n",
						a.Index,
						(int)a.Char.Length(),
						Desc.Get());
				}
				else
				{
					Code.Print("		case 0x%x: return {%i, 1, \"%s\"};\n", a.Char[0], a.Index, Desc.Get());
				}
			}

			if (Grp)
				Code.Print("			break;\n");

			prog.Value(i);
			auto Now = LCurrentTime();
			if (Now - UpdateTs > 100)
				UpdateTs = Now;
		}

		Code.Print(	"	}\n"
					"\n"
					"	return {-1, 0, NULL};\n"
					"}\n"
					"\n"
					"LRange EmojiRanges[2] = {\n"
					"	LRange(0x%x, 0x%x),\n"
					"	LRange(0x%x, 0x%x)\n"
					"};\n"
					"\n",
					(uint32_t)Ranges[0].Start, (uint32_t)Ranges[0].Len,
					(uint32_t)Ranges[1].Start, (uint32_t)Ranges[1].Len);
		
		OutPath = OutPath.GetSystem(LSP_APP_INSTALL, 0);
		OutPath += "EmojiMap.cpp";

		LFile CodeFile;
		if (!CodeFile.Open(OutPath, O_WRITE))
		{
			LgiMsg(this, "Failed to open '%s' to write code", "Error", MB_OK, OutPath.GetFull().Get());
			return false;
		}

		CodeFile.SetSize(0);
		LAutoString Src(Code.NewStr());
		if (Src)
			CodeFile.Write(Src, strlen(Src));
		CodeFile.Close();
				
		OutPath = (OutPath / ".." / "EmojiMap.png");

		LFile ImgFile;
		if (!ImgFile.Open(OutPath, O_WRITE))
		{
			LgiMsg(this, "Failed to open '%s' to write image", "Error", MB_OK, OutPath.GetFull().Get());
			return false;
		}

		ImgFile.SetSize(0);
				
		auto Png = LFilterFactory::New(OutPath, FILTER_CAP_READ, NULL);
		if (!Png)
		{
			LgiMsg(this, "Failed to encode PNG icons", "Error");
			return false;
		}
		
		Png->WriteImage(&ImgFile, &Icons);
		// LgiMsg(this, "Wrote icons and code successfully.", AppName);
		return true;
    }
    
    int OnNotify(LViewI *Ctrl, const LNotification &n) override
    {
		switch (Ctrl->GetId())
		{
			case IDC_SEARCH:
			{
				auto s = Ctrl->Name();
				if (ValidStr(s))
				{
					if (Lst->Lock(_FL))
					{
						List<ListItem> Items;
						if (Lst->GetAll(Items))
						{
							LArray<ListItem*> Matches;
							int SelIdx = -1;
							for (auto i: Items)
							{
								const char *str = i->GetText(4);
								if (str && stristr(str, s))
								{
									if (n.Type == LNotifyReturnKey)
									{
										if (i->Select())
											SelIdx = (int)Matches.Length();
										Matches.Add(i);										
									}
									else
									{
										i->Select(true);
										Lst->ScrollToSelection();
										break;
									}
								}
							}
							
							if (n.Type == LNotifyReturnKey && Matches.Length())
							{
								ListItem *li = NULL;
								if (SelIdx < 0)
									li = Matches[0];
								else
									li = Matches[MIN((unsigned)SelIdx+1, Matches.Length()-1)];
								if (li)
								{
									li->Select(true);
									Lst->ScrollToSelection();
								}
							}
						}
						Lst->Unlock();
					}
				}
				break;
			}
			case IDC_LINK:
			{
				// Link an icon to it's unicode character...
				ListItem *li = dynamic_cast<ListItem*>(Lst->GetSelected());
				if (li && Store.Lock(_FL))
				{
					Emoji &e = Store.Icons[SrcSelect];
					
					LAssert(li->Char.Length() > 0);
					if (li->Char.Length())
					{
						e.Char = li->Char;
						li->SetText("Yes", 3);
						li->Linked = true;
						li->Update();
					}
					
					Store.Unlock();
					
					OnSelectEmoji();
				}
				break;
			}
			case IDC_UNLINK:
			{
				if (Store.Lock(_FL))
				{
					Emoji &e = Store.Icons[SrcSelect];
					e.Char.Empty();
					Store.Unlock();
					OnSelectEmoji();
				}
				break;
			}
			case IDC_CREATE_IMG:
			{
				// create the output image.
				CreateOutputImage();
				break;
			}
		}
		
		return 0;
    }
    
    void OnLoaded()
    {
		// Mark all the list itemed linked if there is a matching emoji
		if (Store.Lock(_FL))
		{
			for (unsigned i=0; i<Store.Icons.Length(); i++)
			{
				Emoji &e = Store.Icons[i];
				if (e.Path && e.Char.Length())
				{
					ListItem *li = CharMap.Find(e.Char.toString());
					if (li)
					{
						li->SetText("Yes", 3);
						li->Linked = true;
					}
				}
			}
			Store.Unlock();
		}
    }

	LMessage::Param OnEvent(LMessage *Msg)
	{
		switch (Msg->Msg())
		{
			case M_ICONS_DONE:
			{
				SetCtrlEnabled(IDC_CREATE_IMG, true);

				#if 1
				CreateOutputImage();
				#endif
				break;
			}
			case M_EMOJI_LOADED:
			{
				// Load any existing mappings from the options...
				if (Store.Lock(_FL))
				{
					LHashTbl<StrKey<char,false>, Emoji*> Map;
					for (unsigned i=0; i<Store.Icons.Length(); i++)
					{
						Emoji &e = Store.Icons[i];
						if (e.Path)
						{
							char *Leaf = strrchr(e.Path, DIR_CHAR);
							if (Leaf++)
							{
								LAssert(Map.Find(Leaf) == NULL);
								Map.Add(Leaf, &e);
							}
						}
					}
					
					LXmlTag *t = Options.LockTag("Emoji", _FL);
					if (t)
					{
						for (unsigned i=0; i<t->Children.Length(); i++)
						{
							LXmlTag *c = t->Children[i];
							if (c->IsTag("char"))
							{
								char *Char = c->GetAttr("char");
								char *File = c->GetAttr("file");
								if (Char && File)
								{
									Emoji *e = Map.Find(File);
									// LAssert(e != NULL);
									if (e)
									{
										LAssert(!"Fixme");
										/*
										e->Char = atoi64(Char);
										if (e->Char == 0x7fffffff)
										{
											e->Char = 0;
										}
										*/
									}
								}
							}
						}
						Options.Unlock();
					}
					
					Store.Unlock();
				}
				
				LAssert(EmojiLoaded == false);
				EmojiLoaded = true;
				if (HtmlLoaded)
				{
					OnLoaded();
				}
				break;
			}			
			case M_PARSE_HTML:
			{
				if (Lst->Lock(_FL))
				{
					List<ListItem> Items;
					if (Lst->GetAll(Items))
					{
						for (auto i: Items)
						{
							if (!CharMap.Find(i->Char.toString()))
							{
								CharMap.Add(i->Char.toString(), i);
							}
							else
							{
								LgiTrace("%s:%i - Duplicate char: %i, 0x%x\n", _FL, i->Char, i->Char);
							}
						}
					}
					Lst->Unlock();
				}
				
				IconCreator.Reset(new IconThread(Lst));
				
				LAssert(HtmlLoaded == false);
				HtmlLoaded = true;
				if (EmojiLoaded)
				{
					OnLoaded();
				}
				break;
			}
		}
		
		return LWindow::OnEvent(Msg);
	}
};

//////////////////////////////////////////////////////////////////
int LgiMain(OsAppArguments &AppArgs)
{
	LApp a(AppArgs, "EmojiMapper");
	if (a.IsOk())
	{
		#if 0
		
		auto t = LReadFile("all-chars.csv");
		if (t)
		{
			auto Lines = t.Split("\n");
			LArray<uint64> Ch;
			for (unsigned i=0; i<Lines.Length(); i++)
			{
				LString::Array Values = Lines[i].SplitDelimit(",");
				Ch.Add(Values[0].Int());
			}
			
			unsigned BestMaxTable = 0;
			double BestStdDev = 10000000.0;
			for (int Size = 16; Size<200000; Size++)
			{
				register unsigned *Table = new unsigned[Size];

				memset(Table, 0, sizeof(*Table) * Size);
				register uint64 *s = &Ch[0];
				register uint64 *e = s + Ch.Length();
				while (s < e)
				{
					if (*s >> 32 == 0)
					{
						unsigned Idx = *s % Size;				
						Table[Idx]++;
					}
					s++;
				}

				unsigned Sum = 0;
				unsigned MaxTable = 0;
				for (int i=0; i<Size; i++)
				{
					Sum += Table[i];
					if (Table[i] > MaxTable)
						MaxTable = Table[i];
				}
				double Mean = (double)Sum / Size;
				double Variance = 0.0;
				for (int i=0; i<Size; i++)
				{
					double Diff = Mean - Table[i];
					Variance += Diff * Diff;
				}
				double StdDev = sqrt(Variance);

				if (!BestMaxTable || MaxTable < BestMaxTable)
				{
					BestMaxTable = MaxTable;
					LgiTrace("BestMaxTable=%i, Size=%i\n", BestMaxTable, Size);
				}
				if (StdDev < BestStdDev)
				{
					BestStdDev = StdDev;
					LgiTrace("BestStdDev=%f, Size=%i\n", StdDev, Size);
				}
				
				DeleteArray(Table);
			}
		}
		
		#else
		a.AppWnd = new App;
		a.Run();
		#endif
	}

	return 0;
}

