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

	void OnCreate() override;
	void OnPosChange() override;

public:
	LSearchView(ScribeWnd *app);

	const char *GetClass() override { return "LSearchView"; }
	void OnPaint(LSurface *pDC) override;
	int OnNotify(LViewI *c, const LNotification &n) override;
	bool TestThing(Thing *Thing) override;
	void Focus(bool Foc) override;
	void OnFolder();
};

