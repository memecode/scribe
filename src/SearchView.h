#pragma once

// This is the edit boxes in the list view when filtering items.
class LSearchView : public LLayout, public ThingFilter, public LResourceLoad
{
	ScribeWnd *App;

	bool Unread;
	int LimitField;
	bool AnyColour = false;
	LArray<uint32_t> Colours;
	LString::Array Keywords;

	void OnCreate();
	void OnPosChange();

public:
	LSearchView(ScribeWnd *app);

	const char *GetClass() { return "LSearchView"; }
	void OnPaint(LSurface *pDC);
	int OnNotify(LViewI *c, LNotification &n) override;
	bool TestThing(Thing *Thing);
	void Focus(bool Foc);
	void OnFolder();
};

