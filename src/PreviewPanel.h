#ifndef __PREVIEW_PANEL_H
#define __PREVIEW_PANEL_H

class LPreviewPanel :
    public LLayout,
	public MailViewOwner
{
	class LPreviewPanelPrivate *d;

    bool NeedsCapability(const char *Name, const char *Param = NULL);
    LDocView *GetDoc(const char *MimeType);
    bool SetDoc(LDocView *v, const char *MimeType);

public:
	LPreviewPanel(ScribeWnd *app);
	~LPreviewPanel();

	void OnThing(Thing *item, bool ChangeEvent);
	Thing *GetCurrent();
	
	void OnPulse();
	void OnPaint(LSurface *pDC);
	void OnPosChange();
	int OnNotify(LViewI *v, LNotification &n) override;
	void OnInstall(CapsHash *Caps, bool Status);
    void OnCloseInstaller();
    LMessage::Param OnEvent(LMessage *Msg);

	bool CallMethod(const char *Name, LScriptArguments &Arg);
};

#endif
