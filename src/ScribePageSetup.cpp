#include <stdio.h>
#include <stdlib.h>

#include "Scribe.h"
#include "ScribePageSetup.h"
#include "resdefs.h"

////////////////////////////////////////////////////////////////
class BorderEdit : public LLayout, public ResObject
{
	friend class ScribePageSetup;

	// These are all in centimeters
	double x1, y1, x2, y2;
	double PageX = PageDefaultX; // A4 is the default
	double PageY = PageDefaultY;

	double Scale = 1.0;

public:
	BorderEdit() : ResObject(Res_Custom)
	{
		x1 = y1 = x2 = y2 = 1.0;
		Sunken(true);
	}

	void SetBorders(double X1, double Y1, double X2, double Y2)
	{
		x1 = limit(X1, 0.0, PageX/2);
		y1 = limit(Y1, 0.0, PageY/2);
		x2 = limit(X2, 0.0, PageX/2);
		y2 = limit(Y2, 0.0, PageY/2);
		Invalidate();
	}

	void OnPaint(LSurface *pDC)
	{
		// calculate scaling
		auto client = GetClient();
		auto TargetY = (double)client.Y() * 0.8;
		Scale = PageY > 0 ? TargetY / PageY : 1.0;

		// paint background
		pDC->Colour(L_LOW);
		pDC->Rectangle();

		// paint doc...
		int Cx = (int) (X()/2);
		int Cy = (int) (Y()/2);
		int x = (int) (Scale * PageX);
		int y = (int) (Scale * PageY);
		LRect r(Cx - (x/2), Cy - (y/2), Cx + (x/2), Cy + (y/2));

		pDC->Colour(L_BLACK);
		pDC->Box(&r);
		r.Inset(1, 1);

		pDC->Colour(L_WHITE);
		pDC->Rectangle(&r);

		// paint borders..
		pDC->Colour(L_LTGREY);
		int n = (int) (r.x1 + (Scale * x1));	// left
		pDC->Line(n, r.y1, n, r.y2);
		n = (int) (r.y1 + (Scale * y1));		// top
		pDC->Line(r.x1, n, r.x2, n);
		n = (int) (r.x2 - (Scale * x2));		// right
		pDC->Line(n, r.y1, n, r.y2);
		n = (int) (r.y2 - (Scale * y2));		// bottom
		pDC->Line(r.x1, n, r.x2, n);

		// paint dummy lines
		r.x1 += (int) (Scale * x1);
		r.y1 += (int) (Scale * y1);
		r.x2 -= (int) (Scale * x2);
		r.y2 -= (int) (Scale * y2);
		r.Inset(2, 2);

		for (int i=r.y1; i<r.y1 + (r.Y()/2); i+=5)
		{
			pDC->Rectangle(r.x1, i, (i%4==3) ? r.x1 + (r.X()*2/3) : r.x2, i+2);
		}
	}
	
	bool OnLayout(LViewLayoutInfo &Inf)
	{
	    if (!Inf.Width.Max)
	    {
	        Inf.Width.Max = -1;
	        Inf.Width.Min = 100;
	    }
	    else if (!Inf.Height.Max)
	    {
	        Inf.Height.Max = -1;
	        Inf.Height.Min = 100;
	    }
	    else return false;
	    
	    return true;
	}
};

class BorderEditFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
	    if (!_stricmp(Class, "BorderEdit"))
	        return new BorderEdit;
	    return NULL;
	}

public:
} BorderEditFact;

////////////////////////////////////////////////////////////////
ScribePageSetup::ScribePageSetup(LView *parent, LOptionsFile *options)
{
	Options = options;
	SetParent(parent);

	if (LoadFromResource(IDD_PAGE_SETUP))
	{
		MoveToCenter();
		SetCtrlEnabled(IDC_FONT, false);
		Serialize(false);

		// update the ctrl
		LViewI *w = FindControl(IDC_LEFT);
		LNotification note(LNotifyValueChanged);
		if (w) OnNotify(w, note);
	}
}

void ScribePageSetup::Serialize(bool Write)
{
	if (Options)
	{
		Font.Serialize(Options, OPT_PrintFont, Write);

		if (Write)
		{
			#define SaveBorder(ctrl, opt) \
			{	LVariant s; \
				s = GetCtrlName(ctrl); \
				if (s.Str()) \
					Options->SetValue(opt, s); \
			}

			SaveBorder(IDC_LEFT, OPT_MarginX1);
			SaveBorder(IDC_TOP, OPT_MarginY1);
			SaveBorder(IDC_RIGHT, OPT_MarginX2);
			SaveBorder(IDC_BOTTOM, OPT_MarginY2);
		}
		else
		{
			char s[128];
			#define LoadBorder(ctrl, opt) \
				{	LVariant d; \
					if (Options->GetValue(opt, d)) \
				{	sprintf_s(s, sizeof(s), "%.2f", d.CastDouble()); \
					SetCtrlName(ctrl, s); } \
					else SetCtrlName(ctrl, "1.00"); }

			LoadBorder(IDC_LEFT, OPT_MarginX1);
			LoadBorder(IDC_TOP, OPT_MarginY1);
			LoadBorder(IDC_RIGHT, OPT_MarginX2);
			LoadBorder(IDC_BOTTOM, OPT_MarginY2);

			if (Font.GetDescription(s, sizeof(s)))
			{
				SetCtrlName(IDC_FONT, s);
			}
		}
	}
}

int ScribePageSetup::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDC_LEFT:
		case IDC_TOP:
		case IDC_RIGHT:
		case IDC_BOTTOM:
		{
			BorderEdit *Ctrl;
			if (GetViewById(IDC_PREVIEW, Ctrl))
			{
				auto x1 = GetCtrlName(IDC_LEFT);
				auto y1 = GetCtrlName(IDC_TOP);
				auto x2 = GetCtrlName(IDC_RIGHT);
				auto y2 = GetCtrlName(IDC_BOTTOM);
				if (x1 && x2 && y1 && y2)
				{
					Ctrl->SetBorders(atof(x1), atof(y1), atof(x2), atof(y2));
				}
			}
			break;
		}
		case IDC_BROWSE_FONT:
		{
			Font.DoUI(this, [this](auto fontType)
			{
				char s[256];				
				Font.GetDescription(s, sizeof(s));
				SetCtrlName(IDC_FONT, s);
			});
			break;
		}
		case IDOK:
		{
			Serialize(true);
			// fall thru
		}
		case IDCANCEL:
		{
			EndModal(Ctrl->GetId());
			break;
		}
	}
	return 0;
}



