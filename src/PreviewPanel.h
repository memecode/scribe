#ifndef __PREVIEW_PANEL_H
#define __PREVIEW_PANEL_H

class LPreviewPanel :
    public LLayout,
	public MailViewOwner
{
	class LPreviewPanelPrivate *d;

    bool NeedsCapability(const char *Name, const char *Param = NULL) override;
    LDocView *GetDoc(const char *MimeType) override;
    bool SetDoc(LDocView *v, const char *MimeType) override;

public:
	LPreviewPanel(ScribeWnd *app);
	~LPreviewPanel();

	void OnThing(Thing *item, bool ChangeEvent);
	Thing *GetCurrent();
	
	void OnPulse() override;
	void OnPaint(LSurface *pDC) override;
	void OnPosChange() override;
	int OnNotify(LViewI *v, const LNotification &n) override;
	void OnInstall(CapsHash *Caps, bool Status) override;
    void OnCloseInstaller() override;
    LMessage::Param OnEvent(LMessage *Msg) override;

	bool CallMethod(const char *Name, LScriptArguments &Arg) override;
};

#endif
