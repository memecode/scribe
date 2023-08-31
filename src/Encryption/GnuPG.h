#ifndef _SCRIBE_GNUPH_H_
#define _SCRIBE_GNUPH_H_

enum GpgFlags
{
	GPG_HAS_PRIV_KEY = 0x1,
};

struct GpgSigCheckResponse
{
	bool SignatureMatch = false;
	LString Error;
	LString Identity;
	LDateTime TimeStamp;
	LMessage::Param UserValue = 0;
};

struct GpgDecryptResponse
{
	LString Error;
	LAutoStreamI Data;
	LMessage::Param UserValue = 0;
};

class GpgConnector
{
	struct GpgConnectorPriv *d;

public:
	static bool IsInstalled();

	GpgConnector();
	~GpgConnector();

	struct KeyInfo
	{
		// One or more of GpgFlags
		int Flags;
		LString KeyId;
		LString Name;
		LString Email;
	};

	/// This will respond by sending the target a M_GNUPG_KEY_INFO message
	bool GetKeyInfo(LViewI *Target, LString::Array &Emails);

	/// This will respond by sending the target a M_GNUPG_SIG_CHECK message	
	bool CheckSignature(LViewI *Target, LAutoStreamI Rfc822Msg, LMessage::Param UserVal);
	
	/// This will respond by sending the target a M_GNUPG_DECRYPT message
	bool Decrypt(LViewI *Target, LAutoStreamI Data, LString Password, LMessage::Param UserVal);
};

class MailUiGpg : public LView
{
	struct MailUiGpgPriv *d;

	bool ReadFile(LArray<char> &Data, const char *Path);
	
public:
	MailUiGpg(ScribeWnd *App, MailUi *Ui, int ColX1, int ColX2, bool WritingEmail);
	~MailUiGpg();
	
	const char *GetClass() { return "MailUiGpg"; }

	// Actions
	void SignEncrypt(bool uSign, bool uEncrypt, bool uAttachPublicKey, std::function<void(int)> callback);
	void Decrypt(std::function<void(int)> callback);
	LString GetPublicKey(const char *Email);
	
	// Mail Events
	void OnRecipientChange();
	
	// View Events
	bool Pour(LRegion &r);
	void OnPaint(LSurface *pDC);
	void OnCreate();
	int OnNotify(LViewI *Ctrl, LNotification n);
	void DoCommand(int Cmd, std::function<void(int)> callback);
	LMessage::Result OnEvent(LMessage *Msg);

	int OnCommand(int Cmd, int Event, OsView Wnd)
	{
		LAssert(!"Call DoCommand...");
		return 0;
	}
};

#endif