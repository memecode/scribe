#ifndef _SCRIBE_PRINT_CONTEXT_H_
#define _SCRIBE_PRINT_CONTEXT_H_

#include "lgi/common/Printer.h"
#include "lgi/common/DrawListSurface.h"

struct ScribePrintContext : public LCss, public LPrinter::Context
{
public:
	ScribeWnd *App = nullptr;
	LSurface *pDC = nullptr;
	LPrintDC *PrintDC = nullptr;
	LPoint Dpi;
	Thing *Object = nullptr;
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
	void OnBeginPrint(LPrintDC *pdc, std::function<void(int)> callback);
	bool OnPrintPage(LPrintDC *pdc, int PageIndex);
	LPrintPageRanges *GetPageRanges() { return PageRanges; }

};

#endif