#ifndef _PRINT_PREVIEW_H_
#define _PRINT_PREVIEW_H_

class PrintPreview : public LDialog
{
	struct PrintPreviewPriv *d;
	
public:
	PrintPreview(ScribeWnd *App, Mail *m, LPrintDC *pDC);
	~PrintPreview();
	
	int OnNotify(LViewI *Ctrl, LNotification n);
	void OnPosChange();
	LSurface *GetImage();
	LAutoPtr<LMemDC> ReleaseImage();
	LString GetPageRanges();
	void OnPulse();
};

#endif