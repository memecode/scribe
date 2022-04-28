#ifndef _LMarkColourSelect_h_
#define _LMarkColourSelect_h_

class LMarkColourSelect : public LView, public ResObject
{
	LRect NoneRc, AnyRc;
	LAutoPtr<LDisplayString> None;
	LAutoPtr<LDisplayString> Any;
	int Pad;
	int ColPx;
	LArray<LRect> ColRc;

public:
	bool ColSel[IDM_MARK_MAX];

	LMarkColourSelect();

	// Getters
	LArray<uint32_t> GetSelected();

	// Setters
	void OnPressColour(size_t i);
	void SelectNone();
	void SelectAll();

	// Events
	bool OnLayout(LViewLayoutInfo &Inf);
	void OnMouseClick(LMouse &m);
	void OnPaint(LSurface *pDC);
};

#endif
