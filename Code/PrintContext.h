#ifndef _SCRIBE_PRINT_CONTEXT_H_
#define _SCRIBE_PRINT_CONTEXT_H_

#include "lgi/common/Printer.h"
#include "lgi/common/DrawListSurface.h"

struct ScribePrintContext : public LCss, public LPrintEvents
{
public:
	constexpr static int OnBeginPrintError = -1;
	constexpr static int OnBeginPrintCancel = 0;

	ScribeWnd *App = NULL;
	LSurface *pDC = NULL;
	LPrintDC *PrintDC = NULL;
	LPoint Dpi;
	Thing *Object = NULL;
	LFontType FontType;
	LRect MarginPx;
	LAutoPtr<LFont> AppFont, MailFont;
	int CurrentY = 0;
	LColour Fore = LColour::Black;
	LArray<LDrawListSurface*> Pages;
	LAutoPtr<LMemDC> HtmlImg;
	LAutoPtr<LPrintPageRanges> PageRanges;

	ScribePrintContext(ScribeWnd *app, Thing *object);
	~ScribePrintContext();

	// Api
	LDrawListSurface *NewPage();
    LDisplayString *Text(const char *str, int x = -1);

	// LPrintEvents impl	
	int OnBeginPrint(LPrintDC *pdc);
	bool OnPrintPage(LPrintDC *pdc, int PageIndex);
	LPrintPageRanges *GetPageRanges() { return PageRanges; }

};

#endif