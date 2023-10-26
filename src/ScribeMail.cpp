/*
**	FILE:			ScribeMail.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			11/11/98
**	DESCRIPTION:	Scribe Mail Object and UI
**
**	Copyright (C) 1998-2003, Matthew Allen
**		fret@memecode.com
*/

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/timeb.h>
#include <math.h>

#include "Scribe.h"
#include "ScribePageSetup.h"
#include "lgi/common/Popup.h"
#include "lgi/common/ColourSelect.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/Html.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Button.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/TabView.h"
#include "lgi/common/Input.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/ThreadEvent.h"
#include "lgi/common/GdcTools.h"
#include "lgi/common/Charset.h"

#include "../src/common/Coding/ScriptingPriv.h"
#include "PrintPreview.h"
#include "ScribeListAddr.h"
#include "PrintContext.h"
#include "lgi/common/LgiRes.h"
#include "Encryption/GnuPG.h"
#include "ObjectInspector.h"
#include "lgi/common/EventTargetThread.h"
#include "Store3Common.h"
#include "Tables.h"
#include "Calendar.h"
#include "CalendarView.h"
#include "AddressSelect.h"
#include "Store3Imap/ScribeImap.h"
#include "lgi/common/TextConvert.h"
#include "lgi/common/FileSelect.h"

#include "resdefs.h"
#include "resource.h"
#include "lgi/common/Printer.h"
#include "lgi/common/SubProcess.h"

#define SAVE_HEADERS					0

static char MsgIdEncodeChars[] = "%/";
static char ScribeReplyClass[] = "scribe_reply";
static char ScribeReplyStyles[] =	"margin-left: 0.5em;\n"
									"padding-left: 0.5em;\n"
									"border-left: 1px solid #ccc;";

char DefaultTextReplyTemplate[] =
{
	"---------- Original Message ----------\n"
	"To: <mail.to[0].name> &lt;<mail.to[0].email>&gt;\n"
	"From: <mail.from.name> &lt;<mail.from.email>&gt;\n"
	"Subject: <mail.subject>\n"
	"Date: <mail.datesent>\n"
	"\n"
	"<mail.bodyastext quote=scribe.quote>\n"
	"<cursor>\n"
	"<mail.sig>\n"
};

char DefaultHtmlReplyTemplate[] =
{
	"<html>\n"
	"<head>\n"
	"<style>\n"
	"	.Style1 {\n"
	"		color: #919191;\n"
	"	}\n"
	"	.Style2 {\n"
	"		font-weight: bold;\n"
	"	}\n"
	"	.Style3 {\n"
	"		color: #ae0000;\n"
	"	}\n"
	"	</style>\n"
	"</head>\n"
	"<body>\n"
	"	<p><span class='Style1'>---------- Original Message ----------</span><br>\n"
	"	<b>To:</b> &lt;?mail.tohtml?&gt;<br>\n"
	"	<b>From:</b> &lt;?mail.fromhtml?&gt;<br>\n"
	"	<b>Subject:</b> &lt;?mail.subject?&gt;<br>\n"
	"	<b>Date:</b> &lt;?mail.datesent?&gt;<br>\n"
	"	<br>\n"
	"	<span class='Style3'>&lt;?mail.bodyastext quote=scribe.quote?&gt;</span><br>\n"
	"	&lt;?cursor?&gt;<br>\n"
	"	&lt;?mail.sig[html]?&gt;</p>\n"
	"</body>\n"
	"</html>\n"
};

class DepthCheck
{
	int &i;

public:
	constexpr static int MaxDepth = 5;

	DepthCheck(int &val) : i(val)
	{
		i++;
		if (!*this)
			LAssert(!"Recursion?");
	}

	~DepthCheck()
	{
		i--;
	}

	operator bool() const
	{
		return i < MaxDepth;
	}
};

void CollectAttachments(LArray<LDataI*> *Attachments,
						LArray<LDataI*> *Related,
						LDataI **Text,
						LDataI **Html,
						LDataPropI *d,
						Store3MimeType *ParentMime = NULL)
{
	if (!d)
		return;

	Store3MimeType Mt(d->GetStr(FIELD_MIME_TYPE));
	auto FileName = d->GetStr(FIELD_NAME);
	if (!Mt)
		Mt = "text/plain";
	
	// printf("Collect %s %s\n", (char*)Mt, FileName);
	
	auto Att = dynamic_cast<LDataI*>(d);
	if (ParentMime && Att && ParentMime->IsRelated() && Related)
	{
		auto Id = d->GetStr(FIELD_CONTENT_ID);
		if (ValidStr(Id))
		{
			if (Related)
				Related->Add(Att);
			Att = NULL;
		}
		else if (Mt.IsHtml() && Html)
		{
			*Html = Att;
			Att = NULL;
		}
	}

	if (Att)
	{
		if (ValidStr(FileName))
		{
			if (Attachments)
				Attachments->Add(Att);
		}
		else if (Mt.IsHtml())
		{
			if (Html)
				*Html = Att;
		}
		else if (Mt.IsPlainText())
		{
			if (Text)
				*Text = Att;
		}
		else if (!Mt.IsMultipart())
		{
			if (Attachments)
				Attachments->Add(Att);
		}

		/*
		if (d->GetInt(FIELD_SIZE) < (512 << 10))
			a.Add(dynamic_cast<LDataI*>(d));
		*/
	}

	auto It = d->GetList(FIELD_MIME_SEG);
	if (It)
	{
		for (auto i = It->First(); i; i = It->Next())
			CollectAttachments(Attachments, Related, Text, Html, i, Mt.IsMultipart() ? &Mt : NULL);
	}
}

void RemoveReturns(char *s)
{
	// Delete out the '\r' chars.
	char *In = s;
	char *Out = s;
	while (*In)
	{
		if (*In != '\r')
		{
			*Out++ = *In;
		}
		In++;
	}
	*Out++ = 0;
}

class XmlSaveStyles : public LXmlTree
{
	void OnParseComment(LXmlTag *Ref, const char *Comment, ssize_t Bytes)
	{
		if (Ref && Ref->IsTag("style"))
		{
			Ref->SetContent(Comment, Bytes);
		}
	}

public:
	XmlSaveStyles(int flags) : LXmlTree(flags)
	{
	}
};

bool ExtractHtmlContent(LString &OutHtml,
						LString &Charset,
						LString &Styles,
						const char *InHtml)
{
	if (!InHtml)
		return false;

	XmlSaveStyles t(GXT_NO_ENTITIES | GXT_NO_DOM | GXT_NO_HEADER);
	LXmlTag r;
	LMemStream mem(InHtml, strlen(InHtml), false);
	if (!t.Read(&r, &mem))
		return false;
	bool InHead = false;
	LStringPipe Style;

	r.Children.SetFixedLength(false);
	for (auto It = r.Children.begin(); It != r.Children.end(); )
	{
		LXmlTag *c = *It;

		if (c->IsTag("style"))
		{
			if (ValidStr(c->GetContent()))
				Style.Print("%s\n", c->GetContent());
			c->Parent = NULL;
			r.Children.Delete(It);
			DeleteObj(c);
		}
		else if (c->IsTag("/head"))
		{
			InHead = false;
			c->Parent = NULL;
			r.Children.Delete(It);
			DeleteObj(c);
		}
		else if (c->IsTag("body"))
		{
			// We remove this tag, but KEEP the content... if any
			if (ValidStr(c->GetContent()))
			{
				c->SetTag(NULL);
				It++;
			}
			else
			{
				// No content, remove entirely.
				c->Parent = NULL;
				r.Children.Delete(It);
				DeleteObj(c);
			}
		}
		else if (InHead || 
				c->IsTag("html") ||
				c->IsTag("/html") ||
				c->IsTag("/body") ||
				c->IsTag("/style"))
		{
			c->Parent = NULL;
			r.Children.Delete(It);
			DeleteObj(c);
		}
		else if (c->IsTag("head"))
		{
			InHead = true;
			c->Parent = NULL;
			r.Children.Delete(It);
			DeleteObj(c);
		}
		else
			It++;
	}

	LStringPipe p;
	t.Write(&r, &p);
	OutHtml = p.NewLStr();
	Styles = Style.NewLStr();

	#if 0
	LgiTrace("InHtml=%s\n", InHtml);
	LgiTrace("OutHtml=%s\n", OutHtml.Get());
	LgiTrace("Styles=%s\n", Styles.Get());
	#endif
	
	return true;
}

//////////////////////////////////////////////////////////////////////////////
char MailToStr[] = "mailto:";
char SubjectStr[] = "subject=";
char ContentTypeDefault[] = "Content-type: text/plain; charset=us-ascii";

extern LString HtmlToText(const char *Html, const char *InitialCharSet);
extern LString TextToHtml(const char *Txt, const char *Charset);

class ImageResizeThread : public LEventTargetThread
{
	LOptionsFile *Opts;

public:
	class Job
	{
		#ifdef __GTK_H__
		/* This object may not exist when the worker is finished.
			However LAppInst->PostEvent can handle that so we'll allow
			it, so long as it's never used in the Sink->PostEvent form.	*/
		LViewI *Sink;
		#else
		OsView Sink;
		#endif

	public:
		LString FileName;
		LAutoStreamI Data;

		void SetSink(LViewI *v)
		{
			#if !LGI_VIEW_HANDLE
			Sink = v;
			#else
			Sink = v->Handle();
			#endif
		}

		bool PostEvent(int Msg, LMessage::Param a = 0, LMessage::Param b = 0)
		{
			#ifdef __GTK_H__
			return LAppInst->PostEvent(Sink, Msg, a, b);
			#else
			return LPostEvent(Sink, Msg, a, b);
			#endif
		}
	};

	ImageResizeThread(LOptionsFile *opts) : LEventTargetThread("ImageResize")
	{
		Opts = opts;
	}

	void Resize(LAutoPtr<ImageResizeThread::Job> &Job)
	{
		LVariant Qual = 80, Px = 1024, SizeLimit = 200, v;
		Opts->GetValue(OPT_ResizeJpegQual, Qual);
		Opts->GetValue(OPT_ResizeMaxPx, Px);
		Opts->GetValue(OPT_ResizeMaxKb, SizeLimit);

		LAutoStreamI Input = Job->Data;
		LAutoStreamI MemBuf(new LMemFile(4 << 10));
		int64 FileSize = Input->GetSize();

		LStream *sImg = dynamic_cast<LStream*>(Input.Get());
		LAutoPtr<LSurface> Img(GdcD->Load(sImg, Job->FileName));
		if (Img)
		{
			int iPx = Px.CastInt32();
			int iKb = SizeLimit.CastInt32();
			
			if (Img->X() > iPx ||
				Img->Y() > iPx ||
				FileSize >= iKb << 10)
			{
				// Create a JPEG filter
				auto Jpeg = LFilterFactory::New(".jpg", FILTER_CAP_WRITE, NULL);
				if (Jpeg)
				{
					// Re-sample the image...
					double XScale = (double) Img->X() / iPx;
					double YScale = (double) Img->Y() / iPx;
					// double Aspect = (double) Img->X() / Img->Y();
					double Scale = XScale > YScale ? XScale : YScale;
					if (Scale > 1.0)
					{
						int Nx = (int)(Img->X() / Scale + 0.001);
						int Ny = (int)(Img->Y() / Scale + 0.001);
						
						LAutoPtr<LSurface> ResizedImg(new LMemDC(Nx, Ny, Img->GetColourSpace()));
						if (ResizedImg)
						{
							if (ResampleDC(ResizedImg, Img))
							{
								Img = ResizedImg;
							}
						}
					}
																
					// Compress the image..
					LXmlTag Props;
					Props.SetValue(LGI_FILTER_QUALITY, Qual);
					Props.SetValue(LGI_FILTER_SUBSAMPLE, v = 1); // 2x2
					Jpeg->Props = &Props;
					if (Jpeg->WriteImage(dynamic_cast<LStream*>(MemBuf.Get()), Img) == LFilter::IoSuccess)
					{
						Job->Data = MemBuf;
					}
				}
			}
		}
		
		Job->PostEvent(M_RESIZE_IMAGE, (LMessage::Param)Job.Get());
		Job.Release(); // Do this after the post event... so the deref doesn't crash.
	}
	
	LMessage::Result OnEvent(LMessage *Msg)
	{
		switch (Msg->Msg())
		{
			case M_RESIZE_IMAGE:
			{
				LAutoPtr<Job> j((Job*)Msg->A());
				if (j)
					Resize(j);
				break;
			}
		}
		
		return 0;
	}	
};

#include "lgi/common/RichTextEdit.h"
#include "../src/common/Widgets/Editor/RichTextEditPriv.h"

class MailRendererScript : public LThread, public LCancel, public LDom
{
	Mail *m;
	LScriptCallback *cb;

public:
	MailRendererScript(Mail *ml, LScriptCallback *script) : m(ml), cb(script), LThread("MailRendererScript")
	{
		Run();
	}

	~MailRendererScript()
	{
		Cancel(true);
		while (!IsExited())
			LSleep(1);
	}
	
	const char *GetClass() override { return "MailRendererScript"; }

	int Main() override
	{
		LScriptArguments Args(NULL);
		Args.New() = new LVariant(m->App);
		Args.New() = new LVariant(m);
		Args.New() = new LVariant((LDom*)this);
		m->App->ExecuteScriptCallback(*cb, Args);
		return 0;
	}

	bool CallMethod(const char *MethodName, LScriptArguments &Args) override
	{
		ScribeDomType Method = StrToDom(MethodName);

		*Args.GetReturn() = false;
		switch (Method)
		{		
			case SdGetTemp:
			{
				*Args.GetReturn() = ScribeTempPath();
				break;
			}
			case SdExecute:
			{
				*Args.GetReturn() = false;
				if (Args.Length() >= 3)
				{
					auto Dir = Args[0]->Str();
					auto Exe = Args[1]->Str();
					auto Arg = Args[2]->Str();
					if (Dir && Exe && Arg)
					{
						LSubProcess p(Exe, Arg);
						p.SetInitFolder(Dir);
						if (p.Start())
						{
							char Buf[256];
							LStringPipe Out;
							Out.Print("%s %s\n", Exe, Arg);

							ssize_t Rd;
							while (p.IsRunning() && !IsCancelled())
							{
								Rd = p.Read(Buf, sizeof(Buf));
								if (Rd < 0)
									break;
								if (Rd == 0)
									LSleep(1);
								else
									Out.Write(Buf, Rd);
							}

							while (!IsCancelled() && (Rd = p.Read(Buf, sizeof(Buf))) > 0)
								Out.Write(Buf, Rd);

							// LgiTrace("%s:%i - Process:\n%s\n", _FL, Out.NewLStr().Get());
							if (p.IsRunning())
								p.Kill();
							else
								*Args.GetReturn() = true;
						}
					}
				}
				break;
			}
			case SdSetHtml:
			{
				if (!IsCancelled() && Args.Length() > 0)
				{
					LView *Ui = m->GetUI();
					if (!Ui)
					{
						// Maybe hasn't finished opening the UI?
						LSleep(100);
						Ui = m->GetUI();
					}
					if (!Ui)
						Ui = m->App;
					if (Ui)
						Ui->PostEvent(M_SET_HTML, (LMessage::Param)new LString(Args[0]->Str()));
				}
				break;
			}
			default:
				LAssert(!"Unsupported call.");
				return false;
		}

		return true;
	}
};

class MailPrivate
{
	LAutoPtr<ImageResizeThread> Resizer;
	Mail *m;

public:
	struct HtmlBody
	{
		LString Html, Charset, Styles;
	};
	LAutoPtr<HtmlBody> Body;
	LAutoPtr<MailRendererScript> Renderer;
	LString MsgIdCache;
	LString DomainCache;
	int InSetFlags = 0;
	int InSetFlagsCache = 0;
	
	MailPrivate(Mail *mail)
	{
		m = mail;
	}
	
	void OnSave()
	{
		Resizer.Reset();
	}

	HtmlBody *GetBody()
	{
		if (!Body && !Body.Reset(new HtmlBody))
			return NULL;
		
		if (ValidStr(m->GetHtml()))
		{
			ExtractHtmlContent(	Body->Html,
								Body->Charset,
								Body->Styles,
								m->GetHtml());
		}
		else if (ValidStr(m->GetBody()))
		{
		    Body->Charset = m->GetCharSet();
			Body->Html = TextToHtml(m->GetBody(), Body->Charset);
		}
		
		return Body;
	}

	bool AddResizeImage(Attachment *File)
	{
		if (!File || !m->GetUI())
			return false;
		
		if (!Resizer)
			Resizer.Reset(new ImageResizeThread(m->App->GetOptions()));
		
		if (!Resizer)
			return false;

		LAutoStreamI Obj = File->GetObject()->GetStream(_FL);
		if (!Obj)
			return false;

		ImageResizeThread::Job *j = new ImageResizeThread::Job;
		if (!j)
			return false;

		// The user interface will handle the response...				
		j->SetSink(m->GetUI());
		j->FileName = File->GetName();					

		// Make a complete copy of the stream...
		j->Data.Reset(new LMemStream(Obj, 0, -1));
	
		// Post the work over to the thread...
		Resizer->PostEvent(M_RESIZE_IMAGE, (LMessage::Param)j);

		// Mark the image resizing						
		File->SetIsResizing(true);
		
		return true;
	}	
};

bool Mail::ResizeImage(Attachment *a)
{
	return d->AddResizeImage(a);
}

AttachmentList::AttachmentList(int id, int x, int y, int cx, int cy, MailUi *ui)
	: LList(id, x, y, cx, cy, 0)
{
	Ui = ui;
}

AttachmentList::~AttachmentList()
{
	RemoveAll();
}

void AttachmentList::OnItemClick(LListItem *Item, LMouse &m)
{
	LList::OnItemClick(Item, m);

	if (!Item &&
		m.IsContextMenu() &&
		Ui)
	{
		LSubMenu RClick;
		RClick.AppendItem(LLoadString(IDS_ATTACH_FILE), IDM_OPEN, true);
		switch (RClick.Float(this, m))
		{
			case IDM_OPEN:
			{
				Ui->PostEvent(M_COMMAND, IDM_ATTACH_FILE, 0);
				break;
			}
		}
	}
}

bool AttachmentList::OnKey(LKey &k)
{
	if (k.vkey == LK_DELETE)
	{
		if (k.Down())
		{
			List<LListItem> s;
			if (GetSelection(s))
			{
				Attachment *a = dynamic_cast<Attachment*>(s[0]);
				if (a)
				{
					a->OnDeleteAttachment(this, true);
				}
			}
		}
		
		return true;
	}
	
	return LList::OnKey(k);
}

//////////////////////////////////////////////////////////////////////////////
int Strnlen(const char *s, int n)
{
	int i = 0;
	if (s)
	{
		if (n < 0)
		{
			while (*s++)
			{
				i++;
			}
		}
		else
		{
			while (*s++ && n-- > 0)
			{
				i++;
			}
		}
	}

	return i;
}

//////////////////////////////////////////////////////////////////////////////
MailContainer::~MailContainer()
{
	MailContainerIter *i;
	while ((i = Iters[0]))
	{
		Iters.Delete(i);
		if (i->Container)
		{
			i->Container = 0;
		}
	}
}

MailContainerIter::MailContainerIter()
{
	Container = 0;
}

MailContainerIter::~MailContainerIter()
{
	if (Container)
	{
		Container->Iters.Delete(this);
	}
}

void MailContainerIter::SetContainer(MailContainer *c)
{
	Container = c;
	if (Container)
	{
		Container->Iters.Insert(this);
	}
}

//////////////////////////////////////////////////////////////////////////////
uint32_t MarkColours32[IDM_MARK_MAX] =
{
	Rgb32(255, 0, 0),	// red
	Rgb32(255, 166, 0),	// orange
	Rgb32(255, 222, 0),	// yellow
	Rgb32(0, 0xa0, 0),	// green
	Rgb32(0, 0xc0, 255),// cyan
	Rgb32(0, 0, 255),	// blue
	Rgb32(192, 0, 255),	// purple
	Rgb32(128, 128, 128) // grey
};

ItemFieldDef MailFieldDefs[] =
{
	{"To", SdTo,							GV_STRING,		FIELD_TO},
	{"From", SdFrom,						GV_STRING,		FIELD_FROM},
	{"Subject", SdSubject,					GV_STRING,		FIELD_SUBJECT},
	{"Size", SdSize,						GV_INT64,		FIELD_SIZE},
	{"Received Date", SdReceivedDate,		GV_DATETIME,	FIELD_DATE_RECEIVED},
	{"Send Date", SdSendDate,				GV_DATETIME,	FIELD_DATE_SENT},
	{"Body", SdBody,						GV_STRING,		FIELD_TEXT},
	{"Internet Header", SdInternetHeader,	GV_STRING,		FIELD_INTERNET_HEADER, IDC_INTERNET_HEADER},
	{"Message ID", SdMessageId, 			GV_STRING,		FIELD_MESSAGE_ID},
	{"Priority", SdPriority, 				GV_INT32,		FIELD_PRIORITY},
	{"Flags", SdFlags, 						GV_INT32,		FIELD_FLAGS},
	{"Html", SdHtml, 						GV_STRING,		FIELD_ALTERNATE_HTML},
	{"Label", SdLabel, 						GV_STRING,		FIELD_LABEL},
	{"From Contact", SdContact,				GV_STRING,		FIELD_FROM_CONTACT_NAME},
	
	{"File", SdFile, 						GV_STRING,		FIELD_CACHE_FILENAME},
	{"ImapFlags", SdImapFlags, 				GV_STRING,		FIELD_CACHE_FLAGS},
	{"ImapSeq", SdFile, 					GV_INT32,		FIELD_IMAP_SEQ},
	{"ImapUid", SdImapFlags, 				GV_INT32,		FIELD_SERVER_UID},
	{"ReceivedDomain", SdReceivedDomain,	GV_STRING,		FIELD_RECEIVED_DOMAIN},
	{"MessageId", SdMessageId,				GV_STRING,		FIELD_MESSAGE_ID},
	
	{0}
};

//////////////////////////////////////////////////////////////////////////////
class LIdentityItem : public LListItem
{
	ScribeWnd *App;
	ScribeAccount *Acc;
	char *Txt;

public:
	LIdentityItem(ScribeWnd *app, ScribeAccount *acc)
	{
		App = app;
		Acc = acc;
		Txt = 0;

		LVariant e, n;
		if (Acc)
		{
			n = Acc->Identity.Name();
			e = Acc->Identity.Email();
		}
		else
		{
			LAssert(!"No account specified");
		}

		if (e.Str() && n.Str())
		{
			char t[256];
			sprintf_s(t, sizeof(t), "%s <%s>", n.Str(), e.Str());
			Txt = NewStr(t);
		}
		else if (e.Str())
		{
			Txt = NewStr(e.Str());
		}
		else if (n.Str())
		{
			Txt = NewStr(n.Str());
		}
		else
		{
			Txt = NewStr("(error)");
		}
	}
	
	~LIdentityItem()
	{
		DeleteArray(Txt);
	}

	ScribeAccount *GetAccount()
	{
		return Acc;
	}

	const char *GetText(int i)
	{
		switch (i)
		{
			case 0:
			{
				return Txt;
				break;
			}
		}

		return 0;
	}
};

class LIdentityDropDrop : public LPopup
{
	ScribeWnd *App;
	Mail *Email;
	LList *Lst;

public:
	LIdentityDropDrop(ScribeWnd *app, Mail *mail, LView *owner) :
		LPopup(owner)
	{
		App = app;
		Email = mail;
		LRect r(0, 0, 300, 100);
		SetPos(r);
		Children.Insert(Lst = new LList(IDC_LIST, 2, 2, X()-4, Y()-4));
		if (Lst)
		{
			Lst->SetParent(this);
			Lst->AddColumn("Identity", Lst->GetClient().X());
			Lst->MultiSelect(false);

			if (App)
			{
				Lst->Insert(new LIdentityItem(App, 0));

				for (auto a : *App->GetAccounts())
				{
					if (a->Identity.Name().Str())
					{
						Lst->Insert(new LIdentityItem(App, a));
					}
				}
				
				/*
				for (LListItem *i = List->First(); i; i = List->Next())
				{
					LIdentityItem *Item = dynamic_cast<LIdentityItem*>(i);
					if (Item)
					{
						char *IdEmail = a->Send.IdentityEmail();
						if (Email &&
							IdEmail &&
							Email->From->Addr &&
							_stricmp(Email->From->Addr, IdEmail) == 0)
						{
							Item->Select(true);
						}
					}
				}
				*/
			}
		}
	}

	void OnPaint(LSurface *pDC)
	{
		LRect r(GetClient());
		LWideBorder(pDC, r, DefaultRaisedEdge);
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_LIST:
			{
				if (n.Type == LNotifyItemClick)
				{
					Visible(false);

					LIdentityItem *NewFrom = dynamic_cast<LIdentityItem*>(Lst->GetSelected());
					if (Email && NewFrom)
					{
						// ScribeAccount *a = NewFrom->GetAccount();
						if (Email->GetUI())
						{
							LList *FromList;
							if (GetViewById(IDC_FROM, FromList))
							{
								// Change item data

								/* FIXME
								DeleteArray(Email->From->Name);
								DeleteArray(Email->From->Addr);
								DeleteArray(Email->Reply->Name);
								DeleteArray(Email->Reply->Addr);
								
								if (a)
								{
									Email->From->Addr = NewStr(a->Send.IdentityEmail().Str());
									Email->From->Name = NewStr(a->Send.IdentityName().Str());
									Email->Reply->Addr = NewStr(a->Send.IdentityReplyTo().Str());
									if (Email->Reply->Addr)
									{
										Email->Reply->Name = NewStr(Email->From->Name);
									}
								}
								else
								{
									LVariant e, n, r;
									App->GetOptions()->GetValue(OPT_EmailAddr, e);
									App->GetOptions()->GetValue(OPT_UserName, n);
									App->GetOptions()->GetValue(OPT_ReplyToEmail, n);
									Email->From->Addr = NewStr(e.Str());
									Email->From->Name = NewStr(n.Str());
									Email->Reply->Addr = NewStr(r.Str());
									if (Email->Reply->Addr)
									{
										Email->Reply->Name = NewStr(Email->From->Name);
									}
								}

								// Change UI
								FromList->Empty();
								LDataPropI *na = new LDataPropI(Email->From);
								if (na)
								{
									na->CC = MAIL_ADDR_FROM;
									FromList->Insert(na);
								}
								*/
							}
						}
					}
				}
				break;
			}
		}

		return 0;
	}
};

//////////////////////////////////////////////////////////////////////////////
LSubMenu*
BuildMarkMenu(	LSubMenu *MarkMenu,
				MarkedState MarkState,
				uint32_t SelectedMark,
				bool None,
				bool All,
				bool Select)
{
	int SelectedIndex = -1;

	// Build image list
	LImageList *ImgLst = new LImageList(16, 16);
	if (ImgLst &&
		ImgLst->Create(16 * CountOf(MarkColours32), 16, System32BitColourSpace))
	{
		// ImgLst->Colour(1);
		ImgLst->Colour(L_MED);
		ImgLst->Rectangle();

		for (int i=0; i<CountOf(MarkColours32); i++)
		{
			if (MarkState == MS_One &&
				MarkColours32[i] == SelectedMark)
			{
				ImgLst->Colour(L_LOW);
				ImgLst->Box(i*16+1, 0, i*16+15, 14);
				SelectedIndex = i;
			}

			ImgLst->Colour(MarkColours32[i], 32);
			ImgLst->Rectangle(i*16+3, 2, i*16+13, 12);
		}
	}

	// Build Submenu
	if (MarkMenu && ImgLst)
	{
		ImgLst->Update(-1);
		MarkMenu->SetImageList(ImgLst);

		LMenuItem *Item = NULL;
		if (None)
		{
			Item = MarkMenu->AppendItem(LLoadString(IDS_NONE), Select ? IDM_SELECT_NONE : IDM_UNMARK, MarkState != MS_None);
		}
		if (All)
		{
			Item = MarkMenu->AppendItem(LLoadString(IDS_ALL), Select ? IDM_SELECT_ALL : IDM_MARK_ALL, true);
		}
		if (Item)
		{
			MarkMenu->AppendSeparator();
		}

		for (int i=0; i<CountOf(MarkColours32); i++)
		{
			char s[32];
			sprintf_s(s, sizeof(s), " (%i, %i, %i)", R32(MarkColours32[i]), G32(MarkColours32[i]), B32(MarkColours32[i]));
			Item = MarkMenu->AppendItem(s,
										((Select) ? IDM_MARK_SELECT_BASE : IDM_MARK_BASE) + i,
										(MarkState != 1) || (i != SelectedIndex));
			if (Item)
			{
				Item->Icon(i);
			}
		}
	}

	return MarkMenu;
}

//////////////////////////////////////////////////////////////////////////////
char *WrapLines(char *Str, int Len, int WrapColumn)
{
	if (Str &&
		Len > 0 &&
		WrapColumn > 0)
	{
		LMemQueue Temp;
		int LastWhite = -1;
		int StartLine = 0;
		int XPos = 0;
		int i;

		for (i=0; Str[i] && i<Len; i++)
		{
			if (Str[i] == ' ')
			{
				int Len = LastWhite - StartLine;
				if (XPos >= WrapColumn && Len > 0)
				{
					Temp.Write((uchar*) Str+StartLine, Len);
					Temp.Write((uchar*) "\n", 1);
					XPos = 0;
					StartLine = StartLine + Len + 1;
					LastWhite = -1;
				}
				else
				{
					LastWhite = i;
					XPos++;
				}
			}
			else if (Str[i] == '\t')
			{
				XPos = ((XPos + 7) / 8) * 8;
			}
			else if (Str[i] == '\n')
			{
				Temp.Write((uchar*) Str+StartLine, i - StartLine + 1);
				XPos = 0;
				StartLine = i+1;
			}
			else
			{
				XPos++;
			}
		}

		Temp.Write((uchar*) Str+StartLine, i - StartLine + 1);

		int WrapLen = (int)Temp.GetSize();
		char *Wrapped = new char[WrapLen+1];
		if (Wrapped)
		{
			Temp.Read((uchar*) Wrapped, WrapLen);
			Wrapped[WrapLen] = 0;

			return Wrapped;
		}
	}

	return Str;
}

char *DeHtml(const char *Str)
{
	char *r = 0;
	if (Str)
	{
		LMemQueue Buf;
		char Buffer[256];

		auto s = Str;
		while (s && *s)
		{
			// Search for start of next tag
			const char *Start = s;
			const char *End = Start;
			for (; *End && *End != '<'; End++);
			
			// Push pre-tag data onto pipe
			size_t Len = End-Start;
			for (size_t i=0; i<Len; )
			{
				size_t n;
				for (n=0; n<sizeof(Buffer) && i<Len; i++)
				{
					if (Start[i] != '\n' &&
						Start[i] != '\r' &&
						Start[i] != '\t')
					{
						if (_strnicmp(Start+i, "&nbsp", 5) == 0)
						{
							Buffer[n++] = ' ';
							i += 5;
							if (Start[i] != ';') i--;
						}
						else if (_strnicmp(Start+i, "&copy", 4) == 0)
						{
							Buffer[n++] = 'c';
							i += 5;
							if (Start[i] != ';') i--;
						}
						else
						{
							Buffer[n++] = Start[i];
						}
					}
					else if (Start[i] == '\n')
					{
						Buffer[n++] = ' ';
					}
				}

				Buf.Write((uchar*)Buffer, n);
			}

			if (*End == '<')
			{
				char Temp[32];
				int n;
				End++;
				for (n=0;
					n<sizeof(Temp)-1 &&
					End[n] &&
					End[n] != ' ' &&
					End[n] != '>';
					n++)
				{
					Temp[n] = End[n];
				}
				Temp[n] = 0;

				if (_stricmp(Temp, "br") == 0 ||
					_stricmp(Temp, "li") == 0)
				{
					Buf.Write((uchar*) "\n", 1);
				}
				else if (_stricmp(Temp, "p") == 0 ||
						 _stricmp(Temp, "/tr") == 0 ||
						 _stricmp(Temp, "/ul") == 0)
				{
					Buf.Write((uchar*) "\n\n", 2);
				}
			}

			// Start to end of tag
			for (; *End && *End != '>'; End++);
			s = End + 1;
		}

		int Len = (int)Buf.GetSize();
		r = new char[Len+1];
		if (r)
		{
			Buf.Read((uchar*)r, Len);
			r[Len] = 0;
		}
	}

	return r;
}

char *ExtractCodePage(char *ContentType)
{
	// int Status = -1;

	if (ContentType)
	{
		const char *Match = "charset=";
		char *Charset = stristr(ContentType, Match);
		if (Charset)
		{
			Charset += strlen(Match);
			char Delim = ' ';
			if (*Charset == '\'' ||
				*Charset == '\"')
			{
				Delim = *Charset++;
			}

			char Str[256];
			char *Out = Str;
			while (	*Charset &&
					*Charset != Delim &&
					*Charset != ';')
			{
				*Out++ = *Charset++;
			}
			*Out++ = 0;

			return ValidStr(Str) ? NewStr(Str) : 0;
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
extern ItemFieldDef CalendarFields[];
extern ItemFieldDef FilterFieldDefs[];
extern ItemFieldDef GroupFieldDefs[];

ItemFieldDef *FieldLists[] =
{
	MailFieldDefs,
	ContactFieldDefs,
	CalendarFields,
	FilterFieldDefs,
	GroupFieldDefs,
	0
};

ItemFieldDef *GetFieldDefByName(char *Name)
{
	if (Name)
	{
		static LHashTbl<ConstStrKey<char,false>, ItemFieldDef*> Lut(256);
		if (Lut.Length() == 0)
		{
			for (ItemFieldDef **l=FieldLists; *l; l++)
			{
				for (ItemFieldDef *i = *l; i->FieldId; i++)
				{
					if (i->Option)
						Lut.Add(i->Option, i);
				}
			}
		}

		return (ItemFieldDef*) Lut.Find(Name);
	}

	return 0;
}

static bool FieldLutInit = false;
static LArray<ItemFieldDef*> IdToFieldLut;

ItemFieldDef *GetFieldDefById(int Id)
{
	if (!FieldLutInit)
	{
		FieldLutInit = true;

		for (ItemFieldDef **l=FieldLists; *l; l++)
		{
			for (ItemFieldDef *i = *l; i->FieldId; i++)
			{
				IdToFieldLut[i->FieldId] = i;
			}
		}
	}

	if (Id >= 0 && Id < (int)IdToFieldLut.Length())
		return IdToFieldLut[Id];

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
void Log(char *File, char *Str, ...)
{
	#if defined WIN32
	const char *DefFile = "c:\\temp\\list.txt";
	#else
	const char *DefFile = "/home/list.txt";
	#endif

	if (Str)
	{
		LFile f;
		if (f.Open((File) ? File : DefFile, O_WRITE))
		{
			char Buf[1024];
			va_list Arg;

			va_start(Arg, Str);
			vsprintf_s(Buf, sizeof(Buf), Str, Arg);
			va_end(Arg);

			f.Seek(0, SEEK_END);
			f.Write(Buf, (int)strlen(Buf));
		}
	}
	else
	{
		LFile f;
		if (f.Open((File) ? File : DefFile, O_WRITE))
		{
			f.SetSize(0);
		}
	}
}

char *NewPropStr(LOptionsFile *Options, char *Name)
{
	LVariant n;
	if (Options->GetValue(Name, n) && n.Str() && strlen(n.Str()) > 0)
	{
		return NewStr(n.Str());
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Columns of controls
#define MAILUI_Y						0

#define	IDM_REMOVE_GRTH					1000
#define IDM_REMOVE_GRTH_SP				1001
#define IDM_REMOVE_HTML					1002
#define IDM_CONVERT_B64_TO_BIN			1003
#define IDM_CONVERT_BIN_TO_B64			1004

#define RECIP_SX						500
#define ADD_X							(RECIP_X + RECIP_SX + 10)
#ifdef MAC
#define	ADD_RECIP_BTN_X					36
#else
#define	ADD_RECIP_BTN_X					20
#endif

#define CONTENT_BORDER					3

#if defined WIN32

	#define DLG_X						15
	#define DLG_Y						30

#else

	#define DLG_X						6
	#define DLG_Y						6

#endif

MailUi::MailUi(Mail *item, MailContainer *container) :
	ThingUi(item, LLoadString(IDS_MAIL_MESSAGE))
{
	// Init everything to 0
	Container = container;
	if (!item || !item->App)
	{
		LAssert(!"Invalid ptrs");
		return;
	}

    // This allows us to hook iconv conversion events
    LFontSystem::Inst()->Register(this);
    
    // This allows us to hook missing image library events
    GdcD->Register(this);

	// Read/Write access
	bool ReadOnly = !TestFlag(GetItem()->GetFlags(), MAIL_CREATED | MAIL_BOUNCE);
	int MinButY = 0;

	// Get position
	LRect r(150, 150, 800, 750);
	SetPos(r);
	int FontHeight = GetFont()->GetHeight();

	LVariant v;
	LOptionsFile *Options = App ? App->GetOptions() : 0;
	if (Options)
	{
		if (Options->GetValue("MailUI.Pos", v))
		{
			r.SetStr(v.Str());
		}
	}
	SetPos(r);
	MoveSameScreen(App);

	Name(LLoadString(IDS_MAIL_MESSAGE));

	#if WINNATIVE
	CreateClassW32("Scribe::MailUi", LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_MAIL)));
	#endif
	bool IsCreated = TestFlag(GetItem()->GetFlags(), MAIL_CREATED);

	if (Attach(0))
	{
		DropTarget(true);

		// Setup main toolbar
		Commands.Toolbar = App->LoadToolbar(this,
											App->GetResourceFile(ResToolbarFile),
											App->GetToolbarImgList());
		if (Commands.Toolbar)
		{
			Commands.Toolbar->Raised(false);
			Commands.Toolbar->Attach(this);

			BtnSend = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SEND)), IDM_SEND_MSG, TBT_PUSH, !ReadOnly, IMG_SEND);
			Commands.Toolbar->AppendSeparator();

			BtnSave = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SAVE)), IDM_SAVE, TBT_PUSH, !ReadOnly, IMG_SAVE);
			BtnSaveClose = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SAVE_CLOSE)), IDM_SAVE_CLOSE, TBT_PUSH, !ReadOnly, IMG_SAVE_AND_CLOSE);
			Commands.Toolbar->AppendSeparator();

			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_DELETE)), IDM_DELETE_MSG, TBT_PUSH, true, IMG_TRASH);
			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SPAM)), IDM_DELETE_AS_SPAM, TBT_PUSH, true, IMG_DELETE_SPAM);
			BtnAttach = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_ATTACH_FILE)), IDM_ATTACH_FILE, TBT_PUSH, !ReadOnly, IMG_ATTACH_FILE);
			Commands.Toolbar->AppendSeparator();

			BtnReply = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_REPLY)), IDM_REPLY, TBT_PUSH, ReadOnly, IMG_REPLY);
			BtnReplyAll = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_REPLYALL)), IDM_REPLY_ALL, TBT_PUSH, ReadOnly, IMG_REPLY_ALL);
			BtnForward = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_FORWARD)), IDM_FORWARD, TBT_PUSH, ReadOnly, IMG_FORWARD);
			BtnBounce = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_BOUNCE)), IDM_BOUNCE, TBT_PUSH, ReadOnly, IMG_BOUNCE);
			Commands.Toolbar->AppendSeparator();

			BtnPrev = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_PREV_MSG)), IDM_PREV_MSG, TBT_PUSH, true, IMG_PREV_ITEM);
			BtnNext = Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_NEXT_MSG)), IDM_NEXT_MSG, TBT_PUSH, true, IMG_NEXT_ITEM);
			Commands.Toolbar->AppendSeparator();

			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_HIGH_PRIORITY)), IDM_HIGH_PRIORITY, TBT_TOGGLE, true, IMG_HIGH_PRIORITY);
			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_LOW_PRIORITY)), IDM_LOW_PRIORITY, TBT_TOGGLE, true, IMG_LOW_PRIORITY);
			Commands.Toolbar->AppendSeparator();

			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_READ_RECEIPT)), IDM_READ_RECEIPT, TBT_TOGGLE, true, IMG_READ_RECEIPT);
			Commands.Toolbar->AppendSeparator();

			Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_PRINT)), IDM_PRINT, TBT_PUSH, true, IMG_PRINT);

			for (LViewI *w: Commands.Toolbar->IterateViews())
				MinButY = MAX(MinButY, w->Y());

			Commands.Toolbar->Customizable(App->GetOptions(), "MailWindowToolbar");
			Commands.SetupCallbacks(App, this, GetItem(), LThingUiToolbar);
		}

		// Setup to/from panels
		LVariant AlwaysShowFrom;
		if (IsCreated)
			App->GetOptions()->GetValue(OPT_MailShowFrom, AlwaysShowFrom);

		bool ShowFrom = !IsCreated &&
						!TestFlag(GetItem()->GetFlags(), MAIL_BOUNCE);

		LDisplayString Recip(LSysFont, LLoadString(IDS_RECIPIENTS));
		LAutoPtr<LCheckBox> ReplyChk(new LCheckBox(IDC_USE_REPLY_TO, 21,
													#ifdef MAC
													1,
													#else
													6,
													#endif
													-1, -1, "Reply To"));
		int ReplyChkPx = ReplyChk ? ReplyChk->X() : 0;
		int RECIP_X = 20 + MAX(ReplyChkPx, Recip.X()) + 10;
		int EditHeight = FontHeight + 6;

		LVariant HideGpg;
		if (!item->App->GetOptions()->GetValue(OPT_HideGnuPG, HideGpg) ||
			HideGpg.CastInt32() == 0)
		{
			GpgUi = new MailUiGpg(	App,
									this,
									21,
									RECIP_X,
									!ReadOnly && !TestFlag(GetItem()->GetFlags(), MAIL_SENT));
			if (GpgUi)
				GpgUi->Attach(this);
		}
		
		ToPanel = new LPanel(LLoadString(IDS_TO), FontHeight * 8, !ShowFrom);
		if (ToPanel)
		{
		    int Cy = 4;
			ToPanel->AddView(SetTo = new LCombo(IDC_SET_TO, 20, Cy, 60, EditHeight, 0));
			if (SetTo)
			{
				SetTo->Insert(LLoadString(IDS_TO));
				SetTo->Insert(LLoadString(IDS_CC));
				SetTo->Insert(LLoadString(IDS_BCC));
				
				if (SetTo->GetPos().x2 + 10 > RECIP_X)
				    RECIP_X = SetTo->GetPos().x2 + 10;
			}
			
			ToPanel->AddView(Entry = new LEdit(IDC_ENTRY, RECIP_X, Cy, RECIP_SX, EditHeight, ""));
			
			Cy = SetTo->GetPos().y2 + 5;

			ToPanel->AddView(To = new AddressList(App, IDC_TO, RECIP_X, Cy, RECIP_SX, ToPanel->GetOpenSize() - Cy - 6));
			if (To)
				To->SetImageList(App->GetIconImgList(), false);
			ToPanel->AddView(new LTextLabel(IDC_STATIC, 20, Cy, -1, -1, LLoadString(IDS_RECIPIENTS)));
			
			ToPanel->Raised(false);
			ToPanel->Attach(this);
		}

		FromPanel = new LPanel(LLoadString(IDS_FROM), FontHeight + 13, AlwaysShowFrom.CastInt32() || ShowFrom);
		if (FromPanel)
		{
			FromPanel->AddView(new LTextLabel(IDC_STATIC, 21,
										#ifdef MAC
										8,
										#else
										4,
										#endif
										-1, -1, LLoadString(IDS_FROM)));

			if (ShowFrom)
			{
				FromPanel->AddView(FromList = new AddressList(App, IDC_FROM, RECIP_X, 2, RECIP_SX, EditHeight));
			}
			else
			{
				FromPanel->AddView(FromCbo = new LCombo(IDC_FROM, RECIP_X, 2, RECIP_SX, EditHeight, 0));
			}

			if (IsCreated)
			{
				LCheckBox *Chk;
				auto Label = LLoadString(IDS_ALWAYS_SHOW);
				FromPanel->AddView(Chk = new LCheckBox(IDC_SHOW_FROM, ADD_X,
														#ifdef MAC
														1,
														#else
														6,
														#endif
														-1, -1, Label));
				if (Chk) Chk->Value(AlwaysShowFrom.CastInt32());
			}

			FromPanel->Raised(false);
			FromPanel->Attach(this);
		}

		ReplyToPanel = new LPanel("Reply To:", FontHeight + 13, false);
		if (ReplyToPanel)
		{
			ReplyToPanel->Raised(false);
			ReplyToPanel->AddView(ReplyToChk = ReplyChk.Release());
			ReplyToPanel->AddView(ReplyToCbo = new LCombo(IDC_REPLY_TO_ADDR, RECIP_X, 2, RECIP_SX, EditHeight, 0));
			ReplyToPanel->Attach(this);
		}

		SubjectPanel = new LPanel(LLoadString(IDS_SUBJECT), -(LSysFont->GetHeight() + 16));
		if (SubjectPanel)
		{
			SubjectPanel->Raised(false);
			SubjectPanel->AddView(			new LTextLabel(IDC_STATIC, 21, 8, -1, -1, LLoadString(IDS_SUBJECT)));
			SubjectPanel->AddView(Subject = new LEdit(IDC_SUBJECT, RECIP_X, 4, RECIP_SX, EditHeight, ""));
			SubjectPanel->Attach(this);
		}

		CalendarPanel = new LPanel(LLoadString(IDC_CALENDAR), -(LSysFont->GetHeight() + 16), false);
		if (CalendarPanel)
		{
			CalendarPanel->Raised(false);
			LTableLayout *t = new LTableLayout(IDC_TABLE);
			if (t)
			{
				t->GetCss(true)->Margin(LCss::Len(LCss::LenPx, LTableLayout::CellSpacing));
				t->GetCss()->Width(LCss::LenAuto);
				t->GetCss()->Height(LCss::LenAuto);

				CalendarPanel->AddView(t);
				auto c = t->GetCell(0, 0);
				
				c->Width(LCss::Len(LCss::LenPx, RECIP_X - (LTableLayout::CellSpacing * 2)));
				c->PaddingLeft(LCss::Len(LCss::LenPx, 21 - LTableLayout::CellSpacing));
				c->Debug = true;

				c->Add(new LTextLabel(IDC_STATIC, 0, 0, -1, -1, LLoadString(IDS_CALENDAR)));
				c = t->GetCell(1, 0);
				c->Add(new LButton(IDC_ADD_CAL_EVENT, 0, 0, -1, -1, LLoadString(IDS_ADD_CAL)));
				c = t->GetCell(2, 0);
				c->Add(new LButton(IDC_ADD_CAL_EVENT_POPUP, 0, 0, -1, -1, LLoadString(IDS_ADD_CAL_POPUP)));
				c = t->GetCell(3, 0);
				c->Add(CalPanelStatus = new LTextLabel(IDC_STATIC, 0, 0, -1, -1, NULL));
			}

			CalendarPanel->Attach(this);
		}

		Tab = new LTabView(IDC_MAIL_UI_TABS, 0, 0, 1000, 1000, 0);
		if (Tab)
		{
			Tab->GetCss(true)->PaddingTop("3px");

			// Don't attach text and html controls here, because OnLoad will do it later...
			TabText = Tab->Append(LLoadString(IDS_TEXT));
			TabHtml = Tab->Append("HTML");

			TabAttachments = Tab->Append(LLoadString(IDS_ATTACHMENTS));
			TabHeader = Tab->Append(LLoadString(IDS_INTERNETHEADER));

			if (TabAttachments)
			{
				TabAttachments->Append(Attachments = new AttachmentList(IDC_ATTACHMENTS,
																		CONTENT_BORDER,
																		CONTENT_BORDER,
																		200,
																		200,
																		this));

				if (Attachments)
				{
					Attachments->AddColumn(LLoadString(IDS_FILE_NAME), 160);
					Attachments->AddColumn(LLoadString(IDS_SIZE), 80);
					Attachments->AddColumn(LLoadString(IDS_MIME_TYPE), 100);
					Attachments->AddColumn(LLoadString(IDS_CONTENT_ID), 250);
				}
			}

			if (TabHeader)
			{
				TabHeader->Append(Header = new LTextView3(	IDC_INTERNET_HEADER,
															CONTENT_BORDER,
															CONTENT_BORDER,
															100,
															20));
				if (Header)
				{
					Header->Sunken(true);
				}
			}

			LTabPage *Fields = Tab->Append(LLoadString(IDS_FIELDS));
			if (Fields)
			{
				Fields->LoadFromResource(IDD_MAIL_FIELDS);
				
				LTableLayout *l;
				if (GetViewById(IDC_TABLE, l))
				{
				    l->SetPourLargest(false);
				}
			}

			Tab->Attach(this);

			// Colours
			LColourSelect *Colour;
			if (GetViewById(IDC_COLOUR, Colour))
			{
				LArray<LColour> c32;
				for (int i=0; i<CountOf(MarkColours32); i++)
					c32.New().Set(MarkColours32[i], 32);
				Colour->SetColourList(&c32);
			}
			else LAssert(!"No colour control?");
		}

		PourAll();

		if (Commands.Toolbar)
		{
			if (ToPanel)
				ToPanel->SetClosedSize(Commands.Toolbar->Y()-1);
			if (FromPanel)
				FromPanel->SetClosedSize(Commands.Toolbar->Y()-1);
			if (ReplyToPanel)
				ReplyToPanel->SetClosedSize(Commands.Toolbar->Y()-1);
		}

		SetIcon("About64px.png");
		OnLoad();
		_Running = true;
		Visible(true);
		RegisterHook(this, LKeyEvents);
		LResources::StyleElement(this);
	}
}

MailUi::~MailUi()
{
	DeleteObj(TextView);

	if (Attachments)
	{
		Attachments->RemoveAll();
	}
	LOptionsFile *Options = (GetItem() && App) ? App->GetOptions() : 0;
	if (Options)
	{
		LRect p = GetPos();
		if (p.x1 >= 0 &&
			p.y1 >= 0)
		{
			LVariant v = p.GetStr();
			Options->SetValue("MailUI.Pos", v);
		}
	}

	Tab = 0;
	if (GetItem())
	{
		GetItem()->Ui = NULL;
	}
	else LgiTrace("%s:%i - Error: no item to clear UI ptr?\n", _FL);

	// We delete the LView here because objects
	// need to have their virtual tables intact
	_Delete();
}

Mail *MailUi::GetItem()
{
	THREAD_UNSAFE(NULL);

	return _Item ? _Item->IsMail() : NULL;
}

void MailUi::SetItem(Mail *m)
{
	THREAD_UNSAFE();

	if (_Item)
	{
		Mail *old = _Item->IsMail();
		if (old)
			old->Ui = NULL;
		else
			LAssert(0);
		_Item = NULL;
	}

	if (m)
	{
		_Item = m;
		m->Ui = this;
	}
}

bool MailUi::SetDirty(bool Dirty, bool Ui)
{
	THREAD_UNSAFE(false);

	bool b = ThingUi::SetDirty(Dirty, Ui);
	
	if (MetaFieldsDirty && !Dirty)
	{
		Mail *m = GetItem();
		if (m)
		{
			LColourSelect *Colour;
			if (GetViewById(IDC_COLOUR, Colour))
			{
				uint32_t Col = (uint32_t)Colour->Value();
				m->SetMarkColour(Col);
			}
			else LgiTrace("%s:%i - Can't find IDC_COLOUR\n", _FL);

			auto s = GetCtrlName(IDC_LABEL);
			m->SetLabel(s);
			
			if (m->GetObject()->GetInt(FIELD_STORE_TYPE) != Store3Imap) // Imap knows to save itself.
				m->SetDirty();
			m->Update();
		}
		
		MetaFieldsDirty = false;
	}
	
	return b;
}

#define IDM_FILTER_BASE		2000
#define IDM_CHARSET_BASE	3000
void AddActions(LSubMenu *Menu, List<Filter> &Filters, LArray<ScribeFolder*> Folders)
{
	if (!Menu)
		return;

	auto StartLen = Menu->Length();

	for (auto Folder: Folders)
	{
		for (ScribeFolder *f=Folder->GetChildFolder(); f; f=f->GetNextFolder())
		{
			auto Sub = Menu->AppendSub(f->GetName(true));
			if (Sub)
			{
				auto Item = Sub->GetParent();
				if (Item) Item->Icon(ICON_CLOSED_FOLDER);
				Sub->SetImageList(Menu->GetImageList(), false);
				f->LoadThings();
				AddActions(Sub, Filters, {f});
			}
		}

		List<Filter> a;
		Folder->LoadThings();
		for (auto t: Folder->Items)
		{
			a.Insert(t->IsFilter());
		}

		a.Sort(FilterCompare);

		for (auto i: a)
		{
			LVariant Name;
			if (i->GetVariant("Name", Name) && Name.Str())
			{
				char n[256];
				strcpy_s(n, sizeof(n), Name.Str());
				for (char *s=n; *s; s++)
				{
					if (*s == '&')
					{
						memmove(s + 1, s, strlen(s)+1);
						s++;
					}
				}

				auto Item = Menu->AppendItem(n, (int) (IDM_FILTER_BASE + Filters.Length()), true);
				if (Item)
				{
					Item->Icon(ICON_FILTER);
					Filters.Insert(i);
				}
			}
		}
	}

	if (StartLen == Menu->Length())
	{
		char s[64];
		sprintf_s(s, sizeof(s), "(%s)", LLoadString(IDS_EMPTY));
		Menu->AppendItem(s, 0, false);
	}
}

bool MailUi::OnViewKey(LView *v, LKey &k)
{
	THREAD_UNSAFE(false);

	if (k.Down() && k.CtrlCmd() && !k.Alt())
	{
		switch (k.vkey)
		{
			case LK_RETURN:
			{
				PostEvent(M_COMMAND, IDM_SEND_MSG);
				return true;
			}
			case LK_UP:
			{
				SeekMsg(-1);
				return true;
			}
			case LK_DOWN:
			{
				SeekMsg(1);
				return true;
			}
		}
		switch (k.c16)
		{
			case 'p':
			case 'P':
			{
				App->ThingPrint(NULL, GetItem(), NULL, this);
				break;
			}
			case 'f':
			case 'F':
			{
				if (Tab->Value() == 0)
				{
					if (TextView)
						TextView->DoFind(NULL);
				}
				else if (Tab->Value() == 1)
				{
					if (HtmlView)
						HtmlView->DoFind(NULL);
				}
				return true;
			}
			case 'r':
			case 'R':
			{
				OnCommand(IDM_REPLY, 0, NULL);
				return true;
			}
			case 'w':
			case 'W':
			{
				if (OnRequestClose(false))
					Quit();
				return true;
			}
			case 's':
			case 'S':
			{
				if (IsDirty())
				{
					SetDirty(false);
				}
				return true;
			}
			case '1':
			{
				Tab->Value(0);
				if (TextView) TextView->Focus(true);
				return true;
			}
			case '2':
			{
				Tab->Value(1);
				if (HtmlView) HtmlView->Focus(true);
				return true;
			}
		}
	}

	return ThingUi::OnViewKey(v, k);
}

void MailUi::SerializeText(bool FromCtrl)
{
	THREAD_UNSAFE();

	if (GetItem() && TextView)
	{
		if (FromCtrl)
		{
			GetItem()->SetBody(TextView->Name());
		}
		else
		{
			TextView->Name(GetItem()->GetBody());
		}
	}
}

const char *FilePart(const char *Uri)
{
	const char *Dir = Uri + strlen(Uri) - 1;
	while (Dir > Uri &&
		Dir[-1] != '/' &&
		Dir[-1] != '\\')
	{
		Dir--;
	}
	
	return Dir;
}

void MailUi::OnAttachmentsChange()
{
	THREAD_UNSAFE();

	Attachments->ResizeColumnsToContent();

	if (GetItem())
	{
		TabAttachments->GetCss(true)->FontBold(GetItem()->Attachments.Length() > 0);
		TabAttachments->OnStyleChange();
	}
}

bool MailUi::NeedsCapability(const char *Name, const char *Param)
{
	THREAD_SAFE();

	if (!InThread())
    {
        PostEvent(M_NEEDS_CAP, (LMessage::Param)NewStr(Name));
    }
    else
    {
        if (!Name)
            return false;
            
        if (Caps.Find(Name))
            return true;

        Caps.Add(Name, true);

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
			for (auto k : Caps)
				ch += sprintf_s(msg+ch, sizeof(msg)-ch, "%s%s", ch?", ":"", k.key);
			ch += sprintf_s(msg+ch, sizeof(msg)-ch, " is required to display this content.");
		}
    		
        if (!MissingCaps)
        {
            MissingCaps = new MissingCapsBar(this, &Caps, msg, App, Actions, Back);

			auto c = IterateViews();
			auto Idx = c.IndexOf(GpgUi);
			AddView(MissingCaps, (int)Idx+1);
            AttachChildren();
            OnPosChange();
        }
        else
        {
		    MissingCaps->SetMsg(msg);
        }
    }

    return true;
}

void MailUi::OnCloseInstaller()
{
	THREAD_UNSAFE();

	if (MissingCaps)
	{
		DeleteObj(MissingCaps);
		PourAll();
	}
}

void MailUi::OnInstall(LCapabilityTarget::CapsHash *Caps, bool Status)
{
	THREAD_SAFE();
}

void MailUi::OnChildrenChanged(LViewI *Wnd, bool Attaching)
{
	THREAD_UNSAFE();

	if (Wnd == (LViewI*)MissingCaps &&
		!Attaching)
	{
		MissingCaps = NULL;
		PourAll();
	}
}

LDocView *MailUi::GetDoc(const char *MimeType)
{
	THREAD_UNSAFE(NULL);

	if (!MimeType)
	{
		MimeType = sTextPlain;
		LVariant v;
		if (App->GetOptions()->GetValue(OPT_DefaultAlternative, v) &&
			v.CastInt32() > 0)
			MimeType = sTextHtml;
	}
	
	LAssert(MimeType != NULL);
	
	if (!_stricmp(MimeType, sTextPlain))
		return TextView;
	else if (!_stricmp(MimeType, sTextHtml))
		return HtmlView;
	else
		LAssert(!"Invalid mime type.");
	
	return NULL;
}

bool MailUi::SetDoc(LDocView *v, const char *MimeType)
{
	THREAD_UNSAFE(false);

	if (!MimeType)
	{
		MimeType = sTextPlain;
		LVariant v;
		if (App->GetOptions()->GetValue(OPT_DefaultAlternative, v) &&
			v.CastInt32() > 0)
			MimeType = sTextHtml;
	}
	
	LAssert(MimeType != NULL);

	if (!_stricmp(MimeType, sTextPlain))
	{
		LTextView3 *Txt = static_cast<LTextView3*>(v);
		if (Txt)
		{
			if (Txt != TextView)
			{
				DeleteObj(TextView);
				TextView = Txt;
				if (TabText && !TextView->IsAttached())
					TabText->Append(TextView);
			}
			TextLoaded = true;
			if (_Running)
				TabText->Select();
		}
		else
		{
			LAssert(!"Invalid ctrl.");
			return false;
		}
	}
	else if (!_stricmp(MimeType, sTextHtml))		
	{
		LDocView *Html = dynamic_cast<LDocView*>(v);
		if (Html)
		{
			if (Html != HtmlView)
			{
				DeleteObj(HtmlView);
				HtmlView = Html;

				LCapabilityClient *cc = dynamic_cast<LCapabilityClient*>(v);
				if (cc)
					cc->Register(this);

				if (TabHtml && !HtmlView->IsAttached())
					TabHtml->Append(HtmlView);
			}
			HtmlLoaded = true;
			if (_Running)
				TabHtml->Select();
		}
		else
		{
			LAssert(!"Invalid ctrl.");
			return false;
		}
	}
	else
	{
		LAssert(!"Invalid mime type.");
		return false;
	}

    return true;
}

bool MailUi::IsWorking(int Set)
{
	THREAD_UNSAFE(false);

	if (!GetItem())
		return false;

	// Are any of the attachments busy doing something?
	LDataI *AttachPoint = GetItem() && Set >= 0 ? GetItem()->GetFileAttachPoint() : NULL;

	List<Attachment> Attachments;
	if (GetItem()->GetAttachments(&Attachments))
	{
		// Attachment *Match = NULL;
		for (auto a: Attachments)
		{
			if (Set >= 0)
			{
				a->SetIsResizing(Set != 0);
				if (AttachPoint)
				{
					a->GetObject()->Save(AttachPoint);
				}
			}
			else if (a->GetIsResizing())
			{
				return true;
			}
		}
	}
	
	// Check rich text control as well...
	auto Rte = dynamic_cast<LRichTextEdit*>(HtmlView);
	if (Rte)
	{
		if (Rte->IsBusy())
		{
			return true;
		}
	}

	return false;
}

class BusyPanel : public LPanel
{
public:
	BusyPanel() : LPanel("Working...", LSysFont->GetHeight() << 1)
	{
		LViewI *v;
		LCss::ColorDef Bk(LColour(255, 128, 0));
		GetCss(true)->BackgroundColor(Bk);

		AddView(v = new LTextLabel(IDC_STATIC, 30, 5, -1, -1, "Still resizing images..."));
		v->GetCss(true)->BackgroundColor(Bk);

		AddView(v = new LButton(IDCANCEL, 200, 3, -1, -1, LLoadString(IDS_CANCEL)));
		v->GetCss(true)->NoPaintColor(Bk);
	}
};

void MailUi::SetCmdAfterResize(int Cmd)
{
	THREAD_UNSAFE();

	if (CmdAfterResize == 0)
	{
		CmdAfterResize = Cmd;
		if (!WorkingDlg)
		{
			WorkingDlg = new BusyPanel;
			if (WorkingDlg)
			{
				AddView(WorkingDlg, 1);
				AttachChildren();
				OnPosChange();
			}
		}
	}
}

bool MailUi::OnRequestClose(bool OsClose)
{
	THREAD_UNSAFE(true);

	bool Working = IsWorking();
	if (Working)
	{
		SetCmdAfterResize(IDM_SAVE_CLOSE);
		return false;
	}
	
	return ThingUi::OnRequestClose(OsClose);
}

void MailUi::OnChange()
{
	THREAD_UNSAFE();

	if (!IsDirty())
		OnLoad();
}

struct MailUiNameAddr
{
	LString Name, Addr;
	
	MailUiNameAddr(const char *name = 0, const char *addr = 0)
	{
		Name = name;
		Addr = addr;
	}
};

void MailUi::OnLoad()
{
	THREAD_UNSAFE();

	bool Edit = false;
	bool ReadOnly = true;
	Mail *Item = GetItem();

	_Running = false;

	if (Item &&
		Item->App)
	{
		Edit = TestFlag(Item->GetFlags(), MAIL_CREATED);
		ReadOnly = !TestFlag(Item->GetFlags(), MAIL_CREATED | MAIL_BOUNCE);

		if (Entry)
		{
			Entry->Name("");
		}

		if (To)
		{
			To->OnInit(Item->GetTo());
		}

		if (FromCbo)
		{
			LHashTbl<StrKey<char,false>, MailUiNameAddr*> ReplyToAddrs;
			const char *Template = "%s <%s>";
			FromCbo->Empty();

			int Idx = -1;
			LVariant DefName, DefAddr;

			// LOptionsFile *Opts = Item->Window->GetOptions();
			for (auto a : *Item->App->GetAccounts())
			{
				LVariant Name = a->Identity.Name();
				LVariant Addr = a->Identity.Email();
				LVariant ReplyTo = a->Identity.ReplyTo();
				
				if (!a->IsValid() || a->Send.Disabled())
					continue;
				
				if (ReplyTo.Str())
				{
					if (!ReplyToAddrs.Find(ReplyTo.Str()))
						ReplyToAddrs.Add(ReplyTo.Str(), new MailUiNameAddr(Name.Str(), ReplyTo.Str()));
				}
				else if (Addr.Str())
				{
					if (!ReplyToAddrs.Find(Addr.Str()))
						ReplyToAddrs.Add(Addr.Str(), new MailUiNameAddr(Name.Str(), Addr.Str()));
				}

				if (Name.Str() && Addr.Str())
				{
					if (!DefAddr.Str() ||
						_stricmp(DefAddr.Str(), Addr.Str()))
					{
						auto FromAddr = Item->GetFromStr(FIELD_EMAIL);
						if (FromAddr && _stricmp(Addr.Str(), FromAddr) == 0)
						{
							Idx = (int)FromCbo->Length();
						}

						LString p;
						p.Printf(Template, Name.Str(), Addr.Str());
						
						int Id = a->Receive.Id();
						int CurLen = (int)FromCbo->Length();
						
						LAssert(Id != 0);
						
						FromAccountId[CurLen] = Id;
						
						FromCbo->Insert(p);
					}
				}
			}
			
			if (Idx < 0)
			{
				auto FromName = Item->GetFromStr(FIELD_NAME);
				auto FromAddr = Item->GetFromStr(FIELD_EMAIL);
				if (FromAddr)
				{
					LStringPipe p;
					if (FromName)
						p.Print(Template, FromName, FromAddr);
					else
						p.Print("<%s>", FromAddr);
					LAutoString s(p.NewStr());
					FromAccountId[FromCbo->Length()] = -1;
					Idx = (int)FromCbo->Length();
					FromCbo->Insert(s);
					FromCbo->Value(Idx);
				}
			}
			else
			{
				FromCbo->Value(Idx);
			}

			if (ReplyToAddrs.Length() > 0 && ReplyToCbo)
			{
				auto CurAddr = Item->GetReply() ? Item->GetReply()->GetStr(FIELD_EMAIL) : NULL;
				int CurIdx = -1;
				// for (MailUiNameAddr *na = ReplyToAddrs.First(); na; na = ReplyToAddrs.Next())
				for (auto na : ReplyToAddrs)
				{
					char s[256];
					sprintf_s(s, sizeof(s), Template, na.value->Name.Get(), na.value->Addr.Get());
					if (CurAddr && !_stricmp(na.value->Addr, CurAddr))
						CurIdx = (int)ReplyToCbo->Length();
					
					ReplyToCbo->Insert(s);
				}
				if (CurIdx >= 0)
				{
					ReplyToCbo->Value(CurIdx);
					ReplyToChk->Value(true);
				}
				else
				{
					ReplyToChk->Value(false);
				}
			}
			ReplyToAddrs.DeleteObjects();
		}
		else if (FromList)
		{
			FromList->Empty();
			ListAddr *na = new ListAddr(App, Item->GetFrom());
			if (na)
			{
				na->CC = MAIL_ADDR_FROM;
				na->OnFind();
				FromList->Insert(na);
			}
		}

		if (Subject)
		{
			Subject->Name(Item->GetSubject());
			
			char Title[140];
			auto Subj = Item->GetSubject();
			if (Subj)
				sprintf_s(Title, sizeof(Title), "%s - %.100s", LLoadString(IDS_MAIL_MESSAGE), Subj);
			else
				sprintf_s(Title, sizeof(Title), "%s", LLoadString(IDS_MAIL_MESSAGE));
			Title[sizeof(Title)-1] = 0;
			for (char *s = Title; *s; s++)
				if (*s == '\n' || *s == '\r') *s = ' ';
			Name(Title);
		}

		int64_t Rgb32 = Item->GetMarkColour();
		SetCtrlValue(IDC_COLOUR, Rgb32 > 0 ? Rgb32 : -1);
		SetCtrlName(IDC_LABEL, Item->GetLabel());

		char Date[256];
		Item->GetDateReceived()->Get(Date, sizeof(Date));
		SetCtrlName(IDC_RECEIVED_DATE, Date);
		Item->GetDateSent()->Get(Date, sizeof(Date));
		SetCtrlName(IDC_SENT_DATE, Date);

		TextLoaded = false;
		HtmlLoaded = false;

		auto TextContent = Item->GetBody();
		auto HtmlContent = Item->GetHtml();

		Sx = Sy = -1;

		LDocView *DocView = NULL;
		if (TabText &&
			(DocView = Item->CreateView(this, sTextPlain, true, -1, !Edit)))
		{
			if (DocView == HtmlView)
			{
				// CreateView converted the text to HTML to embed Emojis. If we have
				// actual HTML content it'll overwrite the text portion, so we need
				// to move the HTML control to the text tab to leave room for actual HTML.
				DeleteObj(TextView);
				TextView = HtmlView;
				HtmlView = NULL;
				TextView->Detach();
				TabText->Append(TextView);
				TextLoaded = true;
				HtmlLoaded = false;
			}
			
			if (!TextView && Edit)
			{
				// What the? Force creation of control...
				LAssert(!"Must have an edit control.");
				LDocView *Dv = App->CreateTextControl(IDC_TEXT_VIEW, sTextPlain, Edit, GetItem());
				if (Dv)
					SetDoc(Dv, sTextPlain);
				LAssert(TextView != NULL);
			}
			
			if (TextView)
			{
				TextView->Visible(true);
				
				// This needs to be below the resize of the control so that
				// any wrapping has already been done and thus the scroll
				// bar is laid out already.
				if (Item->Cursor > 0)
					TextView->SetCaret(Item->Cursor, false);

				TabText->GetCss(true)->FontBold(ValidStr(TextContent));
				TabText->OnStyleChange();
			}
		}

		bool ValidHtml = ValidStr(HtmlContent);
		if (TabHtml &&
			Item->CreateView(this, sTextHtml, true, -1, !Edit) &&
			HtmlView)
		{
			HtmlView->Visible(true);
			if (Item->Cursor > 0)
				HtmlView->SetCaret(Item->Cursor, false);

			TabHtml->GetCss(true)->FontBold(ValidHtml);
			TabHtml->OnStyleChange();
		}
		
		LVariant DefTab;
		Item->App->GetOptions()->GetValue(Edit ? OPT_EditControl : OPT_DefaultAlternative, DefTab);
		CurrentEditCtrl = (Edit || ValidHtml) && DefTab.CastInt32();
		Tab->Value(CurrentEditCtrl);
		HtmlCtrlDirty = !ValidHtml;
		TextCtrlDirty = !ValidStr(TextContent);

		if (CalendarPanel)
		{
			auto CalEvents = GetItem()->GetCalendarAttachments();
			CalendarPanel->Open(CalEvents.Length() > 0);
		}

		OnPosChange();

		if (Attachments)
		{
			Attachments->RemoveAll();
			
			List<Attachment> Files;
			if (Item->GetAttachments(&Files))
			{
				for (auto a: Files)
					Attachments->Insert(a);
			}

			OnAttachmentsChange();
		}

		if (Header)
		{
			LAutoString Utf((char*)LNewConvertCp("utf-8", Item->GetInternetHeader(), "iso-8859-1"));
			Header->Name(Utf);
			Header->SetEnv(Item);
		}

		bool Update =	(Item->GetFlags() & MAIL_READ) == 0 &&
						(Item->GetFlags() & MAIL_CREATED) == 0;
		if (Update)
		{
			Item->SetFlags(Item->GetFlags() | MAIL_READ);
		}

		if (Commands.Toolbar)
		{
			int p = Item->GetPriority();
			Commands.Toolbar->SetCtrlValue(IDM_HIGH_PRIORITY, p < MAIL_PRIORITY_NORMAL);
			Commands.Toolbar->SetCtrlValue(IDM_LOW_PRIORITY, p > MAIL_PRIORITY_NORMAL);
			Commands.Toolbar->SetCtrlValue(IDM_READ_RECEIPT, TestFlag(Item->GetFlags(), MAIL_READ_RECEIPT));
		}
	}

	if (Item->GetFlags() & (MAIL_CREATED | MAIL_BOUNCE))
	{
		if (Entry)
			Entry->Focus(true);
	}
	else
	{
		if (TextView)
			TextView->Focus(true);
	}

	if (BtnPrev && BtnNext)
	{
		if (Item && Container)
		{
			/*
			int Items = Item->GetList()->Length();
			int i = Item->GetList()->IndexOf(Item);
			*/

			auto Items = Container->Length();
			auto i = Container->IndexOf(Item);

			BtnPrev->Enabled(i < (ssize_t)Items - 1);
			BtnNext->Enabled(i > 0);
		}
		else
		{
			BtnPrev->Enabled(false);
			BtnNext->Enabled(false);
		}
	}

	if (BtnSend) BtnSend->Enabled(!ReadOnly);
	if (BtnSave) BtnSave->Enabled(!ReadOnly);
	if (BtnSaveClose) BtnSaveClose->Enabled(!ReadOnly);
	if (BtnAttach) BtnAttach->Enabled(!ReadOnly);

	if (BtnReply) BtnReply->Enabled(ReadOnly);
	if (BtnReplyAll) BtnReplyAll->Enabled(ReadOnly);
	if (BtnForward) BtnForward->Enabled(true);
	if (BtnBounce) BtnBounce->Enabled(ReadOnly);

	if (Commands.Toolbar)
	{
		Commands.Toolbar->SetCtrlEnabled(IDM_HIGH_PRIORITY, Edit);
		Commands.Toolbar->SetCtrlEnabled(IDM_LOW_PRIORITY, Edit);
		Commands.Toolbar->SetCtrlEnabled(IDM_READ_RECEIPT, Edit);
	}

	_Running = true;
}

void MailUi::OnSave()
{
	THREAD_UNSAFE();

	if (!GetItem())
		return;

	if (GetItem()->GetFlags() & MAIL_SENT)
	{
		// Save a copy instead of over writing the original sent email
		Mail *Copy = new Mail(App, GetItem()->GetObject()->GetStore()->Create(MAGIC_MAIL));
		if (Copy)
		{
			*Copy = *_Item;
			Copy->SetFlags(MAIL_READ | MAIL_CREATED, true);
			Copy->SetFolder(GetItem()->GetFolder());				
			Copy->SetDateSent(0);
			SetItem(Copy);
		}
	}

	Mail *Item = GetItem();
	if (To)
	{
		To->OnSave(Item->GetObject()->GetStore(), Item->GetTo());
	}
	
	LMailStore *AccountMailStore = NULL;
	
	if (FromCbo && Item->GetFrom() && FromAccountId.Length() > 0)
	{
		int64 CboVal = FromCbo->Value();
		LAssert(CboVal < (ssize_t)FromAccountId.Length());
		int AccountId = FromAccountId[(int)CboVal];
		
		LDataPropI *Frm = Item->GetFrom();
		if (AccountId < 0)
		{
			// From is a literal address, not an account ID. This can happen when bouncing email.
			Mailto mt(App, FromCbo->Name());
			if (mt.To.Length() == 1)
			{
				AddressDescriptor *a = mt.To[0];
				if (a)
				{
					Frm->SetStr(FIELD_NAME, a->sName);
					Frm->SetStr(FIELD_EMAIL, a->sAddr);
				}
			}
			else LAssert(0);
		}
		else if (AccountId > 0)
		{
			ScribeAccount *a = Item->App->GetAccountById(AccountId);
			if (a)
			{
				Frm->SetStr(FIELD_NAME, a->Identity.Name().Str());
				Frm->SetStr(FIELD_EMAIL, a->Identity.Email().Str());
			}
			else LAssert(!"From account missing.");
			
			// Find the associated mail store for this account. Hopefully we can put any new
			// mail into the mail store that the account is using.
			// 
			// Check for IMAP mail store?
			AccountMailStore = a->Receive.GetMailStore();
			if (!AccountMailStore)
			{
				// Nope... what about a receive path?
				LVariant DestFolder = a->Receive.DestinationFolder();
				if (ValidStr(DestFolder.Str()))
				{
					AccountMailStore = Item->App->GetMailStoreForPath(DestFolder.Str());
				}
			}
		}
		else LAssert(!"No account id.");
	}
	
	LDataPropI *ReplyObj = Item->GetReply();
	if (ReplyToCbo != NULL &&
		ReplyObj != NULL   &&
		ReplyToChk != NULL &&
		ReplyToChk->Value())
	{		
		Mailto mt(App, ReplyToCbo->Name());
		if (mt.To.Length() == 1)
		{
			AddressDescriptor *a = mt.To[0];
			if (a && a->sAddr)
			{
				if (a->sName)
					ReplyObj->SetStr(FIELD_NAME, a->sName);
				ReplyObj->SetStr(FIELD_EMAIL, a->sAddr);
			}			
		}
	}

	Item->SetSubject(Subject->Name());

	Item->SetLabel(GetCtrlName(IDC_LABEL));
	
	auto c32 = GetCtrlValue(IDC_COLOUR);
	Item->SetMarkColour(c32);

	LDocView *Ctrl = CurrentEditCtrl ? HtmlView : TextView;
	if (Ctrl)
	{
		// Delete all existing data...
		Item->SetBody(0);
		Item->SetBodyCharset(0);
		Item->SetHtml(0);
		Item->SetHtmlCharset(0);

		const char *MimeType = Ctrl->GetMimeType();
		const char *Charset = Ctrl->GetCharset();
		if (!_stricmp(MimeType, sTextHtml))
		{
			LArray<LDocView::ContentMedia> Media;

			// Set the HTML part
			LString HtmlFormat;
			if (!Ctrl->GetFormattedContent("text/html", HtmlFormat, &Media))
				HtmlFormat = Ctrl->Name();
			Item->SetHtml(HtmlFormat);
			Item->SetHtmlCharset(Charset);

			// Also set a text version for the alternate
			LString TxtFormat;
			if (!Ctrl->GetFormattedContent(sTextPlain, TxtFormat))
			{
				TxtFormat = HtmlToText(Item->GetHtml(), Charset);
			}
			if (TxtFormat)
			{
				Item->SetBody(TxtFormat);
				Item->SetBodyCharset(Charset);
			}

			auto Obj = Item->GetObject();

			// This clears any existing multipart/related objects...
			Obj->SetObj(FIELD_HTML_RELATED, NULL);

			if (Media.Length() > 0)
			{
				// Make a table of existing attachments so that we don't duplicate
				// these new ones.
				LArray<LDataI*> Objs;
				LHashTbl<ConstStrKey<char,false>,LDataI*> Map;
				if (GetItem()->GetAttachmentObjs(Objs))
				{
					for (auto i : Objs)
					{
						auto Cid = i->GetStr(FIELD_CONTENT_ID);
						if (Cid)
							Map.Add(Cid, i);
					}
				}

				// If there are media attachments, splice them into the MIME tree.
				// This should go after setting the text part so that the right
				// MIME alternative structure is generated.
				auto Store = Obj->GetStore();
				for (auto &Cm : Media)
				{
					LDataI *a = Store->Create(MAGIC_ATTACHMENT);
					if (a)
					{
						LAssert(Cm.Valid());
						
						auto Existing = Map.Find(Cm.Id);
						if (Existing)
						{
							// Delete the existing attachment
							Thing *t = CastThing(Existing);
							Attachment *a = t ? t->IsAttachment() : NULL;
							if (a)
							{
								// Delete both the Attachment and it's store object...
								auto it = GetItem();
								it->DeleteAttachment(a);
							}
							else
							{
								// There is Attachment object for the LDataI.... but we can
								// still delete it from the store.
								LArray<LDataI*> del;
								del.Add(Existing);
								Existing->GetStore()->Delete(del, false);
							}
						}

						LgiTrace("Adding related: %s %s " LPrintfInt64 "\n",
							Cm.FileName.Get(),
							Cm.MimeType.Get(),
							Cm.Stream->GetSize());

						a->SetStr(FIELD_CONTENT_ID, Cm.Id);
						a->SetStr(FIELD_NAME, Cm.FileName);
						a->SetStr(FIELD_MIME_TYPE, Cm.MimeType);
						a->SetStream(Cm.Stream);
						
						Obj->SetObj(FIELD_HTML_RELATED, a);
					}
				}
			}
		}
		else
		{
			auto Text = Ctrl->Name();
			Item->SetBody(Text);
			Item->SetBodyCharset(Charset);
		}
	}

	#if SAVE_HEADERS
	char *Headers = Header ? Header->Name() : 0;
	if (Headers)
	{
		DeleteArray(Item->InternetHeader);
		Item->InternetHeader = NewStr(Headers);
	}
	#endif

	Item->GetMessageId(true);
	Item->CreateMailHeaders();
	Item->Update();
	
	ScribeFolder *Folder = Item->GetFolder();

	// Now get the associated outbox for this mail
	ScribeFolder *Outbox = Item->App->GetFolder(FOLDER_OUTBOX, AccountMailStore);
	
	auto Fld = Folder ? Folder : Outbox;
	LAssert(Fld != NULL);
	bool Status = Fld ? Item->Save(Fld) : false;
	if (Status)
	{
		LArray<LDataI*> c;
		c.Add(Item->GetObject());

		Item->App->SetContext(_FL);
		Item->App->OnChange(c, 0);
	}
}

bool MailUi::AddRecipient(AddressDescriptor *Addr)
{
	THREAD_UNSAFE(false);

	if (Addr && To)
	{
		ListAddr *La = dynamic_cast<ListAddr *>(Addr);
		if (La)
		{
			To->Insert(La);
			return true;
		}
	}
	return false;
}

bool MailUi::AddRecipient(Contact *c)
{
	THREAD_UNSAFE(false);

	ListAddr *La = new ListAddr(c);
	if (La)
	{
		To->Insert(La);
		return true;
	}

	return false;
}

bool MailUi::AddRecipient(const char *Email, const char *Name)
{
	THREAD_UNSAFE(false);

	ListAddr *La = new ListAddr(App, Email, Name);
	if (La)
	{
		To->Insert(La);
		return true;
	}

	return false;
}

bool MailUi::SeekMsg(int delta)
{
	THREAD_UNSAFE(false);

	bool Status = false;

	if (Container)
	{
		Mail *Item = GetItem();
		auto Index = Container->IndexOf(Item);
		Mail *Next = Index >= 0 ? (*Container)[Index + delta] : 0;
		if (Next)
		{
			SetDirty(false); // called OnSave() if necessary
			_Running = false;

			if (Item->Ui)
			{
				// close any existing user interface
				if (Item->Ui != this)
				{
					Item->Ui->Quit();
				}
				else
				{
					Item->Ui = 0;
				}
			}
			if (Header)
				Header->SetEnv(0);

			Caps.Empty();
			SetItem(Next);
			Item = GetItem();

			// select this item in the list
			if (Item->GetList())
			{
				Item->GetList()->Select(Item);
				Item->ScrollTo();
			}

			Status = true;

			OnLoad();
			_Running = true;
		}
	}

	return Status;
}

void MailUi::AttachFile(const char *File)
{
	THREAD_UNSAFE();

	auto m = GetItem();
	if (!m)
		return;

	Attachment *a = m->AttachFile(this, File);
	if (a && Attachments)
	{
		Attachments->Insert(a);
		Attachments->ResizeColumnsToContent();
	}
}

int MailUi::HandleCmd(int Cmd)
{
	THREAD_UNSAFE(0);

	switch (Cmd)
	{
		case IDM_READ_RECEIPT:
		{
			if (Commands.Toolbar)
			{
				int f = GetItem()->GetFlags();
				if (Commands.Toolbar->GetCtrlValue(IDM_READ_RECEIPT))
				{
					SetFlag(f, MAIL_READ_RECEIPT);
				}
				else
				{
					ClearFlag(f, MAIL_READ_RECEIPT);
				}
				GetItem()->SetFlags(f);
			}
			break;
		}
		case IDM_HIGH_PRIORITY:
		{
			if (Commands.Toolbar)
			{
				GetItem()->SetPriority(Commands.Toolbar->GetCtrlValue(IDM_HIGH_PRIORITY) ? MAIL_PRIORITY_HIGH : MAIL_PRIORITY_NORMAL);
				SetDirty(true);

				Commands.Toolbar->SetCtrlValue(IDM_HIGH_PRIORITY, GetItem()->GetPriority() < MAIL_PRIORITY_NORMAL);
				Commands.Toolbar->SetCtrlValue(IDM_LOW_PRIORITY, GetItem()->GetPriority() > MAIL_PRIORITY_NORMAL);
			}
			break;
		}
		case IDM_LOW_PRIORITY:
		{
			if (Commands.Toolbar)
			{
				GetItem()->SetPriority(Commands.Toolbar->GetCtrlValue(IDM_LOW_PRIORITY) ? MAIL_PRIORITY_LOW : MAIL_PRIORITY_NORMAL);
				SetDirty(true);

				Commands.Toolbar->SetCtrlValue(IDM_HIGH_PRIORITY, GetItem()->GetPriority() < MAIL_PRIORITY_NORMAL);
				Commands.Toolbar->SetCtrlValue(IDM_LOW_PRIORITY, GetItem()->GetPriority() > MAIL_PRIORITY_NORMAL);
			}
			break;
		}
		case IDM_PREV_MSG:
		{
			SeekMsg(1);
			break;
		}
		case IDM_NEXT_MSG:
		{
			SeekMsg(-1);
			break;
		}
		case IDM_SEND_MSG:
		{
			bool Working = IsWorking();
			if (Working)
			{
				SetCmdAfterResize(Cmd);
				break;
			}

			if (!GetItem() || !App)
			{
				LAssert(!"Missing item or window ptr.");
				break;
			}
			
			// Normal save
			bool IsInPublicFolder = GetItem()->GetFolder() && GetItem()->GetFolder()->IsPublicFolders();
			if (IsInPublicFolder)
			{
				auto i = GetItem();
				LDateTime n;
				n.SetNow();
				i->SetDateSent(&n);
				i->Update();
			}

			OnDataEntered();
			OnSave();
			SetDirty(false, false);
			GetItem()->Send(true);
			Quit();
			return 0;
		}
		case IDM_DELETE_MSG:
		{
			LVariant ConfirmDelete, DelDirection;
			App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);
			App->GetOptions()->GetValue(OPT_DelDirection, DelDirection);
			int WinAction = DelDirection.CastInt32() - 1; // -1 == Next, 0 == Close, 1 == Prev

			if (!ConfirmDelete.CastInt32() ||
				LgiMsg(this, LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
			{
				Mail *Del = GetItem()->GetObject() ? GetItem() : 0;
				if (Del)
				{
					if (!Del->GetObject()->IsOnDisk())
						Del = 0;
				}
				
				if (!WinAction || !SeekMsg(WinAction))
				{
					SetItem(0);
					PostEvent(M_CLOSE);
				}
				
				if (Del && Del->GetObject())
				{
					Del->OnDelete();
				}
			}
			break;
		}
		case IDM_DELETE_AS_SPAM:
		{
			LVariant DelDirection;
			App->GetOptions()->GetValue(OPT_DelDirection, DelDirection);
			int WinAction = DelDirection.CastInt32() - 1; // -1 == Next, 0 == Close, 1 == Prev
			
			if (GetItem())
			{
				Mail *Del = GetItem()->GetObject() ? GetItem() : 0;

				if (!WinAction || !SeekMsg(WinAction))
				{
					SetItem(0);
					PostEvent(M_CLOSE);
				}

				if (Del)
				{
					Del->DeleteAsSpam(this);
				}
			}
			break;
		}
		case IDM_SAVE:
		{
			OnDataEntered();
			SetDirty(false, false);
			break;
		}
		case IDM_SAVE_CLOSE:
		{
			bool Working = IsWorking();
			if (Working)
			{
				SetCmdAfterResize(Cmd);
				break;
			}

			OnDataEntered();
			SetDirty(false, false);
			// fall thru
		}
		case IDM_CLOSE:
		{
			Quit();
			return 0;
		}
		case IDM_REPLY:
		case IDM_REPLY_ALL:
		{
			App->MailReplyTo(GetItem(), Cmd == IDM_REPLY_ALL);
			SetDirty(false);
			PostEvent(M_CLOSE);
			break;
		}
		case IDM_FORWARD:
		{
			App->MailForward(GetItem());
			if (IsDirty())
				OnSave();
			PostEvent(M_CLOSE);
			break;
		}
		case IDM_BOUNCE:
		{
			App->MailBounce(GetItem());
			OnSave();
			PostEvent(M_CLOSE);
			break;
		}
		case IDM_PRINT:
		{
			if (GetItem() && App)
			{
				if (IsDirty())
				{
					OnDataEntered();
					OnSave();
				}
				App->ThingPrint(NULL, GetItem(), 0, this);
			}
			break;
		}
		case IDM_ATTACH_FILE:
		{
			auto Select = new LFileSelect(this);
			Select->MultiSelect(true);
			Select->Type("All files", LGI_ALL_FILES);
			Select->Open([this](auto dlg, auto status)
			{
				if (status)
				{
					Mail *m = GetItem();
					if (m)
					{
						for (size_t i=0; i<dlg->Length(); i++)
						{
							char File[MAX_PATH_LEN];
							if (!LResolveShortcut((*dlg)[i], File, sizeof(File)))
							{
								strcpy_s(File, sizeof(File), (*dlg)[i]);
							}

							AttachFile(File);
						}
					}
				}
				delete dlg;
			});
			break;
		}
		default:
		{
			if (Commands.ExecuteCallbacks(App, this, GetItem(), Cmd))
				return true;
			
			return false;
		}
	}

	return true;
}

int MailUi::OnCommand(int Cmd, int Event, OsView From)
{
	THREAD_UNSAFE(0);

	if (GpgUi)
	{
		GpgUi->DoCommand(Cmd, [this, Cmd](auto r)
		{
			if (!r)
				HandleCmd(Cmd);
		});
	}
	else
	{
		HandleCmd(Cmd);
	}

	return LWindow::OnCommand(Cmd, Event, From);
}

bool MailUi::AddCalendarEvent(bool AddPopupReminder)
{
	THREAD_UNSAFE(false);

	LString Msg;
	auto Result = GetItem()->AddCalendarEvent(this, AddPopupReminder, &Msg);

	auto css = CalPanelStatus->GetCss(true);
	if (Result) css->Color(LCss::ColorInherit);
	else css->Color(LColour::Red);
	CalPanelStatus->Name(Msg);

	return Result;
}


int MailUi::OnNotify(LViewI *Col, LNotification n)
{
	THREAD_UNSAFE(0);

	if (dynamic_cast<LPanel*>(Col))
	{
		Sx = Sy = -1;
		OnPosChange();
		return 0;
	}

	if (GpgUi)
	{
		if (n.Type == LNotifyItemDelete &&
			Col == (LViewI*)GpgUi)
		{
			GpgUi = NULL;			
		}
		else
		{
			int r = GpgUi->OnNotify(Col, n);
			if (r)
				return r;
		}
	}	

	int CtrlId = Col->GetId();
	switch (CtrlId)
	{
		case IDC_MAIL_UI_TABS:
		{
			Mail *Item = GetItem();
			if (!Item)
				break;
			// bool Edit = TestFlag(Item->GetFlags(), MAIL_CREATED);
			
			if (n.Type == LNotifyValueChanged)
			{
				switch (Col->Value())
				{
					case 0: // Text tab
					{
						if (!TextLoaded)
						{
							Item->CreateView(this, sTextPlain, true, -1);
							OnPosChange();
						}
						
						if (CurrentEditCtrl == 1)
						{
							if (HtmlView &&
								TextView &&
								TextCtrlDirty)
							{
								// Convert HTML to Text here...
								TextCtrlDirty = false;
								auto Html = HtmlView->Name();
								if (Html)
								{
									LString Txt = HtmlToText(Html, HtmlView->GetCharset());
									if (Txt)
										TextView->Name(Txt);
								}
							}

							CurrentEditCtrl = 0;
						}
						break;
					}
					case 1: // Html tab
					{
						if (!HtmlLoaded)
						{
							Item->CreateView(this, sTextHtml, true, -1);
							OnPosChange();
						}

						if (CurrentEditCtrl == 0)
						{
							if (HtmlView &&
								TextView &&
								HtmlCtrlDirty)
							{
								// Convert Text to HTML here...
								HtmlCtrlDirty = false;
								auto Text = TextView->Name();
								if (Text)
								{
									LString Html = TextToHtml(Text, TextView->GetCharset());
									if (Html)
										HtmlView->Name(Html);
								}
							}

							CurrentEditCtrl = 1;
						}
						break;
					}
					default: // Do nothing on other tabs..
						break;
				}
			}
			else if (n.Type == LNotifyItemClick)
			{
				LMouse m;
				if (!Col->GetMouse(m))
					break;
				
				int TabIdx = Tab->HitTest(m);
				if (TabIdx == 0 || TabIdx == 1)
				{
					if (Item && m.IsContextMenu())
					{
						LSubMenu s;
						s.AppendItem(LLoadString(IDS_DELETE), IDM_DELETE);
						
						m.ToScreen();
						int Cmd = s.Float(this, m.x, m.y, false);
						if (Cmd == IDM_DELETE)
						{
							if (TabIdx == 0) // Txt
							{
								Item->SetBody(NULL);
								Item->SetBodyCharset(NULL);
								TextView->Name(NULL);

								if (TabText)
								{
									TabText->GetCss(true)->FontBold(false);
									TabText->OnStyleChange();
								}
								TextCtrlDirty = false;
							}
							else // HTML
							{
								Item->SetHtml(NULL);
								Item->SetHtmlCharset(NULL);
								HtmlView->Name(NULL);

								if (TabHtml)
								{
									TabHtml->GetCss(true)->FontBold(false);
									TabHtml->OnStyleChange();
								}
								HtmlCtrlDirty = false;
							}
						}
					}
				}
			}
			break;
		}
		case IDC_FROM:
		{
			if (_Running &&
				FromCbo &&
				n.Type == LNotifyValueChanged)
				SetDirty(true);
			break;
		}
		case IDC_SHOW_FROM:
		{
			LVariant Show = Col->Value();;
			App->GetOptions()->SetValue(OPT_MailShowFrom, Show);
			break;
		}
		case IDC_LAUNCH_HTML:
		{
			char File[MAX_PATH_LEN];
			if (GetItem() &&
				GetItem()->WriteAlternateHtml(File))
			{
				LExecute(File);
			}
			break;
		}
		case IDC_ENTRY:
		{
			if (Entry)
			{
				if (ValidStr(Entry->Name()))
				{
					if (!Browse)
					{
						Browse = new AddressBrowse(App, Entry, To, SetTo);
					}
				}
				if (Browse)
				{
					Browse->OnNotify(Entry, n);
				}
				
				if (n.Type == LNotifyReturnKey)
				{
					OnDataEntered();
				}
			}
			break;
		}
		case IDC_SET_TO:
		{
			if (SetTo)
			{
				AddMode = (int)SetTo->Value();
			}
			break;
		}
		case IDC_SEND:
		{
			OnCommand(IDM_SEND_MSG, 0,
				#if LGI_VIEW_HANDLE
				Col->Handle()
				#else
				(OsView)NULL
				#endif
				);
			break;
		}
		#if SAVE_HEADERS
		case IDC_INTERNET_HEADER: 
		#endif
		case IDC_TEXT_VIEW:
		case IDC_HTML_VIEW:
		{
			Mail *Item = GetItem();
			if (!Item)
				break;
			bool Edit = TestFlag(Item->GetFlags(), MAIL_CREATED);

			if
			(
				(
					n.Type == LNotifyDocChanged
					||
					n.Type == LNotifyCharsetChanged
					||
					n.Type == LNotifyFixedWidthChanged
					||
					(!IgnoreShowImgNotify && n.Type == LNotifyShowImagesChanged)
				)
				&&
				_Running
			)
			{
				if (GetItem())
					GetItem()->OnNotify(Col, n);
				SetDirty(true);
				
				if (Edit)
				{
					if (CtrlId == IDC_TEXT_VIEW)
					{
						CurrentEditCtrl = 0;
						HtmlCtrlDirty = true;						
						TabText->GetCss(true)->FontBold(true);
						TabText->OnStyleChange();
					}
					else if (CtrlId == IDC_HTML_VIEW)
					{
						CurrentEditCtrl = 1;
						TextCtrlDirty = true;						
						TabHtml->GetCss(true)->FontBold(true);
						TabHtml->OnStyleChange();
					}

					// LgiTrace("%s:%i - OnNotify: TextLoaded=%i, HtmlLoaded=%i\n", _FL, TextLoaded, HtmlLoaded);
				}
			}
			break;
		}
		case IDC_ATTACHMENTS:
		{
			if (n.Type == LNotifyItemInsert ||
				n.Type == LNotifyItemDelete)
			{
				// fall thru
			}
			else
			{
				break;
			}
		}
		case IDC_TO:
		{
			if
			(
				_Running
				&&
				(
					n.Type == LNotifyItemInsert
					||
					n.Type == LNotifyItemDelete
					||
					n.Type == LNotifyItemChange
				)
			)
			{
				SetDirty(true);
			}
			break;
		}
		case IDC_COLOUR:
		{
			if (_Running && GetItem())
			{
				MetaFieldsDirty = true;
				OnDirty(true);
			}
			break;
		}
		case IDC_LABEL:
		{
			if (_Running)
			{
				MetaFieldsDirty = true;
				OnDirty(true);
			}
			break;
		}
		case IDC_SUBJECT:
		{
			if (_Running)
			{
				SetDirty(true);
			}
			break;
		}
		case IDCANCEL:
		{
			if (CmdAfterResize)
			{
				int Cmd = CmdAfterResize;
				CmdAfterResize = 0;
				IsWorking(false);		
				
				OnCommand(Cmd, 0, NULL);
			}
			break;
		}
		case IDC_ADD_CAL_EVENT:
		{
			AddCalendarEvent(false);
			break;
		}
		case IDC_ADD_CAL_EVENT_POPUP:
		{
			AddCalendarEvent(true);
			break;
		}
	}
	return 0;
}

void MailUi::OnDataEntered()
{
	THREAD_UNSAFE();

	auto Name = Entry->Name();
	if (ValidStr(Name))
	{
		List<AddressDescriptor> New;
		
		// Decode the entries
		Mailto mt(App, Name);
		New = mt.To;
		mt.To.Empty();
		
		if (mt.Subject &&
			!ValidStr(GetCtrlName(IDC_SUBJECT)))
		{
			SetCtrlName(IDC_SUBJECT, mt.Subject);
		}

		// Add the new entries
		List<Contact> Cache;
		App->GetContacts(Cache);
		AddressDescriptor *ad;
		while ((ad = New[0]))
		{
			New.Delete(ad);

			ListAddr *t = dynamic_cast<ListAddr*>(ad);
			if (t)
			{
				t->CC = (EmailAddressType)GetCtrlValue(IDC_SET_TO);
				t->OnFind(&Cache);
				To->Insert(t, 0);
			}
			else
			{
				DeleteObj(ad);
			}
		}

		// Clear the entry box for the next one
		Entry->Name("");
		Entry->Select(-1, -1);
	}
}

void MailUi::OnPosChange()
{	
	THREAD_UNSAFE();

	LWindow::OnPosChange();

	if (Tab && (Sx != X() || Sy != Y()))
	{
		Sx = X();
		Sy = Y();

		LRect r = Tab->GetCurrent()->GetClient();
		r.Inset(CONTENT_BORDER, CONTENT_BORDER);

		if (TextView)
			TextView->SetPos(r, true);

		if (HtmlView)
			HtmlView->SetPos(r, true);

		if (Attachments)
			Attachments->SetPos(r, true);

		if (Header)
			Header->SetPos(r, true);
	}
}

void MailUi::OnPaint(LSurface *pDC)
{
	LCssTools Tools(this);
	Tools.PaintContent(pDC, GetClient());
}

void MailUi::OnPulse()
{
	THREAD_UNSAFE();

	if (IsDirty() && TextView && GetItem())
	{
		// Ui -> Object
		OnSave();
		
		// Object -> Disk
		GetItem()->Save(0);
	}
	else
	{
		SetPulse();
	}
}

void MailUi::OnDirty(bool Dirty)
{
	THREAD_UNSAFE();

	SetCtrlEnabled(IDM_SAVE, Dirty);
	SetCtrlEnabled(IDM_SAVE_CLOSE, Dirty);
	
	if (Dirty)
	{
		SetPulse(60 * 1000); // every minute
	}
	else
	{
		SetPulse();
	}
}

bool MailUi::CallMethod(const char *Name, LScriptArguments &Args)
{
	THREAD_UNSAFE(false);

	ScribeDomType Method = StrToDom(Name);

	*Args.GetReturn() = false;
	switch (Method)
	{
		case SdShowRemoteContent: // Type: ()
			if (HtmlView)
			{
				bool Always = Args.Length() > 0 ? Args[0]->CastBool() : false;
				if (Always && GetItem())
				{
					auto From = GetItem()->GetFrom();
					if (From)
						App->RemoteContent_AddSender(From->GetStr(FIELD_EMAIL), true);
					else
						LgiTrace("%s:%i - No from address.\n", _FL);
				}

				IgnoreShowImgNotify = true;
				HtmlView->SetLoadImages(true);
				IgnoreShowImgNotify = false;
				PostEvent(M_UPDATE);
				*Args.GetReturn() = true;
			}
			break;
		case SdSetHtml: // Type: (String Html)
			if (HtmlView)
			{
				if (Args.Length() > 0)
				{
					HtmlView->Name(Args[0]->Str());
					*Args.GetReturn() = true;
				}
			}
			break;
		default:
			return false;
	}
	
	return true;	
}

LMessage::Result MailUi::OnEvent(LMessage *Msg)
{
	THREAD_UNSAFE(0);
	
	switch (Msg->Msg())
	{
		case M_SET_HTML:
		{
			LAutoPtr<LString> s((LString*)Msg->A());
			if (s && HtmlView)
				HtmlView->Name(*s);
			break;
		}
		case M_NEEDS_CAP:
		{
			LAutoString c((char*)Msg->A());
			NeedsCapability(c);
			return 0;
		}
		case M_RESIZE_IMAGE:
		{
			LAutoPtr<ImageResizeThread::Job> Job((ImageResizeThread::Job*)Msg->A());
			if (Job && GetItem())
			{
				// Find the right attachment...
				List<Attachment> Attachments;
				if (GetItem()->GetAttachments(&Attachments))
				{
					Attachment *Match = NULL;
					for (auto a: Attachments)
					{
						LString Nm = a->GetName();
						if (Nm.Equals(Job->FileName))
						{
							Match = a;
							break;
						}
					}
					if (Match)
					{
						if (Job->Data)
							Match->Set(Job->Data);
						
						auto Mt = Match->GetMimeType();
						if (_stricmp(Mt, "image/jpeg"))
						{
							auto Name = Match->GetName();
							char *Ext = LGetExtension(Name);
							if (Ext)
								*Ext = 0;
							LString NewName = Name;
							NewName += "jpg";
							Match->SetName(NewName);
							Match->SetMimeType("image/jpeg");
						}
						
						Match->SetIsResizing(false);
						
						LDataI *AttachPoint = GetItem()->GetFileAttachPoint();
						if (AttachPoint)
						{
							// Do final save to the mail store...
							Match->GetObject()->Save(AttachPoint);
						}
						else
						{
							LAssert(0);
							Match->DecRef();
							Match = NULL;
						}
						
						if (CmdAfterResize)
						{
							bool Working = IsWorking();
							if (!Working)
							{
								// All resizing work is done...
								OnCommand(CmdAfterResize, 0, NULL);
								return 0;
							}
						}
					}
					else LAssert(!"No matching attachment image to store resized image in.");
				}
			}
			break;
		}
		#if WINNATIVE
		case WM_COMMAND:
		{
			LAssert((NativeInt)Commands.Toolbar != 0xdddddddd);

			if (Commands.Toolbar && Commands.Toolbar->Handle() == (HWND)Msg->b)
			{
				return LWindow::OnEvent(Msg);
			}
			break;
		}
		#endif
	}

	return LWindow::OnEvent(Msg);
}

void MailUi::OnReceiveFiles(LArray<const char*> &Files)
{
	THREAD_UNSAFE();

	List<Attachment> Att;
	GetItem()->GetAttachments(&Att);

	for (unsigned i=0; i<Files.Length(); i++)
	{
		auto FileName = Files[i];
		bool Add = true;
		char Path[MAX_PATH_LEN];

		if (!LResolveShortcut(FileName, Path, strlen(Path)))
		{
			strcpy_s(Path, sizeof(Path), FileName);
		}

		for (auto a: Att)
		{
			char *f = Path;
			while (strchr(f, '\\'))
			{
				f = strchr(f, '\\') + 1;
			}

			if (_stricmp(a->GetName(), f) == 0)
			{
				int Result = LgiMsg(this, LLoadString(IDS_ATTACH_WARNING_DLG), LLoadString(IDS_ATTACHMENTS), MB_YESNO);
				if (Result == IDNO)
				{
					Add = false;
					break;
				}
			}
		}

		if (Add)
		{
			Attachment *a = GetItem()->AttachFile(this, Path);
			if (a)
			{
				Attachments->Insert(a);
				Attachments->ResizeColumnsToContent();
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
bool Mail::PreviewLines = false;
bool Mail::RunMailPipes = true;
List<Mail> Mail::NewMailLst;
bool Mail::AdjustDateTz = true;
LHashTbl<ConstStrKey<char>,Mail*> Mail::MessageIdMap;

Mail::Mail(ScribeWnd *app, LDataI *object) : Thing(app, object)
{
	DefaultObject(object);
	d = new MailPrivate(this);
	_New();
}

Mail::~Mail()
{
    if (GetObject())
    {
        auto Id = GetObject()->GetStr(FIELD_MESSAGE_ID);
        if (Id)
            MessageIdMap.Delete(Id);
    }

	NewMailLst.Delete(this);
	_Delete();
	DeleteObj(d);
}

void Mail::_New()
{
	SendAttempts = 0;
	Container = 0;
	FlagsCache = -1;
	PreviewCacheX = 0;
	Cursor = 0;
	ParentFile = 0;
	PreviousMail = 0;
	NewEmail = NewEmailNone;
	
	Ui = 0;
	TotalSizeCache = -1;
}

void Mail::_Delete()
{
	if (ParentFile)
	{
		ParentFile->SetMsg(0);
	}

	UnloadAttachments();
	PreviewCache.DeleteObjects();
	DeleteObj(Container);

	if (Ui)
		Ui->PostEvent(M_CLOSE);
	if (Ui)
		Ui->SetItem(0);
}

bool Mail::AddCalendarEvent(LViewI *Parent, bool AddPopupReminder, LString *Msg)
{
	LString Err, s;
	auto Cal = GetCalendarAttachments();
	int NewEvents = 0, DupeEvents = 0, Processed = 0, Cancelled = 0, NotMatched = 0;
	ScribeFolder *Folder = NULL;
	if (Cal.Length() == 0)
	{
		Err = "There are no attached events to add.";
		goto OnError;
	}

	Folder = App->GetFolder(FOLDER_CALENDAR);
	if (!Folder)
	{
		Err = "There no calendar folder to save to.";
		goto OnError;
	}

	for (auto a: Cal)
	{
		auto Event = App->CreateThingOfType(MAGIC_CALENDAR);
		if (Event)
		{
			LString Mt = a->GetMimeType();
			LAutoPtr<LStreamI> Data(a->GotoObject(_FL));
			if (Data)
			{
				if (Event->Import(AutoCast(Data), Mt))
				{
					auto c = Event->IsCalendar();
					auto obj = c ? c->GetObject() : NULL;
					if (!obj)
						continue;

					if (AddPopupReminder)
					{
						LString s;
						s.Printf("%g,%i,%i,", 10.0, CalMinutes, CalPopup);
						obj->SetStr(FIELD_CAL_REMINDERS, s);
					}

					// Is it a cancellation?
					auto Status = obj->GetStr(FIELD_CAL_STATUS);
					auto IsCancel = Stristr(Status, "CANCELLED") != NULL;
					if (!IsCancel)
					{
						// Does the folder already have a copy of this event?
						bool AlreadyAdded = false;
						for (auto t: Folder->Items)
						{
							auto Obj = t->IsCalendar();
							if (Obj && *Obj == *c)
							{
								AlreadyAdded = true;
								break;
							}
						}

						if (AlreadyAdded)
						{
							DupeEvents++;
						}
						else
						{
							// Write the event to the folder
							auto Status = Folder->WriteThing(Event);
							if (Status > Store3Error)
							{
								NewEvents++;
								Event = NULL;
							}
						}
					}
					else
					{
						// Cancellation processing
						auto Uid = obj->GetStr(FIELD_UID);
						Thing *Match = NULL;
						for (auto t: Folder->Items)
						{
							auto tCal = t->IsCalendar();
							if (tCal && tCal->GetObject())
							{
								auto tUid = tCal->GetObject()->GetStr(FIELD_UID);
								if (!Stricmp(Uid, tUid))
								{
									Match = t;
									break;
								}
							}
						}
						if (Match)
						{
							if (!Parent || LgiMsg(Parent, "Delete cancelled event?", "Calendar", MB_YESNO) == IDYES)
							{
								auto f = Match->GetFolder();
								LArray<Thing*> items;
								items.Add(Match);
								f->Delete(items, true);
								Cancelled++;
							}
						}
						else NotMatched++;
					}
				}
				else LgiTrace("%s:%i - vCal event import failed.\n", _FL);
			}
			else LgiTrace("%s:%i - GotoObject failed.\n", _FL);

			if (Event)
				Event->DecRef();
		}
		else LgiTrace("%s:%i - CreateThingOfType failed.\n", _FL);
	}

	Processed = NewEvents + DupeEvents;
	if (Processed != Cal.Length())
	{
		Err.Printf("There were errors processing %i events, check the console.", (int)Cal.Length() - Processed);
		goto OnError;
	}

	if (NewEvents || DupeEvents)
		s.Printf("%i new events, %i duplicates.", NewEvents, DupeEvents);
	else
		s.Printf("%i events cancelled, %i not matched.", Cancelled, NotMatched);

	if (Msg)
		*Msg = s;

	if (Processed > 0)
	{
		for (auto v: CalendarView::CalendarViews)
			v->OnContentsChanged();
	}

	return true;

OnError:
	if (Msg)
		*Msg = Err;
	return false;
}

LArray<Attachment*> Mail::GetCalendarAttachments()
{
	LArray<Attachment*> Cal;
	List<Attachment> Attachments;

	if (GetAttachments(&Attachments))
	{
		for (auto a: Attachments)
		{
			LString Mt = a->GetMimeType();
			if (Mt.Equals("text/calendar"))
				Cal.Add(a);
		}
	}

	return Cal;
}

bool Mail::AppendItems(LSubMenu *Menu, const char *Param, int Base)
{
	auto Remove = Menu->AppendSub(LLoadString(IDS_REMOVE));
	if (Remove)
	{
		Remove->AppendItem("'>'", IDM_REMOVE_GRTH, true);
		Remove->AppendItem("'> '", IDM_REMOVE_GRTH_SP, true);
		Remove->AppendItem(LLoadString(IDS_HTML_TAGS), IDM_REMOVE_HTML, true);
	}

	auto FilterMenu = Menu->AppendSub(LLoadString(IDS_FILTER));
	if (FilterMenu)
	{
		FilterMenu->SetImageList(App->GetIconImgList(), false);
		Actions.Empty();
		AddActions(FilterMenu, Actions, App->GetThingSources(MAGIC_FILTER));
	}

	auto Convert = Menu->AppendSub(LLoadString(IDS_CONVERT_SELECTION));
	if (Convert)
	{
		Convert->AppendItem("To Base64", IDM_CONVERT_BIN_TO_B64, true);
		Convert->AppendItem("To Binary", IDM_CONVERT_B64_TO_BIN, true);
	}
	
	if (!TestFlag(GetFlags(), MAIL_CREATED))
	{
		auto Charset = Menu->AppendSub(LLoadString(L_CHANGE_CHARSET));
		if (Charset)
		{
			int n=0;
			for (LCharset *c = LGetCsList(); c->Charset; c++, n++)
				Charset->AppendItem(c->Charset, IDM_CHARSET_BASE + n, c->IsAvailable());
		}
	}

	return true;
}

bool Mail::OnMenu(LDocView *View, int Id, void *Context)
{
	const char *RemoveStr = 0;

	switch (Id)
	{
		case IDM_REMOVE_GRTH:
		{
			RemoveStr = ">";
			break;
		}
		case IDM_REMOVE_GRTH_SP:
		{
			RemoveStr = "> ";
			break;
		}
		case IDM_REMOVE_HTML:
		{
			auto s = View ? View->Name() : 0;
			if (s)
			{
				auto n = DeHtml(s);
				if (n)
				{
					View->Name(n);
					DeleteArray(n);
				}
			}
			break;
		}
		default:
		{
			if (Id >= IDM_CHARSET_BASE)
			{
				int n=0;
				LCharset *c;
				for (c = LGetCsList(); c->Charset; c++, n++)
				{
					if (Id - IDM_CHARSET_BASE == n)
					{
						break;
					}
				}
				if (c->Charset)
				{
					SetBodyCharset((char*)c->Charset);
					SetDirty();
					if (GetBodyCharset() && Ui)
					{
						Ui->OnLoad();
					}
				}
			}
			else if (Id >= IDM_FILTER_BASE)
			{
				Filter *Action = Actions[Id-IDM_FILTER_BASE];
				if (Action)
				{
					// Save the message...
					SetDirty(false);

					// Hide the mail window...
					if (Ui) Ui->Visible(false);

					// Do the action
					bool Stop;
					Mail *This = this;
					Action->DoActions(This, Stop);
					if (This != this)
						break;

					if (Ui) DeleteObj(Ui);

					return true;
				}
			}
			break;
		}
		#ifdef _DEBUG
		case IDM_CONVERT_BIN_TO_B64:
		{
			char *t = View->GetSelection();
			if (t)
			{
				size_t In = strlen(t);
				size_t Len = BufferLen_BinTo64(In);
				char *B64 = new char[Len+1];
				if (B64)
				{
					ConvertBinaryToBase64(B64, Len, (uchar*)t, In);
					B64[Len] = 0;

					char Temp[256];
					ConvertBase64ToBinary((uchar*)Temp, sizeof(Temp), B64, Len);

					char16 *Str = Utf8ToWide(B64);
					if (Str)
					{
						LTextView3 *Tv = dynamic_cast<LTextView3*>(View);
						if (Tv)
						{
							Tv->DeleteSelection();
							Tv->Insert(Tv->GetCaret(), Str, Len);
						}
						DeleteArray(Str);
					}
					DeleteArray(B64);
				}
			}
			break;
		}
		case IDM_CONVERT_B64_TO_BIN:
		{
			char *t = View->GetSelection();
			if (t)
			{
				size_t In = strlen(t);
				size_t Len = BufferLen_64ToBin(In);
				char *Bin = new char[Len+1];
				if (Bin)
				{
					ssize_t Out = ConvertBase64ToBinary((uchar*)Bin, Len, t, In);
					Bin[Out] = 0;

					char16 *Str = Utf8ToWide(Bin, Out);
					if (Str)
					{
						LTextView3 *Tv = dynamic_cast<LTextView3*>(View);
						if (Tv)
						{
							Tv->DeleteSelection();
							Tv->Insert(Tv->GetCaret(), Str, Out);
						}
						DeleteArray(Str);
					}
					DeleteArray(Bin);
				}
			}
			break;
		}
		#endif
	}

	if (RemoveStr)
	{
		size_t TokenLen = strlen(RemoveStr);
		auto s = View ? View->Name() : 0;
		if (s)
		{
			LMemQueue Temp;
			auto Start = s;
			const char *End;

			while (*Start)
			{
				// seek to EOL
				for (End = Start; *End && *End != '\n'; End++);
				End++;
				ssize_t Len = End - Start;

				if (_strnicmp(Start, RemoveStr, TokenLen) == 0)
				{
					Temp.Write((uchar*)Start + TokenLen, Len - TokenLen);
				}
				else
				{
					Temp.Write((uchar*)Start, Len);
				}


				Start = End;
			}

			int Size = (int)Temp.GetSize();
			char *Buf = new char[Size+1];
			if (Buf)
			{
				Temp.Read((uchar*) Buf, Size);
				Buf[Size] = 0;
				View->Name(Buf);
				DeleteArray(Buf);
			}

		}
	}

	return true;
}

LDocumentEnv::LoadType Mail::GetContent(LAutoPtr<LoadJob> &j)
{
	if (!j)
		return LoadError;

	LUri Uri(j->Uri);
	if
	(
		Uri.sProtocol &&
		(
			!_stricmp(Uri.sProtocol, "http") ||
			!_stricmp(Uri.sProtocol, "https") ||
			!_stricmp(Uri.sProtocol, "ftp")
		)
	)
	{
		// We don't check OPT_HtmlLoadImages here because it's done elsewhere:
		// - ScribeWnd::CreateTextControl calls LHtml::SetLoadImages with the value from OPT_HtmlLoadImages
		// - LTag::LoadImage checks LHtml::GetLoadImages
		//
		// If there is a remote job here, it's because it's probably whitelisted.			
		if (!Worker)		
			Worker = App->GetImageLoader();
		if (!Worker)
			return LoadError;

		Worker->AddJob(j);
		return LoadDeferred;
	}
	else if (Uri.sProtocol && !_stricmp(Uri.sProtocol, "file"))
	{
		if (!_strnicmp(Uri.sPath, "//", 2))
		{
			// This seems to hang the windows CreateFile function...
		}
		else
		{
			// Is it a local file then?
			if (j->pDC.Reset(GdcD->Load(Uri.sPath)))
			{
				return LoadImmediate;
			}
		}
	}
	else
	{
		List<Attachment> Files;
		if (GetAttachments(&Files))
		{
			auto Dir = FilePart(j->Uri);
			
			Attachment *a = NULL;
			for (auto It = Files.begin(); It != Files.end(); It++)
			{
				a = *It;

				if (_strnicmp(j->Uri, "cid:", 4) == 0)
				{
					char *ContentId = j->Uri + 4;
					auto AttachmentId = a->GetContentId();
					if (AttachmentId)
					{
						if (AttachmentId[0] == '<')
						{
							auto s = AttachmentId + 1;
							auto e = strrchr(s, '>');
							if (e)
							{
								ssize_t len = e - s;
								if (strlen(ContentId) == len &&
									!strncmp(s, ContentId, len))
									break;
							}
						}
						else if (!strcmp(AttachmentId, ContentId))
						{
							break;
						}
					}
				}
				else
				{
					auto Name = a->GetName();
					if (Name)
					{
						auto NameDir = FilePart(Name);
						if (_stricmp(NameDir, Dir) == 0)
						{
							break;
						}
					}
				}
			}

			if (a)
			{
				j->MimeType = a->GetMimeType();
				j->ContentId = a->GetContentId();

				if (j->Pref == LoadJob::FmtStream)
				{
					j->Filename = a->GetName();
					j->Stream = a->GetObject()->GetStream(_FL);
					return LoadImmediate;
				}
				else
				{
					char *Tmp = ScribeTempPath();
					if (Tmp)
					{
						auto File = a->GetName();
						auto Ext = LGetExtension(File);
						char s[MAX_PATH_LEN] = "";
						LString part;
						do
						{
							if (part.Printf("%x.%s", LRand(), Ext) < 0)
								return LoadError;
							if (!LMakePath(s, sizeof(s), Tmp, part))
								return LoadError;
						}
						while (LFileExists(s));

						if (a->SaveTo(s, true))
						{
							if (j->Pref == LoadJob::FmtFilename)
							{
								j->Filename = s;
								return LoadImmediate;
							}
							else
							{
								int Promote = GdcD->SetOption(GDC_PROMOTE_ON_LOAD, 0);
								j->pDC.Reset(GdcD->Load(s));
								j->Filename = a->GetName();
								GdcD->SetOption(GDC_PROMOTE_ON_LOAD, Promote);
								FileDev->Delete(s, NULL, false);
								return LoadImmediate;
							}
						}
					}
				}
			}
		}
	}

	return LoadError;
}

class MailCapabilities : public LLayout
{
    LArray<LAutoString> MissingCaps;
    LDocView *Doc;
    LButton *Install;

public:
    MailCapabilities(LDocView *d)
    {
        Doc = d;
        Install = 0;
    }
    
	const char *GetClass() { return "MailCapabilities"; }
	
    void OnPosChange()
    {
        LRect c = GetClient();
        if (MissingCaps.Length())
        {
            if (!Install)
            {
                if ((Install = new LButton(IDOK, 0, 0, -1, -1, "Install")))
                    Install->Attach(this);
            }
            
            LRect r = c;
            r.x1 = r.x2 - Install->X();
            r.y2 -= 7;
            Install->SetPos(r);
        }
    }
    
    void OnPaint(LSurface *pDC)
    {
        LRect cli = GetClient();
        if (MissingCaps.Length())
        {
            char Msg[256];
            int c = sprintf_s(Msg, sizeof(Msg), "This content requires ");
            for (unsigned i=0; i<MissingCaps.Length(); i++)
                c += sprintf_s(Msg+c, sizeof(Msg)-c, "%s%s", i ? ", " : "", MissingCaps[i].Get());
            c += sprintf_s(Msg+c, sizeof(Msg)-c, " to display correctly");
            
            LDisplayString ds(LSysFont, Msg);
            LSysFont->Transparent(false);
            LSysFont->Colour(L_TEXT, L_MED);
            ds.Draw(pDC, cli.x1, cli.y1, &cli);
        }
        else
        {
            pDC->Colour(L_MED);
            pDC->Rectangle();
        }
    }
};

LDocView *Mail::CreateView(	MailViewOwner *Owner,
							LString MimeType,
							bool Sunken,
							size_t MaxBytes,
							bool NoEdit)
{
	bool Created = TestFlag(GetFlags(), MAIL_CREATED);
	bool Edit = NoEdit ? false : Created;
	bool ReadOnly = !Created;
	bool DisabledLook = false;
	LString TextMem;
	LVariant DefAlt;
	App->GetOptions()->GetValue(OPT_DefaultAlternative, DefAlt);

	auto TextBody = GetBody();
	auto TextCharset = GetBodyCharset();
	
	auto HtmlBody = GetHtml();
	auto HtmlCharset = GetHtmlCharset();

	const char *CtrlType = NULL;

	if (!MimeType)
	{
		bool TextValid = TextBody != NULL;
		bool HtmlValid = HtmlBody != NULL;

		if (TextValid && HtmlValid)
			MimeType = DefAlt.CastInt32() ? sTextHtml : sTextPlain;
		else if (TextValid)
			MimeType = sTextPlain;
		else if (HtmlValid)
			MimeType = sTextHtml;
		else
		{
			auto rootSeg = GetObject()->GetObj(FIELD_MIME_SEG);
			if (rootSeg)
				MimeType = rootSeg->GetStr(FIELD_MIME_TYPE);
			if (!MimeType)
				return NULL;

			// This is the 'multipart/encrypted' case here...
			TextMem.Printf("'%s' content.", MimeType.Get());
			TextBody = TextMem;
			DisabledLook = ReadOnly = true;
		}
	}
	
	#ifdef WINDOWS
	if (DefAlt.CastInt32() == 2 && MimeType == sTextHtml)
		CtrlType = sApplicationInternetExplorer;
	else
	#endif
		CtrlType = MimeType;

	const char *Content, *Charset;
	if (MimeType == sTextHtml)
	{
		Content = HtmlBody;
		Charset = HtmlCharset;
	}
	else
	{
		Content = TextBody;
		Charset = TextCharset;
	}

	// Emoji check
	LVariant NoEmoji;
	App->GetOptions()->GetValue(OPT_NoEmoji, NoEmoji);

	// Check if the control needs changing
	LDocView *View = Owner->GetDoc(MimeType);
	if (View)
	{
		const char *ViewMimeType = View->GetMimeType();
		if (MimeType != ViewMimeType)
		{
			Owner->SetDoc(NULL, ViewMimeType);
			View = NULL;
		}
	}

	if (!View)
	{
		View = App->CreateTextControl(	MimeType == sTextHtml ? IDC_HTML_VIEW : IDC_TEXT_VIEW,
										MimeType,
										Edit,
										this);
	}

	if (!View)
		return NULL;

	// Control setup
	View->Sunken(Sunken);
	View->SetReadOnly(ReadOnly);
	View->SetEnv(this);

	if (DisabledLook)
	{
		View->GetCss(true)->BackgroundColor(L_MED);
		View->GetCss()->Color(L_LOW);
	}
	else
	{
		View->GetCss(true)->BackgroundColor(LCss::ColorInherit);
		View->GetCss()->Color(LCss::ColorInherit);
	}
		
	LVariant UseCid = true;
	View->SetValue(LDomPropToString(HtmlImagesLinkCid), UseCid);
		
	LVariant LoadImages;
	App->GetOptions()->GetValue(OPT_HtmlLoadImages, LoadImages);
	bool AppLoadImages = LoadImages.CastInt32() != 0;
	bool MailLoadImages = TestFlag(GetFlags(), MAIL_SHOW_IMAGES);
	const char *SenderAddr = GetFrom() ? GetFrom()->GetStr(FIELD_EMAIL) : NULL;
	auto SenderStatus = App->RemoteContent_GetSenderStatus(SenderAddr);
		
	View->SetLoadImages
	(
		SenderStatus != RemoteNeverLoad
		&&
		(
			AppLoadImages ||
			MailLoadImages ||
			SenderStatus == RemoteAlwaysLoad
		)
	);

    // Attach control
	Owner->SetDoc(View, MimeType);
	LCharset *CsInfo = LGetCsInfo(Charset);

	// Check for render scripts
	LArray<LScriptCallback*> Renderers;
	LString RenderMsg = "Rendering...";
	if (App->GetScriptCallbacks(LRenderMail, Renderers))
	{
		for (auto r: Renderers)
		{
			LScriptArguments Args(NULL);
			Args.New() = new LVariant(App);
			Args.New() = new LVariant(this);
			Args.New() = new LVariant((void*)NULL);
			bool Status = App->ExecuteScriptCallback(*r, Args);
			if (Status)
			{
				auto Ret = Args.GetReturn();
				if (Ret->IsString() || Ret->CastInt32())
				{
					if (Ret->IsString())
						RenderMsg = Ret->Str();
					d->Renderer.Reset(new MailRendererScript(this, r));
					break;
				}
			}
		}
	}

	if (d->Renderer)
	{
		LString Nm;
		if (MimeType.Equals(sTextHtml))
			Nm.Printf("<html><body>%s</body></html>", RenderMsg.Get());
		else
			Nm = RenderMsg;
		View->Name(Nm);
	}
	else
	{
		// Send the data to the control
		size_t ContentLen = Content ? strlen(Content) : 0;
		Html1::LHtml *Html = dynamic_cast<Html1::LHtml*>(View);
		if (MimeType.Equals(sTextHtml))
		{
			if (CsInfo)
			{
				int OverideDocCharset = *Charset == '>' ? 1 : 0;
				View->SetCharset(Charset + OverideDocCharset);
				if (Html)
					Html->SetOverideDocCharset(OverideDocCharset != 0);
			}
			else
			{
				View->SetCharset(0);
				if (Html)
					Html->SetOverideDocCharset(0);
			}

			View->Name(Content);
		}
		else
		{
			LAutoPtr<uint32_t,true> Utf32((uint32_t*)LNewConvertCp("utf-32",
            														Content,
            														Charset ? Charset : (char*)"utf-8",
            														MaxBytes > 0 ? MIN(ContentLen, MaxBytes) : ContentLen));
			if (Utf32)
			{
				int Len = 0;
				while (Utf32[Len])
					Len++;
					
				#if 0
				LFile f;
				if (f.Open("c:\\temp\\utf32.txt", O_WRITE))
				{
					uchar bom[4] = { 0xff, 0xfe, 0, 0 };
					f.Write(bom, 4);
					f.Write(Utf32, Len * sizeof(uint32));
					f.Close();
				}
				#endif
			}
			
			LAutoWString Wide;
			Wide.Reset((char16*)LNewConvertCp(LGI_WideCharset,
												Content,
												Charset ? Charset : (char*)"utf-8",
												MaxBytes > 0 ? MIN(ContentLen, MaxBytes) : ContentLen));
			if (Wide)
			{
				View->NameW(Wide);
			}
			else
			{
				// Fall back... try and show something at least
				LAutoString t(NewStr(Content, MaxBytes > 0 ? MIN(ContentLen, MaxBytes) : ContentLen));
				if (t)
				{
					uint8_t *i = (uint8_t*)t.Get();
					while (*i)
					{
						if (*i & 0x80)
							*i &= 0x7f;
						i++;
					}
					
					View->Name(t);
				}
				else
					View->NameW(0);
			}

			View->SetFixedWidthFont(TestFlag(GetFlags(), MAIL_FIXED_WIDTH_FONT));
		}
	}

	return View;
}

bool Mail::OnNavigate(LDocView *Parent, const char *Uri)
{
	if (Uri)
	{
		if
		(
			_strnicmp(Uri, "mailto:", 7) == 0
			||
			(
				strchr(Uri, '@')
				&&
				!strchr(Uri, '/')
			)
		)
		{
			// Mail address
			return App->CreateMail(0, Uri, 0) != 0;
		}
		else
		{
			return LDefaultDocumentEnv::OnNavigate(Parent, Uri);
		}
	}

	return false;
}

void Mail::Update()
{
	TotalSizeCache = -1;
	LListItem::Update();
}

LString::Array ParseIdList(const char *In)
{
	LString::Array result;

    if (!In)
        return result;
        
    while (*In && strchr(WhiteSpace, *In))
        In++;

    if (*In == '<')
    {
        // Standard msg-id list..
	    for (auto s = In; s && *s; )
	    {
		    s = strchr(s, '<');
		    if (!s)
				break;
		    while (*s == '<') s++;

            char *e = strchr(s, '>');
            if (e)
            {
	            result.New().Set(s, e-s);
	            s = e + 1;
            }
            else break;
	    }
    }
    else
    {
        // Non compliant msg-id list...
        const char Delim[] = ", \t\r\n";
	    for (auto s = In; s && *s; )
	    {
	        if (strchr(Delim, *s))
	            s++;
		    else
		    {
	            auto Start = s;

	            while (*s && !strchr(Delim, *s))
	                s++;
	            
				result.New().Set(Start, s - Start);
		    }
	    }
	}

	return result;
}

void Base36(char *Out, uint64 In)
{
	while (In)
	{
		int p = (int)(In % 36);
		if (p < 10)
		{
			*Out++ = '0' + p;
		}
		else
		{
			*Out++ = 'A' + p - 10;
		}
		In /= 36;
	}

	*Out++ = 0;
}

void Mail::ClearCachedItems()
{
	TotalSizeCache = -1;
	PreviewCacheX = -1;
	PreviewCache.DeleteObjects();

	Attachment *a;
	int i = 0;
	while ((a = Attachments[i]))
	{
		if (!a->DecRef())
			i++;
	}
}

void Mail::NewRecipient(char *Email, char *Name)
{
	LDataPropI *a = GetTo()->Create(GetObject()->GetStore());
	if (a)
	{
		a->SetStr(FIELD_EMAIL, Email);
		a->SetStr(FIELD_NAME, Name);
		GetTo()->Insert(a);
	}
}

void Mail::PrepSend()
{
	// Set flags and other data
	SetDirty();

	int OldFlags = GetFlags();
	SetFlags((OldFlags | MAIL_READY_TO_SEND) & ~MAIL_SENT); // we want to send now...

	// Check we're in the Outbox
	ScribeFolder *OutBox = App->GetFolder(FOLDER_OUTBOX, GetObject());
	if (OutBox)
	{
		ScribeFolder *f = GetFolder();
		if (!f || f != OutBox)
		{
			LArray<Thing*> Items;
			Items.Add(this);
			OutBox->MoveTo(Items, false);
		}
	}
}

bool Mail::Send(bool Now)
{
	// Check for any "on before send" callbacks:
	bool AllowSend = true;
	LArray<LScriptCallback*> Callbacks;
	if (App->GetScriptCallbacks(LMailOnBeforeSend, Callbacks))
	{
		for (unsigned i=0; AllowSend && i<Callbacks.Length(); i++)
		{
			LScriptCallback &c = *Callbacks[i];
			if (c.Func)
			{
				LVirtualMachine Vm;
				
				LScriptArguments Args(&Vm);
				Args.New() = new LVariant(App);
				Args.New() = new LVariant(this);
				if (App->ExecuteScriptCallback(c, Args))
				{
					if (!Args.GetReturn()->CastInt32())
						AllowSend = false;
				}

				Args.DeleteObjects();
			}
		}
	}
	
	if (!AllowSend)
		return false;

	// Set the ready to send flag..
	bool IsInPublicFolder = GetFolder() && GetFolder()->IsPublicFolders();
	if (!IsInPublicFolder)
	{
		PrepSend();

		if (Now)
		{
			// Kick off send thread if relevant
			LVariant Offline;
			App->GetOptions()->GetValue(OPT_WorkOffline, Offline);
			if (!Offline.CastInt32() && !IsInPublicFolder)
			{
				App->PostEvent(M_COMMAND, IDM_SEND_MAIL, (LMessage::Param)Handle());
			}
		}
	}

	return true;
}

bool Mail::MailMessageIdMap(bool Add)
{
    if (Add)
    {
		auto LoadState = GetLoaded();
		if (LoadState < Store3Headers)
		{
			LAssert(!"Not loaded yet.");
			LStackTrace("MailMessageIdMap msg not loaded yet: %i\n", LoadState);
			return false;
		}

		// char *ObjId = GetObject()->GetStr(FIELD_MESSAGE_ID);
		auto Id = GetMessageId(true);
		if (!Id)
		{
			LAssert(!"No message ID? Impossible!");
			GetMessageId(true);
			return false;
		}

		#if 0
		LgiTrace("MailMessageIdMap(%i) Old=%s Id=%s Mail=%p\n", Add, ObjId, Id, this);
		#endif
		return MessageIdMap.Add(Id, this);
    }
    else
    {
        auto Id = GetMessageId();
    	#if 0
        LgiTrace("MailMessageIdMap(%i) Id=%s Mail=%p\n", Add, Id, this);
        #endif
        return MessageIdMap.Delete(Id);
    }
}

Mail *Mail::GetMailFromId(const char *Id)
{
    Mail *m = MessageIdMap.Find(Id);
    // LgiTrace("GetMailFromId(%s)=%p\n", Id, m);
    return m;
}

bool Mail::SetMessageId(const char *MsgId)
{
	if (LAppInst->InThread())
	{
        LAssert(GetObject() != NULL);
        
        auto OldId = GetObject()->GetStr(FIELD_MESSAGE_ID);
		if (OldId)
		    MessageIdMap.Delete(OldId);

		LAutoString m(NewStr(MsgId));
		LAutoString s(TrimStr(m, "<>"));
		if (GetObject()->SetStr(FIELD_MESSAGE_ID, s))
			SetDirty();
			
		return s != 0;
	}
	else
	{
		// No no no NO NON NOT NADA.
		LAssert(0);
	}

	return false;
}

LAutoString Mail::GetThreadIndex(int TruncateChars)
{
    LAutoString Id;
    LAutoString Raw(InetGetHeaderField(GetInternetHeader(), "Thread-Index"));
    if (Raw)
    {
        Id = ConvertThreadIndex(Raw, TruncateChars);
    }
    
    return Id;
}

const char *Mail::GetMessageId(bool Create)
{
    LAssert(GetObject() != NULL);
 	
 	d->MsgIdCache = GetObject()->GetStr(FIELD_MESSAGE_ID);
 	if (!d->MsgIdCache)
	{
		bool InThread = LCurrentThreadId() == LAppInst->GetGuiThreadId();

		LAutoString Header(InetGetHeaderField(GetInternetHeader(), "Message-ID"));
		if (Header)
		{
			LAssert(InThread);
			if (InThread)
			{
				auto Ids = ParseIdList(Header);
				SetMessageId(d->MsgIdCache = Ids[0]);
			}
		}

		if (!d->MsgIdCache && Create)
		{
			LAssert(InThread);
			
			if (InThread)
			{
				auto FromEmail = GetFromStr(FIELD_EMAIL);
				const char *At = FromEmail ? strchr(FromEmail, '@') : 0;
				LVariant Email;
				if (!At)
				{
					if (App->GetOptions()->GetValue(OPT_Email, Email) && Email.Str())
					{
						At = strchr(Email.Str(), '@');
					}
					else
					{
						At = "@domain.com";
					}
				}
				
				if (At)
				{
					char m[96], a[32], b[32];
					Base36(a, LCurrentTime());
					Base36(b, LRand(RAND_MAX));
					sprintf_s(m, sizeof(m), "<%s.%i%s%s>", a, LRand(RAND_MAX), b, At);
					if (GetObject()->SetStr(FIELD_MESSAGE_ID, m))
						SetDirty();
					
				 	d->MsgIdCache = GetObject()->GetStr(FIELD_MESSAGE_ID);
				}
				else LgiTrace("%s:%i - Error, no '@' in %s.\n", _FL, FromEmail);
			}
			else LgiTrace("%s:%i - Error, not in thread.\n", _FL);
		}
	}
	else
	{
		d->MsgIdCache = d->MsgIdCache.Strip("<>").Strip();
	}

	LAssert
	(
		(!d->MsgIdCache && !Create)
		||
		(d->MsgIdCache && !strchr(d->MsgIdCache, '\n'))
	);

	return d->MsgIdCache;
}

bool Mail::GetReferences(LString::Array &Ids)
{
	LAutoString References(InetGetHeaderField(GetInternetHeader(), "References"));
	if (References)
	{
		Ids = ParseIdList(References);
	}

	LAutoString InReplyTo(InetGetHeaderField(GetInternetHeader(), "In-Reply-To"));
	if (InReplyTo)
	{
		auto To = ParseIdList(InReplyTo);
		
		bool Has = false;
		auto &r = To[0];
		if (r)
		{
		    for (auto h: Ids)
		    {
		        if (!strcmp(h, r))
		            Has = true;
		    }
    		
		    if (!Has)
		    {
			    To.Delete(r);
			    Ids.New() = r;
		    }
		}
		
		To.DeleteArrays();
	}
	
	if (Ids.Length() == 0)
	{
	    LAutoString Id = GetThreadIndex(5);
        if (Id)
        {
            size_t Len = strlen(Id);
            size_t Bytes = Len >> 1;
            if (Bytes >= 22)
            {
                Ids.New() = Id.Get();
            }
        }
	}
	
	return Ids.Length() > 0;
}

LString AddrToHtml(LDataPropI *a)
{
	LString s;
	if (a)
	{
		auto Name = a->GetStr(FIELD_NAME);
		auto Addr = a->GetStr(FIELD_EMAIL);
		LXmlTree t;
		LAutoString eName(t.EncodeEntities(Name));
		LAutoString eAddr(t.EncodeEntities(Addr));

		if (Name && Addr)
			s.Printf("<a href=\"mailto:%s\">%s</a>", eAddr.Get(), eName.Get());
		else if (Name)
			s = eName.Get();
		else if (Addr)
			s.Printf("<a href=\"mailto:%s\">%s</a>", eAddr.Get(), eAddr.Get());
	}

	return s;
}

LDataPropI *FindMimeSeg(LDataPropI *s, char *Type)
{
	if (!s)
		return 0;

	const char *Mt = s->GetStr(FIELD_MIME_TYPE);
	if (!Mt) Mt = "text/plain";
	if (!_stricmp(Type, Mt))
		return s;

	LDataIt c = s->GetList(FIELD_MIME_SEG);
	if (!c)
		return 0;

	for (LDataPropI *i=c->First(); i; i=c->Next())
	{
		LDataPropI *Child = FindMimeSeg(i, Type);
		if (Child)
			return Child;
	}

	return 0;
}

bool Mail::GetAttachmentObjs(LArray<LDataI*> &Objs)
{
	if (!GetObject())
		return false;

	CollectAttachments(&Objs, NULL, NULL, NULL, GetObject()->GetObj(FIELD_MIME_SEG));
	return Objs.Length() > 0;
}

void DescribeMime(LStream &p, LDataI *Seg)
{
	auto Mt = Seg->GetStr(FIELD_MIME_TYPE);
	p.Print("%s", Mt);
	auto Cs = Seg->GetStr(FIELD_CHARSET);
	if (Cs) p.Print("&nbsp;-&nbsp;%s", Cs);
	p.Print("<br>\n");

	LDataIt c = Seg->GetList(FIELD_MIME_SEG);
	if (c && c->First())
	{
		p.Print("<div style='padding-left:1em;'>\n");
		for (LDataPropI *i=c->First(); i; i=c->Next())
		{
			LDataI *a = dynamic_cast<LDataI*>(i);
			if (a)
				DescribeMime(p, a);
		}
		p.Print("</div>\n");
	}
}

bool Mail::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdFrom: // Type: ListAddr
		{
			Value = GetFrom();
			break;
		}
		case SdFromHtml: // Type: String
		{
			LString s = AddrToHtml(GetFrom());
			if (s.Length() == 0)
				return false;
			Value = s;
			break;
		}
		case SdContact: // Type: Contact
		{
			if (!GetFrom())
				return false;

			auto Addr = GetFrom()->GetStr(FIELD_EMAIL);
			if (!Addr)
				return false;
			
			Contact *c = Contact::LookupEmail(Addr);
			if (!c)
				return App->GetVariant("NoContact", Value);

			Value = (LDom*)c;
			break;
		}
		case SdTo: // Type: ListAddr[]
		{
			if (Array)
			{
				LDataIt To = GetTo();

				int Idx = atoi(Array);
				bool Create = false;
				if (Idx < 0)			
				{
					Create = true;
					Idx = -Idx;
				}

				LDataPropI *a = Idx < (int)To->Length() ? (*To)[Idx] : 0;
				if (!a && Create)
				{
					if ((a = To->Create(GetObject()->GetStore())))
					{
						To->Insert(a);
					}
				}

				Value = a;
			}
			else if (Value.SetList())
			{
				LDataIt To = GetTo();
				for (LDataPropI *a=To->First(); a; a=To->Next())
				{
					LVariant *Recip = new LVariant;
					if (Recip)
					{
						*Recip = a;
						Value.Value.Lst->Insert(Recip);
					}
				}
			}
			else return false;
			break;
		}
		case SdToHtml: // Type: String
		{
			if (GetTo())
			{
				LStringPipe p;
				for (LDataPropI *t=GetTo()->First(); t; t=GetTo()->Next())
				{
					if (p.GetSize())
						p.Write((char*)", ", 2);
					LString s = AddrToHtml(t); 
					if (s)
						p.Write(s, s.Length());
				}
				Value.Type = GV_STRING;
				Value.Value.String = p.NewStr();
			}
			break;
		}
		case SdSubject: // Type: String
		{
			Value = GetSubject();
			break;
		}
		case SdBody: // Type: String
		{
			if (Array)
			{
				auto Body = GetBody();
				LAutoString h;

				for (auto c = Body; c && *c; )
				{
					while (*c && strchr(WhiteSpace, *c)) c++;
					auto Start = c;
					while (*c && *c != ':' && *c != '\n') c++;
					if (*c == ':')
					{
						if (Start[0] == '\"' && c[1] == '\"')
							c++;

						LString s(Start, c - Start);
						s = s.Strip("\'\" \t[]<>{}:");
						if (s.Equals(Array))
						{
							// Match...
							c++;
							while (*c && strchr(WhiteSpace, *c)) c++;
							Start = c;
							while (*c && *c != '\n') c++;
							while (c > Start && strchr(WhiteSpace, c[-1])) c--;

							h.Reset(NewStr(Start, c - Start));
							break;
						}
						else
						{
							while (*c && *c != '\n')
								c++;
						}
					}

					if (*c == '\n')
						c++;
				}

				if (h)
				{
					if (GetBodyCharset())
					{
						char *u = (char*)LNewConvertCp("utf-8", h, GetBodyCharset());
						if (u)
						{
							Value.OwnStr(u);
						}
						else
						{
							Value.OwnStr(h.Release());
						}
					}
					else
					{
						Value.OwnStr(h.Release());
					}
				}
			}
			else
			{
				Value = GetBody();
			}
			break;
		}
		case SdBodyAsText: // Type: String
		{
			size_t MaxSize = Atoi(Array);
			auto Txt = GetBody();
			auto Html = GetHtml();

			if (ValidStr(Txt) && ValidStr(Html))
			{
				LVariant Def;
				App->GetOptions()->GetValue(OPT_DefaultAlternative, Def);			
				if (Def.CastInt32())
				{
					goto DoHtml;
				}
				goto DoText;
			}
			else if (ValidStr(Txt))
			{
				DoText:
				LAutoString CharSet = GetCharSet();
				if (CharSet)
				{
					size_t TxtLen = strlen(Txt);
					Value.OwnStr((char*)LNewConvertCp("utf-8", Txt, CharSet, MIN(TxtLen, MaxSize)));
				}
				else
					Value = Txt;
			}
			else if (ValidStr(Html))
			{
				DoHtml:
				auto cs = GetHtmlCharset();
				auto v = HtmlToText(Html, cs);
				Value = v.Get();
			}
			else return false;
			break;
		}
		case SdBodyAsHtml: // Type: String
		{
			// int MaxSize = ValidStr(Array) ? atoi(Array) : -1;

			MailPrivate::HtmlBody *b = d->GetBody();
			if (!b)
				return false;
			
			LStringPipe p;
			p.Print("<div class='%s'>\n%s\n</div>\n", ScribeReplyClass, b->Html.Get());
			Value.OwnStr(p.NewStr());
			LgiTrace("Value=%s\n", Value.Str());
			break;
		}
		case SdHtmlHeadFields: // Type: String
		{
			MailPrivate::HtmlBody *b = d->GetBody();
			if (!b)
				return false;
			
			LStringPipe p;
			if (ValidStr(b->Charset))
				p.Print("\t<meta charset=\"%s\">\n", b->Charset.Get());

			p.Print("\t<style>\n");
			if (ValidStr(b->Styles))
			{
				// rewrite the style rules to change 'body' to 'div.scribe_reply'.
				// Otherwise the source document's styles spill over into the edit
				// section of the reply
				const char *s = b->Styles;
				LCss::Store Store;
				if (Store.Parse(s))
				{
					LCss::SelArray *Body = Store.TypeMap.Find("body");
					if (Body)
					{
						for (unsigned i=0; i<Body->Length(); i++)
						{
							LCss::Selector *Sel = (*Body)[i];
							if (Sel)
							{
								for (unsigned p=0; p<Sel->Parts.Length(); p++)
								{
									LCss::Selector::Part &Part = Sel->Parts[p];
									const char *sBody = "body";
									if (Part.Type == LCss::Selector::SelType &&
										Part.Value &&
										!_stricmp(Part.Value, sBody))
									{
										Part.Value.Reset(NewStr("div"));
										
										LCss::Selector::Part cls;
										cls.Type = LCss::Selector::SelClass;
										cls.Value.Reset(NewStr(ScribeReplyClass));
										Sel->Parts.AddAt(p + 1, cls);
										break;
									}
								}
							}
						}
					}
					
					// Output modified styles to the string buffer
					Store.ToString(p);
				}
			}
			
			// Add our own styles at the end, overwriting source doc styles...
			p.Print("\tdiv.%s { %s }\n",
					ScribeReplyClass,
					ScribeReplyStyles);
					
			p.Print("\t</style>");
		
			Value.OwnStr(p.NewStr());
			break;
		}
		case SdMessageId: // Type: String
		{
			Value = GetMessageId();
			return true;
		}
		case SdInternetHeaders: // Type: String
		{
			Value = GetInternetHeader();
			break;
		}
		case SdInternetHeader: // Type: String[]
		{
			if (!Array || !GetInternetHeader())
				return false;

			LAutoString s(InetGetHeaderField(GetInternetHeader(), Array));
			if (s)
				Value = s;
			else
				Value.Empty();
			break;
		}
		case SdPriority: // Type: Int32
		{
			Value = (int)GetPriority();
			break;
		}
		case SdHtml: // Type: String
		{
			Value = GetHtml();
			break;
		}
		case SdFlags: // Type: Int32
		{
			if (Array)
			{
				int Flag = StringToMailFlag(Array);
				Value = (GetFlags() & Flag) != 0;
			}
			else
			{
				Value = (int)GetFlags();
			}
			break;
		}
		case SdFolder: // Type: ScribeFolder
		{
			ScribeFolder *f = GetFolder();
			if (!f)
				return false;
			Value = (LDom*)f;
			break;
		}
		case SdScribe: // Type: ScribeWnd
		{
			Value = (LDom*)App;
			break;
		}
		case SdMail: // Type: Mail
		{
			Value = PreviousMail;
			break;
		}
		case SdDateSent: // Type: DateTime
		{
			Value = GetDateSent();
			break;
		}
		case SdDateReceived: // Type: DateTime
		{
			Value = GetDateReceived();
			break;
		}
		case SdSig: // Type: String
		{
			bool Type = false;
			if (Array && stristr(Array, "html"))
				Type = true;
			Value = GetSig(Type);
			break;
		}
		case SdSize: // Type: Int64
		{
			Value = TotalSizeof();
			break;
		}
		case SdLabel: // Type: String
		{
			Value = GetLabel();
			break;
		}
		case SdAttachments: // Type: Int32
		{
			LArray<LDataI*> Lst;
			GetAttachmentObjs(Lst);
			Value = (int)Lst.Length();
			break;
		}
		case SdAttachment: // Type: Attachment[]
		{
			List<Attachment> Files;
			if (GetAttachments(&Files) && Array)
			{
				int i = atoi(Array);
				Value = Files[i];
				if (Value.Type == GV_DOM)
					return true;
			}
			
			LAssert(!"Not a valid attachment?");
			return false;
		}
		case SdMimeTree: // Type: String
		{
			LDataI *Root = dynamic_cast<LDataI*>(GetObject()->GetObj(FIELD_MIME_SEG));
			if (!Root)
				return false;
			
			LStringPipe p(256);
			DescribeMime(p, Root);
			Value.OwnStr(p.NewStr());
			break;
		}
		case SdUi: // Type: LView
		{
			Value = Ui;
			break;
		}
		case SdType: // Type: Int32
		{
			Value = GetObject()->Type();
			break;
		}
		case SdSelected: // Type: Bool
		{
			Value = Select();
			break;
		}
		case SdRead: // Type: Bool
		{
			Value = (GetFlags() & MAIL_READ) != 0;
			break;
		}
		case SdShowImages: // Type: Bool
		{
			Value = (GetFlags() & MAIL_SHOW_IMAGES) != 0;
			break;
		}
		case SdColour: // Type: Int32
		{
			Value = GetMarkColour();
			break;
		}
		case SdReceivedDomain: // Type: String
		{
			Value = GetFieldText(FIELD_RECEIVED_DOMAIN);
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

#define DomSetStr(Dom, Fld)							\
	case Dom:										\
		if (GetObject() && Value.Type == GV_STRING)		\
			GetObject()->SetStr(Fld, Value.Str());		\
		else										\
			LAssert(!"Missing object or type err"); \
		break;

#define DomSetInt(Dom, Fld)							\
	case Dom:										\
		if (GetObject() && Value.Type == GV_INT32)		\
			GetObject()->SetInt(Fld, Value.Value.Int);	\
		else										\
			LAssert(!"Missing object or type err"); \
		break;

#define DomSetDate(Dom, Fld)						\
	case Dom:										\
		if (GetObject() && Value.Type == GV_DATETIME)	\
			GetObject()->SetDate(Fld, Value.Value.Date);	\
		else										\
			LAssert(!"Missing object or type err"); \
		break;

bool Mail::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		DomSetStr(SdSubject, FIELD_SUBJECT)
		DomSetStr(SdMessageId, FIELD_MESSAGE_ID)
		DomSetStr(SdInternetHeaders, FIELD_INTERNET_HEADER)
		DomSetInt(SdPriority, FIELD_PRIORITY)
		DomSetDate(SdDateSent, FIELD_DATE_SENT)
		DomSetDate(SdDateReceived, FIELD_DATE_RECEIVED)
		case SdBody:
		{
			if (!SetBody(Value.Str()))
				return false;
			break;
		}
		case SdHtml:
		{
			if (!SetHtml(Value.Str()))
				return false;
			break;
		}
		case SdRead:
		{
			if (Value.CastInt32())
				SetFlags(GetFlags() | MAIL_READ);
			else
				SetFlags(GetFlags() & ~MAIL_READ);
			break;
		}
		case SdColour:
		{
			auto u32 = Value.IsNull() ? 0 : (uint32_t)Value.CastInt32();
			if (!SetMarkColour(u32))
				return false;
			break;
		}
		case SdShowImages:
		{
			if (Value.CastInt32())
				SetFlags(GetFlags() | MAIL_SHOW_IMAGES);
			else
				SetFlags(GetFlags() & ~MAIL_SHOW_IMAGES);
			break;
		}
		case SdSelected:
		{
			Select(Value.CastInt32() != 0);
			break;
		}
		case SdLabel:
		{
			if (!SetLabel(Value.Str()))
				return false;
			break;
		}
		case SdFlags:
		{
			if (Array)
			{
				int Flag = StringToMailFlag(Array);
				if (Value.CastInt32())
					// Set
					SetFlags(GetFlags() | Flag);
				else
					SetFlags(GetFlags() & ~Flag);
			}
			else
			{
				SetFlags(Value.CastInt32());
			}
			break;
		}
		default:
		{
			return false;
		}
	}

	SetDirty();
	Update();
	return true;
}

bool Mail::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	ScribeDomType Fld = StrToDom(MethodName);
	switch (Fld)
	{
		default:
			break;
		case SdSend: // Type: ([Bool SendNow = true])
		{
			bool Now = Args.Length() > 0 ? Args[0]->CastInt32() != 0 : true;
			bool Result = Send(Now);
			*Args.GetReturn() = Result;
			return true;
		}
		case SdAddCalendarEvent: // Type: ([Bool AddPopupReminder])
		{
			bool AddPopupReminder = Args.Length() > 0 ? Args[0]->CastInt32() != 0 : true;
			bool Result = AddCalendarEvent(NULL, AddPopupReminder, NULL);
			*Args.GetReturn() = Result;
			return true;
		}
		case SdGetRead: // Type: ()
		{
			*Args.GetReturn() = (GetFlags() & MAIL_READ) != 0;
			return true;
		}
		case SdSetRead: // Type: (Bool IsRead = true)
		{
			bool Rd = Args.Length() ? Args[0]->CastInt32() != 0 : true;
			auto Flags = GetFlags();
			if (Rd) SetFlags(Flags | MAIL_READ);
			else SetFlags(Flags & ~MAIL_READ);
			*Args.GetReturn() = true;
			return true;
		}
		case SdBayesianChange: // Type: (int SpamWordOffset, int HamWordOffset)
		{
			if (Args.Length() != 2)
			{
				LgiTrace("%s:%i - Invalid arg count, expecting (int SpamWordOffset, int HamWordOffset)\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}

			auto SpamWordOffset = Args[0]->CastInt32();
			auto HamWordOffset = Args[1]->CastInt32();

			if (SpamWordOffset > 1)
				App->OnBayesianMailEvent(this, BayesMailUnknown, BayesMailSpam);
			else if (SpamWordOffset < 0)
				App->OnBayesianMailEvent(this, BayesMailSpam, BayesMailUnknown);
			
			if (HamWordOffset > 1)
				App->OnBayesianMailEvent(this, BayesMailUnknown, BayesMailHam);
			else if (HamWordOffset < 1)
				App->OnBayesianMailEvent(this, BayesMailHam, BayesMailUnknown);
			break;
		}
		case SdBayesianScore: // Type: ()
		{
			double Result;
			if (App->IsSpam(Result, this))
			{
				*Args.GetReturn() = Result;
				return true;
			}
			break;
		}
		case SdSearchHtml: // Type: (String SearchExpression)
		{
			if (Args.Length() != 2)
			{
				LgiTrace("%s:%i - Method needs 1 argument.\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}

			auto Html = GetHtml();
			if (!Html)
			{
				LgiTrace("%s:%i - No HTML to parse.\n", _FL);
				*Args.GetReturn() = false;
				return true;
			}
				
			// auto Cs = GetHtmlCharset();
			SearchHtml(Args.GetReturn(), Html, Args[0]->Str(), Args[1]->Str());
			return true;
		}
		case SdDeleteAsSpam: // Type: ()
		{
			DeleteAsSpam(App);
			return true;
		}
	}
	
	return Thing::CallMethod(MethodName, Args);
}

char *Mail::GetDropFileName()
{
	if (!DropFileName)
	{
		LString Subj = GetSubject();
		Subj = Subj.Strip(" \t\r\n.");
		DropFileName.Reset(MakeFileName(ValidStr(Subj) ? Subj.Get() : (char*)"Untitled", "eml"));
	}
	return DropFileName;
}

bool Mail::GetDropFiles(LString::Array &Files)
{
	if (!GetDropFileName())
		return false;

	if (!LFileExists(DropFileName))
	{
		LAutoPtr<LFile> F(new LFile);
		if (F->Open(DropFileName, O_WRITE))
		{
			F->SetSize(0);
			if (!Export(AutoCast(F), sMimeMessage))
				return false;
		}
		else return false;
	}

	if (!LFileExists(DropFileName))
		return false;

	Files.Add(DropFileName.Get());
	return true;
}

OsView Mail::Handle()
{
	return
		#if LGI_VIEW_HANDLE
		(Ui) ? Ui->Handle() :
		#endif
		NULL;
}

Thing &Mail::operator =(Thing &t)
{
	Mail *m = t.IsMail();
	if (m)
	{
		#define CopyStr(dst, src) \
			{ DeleteArray(dst); dst = NewStr(src); }
			
		/*
		for (LDataPropI *a = m->GetTo()->First(); a; a = m->GetTo()->Next())
		{
			LDataPropI *NewA = GetTo()->Create(Object->GetStore());
			if (NewA)
			{
				*NewA = *a;
				GetTo()->Insert(NewA);
			}
		}
		*/

		if (GetObject() && m->GetObject())
		{
			GetObject()->CopyProps(*m->GetObject());
		}
		else LAssert(!"This shouldn't happen right?");

		Update();
	}

	return *this;
}

bool Mail::HasAlternateHtml(Attachment **Attach)
{
	// New system of storing alternate HTML in a Mail object field.
	if (GetHtml()) return true;

	// Old way of storing alternate HTML, in an attachment.
	// pre v1.53
	List<Attachment> Files;
	if (GetAttachments(&Files))
	{
		for (auto a: Files)
		{
			if ((_stricmp(a->GetText(0), "attachment.html") == 0 ||
				_stricmp(a->GetText(0), "index.html") == 0) &&
				(a->GetMimeType() ? _stricmp(a->GetMimeType(), "text/html") == 0 : 1))
			{
				if (Attach)
				{
					*Attach = a;
				}

				return true;
			}
		}
	}

	// None found
	return false;
}

char *Mail::GetAlternateHtml(List<Attachment> *Refs)
{
	char *Status = 0;
	Attachment *Attach = 0;
	if (HasAlternateHtml(&Attach))
	{
		if (GetHtml())
		{
			// New system of storing alternate HTML in a Mail object field.
			Status = NewStr(GetHtml());
		}
		else if (Attach)
		{
			// Old way of storing alternate HTML, in an attachment.
			// pre v1.53
			char *Ptr;
			ssize_t Size;
			if (Attach->Get(&Ptr, &Size))
			{
				Status = NewStr((char*)Ptr, Size);
			}
		}

		if (Status && Refs)
		{
			// Turn all the cid: references into file names...
			LStringPipe Out;
			
			char *n;
			for (char *s=Status; *s; s=n)
			{
				n = stristr(s, "\"cid:");
				if (n)
				{
					// Extract cid
					n++;
					Out.Push(s, n-s);
					s = n += 4;
					while (*n && !strchr("\"\' >", *n)) n++;
					char *Cid = NewStr(s, n-s);
					if (Cid)
					{
						// Find attachment
						List<Attachment> Files;
						if (GetAttachments(&Files))
						{
							for (auto a: Files)
							{
								if (a->GetContentId() &&
									strcmp(a->GetContentId(), Cid) == 0)
								{
									Refs->Insert(a);
									Out.Push(a->GetName());
								}
							}
						}
					}					
				}
				else
				{
					Out.Push(s);
					break;
				}
			}

			DeleteArray(Status);
			Status = Out.NewStr();
		}
	}

	return Status;
}

bool Mail::WriteAlternateHtml(char *DstFile, int DstFileLen)
{
	bool Status = false;
	char *Tmp = ScribeTempPath();

	List<Attachment> Refs;
	char *Html;
	if (Tmp &&
		(Html = GetAlternateHtml(&Refs)))
	{
		char FileName[256];

		LMakePath(FileName, sizeof(FileName), Tmp, "Alt.html");
		if (DstFile)
		{
			strcpy_s(DstFile, DstFileLen, FileName);
		}

		LFile f;
		if (f.Open(FileName, O_WRITE))
		{
			size_t Len = strlen(Html);
			Status = f.Write(Html, Len) == Len;
			f.Close();
		}

		DeleteArray(Html);

		for (auto a: Refs)
		{
			LMakePath(FileName, sizeof(FileName), Tmp, a->GetName());
			FileDev->Delete(FileName, NULL, false);
			a->SaveTo(FileName);
		}
	}

	return Status;
}

bool Mail::DeleteAttachment(Attachment *File)
{
	bool Status = false;
	if (File)
	{
		if (Attachments.HasItem(File))
		{
			Attachments.Delete(File);
			if (File->GetObject())
			{
				auto r = File->GetObject()->Delete();
				if (r > Store3Error)
				{
					auto o = File->GetObject();
					if (o->IsOrphan())
						o = NULL; // SetObject will delete...
					File->SetObject(NULL, false, _FL);
					DeleteObj(o);
				}
			}

			File->DecRef();
			File = NULL;

			if (Attachments.Length() == 0)
			{
				// Remove attachments flag
				SetFlags(GetFlags() & (~MAIL_ATTACHMENTS));
			}

			Update();

			if (Ui)
			{
				Ui->OnAttachmentsChange();
			}
		}
	}

	return Status;
}

bool Mail::UnloadAttachments()
{
	for (auto it = Attachments.begin(); it != Attachments.end(); )
	{
		Attachment *a = *it;
		if (!a->DecRef())
			it++;
	}

	return true;
}

LArray<Attachment*> Mail::GetAttachments()
{
	LArray<Attachment*> result;

	LArray<LDataI*> Lst;
	if (GetAttachmentObjs(Lst))
	{
		LHashTbl<PtrKey<LDataI*>, bool> Loaded;
		for (size_t i=0; i<Attachments.Length(); i++)
		{
			Loaded.Add(Attachments[i]->GetObject(), true);
		}

		// Load attachments
		for (unsigned i=0; i<Lst.Length(); i++)
		{
			LDataI *Att = Lst[i];
			if (!Loaded.Find(Att))
			{
				Attachment *k = new Attachment(App, Att);
				if (k)
				{
					k->SetOwner(this);
					Attachments.Insert(k);
				}
			}
		}
	}

	for (auto a: Attachments)
	{
		if (a->GetObject()->UserData != a)
			LAssert(!"Wut?");
		else
			result.Add(a);
	}
	
	return result;
}

bool Mail::GetAttachments(List<Attachment> *Files)
{
	if (!Files)
		return false;
	auto files = GetAttachments();
	for (auto a: files)
		Files->Add(a);
	return true;
}

int64 Mail::TotalSizeof()
{
	if (TotalSizeCache < 0)
	{
		int Size = GetObject() ? (int)GetObject()->GetInt(FIELD_SIZE) : -1;
		if (Size >= 0)
		{
			TotalSizeCache = Size;
		}
		else
		{
			TotalSizeCache =	((GetBody()) ? strlen(GetBody()) : 0) +
								((GetHtml()) ? strlen(GetHtml()) : 0);

			List<Attachment> Attachments;
			if (GetAttachments(&Attachments))
			{
				for (auto a: Attachments)
				{
					TotalSizeCache += a->GetSize();
				}
			}
		}
	}

	return TotalSizeCache;
}

bool Mail::_GetListItems(List<LListItem> &l, bool All)
{
	LList *ParentList = LListItem::Parent;

	l.Empty();

	if (All)
	{
		if (ParentList)
		{
			ParentList->GetAll(l);
		}
		else
		{
			l.Insert(this);
		}
	}
	else
	{
		if (ParentList)
		{
			ParentList->GetSelection(l);
		}
		else if (Select())
		{
			l.Insert(this);
		}
	}

	return l.Length() > 0;
}

void Mail::GetThread(List<Mail> &Thread)
{
	MContainer *c;
	for (c=Container; c->Parent; c=c->Parent);
	List<MContainer> Stack;
	Stack.Insert(c);
	while ((c=Stack[0]))
	{
		Stack.Delete(c);
		
		for (unsigned i=0; i<c->Children.Length(); i++)
		    Stack.Insert(c->Children[i]);
		
		if (c->Message && !Thread.HasItem(c->Message))
		{
			Thread.Insert(c->Message);
		}
	}
}

void Mail::SetListRead(bool Read)
{
	List<LListItem> Sel;
	if (!_GetListItems(Sel, false))
		return;

	LArray<LDataI*> a;
	for (auto t: Sel)
	{
		auto m = dynamic_cast<Mail*>(t);
		if (!m)
			continue;

		bool isRead = (m->GetFlags() & MAIL_READ) != 0;
		if (Read ^ isRead)
			a.Add(m->GetObject());
	}

	if (!a.Length())
		return;

	auto Store = a[0]->GetStore();
	if (!Store)
	{
		LAssert(0);
		return;
	}

	LVariant read = MAIL_READ;
	Store->Change(a, FIELD_FLAGS, read, Read ? OpPlusEquals : OpMinusEquals);
}

void SetFolderCallback(LInput *Dlg, LViewI *EditCtrl, void *Param)
{
	ScribeWnd *App = (ScribeWnd*) Param;
	auto Str = EditCtrl->Name();
	auto Select = new FolderDlg(Dlg, App, MAGIC_MAIL, 0, Str);
	Select->DoModal([Select, EditCtrl](auto dlg, auto id)
	{
		if (id)
			EditCtrl->Name(Select->Get());
		delete dlg;
	});
}

void Mail::DoContextMenu(LMouse &m, LView *p)
{
	#ifdef _DEBUG
	LAutoPtr<LProfile> Prof(new LProfile("Mail::DoContextMenu"));
	Prof->HideResultsIfBelow(100);
	#endif
	
	if (!p)
		p = Parent;

	// open the right click menu
	MarkedState MarkState = MS_None;
	
	// Pre-processing for marks
	List<LListItem> Sel;
	if (_GetListItems(Sel, false))
	{
		int Marked = 0;
		for (auto t: Sel)
		{
			Mail *m = dynamic_cast<Mail*>(t);
			if (m)
			{
				if (m->GetMarkColour())
				{
					Marked++;
				}
			}
		}

		if (Marked == 1)
		{
			MarkState = MS_One;
		}
		else if (Marked)
		{
			MarkState = MS_Multiple;
		}
	}

	#ifdef _DEBUG
	Prof->Add("CreateMenu");
	#endif

	// Create menu
	LScriptUi s(new LSubMenu);
	if (s.Sub)
	{
		/*
		Keyboard shortcuts:

		o - Open
		d - Delete
		x - Export
		e - Set read
		u - Set unread
		r - Reply
		a - Reply all
		f - Forward
		b - Bounce
		m - Mark
		s - Select marked
		c - Create filter
		i - Inspect
		p - Properties
		
		*/
		LMenuItem *i;
		s.Sub->SetImageList(App->GetIconImgList(), false);

		s.Sub->AppendItem(LLoadString(IDS_OPEN), IDM_OPEN, true);
		i = s.Sub->AppendItem(LLoadString(IDS_DELETE), IDM_DELETE, true);
		i->Icon(ICON_TRASH);
		s.Sub->AppendItem(LLoadString(IDS_EXPORT), IDM_EXPORT, true);
		int AttachBaseMsg = 10000;

		#ifdef _DEBUG
		Prof->Add("GetAttachments");
		#endif

		List<Attachment> AttachLst;
		if (GetAttachments(&AttachLst) &&
			AttachLst[0])
		{
			LSubMenu *Attachments = s.Sub->AppendSub(LLoadString(IDS_SAVE_ATTACHMENT_AS));
			if (Attachments)
			{
				int n=0;
				for (auto a: AttachLst)
				{
					auto Name = a->GetName();
					Attachments->AppendItem(Name?Name:(char*)"Attachment.txt", AttachBaseMsg+(n++), true);
				}
			}
		}
		s.Sub->AppendSeparator();

		#ifdef _DEBUG
		Prof->Add("Read/Unread");
		#endif

		i = s.Sub->AppendItem(AddAmp(LLoadString(IDS_SET_READ), 'e'), IDM_SET_READ, true);
		i->Icon(ICON_READ_MAIL);
		i = s.Sub->AppendItem(AddAmp(LLoadString(IDS_SET_UNREAD), 'u'), IDM_SET_UNREAD, true);
		i->Icon(ICON_UNREAD_MAIL);
		s.Sub->AppendSeparator();

		#ifdef _DEBUG
		Prof->Add("Reply/Bounce");
		#endif

		i = s.Sub->AppendItem(AddAmp(LLoadString(IDS_REPLY), 'r'), IDM_REPLY, true);
		i->Icon(ICON_FLAGS_REPLY);
		s.Sub->AppendItem(AddAmp(LLoadString(IDS_REPLYALL), 'a'), IDM_REPLY_ALL, true);
		i = s.Sub->AppendItem(AddAmp(LLoadString(IDS_FORWARD), 'f'), IDM_FORWARD, true);
		i->Icon(ICON_FLAGS_FORWARD);
		i = s.Sub->AppendItem(AddAmp(LLoadString(IDS_BOUNCE), 'b'), IDM_BOUNCE, true);
		i->Icon(ICON_FLAGS_BOUNCE);
		s.Sub->AppendSeparator();

		#ifdef _DEBUG
		Prof->Add("Thread");
		#endif

		if (App->GetCtrlValue(IDM_THREAD))
		{
			auto Thread = s.Sub->AppendSub(LLoadString(IDS_THREAD));
			if (Thread)
			{
				Thread->AppendItem(LLoadString(IDS_THREAD_SELECT), IDM_SELECT_THREAD);
				Thread->AppendItem(LLoadString(IDS_THREAD_DELETE), IDM_DELETE_THREAD);
				Thread->AppendItem(LLoadString(IDS_THREAD_IGNORE), IDM_IGNORE_THREAD);
				s.Sub->AppendSeparator();
			}
		}

		#ifdef _DEBUG
		Prof->Add("Mark");
		#endif

		auto MarkMenu = s.Sub->AppendSub(AddAmp(LLoadString(IDS_MARK), 'm'));
		if (MarkMenu)
		{
			BuildMarkMenu(MarkMenu, MarkState, (uint32_t)GetMarkColour(), true);
		}

		MarkMenu = s.Sub->AppendSub(AddAmp(LLoadString(IDS_SELECT_MARKED), 's'));
		if (MarkMenu)
		{
			BuildMarkMenu(MarkMenu, MS_Multiple, 0, true, true, true);
		}

		s.Sub->AppendSeparator();
		s.Sub->AppendItem(AddAmp(LLoadString(IDS_INSPECT), 'i'), IDM_INSPECT);
		s.Sub->AppendItem(LLoadString(IDS_REPARSE), IDM_REPARSE);
		s.Sub->AppendItem(LLoadString(IDS_PROPERTIES), IDM_PROPERTIES);

		#ifdef _DEBUG
		Prof->Add("ScriptCallbacks");
		#endif

		LArray<LScriptCallback*> Callbacks;
		if (App->GetScriptCallbacks(LThingContextMenu, Callbacks))
		{
			LScriptArguments Args(NULL);
			Args[0] = new LVariant(App);
			Args[1] = new LVariant(this);
			Args[2] = new LVariant(&s);
			
			for (auto c: Callbacks)
				App->ExecuteScriptCallback(*c, Args);
		}

		m.ToScreen();
		int Result;

		#ifdef _DEBUG
		Prof.Reset();
		#endif

		int Btn = 0;
		if (m.Left())
			Btn = LSubMenu::BtnLeft;
		else if (m.Right())
			Btn = LSubMenu::BtnRight;

		Result = s.Sub->Float(p, m.x, m.y);
		switch (Result)
		{
			case IDM_OPEN:
			{
				DoUI();
				break;
			}
			case IDM_DELETE:
			{
				LVariant ConfirmDelete = false;
				App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

				if (!ConfirmDelete.CastInt32() ||
					LgiMsg(GetList(), LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
				{
					if (_GetListItems(Sel, false))
					{
						int Index = -1;
						LList *TheList = LListItem::Parent;
						LArray<Thing*> Items;

						for (auto s: Sel)
						{
							Mail *m = dynamic_cast<Mail*>(s);
							if (m)
							{
								if (Index < 0) Index = TheList->IndexOf(m);
								Items.Add(m);
							}
						}
						
						GetFolder()->Delete(Items, true);

						if (Index >= 0)
						{
							LListItem *i = TheList->ItemAt(Index);
							if (i) i->Select(true);
						}
					}
				}
				break;
			}
			case IDM_EXPORT:
			{
				ExportAll(Parent, sMimeMessage, NULL);
				break;
			}
			case IDM_REPLY:
			case IDM_REPLY_ALL:
			{
				App->MailReplyTo(this, Result == IDM_REPLY_ALL);
				SetDirty(false);
				break;
			}
			case IDM_FORWARD:
			{
				App->MailForward(this);
				SetDirty(false);
				break;
			}
			case IDM_BOUNCE:
			{
				App->MailBounce(this);
				SetDirty(false);
				break;
			}
			case IDM_SET_READ:
			{
				SetListRead(true);
				break;
			}
			case IDM_SET_UNREAD:
			{
				SetListRead(false);
				break;
			}
			case IDM_INSPECT:
			{
				OnInspect();
				break;
			}
			case IDM_PROPERTIES:
			{
				OnProperties();
				break;
			}
			case IDM_SELECT_THREAD:
			{
				if (_GetListItems(Sel, false))
				{
					List<Mail> Thread;
					for (auto s: Sel)
					{
						Mail *m = dynamic_cast<Mail*>(s);
						if (m)
							m->GetThread(Thread);
					}
					for (auto m: Thread)
					{
						m->Select(true);
					}
				}
				break;
			}
			case IDM_DELETE_THREAD:
			{
				if (_GetListItems(Sel, false))
				{
					List<Mail> Thread;
					for (auto s: Sel)
					{
						Mail *m = dynamic_cast<Mail*>(s);
						if (m)
							m->GetThread(Thread);
					}
					for (auto m: Thread)
					{
						m->OnDelete();
					}
				}
				break;
			}
			case IDM_IGNORE_THREAD:
			{
				if (_GetListItems(Sel, false))
				{
					List<Mail> Thread;
					for (auto s: Sel)
					{
						Mail *m = dynamic_cast<Mail*>(s);
						if (m)
							m->GetThread(Thread);
					}
					for (auto m: Thread)
					{
						m->SetFlags(m->GetFlags() | MAIL_IGNORE | MAIL_READ);
					}
				}
				break;
			}
			case IDM_REPARSE:
			{
				if (!_GetListItems(Sel, false))
					break;

				for (auto s: Sel)
				{
					Mail *m = dynamic_cast<Mail*>(s);
					if (m)
						m->Reparse();
				}
				break;
			}
			case IDM_UNMARK:
			case IDM_SELECT_NONE:
			case IDM_SELECT_ALL:
			default:
			{
				if (Result == IDM_UNMARK ||
					(Result >= IDM_MARK_BASE && Result < IDM_MARK_BASE+CountOf(MarkColours32)))
				{
					bool Marked = Result != IDM_UNMARK;

					if (_GetListItems(Sel, false))
					{
						COLOUR Col32 = Marked ? MarkColours32[Result - IDM_MARK_BASE] : 0;

						for (auto s: Sel)
						{
							Mail *m = dynamic_cast<Mail*>(s);
							if (m)
							{
								if (m->SetMarkColour(Col32))
								{
									if (m->GetObject()->GetInt(FIELD_STORE_TYPE) != Store3Imap) // Imap knows to save itself.
										m->SetDirty();
								}
								m->Update();
							}
						}
					}
				}
				else if (Result == IDM_SELECT_NONE ||
						 Result == IDM_SELECT_ALL ||
						(Result >= IDM_MARK_SELECT_BASE && Result < IDM_MARK_SELECT_BASE+CountOf(MarkColours32)))
				{
					bool None = Result == IDM_SELECT_NONE;
					bool All = Result == IDM_SELECT_ALL;
					uint32_t c32 = MarkColours32[Result - IDM_MARK_SELECT_BASE];

					if (_GetListItems(Sel, true))
					{
						for (auto s: Sel)
						{
							Mail *m = dynamic_cast<Mail*>(s);
							if (!m)
								break;

							auto CurCol = m->GetMarkColour();
							if (None)
							{
								m->Select(CurCol <= 0);
							}
							else
							{
								if (CurCol > 0)
								{
									if (All)
									{
										m->Select(true);
									}
									else
									{
										m->Select(CurCol && CurCol == c32);
									}
								}
								else
								{
									m->Select(false);
								}
							}
						}
					}
				}
				else if (Result >= AttachBaseMsg &&
						 Result < AttachBaseMsg + (ssize_t)AttachLst.Length())
				{
					// save attachment as...
					Attachment *a = AttachLst.ItemAt(Result - AttachBaseMsg);
					if (a)
					{
						a->OnSaveAs(Parent);
					}
				}
				else
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
				}
				break;
			}
		}

		DeleteObj(s.Sub);
	}
}

void Mail::OnMouseClick(LMouse &m)
{
	if (m.Down())
	{
		if (!m.IsContextMenu())
		{
			if (m.Double())
			{
				// open the UI for the Item
				DoUI();
			}
		}
		else
		{
			DoContextMenu(m);
		}
	}
}

void Mail::OnCreate()
{
	LOptionsFile *Options = App->GetOptions();
	LVariant v;
	if (GetObject()) GetObject()->SetInt(FIELD_FLAGS, MAIL_CREATED);

	// Get identity and set from info
	ScribeAccount *Ident = App->GetCurrentAccount();
	if (Ident)
	{
		v = Ident->Identity.Name();
		GetFrom()->SetStr(FIELD_NAME, v.Str());
		v = Ident->Identity.Email();
		GetFrom()->SetStr(FIELD_EMAIL, v.Str());
		
		v = Ident->Identity.ReplyTo();
		if (v.Str())
			GetReply()->SetStr(FIELD_REPLY, v.Str());
	}
	else
	{
		LAssert(!"No identity selected");
	}

	LVariant EditCtrl;
	App->GetOptions()->GetValue(OPT_EditControl, EditCtrl);
	LAutoString Sig = GetSig(EditCtrl.CastInt32() != 0, Ident);
	if (!Sig && EditCtrl.CastInt32())
	{
		Sig = GetSig(false, Ident);
		SetBody(Sig);
	}
	else if (EditCtrl.CastInt32())
	{
		SetHtmlCharset("utf-8");
		SetHtml(Sig);
	}
	else
	{
		SetBodyCharset("utf-8");
		SetBody(Sig);
	}

	LVariant ClipRecip;
	if (Options->GetValue(OPT_RecipientFromClipboard, ClipRecip) &&
		ClipRecip.CastInt32())
	{
		LClipBoard Clip(App);
		char *Txt = Clip.Text();
		if (Txt && strchr(Txt, '@') && strlen(Txt) < 100)
		{
			for (char *s=Txt; *s; s++)
			{
				if (*s == '\n' || *s == '\r')
				{
					*s = 0;
				}
			}

			Mailto mt(App, Txt);

			for (auto a: mt.To)
			{
				LDataPropI *OldA = dynamic_cast<LDataPropI*>(a);
				if (OldA)
				{
					LDataPropI *NewA = GetTo()->Create(GetObject()->GetStore());
					if (NewA)
					{
						NewA->CopyProps(*OldA);
						GetTo()->Insert(NewA);
					}
				}
			}
			mt.To.Empty();
			
			if (mt.Subject && !ValidStr(GetSubject()))
			{
				SetSubject(mt.Subject);
			}

			/*
			if (_strnicmp(Txt, MailToStr, 7) == 0)
			{
				char *Question = strchr(Txt + 7, '?');
				if (Question)
				{
					*Question = 0;
					Question++;

					int Len = strlen(SubjectStr);
					if (_strnicmp(Question, SubjectStr, Len) == 0)
					{
						Subject = NewStr(Question + Len);
					}
				}

				La->Addr = NewStr(Txt + 7);
			}
			else
			{
				La->Addr = NewStr(Txt);
			}

			To.Insert(La);
			*/
		}
	}

	Update();
}

void Mail::Reparse()
{
	#ifdef _DEBUG
	_debug = true;
	#endif

	// This temporarily removes the attachments that will be 
	// deleted by the call to ParseHeaders after this...
	ClearCachedItems();

	Thing::Reparse();
}

void Mail::CreateMailHeaders()
{
	LStringPipe Hdrs(256);
	MailProtocol Protocol;
	LVariant HideId;
    App->GetOptions()->GetValue(OPT_HideId, HideId);
	Protocol.ProgramName = GetFullAppName(!HideId.CastInt32());
		
	LDataI *Obj = GetObject();
	if (Obj && ::CreateMailHeaders(App, Hdrs, Obj, &Protocol))
	{
		LAutoString FullHdrs(Hdrs.NewStr());
		Obj->SetStr(FIELD_INTERNET_HEADER, FullHdrs);
	}
	else LAssert(!"CreateMailHeaders failed.");
}

bool Mail::OnBeforeSend(ScribeEnvelope *Out)
{
	if (!Out)
	{
		LAssert(!"No output envelope.");
		return false;
	}
	
	// First check the email from address...
	if (!ValidStr(GetFrom()->GetStr(FIELD_EMAIL)))
	{
		auto Options = App->GetOptions();
		if (Options)
		{
			auto Ident = App->GetAccounts()->ItemAt(App->GetCurrentIdentity());
			if (Ident)
			{
				auto v = Ident->Identity.Email();
				GetFrom()->SetStr(FIELD_EMAIL, v.Str());
				v = Ident->Identity.Name();
				GetFrom()->SetStr(FIELD_NAME, v.Str());
			}
		}
	}

	Out->From = GetFromStr(FIELD_EMAIL);
	auto To = GetTo();
	ContactGroup *Group = NULL;
	for (auto t = To->First(); t; t = To->Next())
	{
		LString Addr = t->GetStr(FIELD_EMAIL);
		if (LIsValidEmail(Addr))
			Out->To.New() = Addr;
		else if ((Group = LookupContactGroup(App, Addr)))
		{
			LString::Array a = Group->GetAddresses();
			Out->To.Add(a);
		}
	}

	auto Root = GetObject()->GetObj(FIELD_MIME_SEG);
	if (!Root)
	{
		LAssert(!"No root element.");
		return false;
	}

	// Check the headers have been created..
	if (!Root->GetStr(FIELD_INTERNET_HEADER))
		CreateMailHeaders();

	Out->MsgId = GetMessageId(true);
	
	// Convert the mime stream
	LMime Mime(ScribeTempPath());
	LTempStream Buf(ScribeTempPath());
	
	Store3ToLMime(&Mime, Root);
	
	// Do the encode
	if (!Mime.Text.Encode.Push(&Buf))
	{
		LAssert(!"Mime encode failed.");
		return false;
	}

	Out->References = GetReferences();
	Out->FwdMsgId = GetFwdMsgId();
	Out->BounceMsgId = GetBounceMsgId();
	
	// Read the resulting string into the output envelope
	auto Sz = Buf.GetSize();
	Out->Rfc822.Length(Sz);
	Buf.SetPos(0);
	Buf.Read(Out->Rfc822.Get(), Sz);
	Out->Rfc822.Get()[Sz] = 0;

	return true;
}

void Mail::OnAfterSend()
{
	int f = GetFlags();
	f |= MAIL_SENT | MAIL_READ;	// set sent flag
	f &= ~MAIL_READY_TO_SEND;	// clear read to send flag

	// LgiTrace("Setting flags: %x\n", f);
	SetFlags(f);

	LDateTime n;
	n.SetNow();
	SetDateSent(&n);
	// LString s = GetDateSent()->Get();
	// LgiTrace("Setting sent date: %s\n", s.Get());

	Update();
	SetDirty();

	if (App)
	{
		ScribeFolder *Sent = App->GetFolder(FOLDER_SENT, GetObject());
		if (Sent)
		{
			LArray<Thing*> Items;
			Items.Add(this);
			Sent->MoveTo(Items, false);
		}
	}
}

bool Mail::OnBeforeReceive()
{
	return true;
}

enum MimeDecodeMode
{
	MODE_FIELDS,
	MODE_WAIT_MSG,
	MODE_SEGMENT,
	MODE_WAIT_TYPE,
	MODE_WAIT_BOUNDRY
};

#define MF_UNKNOWN						1
#define MF_SUBJECT						2
#define MF_TO							3
#define MF_FROM							4
#define MF_REPLY_TO						5
#define MF_CONTENT_TYPE					6
#define MF_CC							7
#define MF_CONTENT_TRANSFER_ENCODING	9
#define MF_DATE_SENT					10
#define MF_PRIORITY						11
#define MF_COLOUR						12
#define MF_DISPOSITIONNOTIFICATIONTO	13

void MungCharset(Mail *Msg, bool &HasRealCs, ScribeAccount *Acc)
{
	if (ValidStr(Msg->GetBodyCharset()))
	{
		HasRealCs = true;
	}

	if (Acc)
	{
		// LCharset *Cs = 0;
		if (Msg->GetBodyCharset() &&
			stristr(Msg->GetBodyCharset(), "ascii") &&
			Acc->Receive.AssumeAsciiCharset().Str())
		{
			Msg->SetBodyCharset(Acc->Receive.AssumeAsciiCharset().Str());
			HasRealCs = false;
		}
		else if (!ValidStr(Msg->GetBodyCharset()) ||
				!LGetCsInfo(Msg->GetBodyCharset()))
		{
			Msg->SetBodyCharset(Acc->Receive.Assume8BitCharset().Str());
			HasRealCs = false;
		}
	}
}

bool Mail::OnAfterReceive(LStreamI *Msg)
{
	if (!Msg)
	{
		LAssert(!"No input.");
		return false;
	}

	// Clear the codepage setting here so that we can
	// check later, down the bottom we must set it to
	// the default if it's not set anywhere along the
	// way.
	SetBodyCharset(0);

	auto obj = GetObject();
	if (!obj)
	{
		LAssert(!"No storage object.");
		return false;
	}

	if (!obj->SetRfc822(Msg))
	{
		LgiTrace("%s:%i - SetRfc822 failed with '%s'\n", _FL, obj->GetStr(FIELD_ERROR));
		return false;
	}

	// Now parse them into the meta fields...
	obj->ParseHeaders();
	
	ScribeAccount *Acc = GetAccountSentTo();
	LAutoString ContentType;
	
	// Fill out any Contact's TimeZone if missing
	LAutoString DateStr(InetGetHeaderField(GetInternetHeader(), "Date"));
	LDateTime DateObj;
	if (DateStr &&
		DateObj.Decode(DateStr))
	{
	    double SenderTz = DateObj.GetTimeZoneHours();
	    if (SenderTz != 0.0)
	    {
		    ListAddr *Sender = dynamic_cast<ListAddr*>(GetFrom());
		    if (Sender)
		    {
				auto It = Sender->begin();
				if (*It)
				{
					Contact *c = (*It)->GetContact();
					if (c)
					{
						const char *Tz = "";
						if (!c->Get(OPT_TimeZone, Tz) ||
							atof(Tz) != SenderTz)
						{
							char s[32];
							sprintf_s(s, sizeof(s), "%.1f", SenderTz);
							c->Set(OPT_TimeZone, s);
							c->SetDirty(true);
						}
					}
				}
		    }
	    }
	}

	auto BodyText = GetBody();
	if (BodyText)
	{
		if (!GetBodyCharset())
		{
			if (Acc && Acc->Receive.Assume8BitCharset().Str())
			{
				SetBodyCharset(Acc->Receive.Assume8BitCharset().Str());
			}
			else
			{
				SetBodyCharset("us-ascii");
			}
		}

		LStringPipe NewBody;
		LArray<LDataI*> Files;
		if (DecodeUuencodedAttachment(obj->GetStore(), Files, &NewBody, BodyText))
		{
			LDataI *AttachPoint = GetFileAttachPoint();
			if (AttachPoint)
			{
				for (unsigned i=0; i<Files.Length(); i++)
				{
					Files[i]->SetStr(FIELD_MIME_TYPE, sAppOctetStream);
					Files[i]->Save(AttachPoint);
				}

				SetDirty(!TestFlag(GetFlags(), MAIL_ATTACHMENTS));
				SetFlags(GetFlags() | MAIL_ATTACHMENTS);

				LAutoString n(NewBody.NewStr());
				SetBody(n);

				Update();
				if (Ui) Ui->OnAttachmentsChange();
			}
		}
	}

	LDateTime n;
	n.SetNow();
	SetDateReceived(&n);

	Update();
	SetDirty();

	// Call any 'after receive' callbacks
	LArray<LScriptCallback*> Callbacks;
	if (App->GetScriptCallbacks(LMailOnAfterReceive, Callbacks))
	{
		for (auto c: Callbacks)
		{
			if (!c->Func)
				continue;

			LVirtualMachine Vm;				
			LScriptArguments Args(&Vm);
			Args.New() = new LVariant(App);
			Args.New() = new LVariant(this);
			App->ExecuteScriptCallback(*c, Args);
			Args.DeleteObjects();
		}
	}

	return true;
}

ThingUi *Mail::GetUI()
{
	return Ui;
}

LVariant Mail::GetServerUid()
{
	LVariant v;
	
	if (GetObject())
	{
		auto Type = (Store3Backend)GetObject()->GetInt(FIELD_STORE_TYPE);
		if (Type == Store3Imap)
			v = GetObject()->GetInt(FIELD_SERVER_UID);
		else
			v = GetObject()->GetStr(FIELD_SERVER_UID);
	}
	else LAssert(!"No object.");

	return v;
}

bool Mail::SetServerUid(LVariant &v)
{
	if (!GetObject())
		return false;
	
	Store3Status s;
	if (GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Imap)
		s = GetObject()->SetInt(FIELD_SERVER_UID, v.CastInt32());
	else
		s = GetObject()->SetStr(FIELD_SERVER_UID, v.Str());

	return s > Store3Error;
}

bool Mail::SetObject(LDataI *o, bool IsDestructor, const char *File, int Line)
{
	bool b = LDataUserI::SetObject(o, IsDestructor, File, Line);
	if (b)
	{
		// Clear out stale attachment objects...
		UnloadAttachments();
	}
	return b;
}

bool Mail::SetUI(ThingUi *new_ui)
{
	MailUi *NewMailUi = dynamic_cast<MailUi*>(new_ui);
	if (NewMailUi == Ui)
		return true;

	if (Ui)
	{
		LWindow *w = Ui;
		Ui->SetItem(0);
		w->Quit();
		LAssert(Ui == NULL);
	}
	
	if (new_ui)
	{
		Ui = dynamic_cast<MailUi*>(new_ui);
		if (Ui)
			Ui->SetItem(this);
		else
			LAssert(!"Incorrect object.");
	}
	
	return true;
}

ThingUi *Mail::DoUI(MailContainer *c)
{
	if (App &&
		!Ui)
	{
		if (App->InThread())
		{
			Ui = new MailUi(this, c ? c : GetFolder());
		}
		else
		{
            App->PostEvent(M_SCRIBE_OPEN_THING, (LMessage::Param)this, 0);
		}
	}

	if (Ui)
	{
		#if WINNATIVE
		SetActiveWindow(Ui->Handle());
		#endif
	}

	return Ui;
}

int Mail::Compare(LListItem *t, ssize_t Field)
{
	Thing *T = (Thing*)t->_UserPtr;
	Mail *m = T ? T->IsMail() : 0;
	if (m)
	{
		static int Fields[] = {FIELD_FROM, FIELD_SUBJECT, FIELD_SIZE, FIELD_DATE_RECEIVED};
		if (Field < 0)
		{
			auto i = -Field - 1;
			if (i >= 0 && i < CountOf(Fields))
			{
				Field = Fields[i];
			}
		}

		LVariant v1, v2;
		const char *s1 = "", *s2 = "";
		switch (Field)
		{
			case 0:
			{
				break;
			}
			case FIELD_TO:
			{
				LDataPropI *a1 = GetTo()->First();
				if (a1)
				{
					if (a1->GetStr(FIELD_NAME)) s1 = a1->GetStr(FIELD_NAME);
					else if (a1->GetStr(FIELD_EMAIL)) s1 = a1->GetStr(FIELD_EMAIL);
				}

				LDataPropI *a2 = m->GetTo()->First();
				if (a2)
				{
					if (a2->GetStr(FIELD_NAME)) s2 = a2->GetStr(FIELD_NAME);
					else if (a2->GetStr(FIELD_EMAIL)) s2 = a2->GetStr(FIELD_EMAIL);
				}
				break;
			}
			case FIELD_FROM:
			{
				LDataPropI *f1 = GetFrom();
				LDataPropI *f2 = m->GetFrom();

				if (f1->GetStr(FIELD_NAME)) s1 = f1->GetStr(FIELD_NAME);
				else if (f1->GetStr(FIELD_EMAIL)) s1 = f1->GetStr(FIELD_EMAIL);

				if (f2->GetStr(FIELD_NAME)) s2 = f2->GetStr(FIELD_NAME);
				else if (f2->GetStr(FIELD_EMAIL)) s2 = f2->GetStr(FIELD_EMAIL);
				break;
			}
			case FIELD_SUBJECT:
			{
				s1 = GetSubject();
				s2 = m->GetSubject();
				break;
			}
			case FIELD_SIZE:
			{
				return (int) (TotalSizeof() - m->TotalSizeof());
				break;
			}
			case FIELD_DATE_SENT:
			{
			    auto Sent1 = GetDateSent();
			    auto Sent2 = m->GetDateSent();
			    if (!Sent1 || !Sent2)
			        break;
				return Sent1->Compare(Sent2);
				break;
			}
			case FIELD_DATE_RECEIVED:
			{
				return GetDateReceived()->Compare(m->GetDateReceived());
				break;
			}
			case FIELD_PRIORITY:
			{
				return GetPriority() - m->GetPriority();
				break;
			}
			case FIELD_FLAGS:
			{
				int Mask = MAIL_FORWARDED | MAIL_REPLIED | MAIL_READ;
				return (GetFlags() & Mask) - (m->GetFlags() & Mask);
				break;
			}
			case FIELD_LABEL:
			{
				if (GetLabel()) s1 = GetLabel();
				if (m->GetLabel()) s2 = m->GetLabel();
				break;
			}
			case FIELD_MESSAGE_ID:
			{
				s1 = GetMessageId();
				s2 = m->GetMessageId();
				break;
			}
			case FIELD_FROM_CONTACT_NAME:
			{
				s1 = GetFieldText(FIELD_FROM_CONTACT_NAME);
				s2 = m->GetFieldText(FIELD_FROM_CONTACT_NAME);
				break;
			}
			case FIELD_SERVER_UID:
			{
				v1 = GetServerUid();
				v2 = m->GetServerUid();
				if (v1.Str() && v2.Str())
				{
					s1 = v1.Str();
					s2 = v2.Str();					
				}
				else
				{
					auto diff = v1.CastInt64() - v2.CastInt64();
					if (diff < 0) return -1;
					return diff > 1;
				}
			}
			default:
			{
				s1 = GetObject() ? GetObject()->GetStr((int)Field) : 0;
				s2 = m->GetObject() ? m->GetObject()->GetStr((int)Field) : 0;
				break;
			}
		}

		const char *Empty = "";
		return _stricmp(s1?s1:Empty, s2?s2:Empty);
	}

	return -1;
}

class MailPropDlg : public LDialog
{
	List<Mail> Lst;
	LViewI *Table = NULL;

	struct FlagInfo
	{
		int Flag = 0, Ctrl = 0;

		void Set(int f, int c)
		{
			Flag = f;
			Ctrl = c;
		}
	};
	LArray<FlagInfo> Flags;

public:
	MailPropDlg(LView *Parent, List<Mail> &lst)
	{
		SetParent(Parent);
		
		Lst = lst;

		Flags.New().Set(MAIL_SENT, IDC_SENT);
		Flags.New().Set(MAIL_RECEIVED, IDC_RECEIVED);
		Flags.New().Set(MAIL_CREATED, IDC_CREATED);
		Flags.New().Set(MAIL_FORWARDED, IDC_FORWARDED);
		Flags.New().Set(MAIL_REPLIED, IDC_REPLIED);
		Flags.New().Set(MAIL_ATTACHMENTS, IDC_HAS_ATTACH);
		Flags.New().Set(MAIL_READ, IDC_READ);
		Flags.New().Set(MAIL_READY_TO_SEND, IDC_READY_SEND);

		if (LoadFromResource(IDD_MAIL_PROPERTIES))
		{
			GetViewById(IDC_TABLE, Table);
			LAssert(Table != NULL);
			SetCtrlEnabled(IDC_OPEN_INSPECTOR, Lst.Length() == 1);
			
			for (auto &i: Flags)
			{
				int Set = 0;
				for (auto m: Lst)
				{
					if (m->GetFlags() & i.Flag)
						Set++;
				}
				
				if (Set == Lst.Length())
				{
					SetCtrlValue(i.Ctrl, LCheckBox::CheckOn);
				}
				else if (Set)
				{
					LCheckBox *Cb;
					if (GetViewById(i.Ctrl, Cb))
						Cb->ThreeState(true);

					SetCtrlValue(i.Ctrl, LCheckBox::CheckPartial);
				}
			}

			SetCtrlEnabled(IDC_HAS_ATTACH, false);

			char Msg[512] = "";
			if (Lst.Length() == 1)
			{
				Mail *m = Lst[0];
				auto mObj = m->GetObject();
				if (mObj)
				{
					auto dt = mObj->GetDate(FIELD_DATE_RECEIVED);
					sprintf_s(	Msg, sizeof(Msg),
								LLoadString(IDS_MAIL_PROPS_DLG),
								LFormatSize(mObj->GetInt(FIELD_SIZE)).Get(),
								dt ? dt->Get().Get() : LLoadString(IDS_NONE),
								mObj->GetStr(FIELD_DEBUG));

					SetCtrlName(IDC_MSG_ID, mObj->GetStr(FIELD_MESSAGE_ID));
				}
			}
			else
			{
				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_MULTIPLE_ITEMS), Lst.Length());
				SetCtrlEnabled(IDC_MSG_ID, false);
			}
			SetCtrlName(IDC_MAIL_DESCRIPTION, Msg);

			MoveSameScreen(Parent);
		}
	}
	
	void OnPosChange()
	{
		if (Table)
		{
			LRect r = GetClient();
			r.Inset(LTableLayout::CellSpacing, LTableLayout::CellSpacing);
			Table->SetPos(r);
		}
	}

	int OnNotify(LViewI *Ctr, LNotification n)
	{
		switch (Ctr->GetId())
		{
			case IDC_OPEN_INSPECTOR:
			{
				if (Lst.Length())
					Lst[0]->OnInspect();
				break;
			}
			case IDOK:
			{
				for (auto &i: Flags)
				{
					auto v = GetCtrlValue(i.Ctrl);
					LAssert(v >= 0); // the control couldn't be found... check the .lr8 file
					
					for (auto m: Lst)
					{
						if (v == 1)
							m->SetFlags(m->GetFlags() | i.Flag);
						else if (v == 0)
							m->SetFlags(m->GetFlags() & ~i.Flag);
					}
				}
				// fall thru
			}
			case IDCANCEL:
			{
				EndModal(Ctr->GetId());
				break;
			}
		}

		return 0;
	}
};

void Mail::OnInspect()
{
	new ObjectInspector(App, this);
}

void Mail::OnProperties(int Tab)
{
	List<LListItem> Sel;
	if (GetList()->GetSelection(Sel))
	{
		List<Mail> Lst;
		for (auto i: Sel)
		{
			Lst.Insert(dynamic_cast<Mail*>(i));
		}
		
		if (Lst[0])
		{
			auto Dlg = new MailPropDlg(GetList(), Lst);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id == IDOK)
				{
					SetDirty();
					Update();
				}
				delete dlg;
			});
		}
	}
}

int Mail::GetImage(int SelFlags)
{
	if (TestFlag(GetFlags(), MAIL_READY_TO_SEND) &&
		!TestFlag(GetFlags(), MAIL_SENT))
	{
		return ICON_UNSENT_MAIL;
	}
		
	if (GetFlags() & MAIL_READ)
	{
		return (GetFlags() & MAIL_ATTACHMENTS) ? ICON_READ_ATT_MAIL : ICON_READ_MAIL;
	}
	else
	{
		return (GetFlags() & MAIL_ATTACHMENTS) ? ICON_UNREAD_ATT_MAIL : ICON_UNREAD_MAIL;
	}

	return ICON_READ_MAIL;
}

int *Mail::GetDefaultFields()
{
	return DefaultMailFields;
}

void Mail::DeleteAsSpam(LView *View)
{
	// Set read..
	SetFlags(GetFlags() | MAIL_READ | MAIL_BAYES_SPAM, true);

	// Remove from NewMail
	NewMailLst.Delete(this);

	LVariant DeleteOnServer, DeleteAttachments, SetRead;
	auto Opts = App->GetOptions();
	if (!Opts->GetValue(OPT_BayesDeleteOnServer, DeleteOnServer))
		DeleteOnServer = false;
	if (!Opts->GetValue(OPT_BayesDeleteAttachments, DeleteAttachments))
		DeleteAttachments = false;
	if (!Opts->GetValue(OPT_BayesSetRead, SetRead))
		SetRead = false;

	if (DeleteOnServer.CastBool())
	{
		// Tell the account it's spam... so that any further connects can
		// delete it off the server.
		ScribeAccount *a = GetAccountSentTo();
		if (a)
		{
			auto ServerUid = GetServerUid();
			if (ServerUid.Str())
			{
				a->Receive.DeleteAsSpam(ServerUid.Str());
				ServerUid.Empty();
				SetServerUid(ServerUid);
			}
			else
			{
				LAutoString Uid(InetGetHeaderField(GetInternetHeader(), "X-UIDL"));
				if (Uid)
					a->Receive.DeleteAsSpam(Uid);
			}
		}
		else
		{
			#if 0 // def _DEBUG
			if (LgiMsg(Ui?(LView*)Ui:(LView*)App, "Debug: GetAccountSentTo failed. Debug?", AppName, MB_YESNO) == IDYES)
			{
				LAssert(0);
			}
			#endif
		}
	}

	if (DeleteAttachments.CastBool())
	{
		// Delete all attachments... they're useless and more than likely
		// just virii anyway.
		List<Attachment> Files;
		if (GetAttachments(&Files))
		{
			for (auto a: Files)
			{
				DeleteAttachment(a);
			}
			Files.Empty();
		}
	}

	if (SetRead.CastInt32() != 0)
	{
		SetFlags(GetFlags() | MAIL_READ);
	}

	// Move it to the spam folder if it exists.
	auto FolderPath = GetFolder()->GetPath();
	auto Parts = FolderPath.SplitDelimit("/");
	
	if (Parts.Length() == 0)
		LgiMsg(View, "Error: No folder path?", AppName);
	else
	{
		LString SpamPath;
		LString SpamLeaf = "Spam";
		SpamPath.Printf("/%s/%s", Parts[0].Get(), SpamLeaf.Get());

		ScribeFolder *Spam = App->GetFolder(SpamPath);
		if (!Spam)
		{
			LMailStore *Ms = App->GetMailStoreForPath(FolderPath);
			if (!Ms)
			{
				if (GetFolder()->GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Imap)
				{
					Ms = App->GetDefaultMailStore();
					if (Ms && Ms->Root)
					{
						Spam = Ms->Root->GetSubFolder(SpamLeaf);
						if (!Spam)
							Spam = Ms->Root->CreateSubFolder(SpamLeaf, MAGIC_MAIL);
					}
				}
				else
					LgiMsg(View, "Error: Couldn't get mail store for '%s'.", AppName, MB_OK, FolderPath.Get());
			}
			else
				Spam = Ms->Root->CreateSubFolder("Spam", MAGIC_MAIL);
		}

		if (Spam && Spam != GetFolder())
		{
			LArray<Thing*> Items;
			Items.Add(this);
			Spam->MoveTo(Items, false, [View](auto result, auto status)
			{
				if (!result)
					LgiMsg(View, "Error: Couldn't move email to spam folder.", AppName);
			});
		}
	}
}

void Mail::SetFlagsCache(int64_t NewFlags, bool IgnoreReceipt, bool UpdateScreen)
{
	if (FlagsCache == NewFlags)
		return;

	DepthCheck Depth(d->InSetFlagsCache);
		
	bool Read1 = TestFlag(FlagsCache, MAIL_READ);
	bool Read2 = TestFlag(NewFlags, MAIL_READ);
	bool ChangeRead = Read1 ^ Read2;

	// LgiTrace("%p::SetFlagsCache: %s -> %s\n", this, EmailFlagsToStr(FlagsCache).Get(), EmailFlagsToStr(StoreFlags).Get());
	FlagsCache = NewFlags;

	if (ChangeRead && App)
	{
		if (Read2)
		{
			// Becoming read
			List<Mail> Objs;
			Objs.Insert(this);
			App->OnNewMail(&Objs, false);
			PreviewCache.DeleteObjects();

			// Read receipt
			if (!IgnoreReceipt &&
				!TestFlag(NewFlags, MAIL_CREATED) &&
				TestFlag(NewFlags, MAIL_READ_RECEIPT))
			{
				LAutoString Header(InetGetHeaderField(GetInternetHeader(), "Disposition-Notification-To"));
				if (Header)
				{
					LAutoString Name, Addr;
					DecodeAddrName(Header, Name, Addr, 0);
					if (Addr)
					{
						if (LgiMsg(	Ui != 0 ? (LView*)Ui : (LView*)App,
									LLoadString(IDS_RECEIPT_ASK),
									AppName,
									MB_YESNO,
									GetSubject(),
									GetFrom()->GetStr(FIELD_NAME),
									GetFrom()->GetStr(FIELD_EMAIL),
									(char*)Name,
									(char*)Addr) == IDYES)
						{
							Mail *m = new Mail(App);
							if (m)
							{
								m->App = App;

								LDataPropI *ToAddr = m->GetTo()->Create(m->GetObject()->GetStore());
								if (ToAddr)
								{
									ToAddr->SetStr(FIELD_NAME, Name);
									ToAddr->SetStr(FIELD_EMAIL, Addr);
									m->GetTo()->Insert(ToAddr);
								}
									
								m->OnReceipt(this);
								m->Save();
								App->Send();
							}
						}
					}
				}
			}

			LVariant Inc;
			if (App->GetOptions()->GetValue(OPT_BayesIncremental, Inc) &&
				Inc.CastInt32())
			{
				// Incremental bayesian update
				App->OnBayesianMailEvent(this, BayesMailUnknown, BayesMailHam);
			}
		}

		if (UpdateScreen)
		{
			// Changing read status
			ScribeFolder *t = GetFolder();
			if (t)
				t->OnUpdateUnRead(0, true);
		}
	}

	if (UpdateScreen || ChangeRead)
	{
		Update();
	}
}


uint32_t Mail::GetFlags()
{
	if (GetObject())
	{
		// Make sure the objects are in sync...
		auto StoreFlags = GetObject()->GetInt(FIELD_FLAGS);
		if (FlagsCache < 0)
			FlagsCache = StoreFlags;
		else if (FlagsCache != StoreFlags)
			SetFlagsCache(StoreFlags, false, true);
	}

	return (uint32_t)FlagsCache;
}

void Mail::SetFlags(ulong NewFlags, bool IgnoreReceipt, bool UpdateScreen)
{
	if (FlagsCache == NewFlags)
		return;

	DepthCheck SetFlagsDepth(d->InSetFlags);

	if (GetObject())
	{
		Store3Status Result = GetObject()->SetInt(FIELD_FLAGS, NewFlags);
		if (Result == Store3Success &&
			GetObject()->IsOnDisk())
		{
			// Imap mail shouldn't fall in here.
			SetDirty();
		}
	}
	else
	{
		LAssert(0);
		return;
	}

	SetFlagsCache(NewFlags, IgnoreReceipt, UpdateScreen);
}

const char *Mail::GetFieldText(int Field)
{
	static char Buf[512];

	switch (Field)
	{
		case FIELD_TO:
		{
			size_t ch = 0;
			Buf[0] = 0;
			
			LDataIt To = GetTo();
			for (LDataPropI *a=To->First(); a; a=To->Next())
			{
				if (ch > 0)
					ch += sprintf_s(Buf+ch, sizeof(Buf)-ch, ", ");
				
				auto Name = a->GetStr(FIELD_NAME);
				auto Addr = a->GetStr(FIELD_EMAIL);
				auto n = Name ? Name : Addr;
				if (!n)
					continue;

				// Is the buffer too small?
				size_t n_len = strlen(n);
				if (ch + n_len + 8 >= sizeof(Buf))
				{
					// Yes... just truncate it with "..." and bail.
					ch += sprintf_s(Buf+ch, sizeof(Buf)-ch, "...");
					break;
				}
				ch += sprintf_s(Buf+ch, sizeof(Buf)-ch, "%s", n);
				
				LAssert(ch < sizeof(Buf) - 1);
			}
			return Buf;
			break;
		}
		case FIELD_FROM_CONTACT_NAME:
		{
			static bool InMethod = false;
			if (!InMethod)
			{
				InMethod = true;
				
				LDataPropI *la = GetFrom();
				if (la)
				{
					auto Email = la->GetStr(FIELD_EMAIL);
					Contact *c = Contact::LookupEmail(Email);
					if (c)
					{
						const char *f = 0, *l = 0;
						c->Get(OPT_First, f);
						c->Get(OPT_Last, l);
						
						if (f && l)
							sprintf_s(Buf, sizeof(Buf), "%s %s", f, l);
						else if (f)
							strcpy_s(Buf, sizeof(Buf), f);
						else if (l)
							strcpy_s(Buf, sizeof(Buf), l);
						else
							Buf[0] = 0;

						InMethod = false;
						return Buf;
					}
				}
				
				InMethod = false;
			}
			else
			{
				LAssert(0);
			}
			
			// fall through
		}
		case FIELD_FROM:
		{
		    LDataPropI *f = GetFrom();
			auto n = f->GetStr(FIELD_NAME);
		    if (n)
		        return n;

			auto e = f->GetStr(FIELD_EMAIL);
			if (e)
			    return e;
			break;
		}
		case FIELD_SUBJECT:
		{
			auto s = GetSubject();
			
			if (Strchr(s, '\n'))
			{
				char *e = Buf + sizeof(Buf) - 1;
				char *o = Buf;
				for (auto i = s; o<e && *i; i++)
				{
					if (*i != '\n')
						*o++ = *i;
				}
				*o++ = 0;
				return Buf;
			}
			
			return s;
		}
		case FIELD_SIZE:
		{
			if (TotalSizeCache < 0)
				TotalSizeof();
				
			if (OptionSizeInKiB)
			{
				int ch = sprintf_s(Buf, sizeof(Buf), "%.0f KiB", (double)TotalSizeCache/1024.0);
				if (ch > 4 + 3)
				{
					int digits = ch - 4;
					char *s = Buf + ((digits % 3) ? digits % 3 : 3);
					char *e = Buf + ch - 4;
					while (s < e)
					{
						memmove(s + 1, s, strlen(s)+1);
						*s = ',';
						s += 3;
					}
				}
			}
			else
				LFormatSize(Buf, sizeof(Buf), TotalSizeCache);
			return Buf;
		}
		case FIELD_DATE_SENT:
		{
			auto DateSent = GetDateSent();
			if (DateSent->Year())
			{
				LDateTime dt = *DateSent;
				if (GetObject() &&
					GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Imap)
				{
					ValidateImapDate(dt);
				}

			    if (AdjustDateTz)
			    {
                    if (dt.GetTimeZone() != LDateTime::SystemTimeZone())
    			        dt.SetTimeZone(LDateTime::SystemTimeZone(), true);
				}

				if (ShowRelativeDates)
				{
					auto rel = RelativeTime(dt);
					strcpy_s(Buf, sizeof(Buf), rel ? rel : "#error:RelativeTime");
				}
				else
				{
					dt.Get(Buf, sizeof(Buf));
				}
			}
			else
			{
				sprintf_s(Buf, sizeof(Buf), "(%s)", LLoadString(IDS_NONE, "None"));
			}			
			return Buf;
		}
		case FIELD_DATE_RECEIVED:
		{
			auto DateReceived = GetDateReceived();
			if (DateReceived->Year())
			{
		        LDateTime dt = *DateReceived;
			    if (AdjustDateTz)
			    {
                    if (dt.GetTimeZone() != LDateTime::SystemTimeZone())
    			        dt.SetTimeZone(LDateTime::SystemTimeZone(), true);
			    }

				dt.Get(Buf, sizeof(Buf));
				if (ShowRelativeDates)
				{
					auto rel = RelativeTime(dt);
					strcpy_s(Buf, sizeof(Buf), rel ? rel : "#error:RelativeTime");
				}
				else
				{
				    dt.Get(Buf, sizeof(Buf));
				}
			}
			else
			{
				sprintf_s(Buf, sizeof(Buf), "(%s)", LLoadString(IDS_NONE, "None"));
			}			
			return Buf;
		}
		case FIELD_TEXT:
			return GetBody();
		case FIELD_MESSAGE_ID:
			return GetMessageId();
		case FIELD_INTERNET_HEADER:
			return GetInternetHeader();
		case FIELD_ALTERNATE_HTML:
			return GetHtml();
		case FIELD_LABEL:
			return GetLabel();
		case FIELD_PRIORITY:
		case FIELD_FLAGS:
			return 0;
		case FIELD_SERVER_UID:
			sprintf_s(Buf, sizeof(Buf), "%i", GetObject() ? (int)GetObject()->GetInt(Field) : -1);
			return Buf;
		case FIELD_RECEIVED_DOMAIN:
		{
			if (!d->DomainCache)
			{
				if (!GetObject())
					return NULL;

				auto hdr = GetObject()->GetStr(FIELD_INTERNET_HEADER);
				if (!hdr)
					return NULL;

				for (auto ln: LString(hdr).SplitDelimit("\n"))
				{
					if (ln.Find("Received: from") == 0)
					{
						auto p = ln.SplitDelimit();
						if (p.Length() > 2)
						{
							auto t = p[2];
							if (t.Find(".") > 0)
								d->DomainCache = t.Strip("[]");
						}
					}
				}
			}

			return d->DomainCache;
		}
		default:
			return GetObject() ? GetObject()->GetStr(Field) : NULL;
	}

	return 0;
}

int Mail::DefaultMailFields[] =
{
	/*
	FIELD_FLAGS,
	FIELD_PRIORITY,
	*/
	FIELD_FROM,
	FIELD_SUBJECT,
	FIELD_SIZE,
	FIELD_DATE_SENT
};

const char *Mail::GetText(int i)
{
	if (FieldArray.Length())
	{
		if (i >= 0 && i < (int)FieldArray.Length())
			return GetFieldText(FieldArray[i]);
	}
	else if (i < CountOf(DefaultMailFields))
	{
		return GetFieldText(DefaultMailFields[i]);
	}
	
	return NULL;
}

void DumpMimeTree(LDataPropI *seg, int depth = 0)
{
	if (!seg) return;
	auto indent = LString(" ") * (depth << 2);

	auto mimetype = seg->GetStr(FIELD_MIME_TYPE);
	auto filename = seg->GetStr(FIELD_NAME);
	LgiTrace("%sseg:%s fn=%s\n", indent.Get(), mimetype, filename);

	auto It = seg->GetList(FIELD_MIME_SEG);
	if (It)
	{
		for (auto i = It->First(); i; i=It->Next())
			DumpMimeTree(i, depth + 1);
	}
}

LDataI *Mail::GetFileAttachPoint()
{
	if (!GetObject())
	{
		LAssert(!"No object ptr.");
		return 0;
	}

	auto Store = GetObject()->GetStore();
	
	// Check existing root node for "multipart/mixed"?
	auto r = dynamic_cast<LDataI*>(GetObject()->GetObj(FIELD_MIME_SEG));
	if (!r)
	{
		// Create one...
		r = Store->Create(MAGIC_ATTACHMENT);
		if (!r)
		{		
			LAssert(!"No MIME segment ptr.");
			return NULL;
		}
		
		r->SetStr(FIELD_MIME_TYPE, sMultipartMixed);
		if (!GetObject()->SetObj(FIELD_MIME_SEG, r))
		{
			LAssert(!"Can't set MIME segment.");
			return NULL;
		}
	}

	auto Mt = r->GetStr(FIELD_MIME_TYPE);
	if (!Stricmp(Mt, sMultipartMixed))
	{
		// Yes is it mixed... return that...
		return r;
	}

	// No, a different type of segment, make the parent segment a "mixed", and attach the old segment to the mixed
	auto Mixed = Store->Create(MAGIC_ATTACHMENT);
	if (!Mixed)
	{
		LAssert(!"Failed to create MAGIC_ATTACHMENT.");
		return NULL;
	}

	// Set the type...
	Mixed->SetStr(FIELD_MIME_TYPE, sMultipartMixed);

	// Re-parent the content to the mixed
	if (!r->Save(Mixed))
	{
		LAssert(!"Can't reparent the content into the mixed seg.");
		return NULL;
	}

	// Re-parent the mixed to the mail object
	if (!Mixed->Save(GetObject()))
	{
		LAssert(!"Can't reparent the mixed seg into the mail object.");
		return NULL;
	}

	return Mixed;
}

bool Mail::AttachFile(Attachment *File)
{
	bool Status = false;

	if (File && !Attachments.HasItem(File))
	{
		LDataI *AttachPoint = GetFileAttachPoint();
		if (AttachPoint)
		{
			if (File->GetObject()->Save(AttachPoint))
			{
				File->App = App;
				File->SetOwner(this);
				Attachments.Insert(File);

				Status = true;

				SetDirty(!TestFlag(GetFlags(), MAIL_ATTACHMENTS));
				SetFlags(GetFlags() | MAIL_ATTACHMENTS);

				Update();

				if (Ui)
				{
					Ui->OnAttachmentsChange();
				}
			}
		}
		else
		{
			File->DecRef();
		}
	}

	return Status;
}

Attachment *Mail::AttachFile(LView *Parent, const char *FileName)
{
	LString MimeType = ScribeGetFileMimeType(FileName);
	LVariant Resize = false;
	if (!Strnicmp(MimeType.Get(), "image/", 6))
	{
		// Check if we have to do a image resize
		App->GetOptions()->GetValue(OPT_ResizeImgAttachments, Resize);
	}

	auto AttachPoint = GetFileAttachPoint();
	if (!AttachPoint)
	{
		LAssert(!"No attachment point in MIME heirarchy for file");
		return NULL;
	}
	
	auto File = new Attachment(	App,
								GetObject()->GetStore()->Create(MAGIC_ATTACHMENT),
								FileName);
	if (File)
	{
		File->App = App;
		if (Resize.CastInt32() || File->GetSize() > 0)
		{
			File->SetOwner(this);
			Attachments.Insert(File);
			if (Resize.CastInt32())
				d->AddResizeImage(File);
			else
				File->GetObject()->Save(AttachPoint);
			SetFlags(GetFlags() | MAIL_ATTACHMENTS);
			Update();

			if (Ui)
				Ui->OnAttachmentsChange();
		}
		else
		{
			LgiMsg(Parent, LLoadString(IDS_ERROR_FILE_EMPTY), AppName);
			File->DecRef();
			File = NULL;
		}
	}

	DumpMimeTree(GetObject()->GetObj(FIELD_MIME_SEG));
	return File;
}

bool Mail::Save(ScribeFolder *Into)
{
	bool Status = false;

	if (!Into)
	{
	    if (GetFolder())
		    Into = GetFolder();
		else
			Into = App->GetFolder(FOLDER_OUTBOX);
	}

	if (Into)
	{
		ScribeFolder *Old = GetFolder();

        if (!GetFolder())
        {
	        SetParentFolder(Into);
	    }
	    else if (GetFolder() != Into)
	    {
            // If this fails, you should really by using ScribeFolder::MoveTo to move
            // the item from it's existing location to the new folder...
            LAssert(!"Use MoveTo instead.");
            return false;
	    }

		Store3Status Result = Into->WriteThing(this);
		if (Result != Store3Error)
		{
			Status = true;
			SetDirty(false);
			
			if (Into &&
				Into == App->GetFolder(FOLDER_TEMPLATES, NULL, true))
			{
				// saving into the templates folder... update the menu
				App->BuildDynMenus();
			}

			if (Status && DropFileName)
			{
				FileDev->Delete(DropFileName, NULL, false);
				DropFileName.Reset();
			}
		}
		else
		{
			SetParentFolder(Old);
		}
	}
	
	// This frees the resizer thread... 
	// so they aren't hanging around pointlessly
	d->OnSave();	
	
	return Status;
}

struct WrapQuoteBlock
{
	int Depth;
	LArray<char*> Text;
};

void WrapAndQuote(	LStream &Pipe,
					const char *Quote,
					int Wrap,
					const char *Text,
					const char *Cp,
					const char *MimeType)
{
	int IsHtml = MimeType && !_stricmp(MimeType, sTextHtml);
	LAutoString In((char*) LNewConvertCp("utf-8", Text, Cp ? Cp : (char*)"utf-8"));
	const char *QuoteStr = Quote;
	if (In)
	{
		size_t QuoteChars = Strlen(QuoteStr);
		char NewLine[8];
		int NewLineLen = sprintf_s(NewLine, sizeof(NewLine), "%s", IsHtml ? "<br>\n" : "\n");

		// Two step process, parse all the input into an array of paragraph blocks and then output each block
		// in wrapped form
		LArray<WrapQuoteBlock> Para;
		
		// 1) Parse all input into paragraphs
		char *i = In;
		while (*i)
		{
			// Parse out the next line...
			char *e = i;
			while (*e && *e != '\n')
				e++;
			ssize_t len = e - i;
			int depth = 0;
			for (int n=0; n<len; n++)
			{
				if (i[n] == '>')
					depth++;
				else if (i[n] != ' ' || i[n] != '\t')
					break;
			}
			
			if (Para.Length())
			{
				// Can this be added to the previous paragraph?
				WrapQuoteBlock &Prev = Para[Para.Length()-1];
				if (Prev.Depth == depth)
				{
					// Yes?!
					Prev.Text.Add(NewStr(i, len));
					i = *e ? e + 1 : e;
					continue;
				}
			}

			// Create a new paragraph
			WrapQuoteBlock &New = Para.New();
			New.Depth = depth;
			New.Text.Add(NewStr(i, len));
			
			// Move current position to next line
			i = *e ? e + 1 : e;
		}
		
		// 2) Output all paragraphs
		const char *PrefixCharacters = "> \t";
		for (unsigned n=0; n<Para.Length(); n++)
		{
			WrapQuoteBlock &p = Para[n];
			ssize_t CharPos = 0;
			
			if (QuoteStr)
			{
				Pipe.Write(QuoteStr, QuoteChars * sizeof(*QuoteStr));
				CharPos += QuoteChars;
			}
			
			// Each line has the format:
			//		[QuoteStr][Prefix][TextWords][NewLine]
			
			LAutoString Prefix;
			ssize_t PrefixChars = 0;
			for (unsigned l=0; l<p.Text.Length(); l++)
			{
				char *Line = p.Text[l];
				// int LineLen = strlen(Line);
				bool HardNewLine = true; // LineLen < Wrap;
				char *Start = Line;
				while (*Start && strchr(PrefixCharacters, *Start))
					Start++;

				if (!l)
				{
					// Create the prefix string
					PrefixChars = Start - Line;
					Prefix.Reset(NewStr(Line, PrefixChars));
					
					// Write the prefix string for the current line...
					Pipe.Write(Prefix, PrefixChars * sizeof(*Prefix));
					CharPos += PrefixChars;
				}
				
				// Output [Start:EndOfLine] unless we run out of space on this line
				while (*Start)
				{
					// Find end of the word.
					char *End = Start;
					while (*End && strchr(WhiteSpace, *End))
						End++;
					while (*End && !strchr(WhiteSpace, *End))
						End++;
					ssize_t WordLen = End - Start;
					LAssert(WordLen > 0);
					if (WordLen == 0)
						break; // This won't end well...
					
					if (Wrap > 0)
					{
						// Check if we can put this word on the current line without making it longer than 'Wrap'
						if (CharPos + WordLen > Wrap)
						{
							// No it won't fit... so emit [NewLine][QuoteStr][Prefix]
							Pipe.Write(NewLine, NewLineLen);
							if (QuoteStr)
								Pipe.Write(QuoteStr, QuoteChars * sizeof(*QuoteStr));
							Pipe.Write(Prefix, PrefixChars * sizeof(*Prefix));
							
							// Now we are ready to write more words...
							CharPos = QuoteChars + PrefixChars;
						}
						// else Yes it fits...
					}
					
					// Write out the word...
					if (IsHtml && Strnchr(Start, '<', WordLen))
					{
						for (auto *c = Start; c < Start + WordLen; c++)
							if (*c == '<')
								Pipe.Print("&lt;");
							else if (*c == '>')
								Pipe.Print("&gt;");
							else
								Pipe.Write(c, sizeof(*c));
					}
					else
						Pipe.Write(Start, WordLen * sizeof(*Start));
					CharPos += WordLen;
					
					Start = End;
				}
				
				if (HardNewLine)
				{
					Pipe.Write(NewLine, NewLineLen);
					if (QuoteStr)
						Pipe.Write(QuoteStr, QuoteChars * sizeof(*QuoteStr));
					Pipe.Write(Prefix, PrefixChars * sizeof(*Prefix));
					CharPos = QuoteChars + PrefixChars;
				}
			}

			// Finish the paragraph
			Pipe.Write(NewLine, NewLineLen);
		}		
	}
}

void Mail::WrapAndQuote(LStringPipe &p, const char *Quote, int WrapAt)
{
	LString Mem;
	LAutoString Cp;
	if (!ValidStr(GetBody()))
		Mem = HtmlToText(GetHtml(), GetHtmlCharset());
	else
		Cp = GetCharSet();
	
	::WrapAndQuote(p, Quote, WrapAt > 0 ? WrapAt : 76, Mem ? Mem.Get() : GetBody(), Cp);
}

LAutoString Mail::GetSig(bool HtmlVersion, ScribeAccount *Account)
{
	LAutoString r;
	LVariant Xml;
		
	if (!Account)
		Account = GetAccountSentTo();

	if (Account)
	{
		if (HtmlVersion)
			Xml = Account->Identity.HtmlSig();
		else
			Xml = Account->Identity.TextSig();
	}
	
	if (Xml.Str())
	{
		r = App->ProcessSig(this, Xml.Str(), HtmlVersion ? sTextHtml : sTextPlain);
	}

	return r;
}

void Mail::ProcessTextForResponse(Mail *From, LOptionsFile *Options, ScribeAccount *Account)
{
	LStringPipe Temp;
	LVariant QuoteStr, Quote, WrapAt;
	Options->GetValue(OPT_QuoteReply, Quote);
	Options->GetValue(OPT_WrapAtColumn, WrapAt);
	Options->GetValue(OPT_QuoteReplyStr, QuoteStr);

	From->WrapAndQuote(Temp, Quote.CastInt32() ? QuoteStr.Str() : NULL, WrapAt.CastInt32());
	
	LAutoString s = From->GetSig(false, Account);
	if (s)
		Temp.Write(s, strlen(s));

	s.Reset(Temp.NewStr());
	SetBody(s);
	SetBodyCharset("utf-8");
}

ScribeAccount *Mail::GetAccountSentTo()
{
	ScribeAccount *Account = App->GetAccountById(GetAccountId());
	if (!Account)
	{
		// Older style lookup based on to address...
		for (auto a : *App->GetAccounts())
		{
			LVariant e = a->Identity.Email();
			if (ValidStr(e.Str()))
			{
				for (LDataPropI *t=GetTo()->First(); t; t=GetTo()->Next())
				{
					auto Addr = t->GetStr(FIELD_EMAIL);
					if (ValidStr(Addr))
					{
						if (_stricmp(Addr, e.Str()) == 0)
						{
							Account = a;
							break;
						}
					}
				}
			}
		}
	}
	return Account;
}

void Mail::OnReceipt(Mail *m)
{
	if (m)
	{
		SetFlags(MAIL_CREATED | MAIL_READY_TO_SEND);

		ScribeAccount *MatchedAccount = m->GetAccountSentTo();
		if (!MatchedAccount)
		{
			MatchedAccount = App->GetAccounts()->ItemAt(App->GetCurrentIdentity());
		}
		if (MatchedAccount)
		{
			GetFrom()->SetStr(FIELD_NAME, MatchedAccount->Identity.Name().Str());
			GetFrom()->SetStr(FIELD_EMAIL, MatchedAccount->Identity.Email().Str());
		}

		char Sent[64];
		m->GetDateReceived()->Get(Sent, sizeof(Sent));

		char Read[64];
		LDateTime t;
		t.SetNow();
		t.Get(Read, sizeof(Read));

		char s[256];
		sprintf_s(s, sizeof(s), 
				LLoadString(IDS_RECEIPT_BODY),
				GetFrom()->GetStr(FIELD_NAME),
				GetFrom()->GetStr(FIELD_EMAIL),
				Sent,
				m->GetSubject(),
				Read);
		SetBody(s);

		sprintf_s(s, sizeof(s), "Read: %s", m->GetSubject() ? m->GetSubject() : (char*)"");
		SetSubject(s);
	}
}

LString Mail::GetMailRef()
{
	LString Ret;

	if (auto MsgId = GetMessageId(true))
	{
	    LAssert(!strchr(MsgId, '\n'));
	    
		ScribeFolder *Fld = GetFolder();
		LString FldPath;
		if (Fld)
		    FldPath = Fld->GetPath();

		if (FldPath)
		{
			LUri u;
			auto a = u.EncodeStr(MsgId, MsgIdEncodeChars);
			if (a)
				Ret.Printf("%s/%s", FldPath.Get(), a.Get());
		}
		else
		{
			Ret = MsgId;
		}
	}
	
	return Ret;
}

void Mail::OnReply(Mail *m, bool All, bool MarkOriginal)
{
	if (!m)
	    return;

	SetWillDirty(false);

	LVariant ReplyAllSetting = MAIL_ADDR_TO;
	if (All)
	{
		App->GetOptions()->GetValue(OPT_DefaultReplyAllSetting, ReplyAllSetting);
	}
		
	if (MarkOriginal)
	{
		// source email has been replyed to
		m->SetFlags(m->GetFlags() | MAIL_READ);
	}

	// this email is ready to send
	SetFlags(GetFlags() | MAIL_READ | MAIL_CREATED);

	LDataPropI *ToAddr = GetTo()->Create(GetObject()->GetStore());
	if (ToAddr)
	{
		bool CopyStatus;
		auto From = m->GetFrom();
		auto Reply = m->GetReply();
		auto ReplyTo = Reply->GetStr(FIELD_EMAIL);
		if (ReplyTo)
			CopyStatus = ToAddr->CopyProps(*Reply);
		else
			CopyStatus = ToAddr->CopyProps(*From);
		LAssert(CopyStatus);

		ToAddr->SetInt(FIELD_CC, ReplyAllSetting.CastInt32());
		GetTo()->Insert(ToAddr);
	}

	LVariant UserName, EmailAddr, ReplyTo;

	ScribeAccount *a = App->GetCurrentAccount();
	if (a)
	{
		UserName = a->Identity.Name();
		EmailAddr = a->Identity.Email();
		ReplyTo = a->Identity.ReplyTo();
	}
	else LAssert(!"No current account.");

	if (All)
	{
		for (LDataPropI *a = m->GetTo()->First(); a; a = m->GetTo()->Next())
		{
			bool Same = App->IsMyEmail(a->GetStr(FIELD_EMAIL));
			
			bool AlreadyThere = false;
			for (LDataPropI *r = GetTo()->First(); r; r=GetTo()->Next())
			{
				if (_stricmp(r->GetStr(FIELD_EMAIL), a->GetStr(FIELD_EMAIL)) == 0)
				{
					AlreadyThere = true;
					break;
				}
			}

			if (!Same && !AlreadyThere)
			{
				LDataPropI *New = GetTo()->Create(GetObject()->GetStore());
				if (New)
				{
					New->SetInt(FIELD_CC, ReplyAllSetting.CastInt32());
					New->SetStr(FIELD_EMAIL, a->GetStr(FIELD_EMAIL));
					New->SetStr(FIELD_NAME, a->GetStr(FIELD_NAME));
					GetTo()->Insert(New);
				}
			}
		}
	}

	ScribeAccount *MatchedAccount = m->GetAccountSentTo();
	if (MatchedAccount &&
		MatchedAccount->Identity.Email().Str())
	{
		GetFrom()->SetStr(FIELD_NAME, MatchedAccount->Identity.Name().Str());
		GetFrom()->SetStr(FIELD_EMAIL, MatchedAccount->Identity.Email().Str());
		
		ReplyTo = MatchedAccount->Identity.ReplyTo();
		if (ReplyTo.Str())
		{
			GetReply()->SetStr(FIELD_NAME, MatchedAccount->Identity.Name().Str());
			GetReply()->SetStr(FIELD_EMAIL, ReplyTo.Str());
		}
	}
	else
	{
		GetFrom()->SetStr(FIELD_NAME, UserName.Str());
		GetFrom()->SetStr(FIELD_EMAIL, EmailAddr.Str());

		if (ValidStr(ReplyTo.Str()))
		{
			GetReply()->SetStr(FIELD_NAME, UserName.Str());
			GetReply()->SetStr(FIELD_EMAIL, ReplyTo.Str());
		}
	}

	char Re[32];
	sprintf_s(Re, sizeof(Re), "%s:", LLoadString(IDS_REPLY_PREFIX));

	auto SrcSubject = (m->GetSubject()) ? m->GetSubject() : "";
	if (_strnicmp(SrcSubject, Re, strlen(Re)) != 0)
	{
		size_t Len = strlen(SrcSubject) + strlen(Re) + 2;
		char *s = new char[Len];
		if (s)
		{
			sprintf_s(s, Len, "%s %s", Re, SrcSubject);
			SetSubject(s);
			DeleteArray(s);
		}
	}
	else
	{
		SetSubject(m->GetSubject());
	}

	auto EditMimeType = App->EditCtrlMimeType();
	auto Xml = App->GetReplyXml(EditMimeType);
	if (ValidStr(Xml))
	{
		RemoveReturns(Xml);
		PreviousMail = m;

		LString Body = App->ProcessReplyForwardTemplate(m, this, Xml, Cursor, EditMimeType);
		
		LVariant EditCtrl;
		if (App->GetOptions()->GetValue(OPT_EditControl, EditCtrl) &&
			EditCtrl.CastInt32())
		{
			SetHtml(Body);
		}
		else
		{			
			SetBody(Body);
			SetBodyCharset("utf-8");
		}

		PreviousMail = 0;
	}
	else
	{
		ProcessTextForResponse(m, App->GetOptions(), MatchedAccount);
	}

	// Reference
	SetReferences(0);
	auto MailRef = m->GetMailRef();
	if (MailRef)
		SetReferences(MailRef);

	GetMessageId(true);
	SetWillDirty(true);

	ScribeFolder *Folder = m->GetFolder();
	if (Folder && Folder->IsPublicFolders())
	{
		SetParentFolder(Folder);
	}
}

bool Mail::OnForward(Mail *m, bool MarkOriginal, int WithAttachments)
{
	if (!m)
	{
		LAssert(!"No mail object.");
		LgiTrace("%s:%i - No mail object.\n", _FL);
		return false;
	}

	// ask about attachments?
	List<Attachment> Attach;
	if (m->GetAttachments(&Attach) &&
		Attach.Length() > 0)
	{
		if (WithAttachments < 0)
		{
			LView *p = (m->Ui)?(LView*)m->Ui:(LView*)App;
			int Result = LgiMsg(p, LLoadString(IDS_ASK_FORWARD_ATTACHMENTS), AppName, MB_YESNOCANCEL);
			if (Result == IDYES)
			{
				WithAttachments = 1;
			}
			else if (Result == IDCANCEL)
			{
				return false;
			}
		}
	}

	if (MarkOriginal)
	{
		// source email has been forwarded
		auto MailRef = m->GetMailRef();
		if (MailRef)
			SetFwdMsgId(MailRef);
	}

	// this email is ready to send
	SetFlags(GetFlags() | MAIL_CREATED);
	SetDirty();

	LVariant UserName, EmailAddr, ReplyTo;
	ScribeAccount *a = App->GetCurrentAccount();
	if (a)
	{
		UserName = a->Identity.Name();
		EmailAddr = a->Identity.Email();
		ReplyTo = a->Identity.ReplyTo();
	}
	else LAssert(!"No current account.");

	ScribeAccount *MatchedAccount = m->GetAccountSentTo();
	if (MatchedAccount &&
		MatchedAccount->Identity.Email().Str())
	{
		GetFrom()->SetStr(FIELD_NAME, MatchedAccount->Identity.Name().Str());
		GetFrom()->SetStr(FIELD_EMAIL, MatchedAccount->Identity.Email().Str());
		
		ReplyTo = MatchedAccount->Identity.ReplyTo();
		if (ValidStr(ReplyTo.Str()))
		{
			GetReply()->SetStr(FIELD_NAME, MatchedAccount->Identity.Name().Str());
			GetReply()->SetStr(FIELD_EMAIL, ReplyTo.Str());
		}
	}
	else
	{
		GetFrom()->SetStr(FIELD_NAME, UserName.Str());
		GetFrom()->SetStr(FIELD_EMAIL, EmailAddr.Str());

		if (ValidStr(ReplyTo.Str()))
		{
			GetReply()->SetStr(FIELD_NAME, UserName.Str());
			GetReply()->SetStr(FIELD_EMAIL, ReplyTo.Str());
		}
	}

	SetBodyCharset(0);

	char PostFix[32];
	sprintf_s(PostFix, sizeof(PostFix), "%s:", LLoadString(IDS_FORWARD_PREFIX));

	auto SrcSubject = (m->GetSubject()) ? m->GetSubject() : "";
	if (_strnicmp(SrcSubject, PostFix, strlen(PostFix)) != 0)
	{
		size_t Len = strlen(SrcSubject) + strlen(PostFix) + 2;
		char *s = new char[Len];
		if (s)
		{
			sprintf_s(s, Len, "%s %s", PostFix, SrcSubject);
			SetSubject(s);
			DeleteArray(s);
		}
	}
	else
	{
		SetSubject(m->GetSubject());
	}

	// Wrap/Quote/Sig the text...
	const char *EditMimeType = App->EditCtrlMimeType();
	LAutoString Xml = App->GetForwardXml(EditMimeType);
	if (ValidStr(Xml))
	{
		RemoveReturns(Xml);
		PreviousMail = m;
		
		LString Body = App->ProcessReplyForwardTemplate(m, this, Xml, Cursor, EditMimeType);
		
		LVariant EditCtrl;
		if (App->GetOptions()->GetValue(OPT_EditControl, EditCtrl) &&
			EditCtrl.CastInt32())
		{
			SetHtml(Body);
		}
		else
		{			
			SetBody(Body);
			SetBodyCharset("utf-8");
		}

		PreviousMail = 0;
	}
	else
	{
		ProcessTextForResponse(m, App->GetOptions(), MatchedAccount);
	}

	// Attachments
	if (WithAttachments > 0)
		for (auto From: Attach)
			AttachFile(new Attachment(App, From));

	// Set MsgId
	GetMessageId(true);

	// Unless the user edits the message it shouldn't be made dirty.
	SetDirty(false);

	return true;
}

bool Mail::OnBounce(Mail *m, bool MarkOriginal, int WithAttachments)
{
	if (m)
	{
		if (MarkOriginal)
		{
			// source email has been forwarded
			auto MsgId = m->GetMessageId(true);
			if (MsgId)
			{
				ScribeFolder *Fld = m->GetFolder();
				LString FldPath;
				if (Fld)
				    FldPath = Fld->GetPath();

				if (FldPath)
				{
					LUri u;
					LString a = u.EncodeStr(MsgId, MsgIdEncodeChars);
					if (a)
					{
						LString p;
						p.Printf("%s/%s", FldPath.Get(), a.Get());
						SetBounceMsgId(p);
					}
				}
				else
				{
					SetBounceMsgId(MsgId);
				}
			}
			else m->SetFlags(m->GetFlags() | MAIL_BOUNCED);
		}
		
		*this = (Thing&)*m;

		GetTo()->DeleteObjects();
		SetFlags(MAIL_READ | MAIL_BOUNCE);
		return true;
	}
	
	return false;
}

void Mail::OnMeasure(LPoint *Info)
{
	LListItem::OnMeasure(Info);

	if (PreviewLines &&
		!(GetFlags() & MAIL_READ) &&
		(!GetObject() || !GetObject()->GetInt(FIELD_DONT_SHOW_PREVIEW)))
	{
    	LFont *PreviewFont = App->GetPreviewFont();
		Info->y += PreviewFont ? PreviewFont->GetHeight() << 1 : 28;
	}
}

void Mail::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
{
	int Field = 0;
	if (FieldArray.Length())
	{
		Field = i >= 0 && i < (int)FieldArray.Length() ? FieldArray[i] : 0;
	}
	else if (i >= 0 && i < CountOf(DefaultMailFields))
	{
		Field = DefaultMailFields[i];
	}

	if (Container &&
		i >= 0 &&
		Field == FIELD_SUBJECT)
	{
		LFont *f = Parent->GetFont();
		auto Subj = GetSubject();

		if (Container)
		{
			// Container needs to paint the subject...
			Container->OnPaint(Ctx.pDC, Ctx, c, Ctx.Fore, Ctx.Back, f?f:LSysFont, Subj);
		}
	}
	else
	{
		LListItem::OnPaintColumn(Ctx, i, c);

		if (i >= 0 && App->GetIconImgList())
		{
			int Icon = -1;
			
			switch (Field)
			{
				case FIELD_PRIORITY:
				{
					switch (GetPriority())
					{
						case MAIL_PRIORITY_HIGH:
						{
							Icon = ICON_PRIORITY_HIGH;
							break;
						}
						case MAIL_PRIORITY_LOW:
						{
							Icon = ICON_PRIORITY_LOW;
							break;
						}
						default:
							break;
					}

					break;
				}
				case FIELD_FLAGS:
				{
					if (GetFlags() & MAIL_BOUNCED)
					{
						Icon = ICON_FLAGS_BOUNCE;
					}
					else if (GetFlags() & MAIL_REPLIED)
					{
						Icon = ICON_FLAGS_REPLY;
					}
					else if (GetFlags() & MAIL_FORWARDED)
					{
						Icon = ICON_FLAGS_FORWARD;
					}
					break;
				}
				default:
					break;
			}

			if (Icon >= 0)
			{
				LRect *b = App->GetIconImgList()->GetBounds();
				if (b)
				{
					b += Icon;
					int x = Ctx.x1 + ((Ctx.X()-b->X())/2) - b->x1;
					int y = Ctx.y1 + ((MIN(Ctx.Y(), 16)-b->Y())/2) - b->y1;

					LColour Back(Ctx.Back);
					App->GetIconImgList()->Draw(Ctx.pDC, x, y, Icon, Back);
				}
			}
		}
	}
}

void Mail::OnPaint(LItem::ItemPaintCtx &InCtx)
{
	LItem::ItemPaintCtx Ctx = InCtx;
	
	int64_t MarkCol = GetMarkColour();
	if (MarkCol > 0)
	{
        LColour Col((uint32_t)MarkCol, 32);
        LColour CtxBack(Ctx.Back);
		LColour Mixed = Col.Mix(CtxBack, MarkColourMix);
        Ctx.Back = Mixed;
	}

	if (Parent) ((ThingList*)Parent)->CurrentMail = this;
	LListItem::OnPaint(Ctx);
	if (Parent) ((ThingList*)Parent)->CurrentMail = 0;
	LFont *PreviewFont = App->GetPreviewFont();
	LFont *ColumnFont = Parent && Parent->GetFont() ? Parent->GetFont() : LSysFont;

	if (Parent &&
		PreviewLines &&
		PreviewFont &&
		ColumnFont &&
		GetObject() &&
		!GetObject()->GetInt(FIELD_DONT_SHOW_PREVIEW) &&
		!(GetFlags() & MAIL_READ))
	{
		int y = ColumnFont->GetHeight() + 2;
		int Space = (Ctx.Y() - y) >> 1;
		int Line = MAX(PreviewFont->GetHeight(), Space);
		PreviewFont->Transparent(true);

		// Setup
		if (PreviewCache.Length() == 0 || abs(PreviewCacheX - Ctx.X()) > 20)
		{
			PreviewCache.DeleteObjects();
			PreviewCacheX = Ctx.X();

			LVariant v;
			if (GetValue("BodyAsText[1024]", v) &&
				v.Str())
			{
				char16 *Base = Utf8ToWide(v.Str());
				char16 *Txt = Base;
				int i;
				for (i=0; Txt[i]; i++)
				{
					if (Txt[i] < ' ')
						Txt[i] = ' ';
				}

				auto TxtLen = StrlenW(Txt);
				for (i=0; Txt && *Txt && i<2; i++)
				{
					LDisplayString *Temp = new LDisplayString(PreviewFont, Txt, MIN(1024, TxtLen));
					if (Temp)
					{
						int W = Ctx.X()-18;
						ssize_t Cx = Temp->CharAt(W);
						if (Cx > 0 && Cx <= TxtLen)
						{
							LDisplayString *d = new LDisplayString(PreviewFont, Txt, Cx);
							if (d)
							{
								PreviewCache.Insert(d);
							}
							DeleteObj(Temp);
							Txt += Cx; // LSeekUtf8
							TxtLen -= Cx;
						}
						else break;
					}
				}
				DeleteArray(Base);
			}
		}

		// Display
		LColour PreviewCol(App->GetColour(L_MAIL_PREVIEW));
		if (Select())
		{
			int GreyPrev = PreviewCol.GetGray();
			int GreyBack = Ctx.Back.GetGray();
			
			int d = GreyPrev - GreyBack;
			if (d < 0) d = -d;
			if (d < 128)
			{
				// too near
				PreviewFont->Colour(Ctx.Fore, Ctx.Back);
			}
			else
			{
				// ok
				PreviewFont->Colour(PreviewCol, Ctx.Back);
			}
		}
		else
		{
			PreviewFont->Colour(PreviewCol, Ctx.Back);
		}

		for (auto p: PreviewCache)
		{
			p->Draw(Ctx.pDC, Ctx.x1+16, Ctx.y1+y, 0);
			y += Line;
		}
	}
}

bool Mail::GetFormats(bool Export, LString::Array &MimeTypes)
{
	if (Export)
	{
		MimeTypes.Add("text/plain");
		MimeTypes.Add(sMimeMbox);
	}

	MimeTypes.Add(sMimeMessage);
	return MimeTypes.Length() > 0;
}

Thing::IoProgress Mail::Import(IoProgressImplArgs)
{
	if (!mimeType)
		IoProgressError("No mime type.");

	if (Stricmp(mimeType, sMimeMessage))
		IoProgressNotImpl();

	// Single email..
	OnAfterReceive(stream);

	int Flags = GetFlags();
	Flags &= ~MAIL_CREATED;
	Flags |= MAIL_READ;
	SetFlags(Flags);

	Update();

	IoProgressSuccess();
}

#define TIMEOUT_OBJECT_LOAD		20000

Thing::IoProgress Mail::Export(IoProgressImplArgs)
{
	if (!mimeType)	
		IoProgressError("No mimetype.");

	if (!Stricmp(mimeType, "text/plain"))
	{
		LStringPipe Buf;
		char Str[256];
		char Eol[] = EOL_SEQUENCE;

		// Addressee
		if (GetFlags() & MAIL_CREATED)
		{
			Buf.Push("To:");
			for (LDataPropI *a=GetTo()->First(); a; a=GetTo()->Next())
			{
				sprintf_s(Str, sizeof(Str), "\t%s <%s>%s", a->GetStr(FIELD_NAME), a->GetStr(FIELD_EMAIL), Eol);
				Buf.Push(Str);
			}
		}
		else
		{
			sprintf_s(Str, sizeof(Str), "From: %s <%s>%s", GetFrom()->GetStr(FIELD_NAME), GetFrom()->GetStr(FIELD_EMAIL), Eol);
			Buf.Push(Str);
		}

		// Subject
		sprintf_s(Str, sizeof(Str), "Subject: %s%s", GetSubject(), Eol);
		Buf.Push(Str);

		// Sent date
		if (GetDateSent()->Year())
		{
			int ch = sprintf_s(Str, sizeof(Str), "Sent Date: ");
			GetDateSent()->Get(Str+ch, sizeof(Str)-ch);
		}
		else
		{
			int ch = sprintf_s(Str, sizeof(Str), "Date Received: ");
			GetDateReceived()->Get(Str+ch, sizeof(Str)-ch);
		}
		strcat(Str, Eol);
		Buf.Push(Str);

		// Body
		Buf.Push(Eol);
		Buf.Push(GetBody());

		// Write the output
		auto s = Buf.NewLStr();
		if (!s)
			IoProgressError("No data to output.");
		stream->Write(s.Get(), s.Length());

		IoProgressSuccess();
	}
	else if (!Stricmp(mimeType, sMimeMbox))
	{
		char Temp[256];
		
		// generate from header
		sprintf_s(Temp, sizeof(Temp), "From %s ", GetFrom()->GetStr(FIELD_EMAIL));
		struct tm Ft;
		
		ZeroObj(Ft);
		LDateTime Rec = *GetDateReceived();
		if (!Rec.Year())
		    Rec.SetNow();
		Ft.tm_sec = Rec.Seconds();			/* seconds after the minute - [0,59] */
		Ft.tm_min = Rec.Minutes();			/* minutes after the hour - [0,59] */
		Ft.tm_hour = Rec.Hours();			/* hours since midnight - [0,23] */
		Ft.tm_mday = Rec.Day();			/* day of the month - [1,31] */
		Ft.tm_mon = Rec.Month() - 1;		/* months since January - [0,11] */
		Ft.tm_year = Rec.Year() - 1900;    /* years since 1900 */
		Ft.tm_wday = Rec.DayOfWeek();

		strftime(Temp+strlen(Temp), MAX_NAME_SIZE, "%a %b %d %H:%M:%S %Y", &Ft);
		strcat(Temp, "\r\n");
		
		// write mail
		stream->Write(Temp, strlen(Temp));
		auto Status = Export(stream, sMimeMessage, [cb](auto io, auto stream)
		{
			if (io->status == Store3Success)
				stream->Write((char*)"\r\n.\r\n", 2);
			else if (io->status == Store3Delayed)
				LAssert(!"We should never get delayed here... it's the callback!");

			if (cb)
				cb(io, stream);
		});

		return Status;
	}
	else if (!Stricmp(mimeType, sMimeMessage))
	{
		// This function can't be asynchronous, it must complete with UI or waiting for a callback.
		// Because it is used by the drag and drop system. Which won't wait.
		auto state = GetLoaded();
		if (state != Store3Loaded)
			IoProgressError("Mail not loaded.");

		if (!GetObject())
			IoProgressError("No store object.");
		
		auto Data = GetObject()->GetStream(_FL);
		if (!Data)
			IoProgressError("Mail for export has no data.");
		
		Data->SetPos(0);
		LCopyStreamer Cp(512<<10);
		if (Cp.Copy(Data, stream) <= 0)
			IoProgressError("Mail copy stream failed.");

		IoProgressSuccess();
	}

	IoProgressNotImpl();
}

char *Mail::GetNewText(int Max, const char *AsCp)
{
	LAutoString CharSet = GetCharSet();
	char *Txt = 0;

	// This limits the preview of the text to
	// the first 64kb, which is reasonable.
	int Len = Max > 0 ? Strnlen(GetBody(), Max) : -1;

	// Convert or allocate the text.
	if (CharSet)
	{
		Txt = (char*)LNewConvertCp(AsCp, GetBody(), GetBodyCharset(), Len);
	}
	else
	{
		Txt = NewStr(GetBody(), Len);
	}

	return Txt;
}

LAutoString Mail::GetCharSet()
{
	LAutoString Status;
	LAutoString ContentType;

	if (GetBodyCharset())
	{
		// If the charset has a stray colon...
		char *Colon = strchr(GetBodyCharset(), ';');
		// Kill it.
		if (Colon) *Colon = 0;
		
		// Copy the string..
		Status.Reset(NewStr(GetBodyCharset()));
	}

	if (!GetBodyCharset())
	{
		ContentType.Reset(InetGetHeaderField(GetInternetHeader(), "Content-Type"));
		if (ContentType)
		{
			char *CharSet = stristr(ContentType, "charset=");
			if (CharSet)
			{
				CharSet += 8;
				if (*CharSet)
				{
					if (strchr("\"\'", *CharSet))
					{
						char Delim = *CharSet++;
						char *e = CharSet;
						
						while (*e && *e != Delim)
						{
							e++;
						}
						Status.Reset(NewStr(CharSet, e - CharSet));
					}
					else
					{
						char *e = CharSet;
						while (*e && (IsDigit(*e) || IsAlpha(*e) || strchr("-_", *e)))
						{
							e++;
						}
						Status.Reset(NewStr(CharSet, e - CharSet));
					}
				}
			}
		}

		/*
		// If it's not a valid charset...
		if (!LGetCsInfo(Status))
		{
			// Then kill it.
			Status.Reset();
			if (GetBodyCharset())
			{
				SetBodyCharset(0);
				SetDirty();
			}
		}
		*/
	}

	return Status;
}

/* Old Body Printing Code

	int Lines = 0;
	char *n;
	for (n=Body.Str(); n && *n; n++)
	{
		if (*n == '\n') Lines++;
	}

	char *c = Body.Str();
	if (c)
	{
		c = WrapLines(c, c ? strlen(c) : 0, 76);

		Lines = 0;
		for (n=c; *n; n++)
		{
			if (*n == '\n') Lines++;
		}

		while (c && *c)
		{
			if (Cy + Line > r.y2)
			{
				pDC->EndPage();

				if (Window->GetMaxPages() > 0 &&
					++Page >= Window->GetMaxPages())
				{
					break;
				}

				pDC->StartPage();
				Cy = 0;
			}

			char *Eol = strchr(c, '\n');
			int Len = 0;
			int Ret = 0;
			if (Eol)
			{
				Len = (int) Eol - (int) c;
				if (Len > 0 && c[Len-1] == '\r')
				{
					Len--;
					Ret = 1;
				}
			}
			else
			{
				Len = strlen(c);
			}

			MailFont->Text(pDC, r.x1, Cy, c, Len);
			Cy += Line;
			c += Len + Ret;
			if (*c == '\n') c++;
		}
	}
*/

int Mail::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDC_TEXT:
		{
			LDocView *Doc = dynamic_cast<LDocView*>(Ctrl);
			if (Doc)
			{
				if (n.Type == LNotifyShowImagesChanged)
				{
					if (Doc->GetLoadImages() ^ TestFlag(GetFlags(), MAIL_SHOW_IMAGES))
					{
						if (Doc->GetLoadImages())
						{
							SetFlags(GetFlags() | MAIL_SHOW_IMAGES);
						}
						else
						{
							SetFlags(GetFlags() & ~MAIL_SHOW_IMAGES);
						}
					}
				}

				if (n.Type == LNotifyFixedWidthChanged)
				{
					bool DocFixed = Doc->GetFixedWidthFont();
					bool MailFixed = TestFlag(GetFlags(), MAIL_FIXED_WIDTH_FONT);

					if (DocFixed ^ MailFixed)
					{
						if (Doc->GetFixedWidthFont())
						{
							SetFlags(GetFlags() | MAIL_FIXED_WIDTH_FONT);
						}
						else
						{
							SetFlags(GetFlags() & ~MAIL_FIXED_WIDTH_FONT);
						}
					}
				}

				if (n.Type == LNotifyCharsetChanged &&
					dynamic_cast<Html1::LHtml*>(Doc))
				{
					char s[256];
					sprintf_s(s, sizeof(s), ">%s", Doc->GetCharset());
					SetHtmlCharset(s);
				}
			}
			break;
		}
	}

	return 0;
}

void Mail::OnPrintHeaders(ScribePrintContext &Context)
{
	char Str[256];

	// print document
	LDrawListSurface *Page = Context.Pages.Last();
	
	int Line = Context.AppFont->GetHeight();
	Page->SetFont(Context.AppFont);
    LDisplayString *Ds = Context.Text(LLoadString(IDS_MAIL_MESSAGE));
	Context.CurrentY += Line;

	LDataPropI *Ad = GetTo()->First();
	if (Ad)
	{
		sprintf_s(Str, sizeof(Str), "%s: ", LLoadString(FIELD_TO));

        Ds = Page->Text(Context.MarginPx.x1, Context.CurrentY, Str);
		int x = Ds->X();

		for (; Ad; Ad = GetTo()->Next())
		{
			if (Ad->GetStr(FIELD_EMAIL) && Ad->GetStr(FIELD_NAME))
			{
				sprintf_s(Str, sizeof(Str), "%s <%s>", Ad->GetStr(FIELD_NAME), Ad->GetStr(FIELD_EMAIL));
			}
			else if (Ad->GetStr(FIELD_EMAIL))
			{
				strcpy_s(Str, sizeof(Str), Ad->GetStr(FIELD_EMAIL));
			}
			else if (Ad->GetStr(FIELD_EMAIL))
			{
				strcpy_s(Str, sizeof(Str), Ad->GetStr(FIELD_EMAIL));
			}
			else continue;

			Ds = Page->Text(Context.MarginPx.x1 + x, Context.CurrentY, Str);
			Context.CurrentY += Ds->Y();
		}
	}

	if (GetFrom()->GetStr(FIELD_EMAIL) || GetFrom()->GetStr(FIELD_NAME))
	{
		sprintf_s(Str, sizeof(Str), "%s: ", LLoadString(FIELD_FROM));

        Ds = Page->Text(Context.MarginPx.x1, Context.CurrentY, Str);
		int x = Ds->X();

		if (GetFrom()->GetStr(FIELD_EMAIL) && GetFrom()->GetStr(FIELD_NAME))
		{
			sprintf_s(Str, sizeof(Str), "%s <%s>", GetFrom()->GetStr(FIELD_NAME), GetFrom()->GetStr(FIELD_EMAIL));
		}
		else if (GetFrom()->GetStr(FIELD_EMAIL))
		{
			strcpy_s(Str, sizeof(Str), GetFrom()->GetStr(FIELD_EMAIL));
		}
		else if (GetFrom()->GetStr(FIELD_NAME))
		{
			strcpy_s(Str, sizeof(Str), GetFrom()->GetStr(FIELD_NAME));
		}
		else Str[0] = 0;

		Ds = Page->Text(Context.MarginPx.x1 + x, Context.CurrentY, Str);
		Context.CurrentY += Ds->Y();
	}
	
	{
		// Subject
		LString s;
		const char *Subj = GetSubject();
		s.Printf("%s: %s", LLoadString(FIELD_SUBJECT), Subj?Subj:"");
		Context.Text(s);
	}

	{
		// Date
		LString s;
		GetDateSent()->Get(Str, sizeof(Str));
		s.Printf("%s: %s", LLoadString(IDS_DATE), Str);
		Context.Text(s);
	}

	// attachment list...
	List<Attachment> Attached;
	if (GetAttachments(&Attached) &&
		Attached.Length() > 0)
	{
		sprintf_s(Str, sizeof(Str), "%s: ", LLoadString(IDS_ATTACHMENTS));
		Ds = Page->Text(Context.MarginPx.x1, Context.CurrentY, Str);
		int x = Ds->X();

		for (auto a: Attached)
		{
			char Size[32];
			LFormatSize(Size, sizeof(Size), a->GetSize());
			sprintf_s(Str, sizeof(Str), "%s (%s)", a->GetName(), Size);
			Ds = Page->Text(Context.MarginPx.x1 + x, Context.CurrentY, Str);
			Context.CurrentY += Ds->Y();
		}
	}

	// separator line
	LRect Sep(Context.MarginPx.x1, Context.CurrentY + (Line * 5 / 10), Context.MarginPx.x2, Context.CurrentY + (Line * 6 / 10));
	Page->Rectangle(&Sep);
	Context.CurrentY += Line;
}

void Mail::OnPrintText(ScribePrintContext &Context, LPrintPageRanges &Pages)
{
	// Text printing
	LDrawListSurface *Page = Context.Pages.Last();
	LVariant Body;
	if (!GetVariant("BodyAsText", Body) || !Body.Str())
	{
		LgiTrace("%s:%i - No content to print.\n", _FL);
		return;
	}

	LAutoWString w(Utf8ToWide(Body.Str()));
	if (!w)
	{
		LgiTrace("%s:%i - Utf8ToWide failed.\n", _FL);
		return;
	}

	Page->SetFont(Context.MailFont);
	
	for (char16 *s = w; s && *s; )
	{
		// Find end of line..
		char16 *n = StrchrW(s, '\n');
		if (!n) n = s + StrlenW(s);
		ssize_t LineLen = n - s;

		// Find how many characters will fit on the page
		ssize_t Fit = 0;
		if (n > s)
		{
			LDisplayString a(Context.MailFont, s, MIN(LineLen, 1024));
			Fit = a.CharAt(Context.MarginPx.X());
		}
		if (Fit < 0)
		{
			break;
		}

		char16 *e = s + Fit;

		if (e < n)
		{
			// The whole line doesn't fit on the page...
			// Find the best breaking opportunity before that...
			#define ExtraBreak(c) (	( (c) >= 0x3040 && (c) <= 0x30FF ) ||	\
									( (c) >= 0x3300 && (c) <= 0x9FAF )		\
								)
			char16 *StartE = e;
			while (e > s)
			{
				if (e[-1] == ' ' || ExtraBreak(e[-1]))
				{
					break;
				}
				e--;
			}
			if (e == s)
			{
				e = StartE;
			}
		}

		// Output the segment of text
		bool HasRet = e > s ? e[-1] == '\r' : false;
		LString Str(s, (e - s) - (HasRet ? 1 : 0));
		Context.Text(Str);

		// Update the pointers
		s = e;
		if (*s == '\n') s++;
	}
}

/// \returns the number of pages printed
int Mail::OnPrintHtml(ScribePrintContext &Context, LPrintPageRanges &Pages, LSurface *RenderedHtml)
{
	// HTML printing...
	LDrawListSurface *Page = Context.Pages.Last();

	// Work out the scaling from memory bitmap to page..
	double MemScale = (double) Context.pDC->X() / (double) RenderedHtml->X();
	
	// Now paint the bitmap onto the existing page
	int PageIdx = 1, Printed = 0;
	for (int y = 0; y < RenderedHtml->Y(); PageIdx++)
	{
		// Work out how much bitmap we can paint onto the current page...
		int PageRemaining = Context.MarginPx.Y() - Context.CurrentY;
		int MemPaint = (int) (PageRemaining / MemScale);

		// This is how much of the memory context we can fit on the page
		LRect MemRect(0, y, RenderedHtml->X()-1, y + MemPaint - 1);
		LRect Bnds = RenderedHtml->Bounds();
		MemRect.Bound(&Bnds);
		
		// Work out how much page that is take up
		int PageHeight = (int) (MemRect.Y() * MemScale);
		
		// This is the position on the page we are blting to
		LRect PageRect(Context.MarginPx.x1, Context.CurrentY, Context.MarginPx.x2, Context.CurrentY + PageHeight - 1);
		
		if (Pages.InRanges(PageIdx))
		{
			// Do the blt
			Page->StretchBlt(&PageRect, RenderedHtml, &MemRect);
			Printed++;

			// Now move our position down the page..
			Context.CurrentY += PageHeight;
			if ((Context.MarginPx.Y() - Context.CurrentY) * 100 / Context.MarginPx.Y() < 5)
			{
				// Ok we hit the end of the page and need to go to the next page
				Context.CurrentY = Context.MarginPx.y1;
				Page = Context.NewPage();
			}
		}

		y += MemRect.Y();
	}

	return Printed;
}

//////////////////////////////////////////////////////////////////////////////
size_t Mail::Length()
{
	size_t Status = 0;

	for (auto a: Attachments)
	{
		if (a->GetMimeType() &&
			_stricmp(a->GetMimeType(), sMimeMessage) == 0)
		{
			Status++;
		}
	}

	return Status;
}

ssize_t Mail::IndexOf(Mail *m)
{
	ssize_t Status = -1;

	ssize_t n = 0;
	for (auto a: Attachments)
	{
		if (a->GetMimeType() &&
			_stricmp(a->GetMimeType(), sMimeMessage) == 0)
		{
			if (a->GetMsg() == m)
			{
				Status = n;
				break;
			}
			n++;
		}
	}

	return Status;
}

Mail *Mail::operator [](size_t i)
{
	size_t n = 0;
	for (auto a: Attachments)
	{
		if (a->GetMimeType() &&
			_stricmp(a->GetMimeType(), sMimeMessage) == 0)
		{
			if (n == i)
				return a->GetMsg();
			n++;
		}
	}

	return NULL;
}

bool Mail::LoadFromFile(char *File)
{
	if (File)
	{
		LAutoPtr<LFile> f(new LFile);
		if (f->Open(File, O_READ))
		{
			return Import(AutoCast(f), sMimeMessage);
		}
	}

	return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////
bool CreateMailAddress(LStream &Out, LDataPropI *Addr, MailProtocol *Protocol)
{
    if (!Addr)
        return false;

    auto Email = Addr->GetStr(FIELD_EMAIL);
    if (!Email)
        return false;
    
	auto Name = LEncodeRfc2047(Addr->GetStr(FIELD_NAME), NULL/*charset*/, &Protocol->CharsetPrefs);
	if (Name)
	{
		if (Name.Find("\"") >= 0)
			Out.Print("'%s' ", Name.Get());
		else
			Out.Print("\"%s\" ", Name.Get());
	}

	Out.Print("<%s>", Email);
	return true;
}

bool CreateMailHeaders(ScribeWnd *App, LStream &Out, LDataI *Mail, MailProtocol *Protocol)
{
	bool Status = true;

	// Setup
	char Buffer[1025];

	// Construct date
	LDateTime Dt;
	int TimeZone = Dt.SystemTimeZone();
	Dt.SetNow();
	sprintf_s(Buffer, sizeof(Buffer),
			"Date: %s, %i %s %i %i:%2.2i:%2.2i %s%2.2d%2.2d\r\n",
			LDateTime::WeekdaysShort[Dt.DayOfWeek()],
			Dt.Day(),
			LDateTime::MonthsShort[Dt.Month()-1],
			Dt.Year(),
			Dt.Hours(),
			Dt.Minutes(),
			Dt.Seconds(),
			(TimeZone >= 0) ? "+" : "",
			TimeZone / 60,
			abs(TimeZone) % 60);
	Status &= Out.Write(Buffer, strlen(Buffer)) > 0;

	if (Protocol && Protocol->ProgramName)
	{
		// X-Mailer:
		Status &= Out.Print("X-Mailer: %s\r\n", Protocol->ProgramName.Get()) > 0;
	}

	if (Protocol && Protocol->ExtraOutgoingHeaders)
	{
		for (char *s=Protocol->ExtraOutgoingHeaders; s && *s; )
		{
			char *e = s;
			while (*e && *e != '\r' && *e != '\n') e++;
			ssize_t l = e-s;
			if (l > 0)
			{
				Status &= Out.Write(s, l) > 0;
				Status &= Out.Write((char*)"\r\n", 2) > 0;
			}
			while (*e && (*e == '\r' || *e == '\n')) e++;
			s = e;
		}
	}

	int Priority = (int)Mail->GetInt(FIELD_PRIORITY);
	if (Priority != MAIL_PRIORITY_NORMAL)
	{
		// X-Priority:
		Status &= Out.Print("X-Priority: %i\r\n", Priority) > 0;
	}

    uint32_t MarkColour = (uint32_t)Mail->GetInt(FIELD_COLOUR);
	if (MarkColour)
	{
		// X-Color (HTML Colour Ref for email marking)
		Status &= Out.Print("X-Color: #%2.2X%2.2X%2.2X\r\n",
			                R24(MarkColour),
			                G24(MarkColour),
			                B24(MarkColour)) > 0;
	}

	// Message-ID:
	auto MessageID = Mail->GetStr(FIELD_MESSAGE_ID);
	if (MessageID)
	{
		for (auto m=MessageID; *m; m++)
		{
			if (*m <= ' ')
			{
				printf("%s:%i - Bad message ID '%s'\n", _FL, MessageID);
				return false;
			}			
		}
		
		Status &= Out.Print("Message-ID: %s\r\n", MessageID) > 0;
	}

	// References:
	auto References = Mail->GetStr(FIELD_REFERENCES);
	if (ValidStr(References))
	{
		auto Dir = strrchr(References, '/');
		LString Ref;
		if (Dir)
		{
			LUri u;
			Ref = u.DecodeStr(Dir + 1);
		}
		else
			Ref = References;

		if (*Ref == '<')
    		Status &= Out.Print("References: %s\r\n", Ref.Get()) > 0;
    	else
	    	Status &= Out.Print("References: <%s>\r\n", Ref.Get()) > 0;
	}

	// To:
	LDataIt Addr = Mail->GetList(FIELD_TO);
	LArray<Store3Addr*> Objs;
	LArray<LDataPropI*> To, Cc;
	ContactGroup *Group;
	for (unsigned i=0; i<Addr->Length(); i++)
	{
	    LDataPropI *a = (*Addr)[i];
	    EmailAddressType AddrType = (EmailAddressType)a->GetInt(FIELD_CC);

		LString Addr = a->GetStr(FIELD_EMAIL);
		if (LIsValidEmail(Addr))
		{
			if (AddrType == MAIL_ADDR_CC)
				Cc.Add(a);
			else if (AddrType == MAIL_ADDR_TO)
				To.Add(a);
		}
		else if ((Group = LookupContactGroup(App, Addr)))
		{
			Group->UsedTs.SetNow();

			LString::Array Addrs = Group->GetAddresses();
			for (unsigned n=0; n<Addrs.Length(); n++)
			{
				Store3Addr *sa = new Store3Addr(Group->GetObject()->GetStore(), NULL);
				if (sa)
				{
					sa->Addr = Addrs[n];
					Objs.Add(sa);

					if (AddrType == MAIL_ADDR_CC)
						Cc.Add(sa);
					else if (AddrType == MAIL_ADDR_TO)
						To.Add(sa);
				}
			}
		}
	}
	if (To.Length())
	{
	    for (unsigned i=0; i<To.Length(); i++)
	    {
	        if (i)
	            Out.Print(",\r\n\t");
	        else
    	        Out.Print("To: ");
            
            if (!CreateMailAddress(Out, To[i], Protocol))
                return false;
	    }
	    Out.Print("\r\n");
	}
	if (Cc.Length())
	{
	    for (unsigned i=0; i<Cc.Length(); i++)
	    {
	        if (i)
	            Out.Print(",\r\n\t");
	        else
    	        Out.Print("Cc: ");
            
            if (!CreateMailAddress(Out, Cc[i], Protocol))
                return false;
	    }
	    Out.Print("\r\n");
	}

	// From:
	LDataPropI *From = Mail->GetObj(FIELD_FROM);
	if (From && From->GetStr(FIELD_EMAIL))
	{
		Out.Print("From: ");
		if (!CreateMailAddress(Out, From, Protocol))
		    return false;
		Out.Print("\r\n");
	}
	else
	{
		LgiTrace("%s:%i - No from address.\n", _FL);
		return false;
	}

	// Reply-To:
	LDataPropI *Reply = Mail->GetObj(FIELD_REPLY);
	if (Reply && ValidStr(Reply->GetStr(FIELD_EMAIL)))
	{
		Out.Print("Reply-To: ");
		if (!CreateMailAddress(Out, Reply, Protocol))
		    return false;
		Out.Print("\r\n");
	}

	// Subject:
	auto Subj = LEncodeRfc2047(Mail->GetStr(FIELD_SUBJECT), 0, &Protocol->CharsetPrefs, 9);
	sprintf_s(Buffer, sizeof(Buffer), "Subject: %s\r\n", Subj ? Subj.Get() : "");
	Status &= Out.Write(Buffer, strlen(Buffer)) > 0;

	// DispositionNotificationTo
	uint8_t DispositionNotificationTo = TestFlag(Mail->GetInt(FIELD_FLAGS), MAIL_READ_RECEIPT);
	if (DispositionNotificationTo && From)
	{
		int ch = sprintf_s(Buffer, sizeof(Buffer), "Disposition-Notification-To:");
		if (auto Nme = LEncodeRfc2047(From->GetStr(FIELD_NAME), 0, &Protocol->CharsetPrefs))
			ch += sprintf_s(Buffer+ch, sizeof(Buffer)-ch, " \"%s\"", Nme.Get());
		ch += sprintf_s(Buffer+ch, sizeof(Buffer)-ch, " <%s>\r\n", From->GetStr(FIELD_EMAIL));
		Status &= Out.Write(Buffer, ch) > 0;
	}
	
	// Content-Type
	LDataPropI *Root = Mail->GetObj(FIELD_MIME_SEG);
	if (Root)
	{
		auto MimeType = Root->GetStr(FIELD_MIME_TYPE);
		auto Charset = Root->GetStr(FIELD_CHARSET);
		if (MimeType)
		{
			LString s;
			s.Printf("Content-Type: %s%s%s\r\n", MimeType, Charset?"; charset=":"", Charset?Charset:"");
			Status &= Out.Write(s, s.Length()) == s.Length();
		}
	}
	else LAssert(0);
	
	Objs.DeleteObjects();
	return Status;
}
