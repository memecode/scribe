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
	Dpi.x = Dpi.y = LScreenDpi();
	Object = object;
	MarginPx.ZOff(0, 0);
}

ScribePrintContext::~ScribePrintContext()
{
	Pages.DeleteObjects();
}

LDrawListSurface *ScribePrintContext::NewPage()
{
	LDrawListSurface *dls = new LDrawListSurface(PrintDC);
	if (dls)
		Pages.Add(dls);
	return dls;
}

int ScribePrintContext::OnBeginPrint(LPrintDC *pdc)
{
	pDC = pdc;
	PrintDC = pdc;
	Dpi.x = PrintDC->DpiX();
	Dpi.y = PrintDC->DpiY();

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

	CurrentY = MarginPx.x1 = MarginLeft().ToPx(PrintDC->X(), AppFont, Dpi.x);
	MarginPx.y1 = MarginTop().ToPx(PrintDC->Y(), AppFont, Dpi.y);
	MarginPx.x2 = pDC->X() - MarginRight().ToPx(PrintDC->X(), AppFont, Dpi.x);
	MarginPx.y2 = pDC->Y() - MarginBottom().ToPx(PrintDC->Y(), AppFont, Dpi.y);

	LVariant DefAlt;
	if (!App->GetOptions()->GetValue(OPT_DefaultAlternative, DefAlt))
		DefAlt = 1; // HTML
	
	if (!AppFont || !MailFont)
	{
		LgiTrace("%s:%i - Error creating fonts for printing\n", _FL);
		return OnBeginPrintError;
	}
	
	// There is always at least one page...
	Pages.DeleteObjects();
	NewPage();
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
		PrintPreview Dlg(App, mail, pdc);
		if (!Dlg.DoModal())
		    return OnBeginPrintCancel;

		PageRanges.Reset(new LPrintPageRanges(Dlg.GetPageRanges()));
		HtmlImg = Dlg.ReleaseImage();
		mail->OnPrintHeaders(*this);
		mail->OnPrintHtml(*this, *PageRanges, HtmlImg);
	}
	else
	{
		PageRanges.Reset(new LPrintPageRanges(NULL));
		Object->OnPrintHeaders(*this);
		Object->OnPrintText(*this, *PageRanges);
	}

	return (int)Pages.Length();
}

bool ScribePrintContext::OnPrintPage(LPrintDC *pdc, int PageIndex)
{
	pDC = pdc;
	if (!pDC || PageIndex < 0 || PageIndex >= (int)Pages.Length())
	    return false;
	
	return Pages[PageIndex]->OnPaint(pDC);
}

LDisplayString *ScribePrintContext::Text(const char *str, int x)
{
    LDrawListSurface *p = Pages.Last();
    LFont *f = p->GetFont();
    if (!f)
		p->SetFont(f = AppFont);
	
    if (CurrentY + f->GetHeight() >= MarginPx.Y())
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

