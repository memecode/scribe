/*
**	FILE:			ScribePreview.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			31/8/99
**	DESCRIPTION:	Scribe Mail Preview UI
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "lgi/common/Lgi.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/Html.h"
#include "lgi/common/Scripting.h"

#include "Scribe.h"
#include "PreviewPanel.h"
#include "resdefs.h"
#include "ScribeListAddr.h"
#include "Calendar.h"
#include "Components.h"

#include "../src/common/Text/HtmlPriv.h"

#define OPT_PreviewSize					"LPreviewPanel::OpenSize"
#define OPT_PreviewOpen					"LPreviewPanel::IsOpen"

#if defined(WIN32) && defined(_DEBUG)
#define DEBUG_FOCUS						1
#else
#define DEBUG_FOCUS						0
#endif

#ifdef MAC
#define HEADER_POS						1
#else
#define HEADER_POS						0
#endif

class LPreviewPanelPrivate : public LDocumentEnv, public LScriptContext
{
public:
	LPreviewPanel *Panel;
	ScribeWnd *App;
	LDocView *TextCtrl;
	Thing *Item;
	Thing *Pulse;
	LRect TxtPos;
	int Time;
	LCapabilityTarget::CapsHash MissingCaps;
	MissingCapsBar *Bar;
	bool IgnoreShowImgNotify;
	Contact *CtxMenuContact;

	// Dynamic header content
	Html1::LHtml *Header;
	int HeaderY;
	ScribeDom *HeaderDom;
	LString HeaderMailFile;
	LString HeaderMailTemplate;
	LString HeaderContactFile;
	LString HeaderContactTemplate;
	LString HeaderGroupFile;
	LString HeaderGroupTemplate;
	
	// Scripting
	LAutoPtr<LCompiledCode> ScriptObj;

	// Methods
	LPreviewPanelPrivate(LPreviewPanel *p) : Panel(p)
	{
		HeaderY = 88;
		Header = 0;
		HeaderDom = 0;
		Bar = 0;
		IgnoreShowImgNotify = false;
		CtxMenuContact = NULL;

		Item = 0;
		Pulse = 0;
		TextCtrl = 0;
		Time = -1;
		TxtPos.ZOff(-1, -1);
	}

	~LPreviewPanelPrivate()
	{
		DeleteObj(Bar);
	}

	LString GetIncludeFile(const char *FileName) override
	{
		return NULL;
	}

	bool AppendItems(LSubMenu *Menu, const char *Param, int Base) override
	{
		if (!Menu)
			return false;

		Mailto mt(App, Param);
		CtxMenuContact = mt.To.Length() > 0 ? Contact::LookupEmail(mt.To[0]->sAddr) : NULL;

		if (CtxMenuContact)
			Menu->AppendItem(LLoadString(IDS_OPEN_CONTACT), IDM_OPEN, true);
		else
			Menu->AppendItem(LLoadString(IDS_ADD_CONTACTS), IDM_NEW_CONTACT, true);

		return true;
	}

	bool OnMenu(LDocView *View, int Id, void *Context) override
	{
		if (Id == IDM_NEW_CONTACT)
		{
			if (Context)
			{
				Html1::LTag *a = (Html1::LTag*) Context;
				char *Name = WideToUtf8(a->Text());
				const char *Email = 0;
				a->Get("href", Email);

				ListAddr *La = new ListAddr(App, Email, Name);
				if (La)
				{
					La->AddToContacts(true);
					DeleteObj(La);
				}
			}
		}
		else if (Id == IDM_OPEN && CtxMenuContact)
		{
			CtxMenuContact->DoUI();
			CtxMenuContact = NULL;
		}
		else return false;

		return true;
	}

	bool OnNavigate(LDocView *Parent, const char *Uri) override
	{
		Mailto m(App, Uri);
		if (m.To[0])
		{
			Mail *email = App->CreateMail();
			if (email)
			{
				m.Apply(email);
				email->DoUI();
				return true;
			}
		}
		return false;
	}

	LDocumentEnv::LoadType GetContent(LoadJob *&j) override
	{
		LUri i;

		if (!j)
			goto GetContentError;
		if (_strnicmp(j->Uri, "LC_", 3) == 0)
			goto GetContentError;

		i.Set(j->Uri);
		if (!i.sProtocol || !i.sPath)
			goto GetContentError;

		if (_stricmp(i.sProtocol, "file") == 0)
		{
			char p[MAX_PATH_LEN];
			strcpy_s(p, sizeof(p), i.sPath);
			#ifdef WIN32
			char *c;
			while (c = strchr(p, '/')) *c = '\\';
			#endif

			if (LFileExists(p))
			{
				j->pDC.Reset(GdcD->Load(p));

				return LoadImmediate;
			}
		}
		else if
		(
			!_stricmp(i.sProtocol, "http") ||
			!_stricmp(i.sProtocol, "https") ||
			!_stricmp(i.sProtocol, "ftp")
		)
		{
			// We don't check OPT_HtmlLoadImages here because it's done elsewhere:
			// - ScribeWnd::CreateTextControl calls LHtml::SetLoadImages with the value from OPT_HtmlLoadImages
			// - LTag::LoadImage checks LHtml::GetLoadImages
			//
			// If there is a remote job here, it's because it's probably whitelisted.			
			Worker = App->GetImageLoader();
			if (Worker)
			{
				Worker->AddJob(j);
				j = 0;

				return LoadDeferred;
			}
		}

	GetContentError:
		return LoadError;
	}

	bool trace(LScriptArguments &Args)
	{
		LgiTrace("Script: ");
		for (unsigned i=0; i<Args.Length(); i++)
		{
			LgiTrace("%s%s", i ? ", " : "", Args[i]->CastString());
		}
		LgiTrace("\n");

		return true;
	}

	bool encodeURI(LScriptArguments &Args)
	{
		if (Args.Length() == 1)
		{
			LUri u;
			*Args.GetReturn() = u.EncodeStr(Args[0]->CastString());
			return true;
		}

		return false;
	}

	bool getElementById(LScriptArguments &Args)
	{
		if (Args.Length() == 1 && Header)
		{
			LDom *e = Header->getElementById(Args[0]->CastString());
			if (e)
			{
				*Args.GetReturn() = e;
				return true;
			}
		}

		return false;
	}

	// Convert dynamic fields into string values...
	LString OnDynamicContent(LDocView *Parent, const char *Code) override
	{
		if (!HeaderDom)
			return NULL;

		LVariant v;
		if (!HeaderDom->GetValue(Code, v))
			return NULL;

		return v.CastString();
	}

	void SetEngine(LScriptEngine *Eng) {}
	LHostFunc *GetCommands() override;

	void SetGlobals(LCompiledCode *obj)
	{
		if (!obj)
			return;
		
		// Set global 'Thing' variable to the current object.
		LVariant v = (LDom*)Item;
		obj->Set("Thing", v);

		// Set 'document' variable to the document viewer object.
		if (TextCtrl)
		{
			v = (LDom*)TextCtrl;
			obj->Set("document", v);
		}
	}

	bool OnCompileScript(LDocView *Parent, char *Script, const char *Language, const char *MimeType) override
	{
		LScriptEngine *Engine = App->GetScriptEngine();
		if (!Engine || !Script)
			return false;

		if (!ScriptObj)
		{
			if (ScriptObj.Reset(new LCompiledCode))
				SetGlobals(ScriptObj);
		}
			
		if (!ScriptObj)
			return false;

		SetLog(LScribeScript::Inst->GetLog());

		const char *FileName = "script.html";
		if (Item->Type() == MAGIC_MAIL)
			FileName = HeaderMailFile;
		else if (Item->Type() == MAGIC_CONTACT)
			FileName = HeaderContactFile;

		return Engine->Compile(ScriptObj, this, Script, FileName);
	}

	bool OnExecuteScript(LDocView *Parent, char *Script) override
	{
		LScriptEngine *Engine = App->GetScriptEngine();
		if (Engine && Script)
		{
			if (!ScriptObj)
			{
				if (ScriptObj.Reset(new LCompiledCode))
					SetGlobals(ScriptObj);
			}

			// Run the fragment of code.
			if (Engine->RunTemporary(ScriptObj, Script))
			{
				return true;
			}
		}

		return false;
	}
};

LHostFunc Cmds[] =
{
	LHostFunc("getElementById", "", (ScriptCmd)&LPreviewPanelPrivate::getElementById),
	LHostFunc("encodeURI", "", (ScriptCmd)&LPreviewPanelPrivate::encodeURI),
	LHostFunc("trace", "", (ScriptCmd)&LPreviewPanelPrivate::trace),
	LHostFunc(0, 0, 0)
};

LHostFunc *LPreviewPanelPrivate::GetCommands()
{
	return Cmds;
}

LPreviewPanel::LPreviewPanel(ScribeWnd *app)
{
	d = new LPreviewPanelPrivate(this);
	d->App = app;
	
    // This allows us to hook iconv conversion events
    LFontSystem::Inst()->Register(this);
    
    // This allows us to hook missing image library events
    GdcD->Register(this);
}

LPreviewPanel::~LPreviewPanel()
{
	DeleteObj(d);
}

LMessage::Param LPreviewPanel::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_NEEDS_CAP:
		{
			LAutoString c((char*)Msg->A());
			NeedsCapability(c);
			return 0;
		}
		case M_UPDATE:
		{
			OnPosChange();
			break;
		}
	}
    
    return LLayout::OnEvent(Msg);
}

void LPreviewPanel::OnCloseInstaller()
{
	d->Bar = NULL;
}

void LPreviewPanel::OnInstall(CapsHash *Caps, bool Status)
{
	if (Caps && Status)
	{
	    LDataI *Obj;
		if (d->Item &&
			(Obj = d->Item->GetObject()) &&
			Obj->Type() == MAGIC_MAIL)
		{
			LFontSystem::Inst()->ResetLibCheck();
			d->Item->Reparse();
		}

		OnThing(d->Item, true);
	}
	else
	{
		// Install failed...
	}
}

bool LPreviewPanel::NeedsCapability(const char *Name, const char *Param)
{
    if (!InThread())
    {
        PostEvent(M_NEEDS_CAP, (LMessage::Param)NewStr(Name));
    }
    else
    {
        if (!Name)
            return false;
            
        if (d->MissingCaps.Find(Name))
            return true;

        d->MissingCaps.Add(Name, true);

	    char msg[256];
		LArray<const char *> Actions;
		LAutoPtr<LColour> Back;
	    
	    if (!_stricmp(Name, "RemoteContent"))
	    {
			Actions.Add(LLoadString(IDS_ALWAYS_SHOW_REMOTE_CONTENT));
			Actions.Add(LLoadString(IDS_SHOW_REMOTE_CONTENT));

			Back.Reset(new LColour(L_LOW));
			strcpy_s(msg, sizeof(msg),
					LLoadString
					(
						IDS_REMOTE_CONTENT_MSG,
						"To protect your privacy Scribe has blocked the remote content in this message."
					));
	    }
	    else
	    {
			Actions.Add(LLoadString(IDS_INSTALL));
			int ch = 0;
			for (auto k : d->MissingCaps)
				ch += sprintf_s(msg+ch, sizeof(msg)-ch, "%s%s", ch?", ":"", k.key);
			ch += sprintf_s(msg+ch, sizeof(msg)-ch, " is required to display this content.");
		}
    		
        if (!d->Bar)
        {
            d->Bar = new MissingCapsBar(this, &d->MissingCaps, msg, d->App, Actions, Back);
            d->Bar->Attach(this);
            OnPosChange();
        }
        else
        {
		    d->Bar->SetMsg(msg);
        }
    }

    return true;
}

LDocView *LPreviewPanel::GetDoc(const char *MimeType)
{
    return d->TextCtrl;
}

bool LPreviewPanel::SetDoc(LDocView *v, const char *MimeType)
{
    if (v != d->TextCtrl)
    {
        DeleteObj(d->TextCtrl);
        if ((d->TextCtrl = v))
        {
			LCapabilityClient *cc = dynamic_cast<LCapabilityClient*>(d->TextCtrl);
			if (cc)
				cc->Register(this);

			if (IsAttached())
				return v->Attach(this);				
        }
    }
    return true;
}

void LPreviewPanel::OnPaint(LSurface *pDC)
{
	pDC->Colour(L_MED);
	pDC->Rectangle();
	
	#ifdef MAC
	if (d->Header)
	{
		LRect r = d->Header->GetPos();
		pDC->Colour(Rgb24(0xB0, 0xB0, 0xB0), 24);
		pDC->Line(r.x1-1, r.y1-1, r.x2, r.y1-1);
		pDC->Line(r.x1-1, r.y1-1, r.x1-1, r.y2);
	}
	#endif
}

void LPreviewPanel::OnPosChange()
{
	int y = 0;
	LRect c = GetClient();

	if (d->Header)
	{
		LRect r(HEADER_POS, HEADER_POS, c.X()-(HEADER_POS<<1), c.Y()-(HEADER_POS<<1));
		d->Header->SetPos(r);
		LPoint Size = d->Header->Layout(true);
		if (Size.y > 0)
		{
			/*	This size limit is now implemented as CSS in the HTML itself. */
			d->HeaderY = Size.y;
			if (d->HeaderY != r.Y())
			{
				r.Set(HEADER_POS, HEADER_POS, c.X()-(HEADER_POS<<1), HEADER_POS+d->HeaderY-1);
				d->Header->SetPos(r);
			}
		}

		y += r.Y();
	}
	
	if (d->Bar)
	{
		LRect r(0, y, c.X()-1, y + d->Bar->Y() - 1);
		d->Bar->SetPos(r);
		y += d->Bar->Y();
	}

	if (d->TextCtrl)
	{
		LRect r(0, y, c.X()-1, c.Y()-1);
		d->TextCtrl->SetPos(r);
		d->TextCtrl->Visible(true);
	}
}

bool AttachViewToPanel(LDocView *v, void *p)
{
	LPreviewPanel *pp = (LPreviewPanel*)p;

	return v->Attach(pp);
}

Thing *LPreviewPanel::GetCurrent()
{
	return d->Item;
}

bool LPreviewPanel::CallMethod(const char *Name, LVariant *Dst, LArray<LVariant*> &Arg)
{
	ScribeDomType Method = StrToDom(Name);

	*Dst = false;
	switch (Method)
	{
		case SdShowRemoteContent:
			if (d->TextCtrl)
			{
				bool Always = Arg.Length() > 0 ? Arg[0]->CastBool() : false;
				if (Always)
				{
					auto m = d->Item->IsMail();
					if (m)
					{
						auto From = m->GetFrom();
						if (From)
							d->App->RemoteContent_AddSender(From->GetStr(FIELD_EMAIL), true);
						else
							LgiTrace("%s:%i - No from address.\n", _FL);
					}
					else LgiTrace("%s:%i - Not an email.\n", _FL);
				}

				d->IgnoreShowImgNotify = true;
				d->TextCtrl->SetLoadImages(true);
				d->IgnoreShowImgNotify = false;
				PostEvent(M_UPDATE);
				*Dst = true;
			}
			break;
		case SdSetHtml:
			if (d->TextCtrl && Arg.Length() > 0)
			{
				d->TextCtrl->Name(Arg[0]->Str());
				*Dst = true;
			}
			break;
		default:
			return false;
	}
	
	return true;	
}

void LPreviewPanel::OnThing(Thing *item, bool ChangeEvent)
{
	if (d->Item == item && !ChangeEvent)
		return;

    d->MissingCaps.Empty();
    DeleteObj(d->Bar);
    
	if (d->Item &&
		d->TextCtrl &&
		d->TextCtrl->IsDirty() &&
		!dynamic_cast<Html1::LHtml*>(d->TextCtrl))
	{
		Mail *m = d->Item->IsMail();
		if (m)
		{
			MailUi *Ui = dynamic_cast<MailUi*>(m->GetUI());
			bool AlreadyDirty = Ui ? Ui->IsDirty() : false;
			if (AlreadyDirty)
			{
				LgiMsg(	this,
						"Email already open and edited.\n"
						"Preview changes lost.",
						AppName);
			}
			else
			{
				m->SetBody(d->TextCtrl->Name());
				m->SetDirty();

				if (Ui)
					Ui->OnLoad();
			}
		}
	}

	d->Item = item;

	if (!d->Item || d->Item->Type() != MAGIC_MAIL)
	{
		DeleteObj(d->Header);
		DeleteObj(d->HeaderDom);
	}
	
	if (!d->Item)
	{
		DeleteObj(d->TextCtrl);
	}
	else
	{
		d->ScriptObj.Reset();

		if (d->TextCtrl &&
			d->TextCtrl->IsAttached())
		{
			d->TxtPos = d->TextCtrl->GetPos();
		}

		switch ((uint32_t)d->Item->Type())
		{
			case MAGIC_MAIL:
			{
				auto m = d->Item->IsMail();
				if (!m)
					break;

				if (!d->Header)
				{
					if (!d->HeaderMailTemplate)
					{
						char Base[] = "PreviewMail.html";
							
						d->HeaderMailFile = LFindFile("PreviewMailCustom.html");
							
						if (d->HeaderMailFile || (d->HeaderMailFile = LFindFile(Base)))
							d->HeaderMailTemplate = LFile(d->HeaderMailFile).Read();
						else
							d->HeaderMailTemplate.Printf("Failed to find '%s'", Base);
					}

					if (d->HeaderMailTemplate)
					{
						d->Header = new Html1::LHtml(100, HEADER_POS, HEADER_POS, GetPos().X()-1, d->HeaderY);
						if (d->Header)
						{
							d->HeaderDom = new ScribeDom(d->App);
							d->Header->SetEnv(d);
							d->Header->Attach(this);
							Invalidate();
						}
					}
				}

				if (!TestFlag(m->GetFlags(), MAIL_READ) && !ChangeEvent)
				{
					LVariant MarkReadAfterPreview;
					d->App->GetOptions()->GetValue(OPT_MarkReadAfterPreview, MarkReadAfterPreview);
					if (MarkReadAfterPreview.CastInt32())
					{
						d->Pulse = d->Item;
							
						LVariant Secs = 5;
						d->App->GetOptions()->GetValue(OPT_MarkReadAfterSeconds, Secs);
						if (Secs.CastInt32())
						{
							d->Time = Secs.CastInt32();
						}
						else
						{
							m->SetFlags(m->GetFlags() | MAIL_READ);
						}
					}
				}

				if (!m->CreateView(this, LString()/*mimetype*/, false, 512<<10, true))
				{
					DeleteObj(d->TextCtrl);
				}

				if (d->Header && d->HeaderMailTemplate)
				{
					if (d->HeaderDom)
						d->HeaderDom->Email = m;
						
					d->Header->Name(d->HeaderMailTemplate);
				}
				break;
			}
			case MAGIC_CONTACT:
			{
				auto c = d->Item->IsContact();
				if (!c)
					break;

				if (!d->Header)
				{
					if (!d->HeaderContactTemplate)
					{
						char Base[] = "PreviewContact.html";
						if ((d->HeaderContactFile = LFindFile(Base)))
							d->HeaderContactTemplate = LFile(d->HeaderContactFile).Read();
						else
							d->HeaderContactTemplate.Printf("Failed to find '%s'", Base);
					}

					if (d->HeaderContactTemplate)
					{
						d->Header = new Html1::LHtml(100, HEADER_POS, HEADER_POS, GetPos().X()-1, d->HeaderY);
						if (d->Header)
						{
							d->HeaderDom = new ScribeDom(d->App);
							d->Header->SetEnv(d);
							d->Header->Attach(this);
							Invalidate();
						}
					}
				}

				if (d->Header && d->HeaderContactTemplate)
				{
					if (d->HeaderDom)
					{
						d->HeaderDom->Con = c;
					}
					d->Header->Name(d->HeaderContactTemplate);
				}					
				break;
			}
			case MAGIC_GROUP:
			{
				auto g = d->Item->IsGroup();
				if (!g)
					break;

				if (!d->Header)
				{
					if (!d->HeaderGroupTemplate)
					{
						char Base[] = "PreviewGroup.html";
						if ((d->HeaderGroupFile = LFindFile(Base)))
							d->HeaderGroupTemplate = LFile(d->HeaderGroupFile).Read();
						else
							d->HeaderGroupTemplate.Printf("Failed to find '%s'", Base);
					}

					if (d->HeaderGroupTemplate)
					{
						d->Header = new Html1::LHtml(100, HEADER_POS, HEADER_POS, GetPos().X()-1, d->HeaderY);
						if (d->Header)
						{
							d->HeaderDom = new ScribeDom(d->App);
							d->Header->SetEnv(d);
							d->Header->Attach(this);
							Invalidate();
						}
					}
				}

				if (d->Header && d->HeaderGroupTemplate)
				{
					if (d->HeaderDom)
					{
						d->HeaderDom->Grp = g;
					}
					d->Header->Name(d->HeaderGroupTemplate);
				}
				break;
			}
			case MAGIC_CALENDAR:
			{
				auto c = d->Item->IsCalendar();
				if (!c)
					break;

				LStringPipe p;
				LDateTime Start, End;
				uint64 StartTs, EndTs;
				char s[256];
				if (c->GetField(FIELD_CAL_START_UTC, Start))
				{
					Start.Get(s, sizeof(s));
					Start.Get(StartTs);
					p.Print("Start: %s\n", s);

					if (c->GetField(FIELD_CAL_END_UTC, End))
					{
					    End.Get(s, sizeof(s));
    					End.Get(EndTs);
    					    
                        int Min = (int) ((EndTs - StartTs) / LDateTime::Second64Bit / 60);
                        if (Min >= 24 * 60)
                        {
                            double Days = (double)Min / 24.0 / 60.0;
    					    p.Print("End: %s (%.1f day%s)\n", s, Days, Days == 1.0 ? "" : "s");
                        }
                        else
                        {
                            int Hrs = Min / 60;
                            int Mins = Min % 60;
    					    p.Print("End: %s (%i:%02i)\n", s, Hrs, Mins);
                        }
					}
				}
					    
				const char *Str = 0;
				if (c->GetField(FIELD_CAL_SUBJECT, Str))
					p.Print("Subject: %s\n", Str);
				if (c->GetField(FIELD_CAL_LOCATION, Str))
					p.Print("Location: %s\n", Str);
				if (c->GetField(FIELD_CAL_NOTES, Str))
					p.Print("Notes: %s\n", Str);
					
				LAutoString Txt(p.NewStr());
				if (!dynamic_cast<LTextView3*>(d->TextCtrl))
					DeleteObj(d->TextCtrl);
				
				if (!d->TextCtrl)
				{
					d->TextCtrl = d->App->CreateTextControl(100, "text/plain", false);
					if (d->TextCtrl)
					{
						d->TextCtrl->Visible(false);
						d->TextCtrl->Sunken(false);
						d->TextCtrl->Attach(this);
					}
				}

				if (d->TextCtrl)
				{
					d->TextCtrl->SetReadOnly(true);
					d->TextCtrl->Name(Txt);
				}
				break;
			}
			case MAGIC_FILTER:
			{
				if (!dynamic_cast<Html1::LHtml*>(d->TextCtrl))
					DeleteObj(d->TextCtrl);
				
				if (!d->TextCtrl)
				{
					LRect c = GetClient();
					d->TextCtrl = new Html1::LHtml(100, 0, 0, c.X(), c.Y(), d);
					if (d->TextCtrl)
					{
						d->TextCtrl->Visible(false);
						d->TextCtrl->Sunken(false);
						d->TextCtrl->Attach(this);
					}
				}
				
				if (d->TextCtrl)
				{
					Filter *f = d->Item->IsFilter();
					if (f)
					{
						LAutoString Desc = f->DescribeHtml();
						if (Desc)
						{
							d->TextCtrl->Name(Desc);
							d->TextCtrl->Visible(true);	
						}
					}
				}						
				break;
			}
		}
	}

	if (d->TextCtrl)
	{
		d->TextCtrl->SetCaret(0, false);
		d->TextCtrl->UnSelectAll();
	}

	OnPosChange();
}

void LPreviewPanel::OnPulse()
{
	if (d->Time > 0)
	{
		d->Time--;
	}
	else if (d->Time == 0)
	{
		if (d->Item == d->Pulse)
		{
			Mail *m = d->Item->IsMail();
			if (m)
			{
				m->SetFlags(m->GetFlags() | MAIL_READ);
			}
		}

		d->Pulse = 0;
		d->Time = -1;
	}
}

int LPreviewPanel::OnNotify(LViewI *v, LNotification n)
{
	switch (v->GetId())
	{
		case IDC_TEXT:
		{
			if (d->Item && d->TextCtrl)
			{
				if (n.Type == LNotifyShowImagesChanged &&
					!d->IgnoreShowImgNotify)
				{
					bool LdImg = d->TextCtrl->GetLoadImages();
					if (LdImg == true)
					{
						DeleteObj(d->Bar);
						OnPosChange();
					}
				}

				Mail *m = d->Item->IsMail();
				if (m)
				{
					m->OnNotify(v, n);
				}
			}
			break;
		}
	}

	return 0;
}

