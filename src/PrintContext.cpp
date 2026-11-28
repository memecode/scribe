#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "lgi/common/Printer.h"
#include "lgi/common/DrawListSurface.h"
#include "ScribePageSetup.h"
#include "PrintPreview.h"
#include "PrintContext.h"

ScribePrintContext::ScribePrintContext(ScribeWnd *app, Thing *object) : FontType("Courier New", 9)
{
	App = app;
	Dpi = LScreenDpi();
	Object = object;
	MarginPx.ZOff(0, 0);
}

ScribePrintContext::~ScribePrintContext()
{
	Pages.DeleteObjects();
}

LDrawListSurface *ScribePrintContext::NewPage()
{
	auto dls = new LDrawListSurface(PrintDC);
	if (dls)
		Pages.Add(dls);
	return dls;
}

#define PrintStatus(val) \
	{ if (callback) callback(val); return; }

void ScribePrintContext::OnBeginPrint(LPrintDC *pdc, std::function<void(int)> callback)
{
	pDC = pdc;
	PrintDC = pdc;
	LRect printable;
	if (!PrintDC || !PrintDC->GetPrintableRect(printable))
	{
		LgiTrace("%s:%i - Printer has no valid printable area.\n", _FL);
		PrintStatus(OnBeginPrintError);
	}
	Dpi = PrintDC->GetDpi();
	#if defined(__GTK_H__)
		Dpi = LPoint(72, 72);
	#endif
	if (Dpi.x <= 0 || Dpi.y <= 0)
	{
		LgiTrace("%s:%i - Invalid printer coordinate resolution.\n", _FL);
		PrintStatus(OnBeginPrintError);
	}

	// read any options out..
	LVariant v;
	#define GetMargin(opt, var) \
		var(LCss::Len(LenCm, App->GetOptions()->GetValue(opt, v) ? (float)v.CastDouble() : 1.0f));
	GetMargin(OPT_MarginX1, MarginLeft);
	GetMargin(OPT_MarginY1, MarginTop);
	GetMargin(OPT_MarginX2, MarginRight);
	GetMargin(OPT_MarginY2, MarginBottom);

	FontType.Serialize(App->GetOptions(), OPT_PrintFont, false);
	AppFont.Reset(FontType.Create(pdc));

	Mail *mail = Object->IsMail();
	if (mail && TestFlag(mail->GetFlags(), MAIL_FIXED_WIDTH_FONT))
		FontType.GetSystemFont("Fixed");

	MailFont.Reset(FontType.Create(pdc));

	if (!AppFont || !MailFont)
	{
		LgiTrace("%s:%i - Error creating fonts for printing\n", _FL);
		PrintStatus(OnBeginPrintError);
	}

	MailFont->Transparent(true);
	MailFont->Colour(L_TEXT, L_WORKSPACE);
	MailFont->Create(0, 0, pdc);
	LDisplayString dsSpace(MailFont, " ");
	MailFont->TabSize(dsSpace.X() * 8);

	AppFont->Transparent(true);
	AppFont->Colour(L_TEXT, L_WORKSPACE);
	AppFont->Create(0, 0, pdc);
	LDisplayString dsSpace2(AppFont, " ");
	AppFont->TabSize(dsSpace2.X() * 8);
	
	// int Line = AppFont->GetHeight();

	MarginPx.x1 = printable.x1 + MarginLeft().ToPx(printable.X(), AppFont, Dpi.x);
	MarginPx.y1 = printable.y1 + MarginTop().ToPx(printable.Y(), AppFont, Dpi.y);
	MarginPx.x2 = printable.x2 - MarginRight().ToPx(printable.X(), AppFont, Dpi.x);
	MarginPx.y2 = printable.y2 - MarginBottom().ToPx(printable.Y(), AppFont, Dpi.y);

	LVariant DefAlt;
	if (!App->GetOptions()->GetValue(OPT_DefaultAlternative, DefAlt))
		DefAlt = 1; // HTML
	
	if (!MarginPx.Valid() || MarginPx.x1 < printable.x1 || MarginPx.y1 < printable.y1 ||
		MarginPx.x2 > printable.x2 || MarginPx.y2 > printable.y2)
	{
		LgiTrace("%s:%i - Invalid print margins for printable area.\n", _FL);
		PrintStatus(OnBeginPrintError);
	}
	
	// There is always at least one page...
	Pages.DeleteObjects();
	if (!NewPage())
	{
		LgiTrace("%s:%i - Failed to create the first print page.\n", _FL);
		PrintStatus(OnBeginPrintError);
	}
	CurrentY = MarginPx.y1;
	
	// Check for HTML or text printing:
	bool HtmlPrinting = false;
	if (mail)
	{
		LAutoString HtmlContent(NewStr(mail->GetHtml()));
		HtmlPrinting = DefAlt.CastInt32() != 0 && ValidStr(HtmlContent);
	}    	
	if (HtmlPrinting)
	{
		mail->OnPrintHeaders(*this);
		HtmlStartPage = (int)Pages.Length() - 1;
		HtmlStartY = CurrentY;
		auto Dlg = new PrintPreview(App, mail, this);
		Dlg->DoModal([this, Dlg, callback, mail](auto dlg, auto id)
		{
			if (!id)
				PrintStatus(OnBeginPrintCancel);

			PageRanges.Reset(new LPrintPageRanges(Dlg->GetPageRanges()));
			HtmlPrint.Reset(Dlg->ReleaseHtml());
			if (!HtmlPrint)
			{
				LgiTrace("%s:%i - Failed to retain HTML control for printing.\n", _FL);
				PrintStatus(OnBeginPrintError);
			}

			LPoint HtmlSize;
			if (!LayoutHtml(HtmlPrint, HtmlSize))
			{
				LgiTrace("%s:%i - Invalid HTML print layout geometry.\n", _FL);
				PrintStatus(OnBeginPrintError);
			}

			int HtmlPages = HtmlSize.y > 0 ? 1 : 0;
			if (HtmlPages && HtmlSize.y > HtmlFirstPageStep)
			{
				int Remaining = HtmlSize.y - HtmlFirstPageStep;
				HtmlPages += Remaining / HtmlPageStep + (Remaining % HtmlPageStep != 0);
			}

			int TotalPages = HtmlPages ? HtmlStartPage + HtmlPages : (int)Pages.Length();
			while ((int)Pages.Length() < TotalPages)
			{
				if (!NewPage())
				{
					LgiTrace("%s:%i - Failed to create an HTML print page.\n", _FL);
					PrintStatus(OnBeginPrintError);
				}
			}

			PrintStatus((int)Pages.Length());
		});

		return; // Don't call callback yet, the UI lambda will do it.
	}
	else
	{
		PageRanges.Reset(new LPrintPageRanges(LString()));
		Object->OnPrintHeaders(*this);
		Object->OnPrintText(*this, *PageRanges);
	}

	PrintStatus((int)Pages.Length());
}

bool ScribePrintContext::LayoutHtml(Html2::LHtml *html, LPoint &size)
{
	size = html->Layout(true);
	HtmlLineHeight = html->GetFont() ? html->GetFont()->GetHeight() : 0;
	if (size.x <= 0 || size.y <= 0 || HtmlLineHeight <= 0)
		return false;
	HtmlScale = 1.0;
	#if defined(__GTK_H__)
		if (size.x > MarginPx.X())
			HtmlScale = (double)MarginPx.X() / size.x;
	#else
		if (size.x > MarginPx.X())
			LgiTrace("%s:%i - HTML exceeds the printable width on this backend.\n", _FL);
	#endif
	int first = (int)((MarginPx.y2 - HtmlStartY + 1) / HtmlScale);
	int full = (int)(MarginPx.Y() / HtmlScale);
	if (full < HtmlLineHeight)
		return false;
	if (first < HtmlLineHeight)
	{
		if (!NewPage())
			return false;
		HtmlStartPage = (int)Pages.Length() - 1;
		HtmlStartY = CurrentY = MarginPx.y1;
		first = full;
	}
	HtmlFirstPageStep = first / HtmlLineHeight * HtmlLineHeight;
	HtmlPageStep = full / HtmlLineHeight * HtmlLineHeight;
	return true;
}

bool ScribePrintContext::OnPrintPage(LPrintDC *pdc, int PageIndex)
{
	return PaintPage(pdc, PageIndex, HtmlPrint);
}

bool ScribePrintContext::PaintPage(LPrintDC *pdc, int PageIndex, Html2::LHtml *html)
{
	if (!pdc || PageIndex < 0 || PageIndex >= (int)Pages.Length())
	    return false;

	LPoint OldOrigin = pdc->GetOrigin();
	pdc->SetOrigin(0, 0);
	bool Status = Pages[PageIndex]->OnPaint(pdc);
	if (Status && html && PageIndex >= HtmlStartPage)
	{
		int HtmlPage = PageIndex - HtmlStartPage;
		int Scroll = HtmlPage == 0 ?
			0 :
			HtmlFirstPageStep + (HtmlPage - 1) * HtmlPageStep;
		int BodyTop = HtmlPage == 0 ? HtmlStartY : MarginPx.y1;

		#if defined(__GTK_H__)
			auto cr = pdc->Handle();
			Gtk::cairo_save(cr);
			Gtk::cairo_rectangle(cr, MarginPx.x1, BodyTop, MarginPx.X(), MarginPx.y2 - BodyTop + 1);
			Gtk::cairo_clip(cr);
			Gtk::cairo_translate(cr, MarginPx.x1, BodyTop);
			Gtk::cairo_scale(cr, HtmlScale, HtmlScale);
			pdc->SetHandle(cr);
			LRect htmlClip(0, 0, (int)ceil(MarginPx.X() / HtmlScale) - 1,
				(int)ceil((MarginPx.y2 - BodyTop + 1) / HtmlScale) - 1);
			pdc->SetDefaultClip(htmlClip);
		#elif WINNATIVE
			int saved = SaveDC(pdc->Handle());
			if (!saved ||
				IntersectClipRect(pdc->Handle(), MarginPx.x1, BodyTop, MarginPx.x2 + 1, MarginPx.y2 + 1) == ERROR ||
				SetMetaRgn(pdc->Handle()) == ERROR)
			{
				if (saved) RestoreDC(pdc->Handle(), saved);
				pdc->SetOrigin(OldOrigin);
				LgiTrace("%s:%i - Failed to clip HTML print page.\n", _FL);
				return false;
			}
		#endif
		html->SetVScroll(0);
		#if defined(__GTK_H__)
		pdc->SetOrigin(0, Scroll);
		#else
		pdc->SetOrigin(-MarginPx.x1, Scroll - BodyTop);
		#endif
		html->OnPaint(pdc);
		Status = !html->GetMaxPaintTimeout();
		#if defined(__GTK_H__)
			Gtk::cairo_restore(cr);
			pdc->SetHandle(cr);
			LRect printable;
			if (pdc->GetPrintableRect(printable))
				pdc->SetDefaultClip(printable);
			else
			{
				LgiTrace("%s:%i - Failed to restore print page clipping.\n", _FL);
				Status = false;
			}
		#elif WINNATIVE
			RestoreDC(pdc->Handle(), saved);
		#endif
	}
	pdc->SetOrigin(OldOrigin);
	return Status;
}

LDisplayString *ScribePrintContext::Text(const char *str, int x)
{
    LDrawListSurface *p = Pages.Last();
    LFont *f = p->GetFont();
    if (!f)
		p->SetFont(f = AppFont);
	
    if (CurrentY + f->GetHeight() > MarginPx.y2 + 1)
    {
		p = NewPage();
		if (!p)
			return NULL;
        p->SetFont(f);
        CurrentY = MarginPx.y1;
    }
    
    LDisplayString *ds = p->Text(MarginPx.x1 + (x >= 0 ? x : 0), CurrentY, str);
    if (ds)
        CurrentY += ds->Y();
    return ds;
}

