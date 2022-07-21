#include "Scribe.h"
#include "PrintPreview.h"
#include "resdefs.h"
#include "lgi/common/ZoomView.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/Slider.h"
#include "lgi/common/FileSelect.h"

#define DEFAULT_PAGE_SIZE			1000

struct PrintPreviewPriv
{
	ScribeWnd *App = NULL;
	LPrintDC *PrintDC = NULL;
	Mail *m = NULL;
	PrintPreview *Wnd = NULL;
	LZoomView *Zoom = NULL;
	LAutoPtr<LMemDC> Mem;
	LAutoPtr<Html1::LHtml> Ctrl;
	bool IsLoaded = false;
	int HeaderContentHeight = 0;
	LSlider *Slider = NULL;
	bool InUpdate = false;
	LString Ranges;
	
	PrintPreviewPriv(PrintPreview *w)
	{
		Wnd = w;
		if (Ctrl.Reset(new Html1::LHtml(IDC_HTML_IMAGES, 0, 0, DEFAULT_PAGE_SIZE, 4000)))
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

		// Set width		
		int Width = (int)Wnd->GetCtrlValue(IDC_WIDTH);
		LRect p(0, 0, Width-1, 100000);
		Ctrl->SetPos(p);

		// Get the size of the HTML layout
		LPoint Size = Ctrl->Layout();
		if (Size.x <= 0 || Size.y <= 0)
		{
			LgiTrace("%s:%i - No content to print.\n", _FL);
			return false;
		}
		
		// Create a memory context big enough for all the content
		if (!Mem || Mem->X() < Width || Mem->Y() < Size.y)
		{
			Zoom->SetSurface(NULL, true);
			if (!Mem.Reset(new LMemDC(Width, Size.y, System24BitColourSpace)))
			{
				LgiTrace("%s:%i - Can't create memory bitmap context (%ix%i).\n", _FL, Width, Size.y);
				return false;
			}
		}
		
		// Clear the page to white
		Mem->Colour(LColour::White);
		Mem->Rectangle();

		// Ask the HTML control to paint itself into the memory context
		Ctrl->OnPaint(Mem);
		if (Ctrl->GetMaxPaintTimeout())
		{
			LAssert(!"Max paint time reached.");
		}
		Zoom->SetDefaultZoomMode(LZoomView::ZoomFitX);
		Zoom->SetSurface(Mem, false);
		
		return true;		
	}	
};

PrintPreview::PrintPreview(ScribeWnd *App, Mail *m, LPrintDC *PrintDC)
{
	d = new PrintPreviewPriv(this);
	d->App = App;
	d->m = m;
	d->PrintDC = PrintDC;
	
	if (!LoadFromResource(IDD_PRINT_PREVIEW))
	{
		LAssert(!"Resource missing.");
		return;
	}

	ThingUi *Ui = m->GetUI();
	MoveSameScreen(Ui ? (LWindow*)Ui : App);
		
	SetCtrlValue(IDC_WIDTH, DEFAULT_PAGE_SIZE);
	SetCtrlValue(IDC_SLIDE, DEFAULT_PAGE_SIZE);
	SetCtrlName(IDC_STATUS, LLoadString(IDS_LOADING));
	SetCtrlValue(IDC_PAGE_ALL, true);
	if (GetViewById(IDC_SLIDE, d->Slider))
		d->Slider->SetRange(LRange(500, 1500));

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
	return d->Mem;
}

LAutoPtr<LMemDC> PrintPreview::ReleaseImage()
{
	return d->Mem;
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

int PrintPreview::OnNotify(LViewI *Ctrl, LNotification n)
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
		case IDC_SLIDE:
		{
			if (!d->InUpdate)
			{
				d->InUpdate = true;
				SetCtrlValue(IDC_WIDTH, Ctrl->Value());
				d->InUpdate = false;
			}
			break;
		}
		case IDC_WIDTH:
		{
			if (!d->InUpdate)
			{			
				d->InUpdate = true;
				SetCtrlValue(IDC_SLIDE, Ctrl->Value());
				d->InUpdate = false;
			}
			break;
		}
		case IDC_UPDATE:
		{
			d->Zoom->SetSurface(NULL, false);
			d->Mem.Reset();
			
			SetCtrlName(IDC_STATUS, LLoadString(IDS_LOADING));
			d->Render();
			SetCtrlName(IDC_STATUS, LLoadString(IDS_PREVIEW));
			break;
		}
		case IDC_SAVE_IMG:
		{
			if (d->Mem)
			{
				auto s = new LFileSelect(this);
				s->Type("JPEG", "*.jpg");
				
				char p[MAX_PATH_LEN];
				LGetSystemPath(LSP_USER_DOWNLOADS, p, sizeof(p));
				LMakePath(p, sizeof(p), p, "print-preview.jpg");
				s->Name(p);
				
				s->Save([&](auto dlg, auto status)
				{
					if (status)
						GdcD->Save(s->Name(), d->Mem);
					delete dlg;
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
				d->Render();
				SetCtrlEnabled(IDOK, true);
				SetCtrlName(IDC_STATUS, LLoadString(IDS_PREVIEW));
			}
			break;			
		}
	}

	return 0;
}
