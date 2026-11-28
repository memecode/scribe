#include "Scribe.h"
#include "PrintPreview.h"
#include "PrintContext.h"
#include "resdefs.h"
#include "lgi/common/ZoomView.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/FileSelect.h"
#include "lgi/common/Html2.h"

#include "DynamicHtml.h"

#define DEFAULT_PREVIEW_WIDTH		1000
#define MIN_PREVIEW_DPI				300

struct PrintPreviewPriv
{
	ScribeWnd *App = nullptr;
	LPrintDC *PrintDC = nullptr;
	ScribePrintContext *Context = nullptr;
	Mail *m = nullptr;
	PrintPreview *Wnd = nullptr;
	LZoomView *Zoom = nullptr;
	LAutoPtr<LMemDC> LayoutImage;
	LAutoPtr<Html2::LHtml> Ctrl;
	bool PrintSurfaceReady = false;
	bool IsLoaded = false;
	int HeaderContentHeight = 0;
	LString Ranges;
	
	PrintPreviewPriv(PrintPreview *w)
	{
		Wnd = w;
		if (Ctrl.Reset(new Html2::LHtml(IDC_HTML_IMAGES, 0, 0, DEFAULT_PREVIEW_WIDTH, 4000)))
		{
			Ctrl->SetMaxPaintTime(5000);
			Ctrl->SetLoadImages(true);
		}
	}

	bool Render()
	{
		// Create a HTML control
		if (!Zoom)
			Wnd->GetViewById(IDC_PREVIEW, Zoom);		
		if (!Ctrl || !Zoom)
		{
			LgiTrace("%s:%i - Failed to create controls.\n", _FL);
			return false;
		}

		if (!PrintSurfaceReady || !PrintDC || PrintDC->X() <= 0)
			return false;

		int LayoutWidth = Context->MarginPx.X();
		LRect p(0, 0, LayoutWidth-1, 100000);
		Ctrl->SetPos(p);

		// Get the size of the HTML layout
		LPoint Size;
		if (!Context->LayoutHtml(Ctrl, Size))
		{
			LgiTrace("%s:%i - No content to print.\n", _FL);
			return false;
		}
		
		#if defined(__GTK_H__)
			auto Dpi = PrintDC->GetDpi();
			if (Dpi.x <= 0 || Dpi.y <= 0)
			{
				LgiTrace("%s:%i - Invalid printer DPI for HTML preview (%i,%i).\n", _FL, Dpi.x, Dpi.y);
				return false;
			}
			LPoint PreviewDpi(MAX(Dpi.x, MIN_PREVIEW_DPI), MAX(Dpi.y, MIN_PREVIEW_DPI));
			Zoom->SetSurface(nullptr, false);
			LayoutImage.Reset();
			LAutoPtr<LMemDC> Image(PrintDC->RenderPageToBitmap(PreviewDpi, [this](LPrintDC *PreviewDC)
			{
				return Context->PaintPage(PreviewDC, 0, Ctrl);
			}));
			if (!Image)
				return false;
			LayoutImage.Reset(Image.Release());
		#else
			int PreviewWidth = LayoutWidth;
			int PreviewHeight = Size.y;
			if (!LayoutImage || LayoutImage->X() != PreviewWidth || LayoutImage->Y() != PreviewHeight)
			{
				Zoom->SetSurface(nullptr, false);
				if (!LayoutImage.Reset(new LMemDC(_FL, PreviewWidth, PreviewHeight, System24BitColourSpace)))
				{
					LgiTrace("%s:%i - Can't create print-layout bitmap (%ix%i).\n", _FL, PreviewWidth, PreviewHeight);
					return false;
				}
			}
			LayoutImage->Colour(LColour::White);
			LayoutImage->Rectangle();
			Ctrl->OnPaint(LayoutImage);
		#endif
		if (Ctrl->GetMaxPaintTimeout())
		{
			LAssert(!"Max paint time reached.");
		}

		Zoom->SetDefaultZoomMode(LZoomView::ZoomFitX);
		Zoom->SetSurface(LayoutImage, false);
		
		return true;		
	}	
};

PrintPreview::PrintPreview(ScribeWnd *App, Mail *m, ScribePrintContext *context)
{
	d = new PrintPreviewPriv(this);
	d->App = App;
	d->m = m;
	d->Context = context;
	d->PrintDC = context->PrintDC;
	d->PrintSurfaceReady = d->Ctrl && d->Ctrl->SetFontSurface(d->PrintDC);
	if (!d->PrintSurfaceReady)
		LgiTrace("%s:%i - Failed to create print fonts for HTML preview.\n", _FL);
	
	if (!LoadFromResource(IDD_PRINT_PREVIEW))
	{
		LAssert(!"Resource missing.");
		return;
	}

	ThingUi *Ui = m->GetUI();
	MoveSameScreen(Ui ? (LWindow*)Ui : App);
		
	SetCtrlName(IDC_STATUS, LLoadString(IDS_LOADING));
	SetCtrlValue(IDC_PAGE_ALL, true);
	SetCtrlEnabled(IDOK, false);

	// Give it the HTML to parse
	d->Ctrl->SetNotify(this);
	d->Ctrl->Visible(false);
	AddView(d->Ctrl);

	LAutoString HtmlContent(NewStr(m->GetHtml()));
	d->Ctrl->SetEnv(m);
	d->Ctrl->SetCharset(m->GetHtmlCharset());
	d->Ctrl->Name(HtmlContent);

	d->Render();
	SetPulse(300);
}

PrintPreview::~PrintPreview()
{
	if (d->Zoom)
		d->Zoom->SetSurface(nullptr, false);
	delete d;
}

void PrintPreview::OnPulse()
{
	if (d->Ctrl)
	{
		// This is basically a hack to get images working in the HTML control.
		// As it's not connected to a View heirachy it can't send messages to
		// itself like normal.
		LMessage m(M_JOBS_LOADED);
		d->Ctrl->OnEvent(&m);
	}
}

LSurface *PrintPreview::GetImage()
{
	return d->LayoutImage;
}

LAutoPtr<LMemDC> PrintPreview::ReleaseImage()
{
	if (d->Zoom)
		d->Zoom->SetSurface(nullptr, false);
	return d->LayoutImage;
}

Html2::LHtml *PrintPreview::ReleaseHtml()
{
	if (!d->Ctrl || !d->Ctrl->Detach())
		return nullptr;
	d->Ctrl->SetNotify(nullptr);
	return d->Ctrl.Release();
}

LString PrintPreview::GetPageRanges()
{
	return d->Ranges;
}

void PrintPreview::OnPosChange()
{
    auto it = Children.begin();
    LLayout *t = dynamic_cast<LLayout*>((LViewI*)it);
    if (t)
    {
        LRect r = GetClient();
        r.Inset(LTableLayout::CellSpacing, LTableLayout::CellSpacing);
        t->SetPos(r);
    }
}

int PrintPreview::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_PAGE_RANGES:
		{
			bool HasContent = Strlen(Ctrl->Name()) > 0;
			SetCtrlValue(IDC_PAGE_PARTIAL, HasContent);
			SetCtrlValue(IDC_PAGE_ALL, !HasContent);
			break;
		}
		case IDC_SAVE_IMG:
		{
			if (d->LayoutImage)
			{
				auto s = new LFileSelect(this);
				s->Type("JPEG", "*.jpg");
				
				char p[MAX_PATH_LEN];
				LGetSystemPath(LSP_USER_DOWNLOADS, p, sizeof(p));
				LMakePath(p, sizeof(p), p, "print-preview.jpg");
				s->Name(p);
				
				s->Save([this](auto s, auto ok)
				{
					if (ok)
						GdcD->Save(s->Name(), d->LayoutImage);
				});
			}
			else LgiMsg(this, "No image to save.", LLoadString(IDS_ERROR));
			break;
		}
		case IDOK:
		{
			auto Partial = GetCtrlValue(IDC_PAGE_PARTIAL);
			d->Ranges = Partial ? GetCtrlName(IDC_PAGE_RANGES) : NULL;
			EndModal(1);
			break;
		}
		case IDCANCEL:
		{
			EndModal(0);
			break;
		}
		case IDC_HTML_IMAGES:
		{
			if (n.Type == LNotifyDocLoaded)
			{
				// Html control finished loading...
				d->IsLoaded = true;
				if (d->Render())
				{
					SetCtrlEnabled(IDOK, true);
					SetCtrlName(IDC_STATUS, LLoadString(IDS_PREVIEW));
				}
				else
				{
					SetCtrlEnabled(IDOK, false);
					SetCtrlName(IDC_STATUS, LLoadString(IDS_ERROR));
					LgiMsg(this, "Failed to render the print preview.", LLoadString(IDS_ERROR));
				}
			}
			break;			
		}
	}

	return 0;
}
