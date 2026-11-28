#ifndef _PRINT_PREVIEW_H_
#define _PRINT_PREVIEW_H_

namespace Html2 { class LHtml; }
struct ScribePrintContext;

class PrintPreview : public LDialog
{
	struct PrintPreviewPriv *d;
	
public:
	PrintPreview(ScribeWnd *App, Mail *m, ScribePrintContext *context);
	~PrintPreview();
	
	int OnNotify(LViewI *Ctrl, const LNotification &n) override;
	void OnPosChange() override;
	LSurface *GetImage();
	LAutoPtr<LMemDC> ReleaseImage();
	Html2::LHtml *ReleaseHtml();
	LString GetPageRanges();
	void OnPulse() override;
};

#endif
