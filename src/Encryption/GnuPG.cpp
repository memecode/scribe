/*

https://lists.gnupg.org/pipermail/gnupg-users/2011-November/043223.html
gpg --list-packets ?

Mime hierarchy:

	Signed:
	
		multipart/signed							"This is an OpenPGP/MIME signed message (RFC 4880 and 3156)"
			{
				multipart/mixed
					text/plain						Body of the message
			}
			application/pgp-signature				"-----BEGIN PGP SIGNATURE-----"
	
	Signed, key attached:
	
		multipart/signed							"This is an OpenPGP/MIME signed message (RFC 4880 and 3156)"
			{
				multipart/mixed
					text/plain						Body of the message
					application/pgp-keys			"-----BEGIN PGP PUBLIC KEY BLOCK-----"
			}
			application/pgp-signature				"-----BEGIN PGP SIGNATURE-----"
	
	Signed, HTML, attachment, key:
		
		multipart/signed
			{
				multipart/mixed
					multipart/mixed
						multipart/alternative
							text/plain
							text/html
						application/octet-stream
						application/pgp-keys
			}
			application/pgp-signature
	
	Encrypted:
	
		multipart/encrypted
			application/pgp-encrypted
			application/octet-stream
			{
				multipart/mixed
					multipart/mixed
						text/plain
			}
	
	Encrypted + key attached:
	
		multipart/encrypted
			application/pgp-encrypted
			application/octet-stream
			{
				multipart/mixed
					multipart/mixed
						text/plain
						application/pgp-keys
			}



*/
#include "lgi/common/Lgi.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Css.h"
#include "lgi/common/ThreadEvent.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/CssTools.h"
#include "lgi/common/EventTargetThread.h"

#include "Scribe.h"
#include "GnuPG.h"
#include "ScribeListAddr.h"
#include "Store3Common.h"
#include "resdefs.h"

//////////////////////////////////////////////////////////////////////////////////////////
enum Ctrls
{
	IDC_SIGN = 700,
	IDC_ENCRYPT,
	IDC_ATTACH_PUB_KEY,
	IDC_DECRYPT,
	IDC_INSTALL
};

static LColour cGood					(0, 180, 0);
static LColour cWarn					(255, 154, 0);
static LColour cError					(255, 0, 0);
static LColour cTxt						(L_TEXT);
#define cDefaultListItemColour			0
#define PANEL_BORDER_PX					2

static const char *GpgInstall =			"https://www.gnupg.org/download/index.en.html";
static const char *GpgBin =				"gpg" LGI_EXECUTABLE_EXT;
struct OutFileNames {
	const char *in;
	const char *out;
} 	GpgOut = { "gpg-out.txt", "gpg-out.gpg" };
struct CheckFileNames {
	const char *msg;
	const char *sig;
}	GpgIn = { "gpg-msg.gpg", "gpg-sig.txt" };
static LString GpgBinPath;

// These are all in milliseconds
#define SECONDS(s)						((s) * 1000)
#define MINUTES(m)						((m) * SECONDS(60))
#define GPG_KEY_STALE_TIMEOUT			MINUTES(10)

#define DecryptStatus(val) \
	{ \
		if (callback) callback(val); \
		return; \
	}

typedef LArray<GpgConnector::KeyInfo>	KeyArr;
typedef LAutoPtr<KeyArr>				KeyArrAuto;

struct GpgJob
{
	enum JobType
	{
		JobGetKeys = M_USER,
		JobCheckSig,
		JobDecrypt,
	};	

	// Dispatch handle of view
	int OwnerId = 0;
	
	// Get keys:
	LAutoPtr<LString::Array> Emails;

	// Check sig:
	LAutoStreamI Msg;
	LMessage::Param UserValue;
};

struct LTempFile : public LFile
{
	LString Path;
	
	LTempFile(const char *path)
	{
		if (Open(path, O_READ))
		{
			Path = path;
		}
	}
	
	~LTempFile()
	{
		if (Path)
		{
			Close();
			FileDev->Delete(Path, NULL, false);
		}
	}
};

class GpgConnectorPriv : public LEventTargetThread
{
private:
	KeyArr Keys;
	uint64_t KeysTs = 0;

public:	
	GpgConnectorPriv() : LEventTargetThread("GpgConnectorPriv")
	{
	}
	
private:
	LString RunGpg(const char *Args)
	{
		LString Output;
		LSubProcess p(GpgBinPath, Args);
		if (p.Start(true, false))
		{
			char Buf[256];
			while (true)
			{
				ssize_t r = p.Read(&Buf, sizeof(Buf)-1);
				if (r > 0)
				{
					Buf[r] = 0;
					Output += Buf;
				}
				else break;
			}
		}
		
		return Output;
	}
	
	void ParseKeys(KeyArr &k, LString &str)
	{
		bool GotDash = false;
		auto lines = str.Split(EOL_SEQUENCE);
		LString KeyId, Type;
		for (unsigned i=0; i<lines.Length(); i++)
		{
			auto &ln = lines[i];
			auto *next = i < lines.Length() - 1 ? &lines[i+1] : nullptr;

			if (ln.Find("-----") >= 0)
			{
				GotDash = true;
			}
			else if (GotDash)
			{
				auto parts = ln.SplitDelimit(" \t/");
				if (parts.Length() > 0)
				{
					if (parts[0].Equals("sec") ||
						parts[0].Equals("pub"))
					{
						Type = parts[1];
						if (parts.Length() == 4)
							KeyId = parts[2];
						else if (next && (*next)(0) == ' ')
							KeyId = next->Strip();							
					}
					else if (parts[0].Equals("uid"))
					{
						LArray<char*> vars;
						for (auto &p: parts)
						{
							if (p(0) == '[')
								vars.Add(p);
						}

						LString s;
						if (vars.Length())
						{
							auto last = vars.Last();
							auto pos = ln.Find(last);
							LAssert(pos > 0);
							s = ln(pos + Strlen(last), -1).Strip();
						}
						else
						{
							s = ln(4, -1).Strip();
						}

						LAutoString Name, Addr;
						DecodeAddrName(s, Name, Addr, NULL);
						if (ValidStr(Name) && ValidStr(Addr))
						{
							GpgConnector::KeyInfo &info = k.New();
							info.keyId = KeyId;
							info.name = Name;
							info.email = Addr;
							info.type = Type;
						}
						else LgiTrace("%s:%i - Error parsing '%s'\n", _FL, s.Get());
					}
				}
			}
		}
	}

	bool GetKeys()
	{
		auto Now = LCurrentTime();
		if (Now - KeysTs > GPG_KEY_STALE_TIMEOUT)
		{
			KeysTs = Now;

			auto s = RunGpg("-k");
			if (s)
				ParseKeys(Keys, s);
			
			s = RunGpg("-K");
			if (s)
			{
				KeyArr Priv;
				ParseKeys(Priv, s);

				LHashTbl<ConstStrKey<char>, GpgConnector::KeyInfo*> Hash;
				for (auto &k: Keys)
				{					
					if (k.email)
						Hash.Add(k.email, &k);
					else
						LgiTrace("%s:%i - No email for key?\n", _FL);
				}

				for (unsigned i=0; i<Priv.Length(); i++)
				{
					GpgConnector::KeyInfo *k = Hash.Find(Priv[i].email);
					if (k)
						k->flags |= GPG_HAS_PRIV_KEY;
				}
			}
		}

		return Keys.Length() > 0;
	}
	
	bool Save(const char *Path, LString s)
	{
		LFile f;
		if (!f.Open(Path, O_WRITE))
			return false;
		f.SetSize(0);
		return f.Write(s, s.Length()) == s.Length();
	}

	bool Save(const char *Path, LStreamI *s)
	{
		LFile f;
		if (!f.Open(Path, O_WRITE))
			return false;
		f.SetSize(0);
		LCopyStreamer Cp;
		int64 SrcSz = s->GetSize();
		int64 Copied = Cp.Copy(s, &f);
		return SrcSz == Copied;
	}
	
	void CheckSignature(int OwnerId, LAutoStreamI UserMsg, LMessage::Param UserVal)
	{
		LString Msg;
		int64 Sz = 0;
		int64 Rd = 0;
		ptrdiff_t HdrSize = 0;
		LString Start;
		LArray<LRange> Segs;
		LMime Tmp;
		LString Hdrs;
		LString Boundary;
		LAutoPtr<GpgSigCheckResponse> Resp(new GpgSigCheckResponse);
		
		if (!Resp)
			return; // Not much else we can do.
		Resp->UserValue = UserVal;
	
		if (!UserMsg)
		{
			Resp->Error.Printf(LLoadString(IDS_GNUPG_ERR_NOMSG));
			goto OnSigCheckError;
		}
		
		// We have to parse out just the part of the email that is signed. And we
		// can't use the LMime class because that would decode it too much and lose
		// the exact formatting. Read the whole thing into memory and find the MIME
		// boundary.
		Sz = UserMsg->GetSize();
		UserMsg->SetPos(0);
		if (!Msg.Length((int)Sz))
		{
			Resp->Error.Printf("Can't size string to " LPrintfInt64, Sz);
			goto OnSigCheckError;
		}
		
		for (int64 i=0; i<Sz;)
		{
			Rd = UserMsg->Read(Msg.Get() + i, Msg.Length() - i);
			if (Rd <= 0)
			{
				Resp->Error.Printf("Read error: i=%i, Rd=%i, Sz=" LPrintfInt64, i, Rd, Sz);
				goto OnSigCheckError;
			}
			
			i += Rd;
		}
		
		HdrSize = Msg.Find("\r\n\r\n");
		Hdrs = Msg(0, HdrSize);
		Tmp.SetHeaders(Hdrs);
		Boundary = Tmp.LGetBoundary();
		if (!Boundary)
		{
			Resp->Error = LLoadString(IDS_GNUPG_ERR_NO_BOUNDARY);
			goto OnSigCheckError;
		}
		
		// Now look through the message and find all the segments...
		Start.Printf("\r\n--%s", Boundary.Get());
		for (ptrdiff_t i = 0; i < (ptrdiff_t)Msg.Length(); )
		{
			ptrdiff_t Next = Msg.Find(Start, i);
			if (Next > 0)
			{
				// Is it an end or starting boundary?
				ptrdiff_t k = Next + Start.Length();
				LString Chars = Msg(k, k + 2);
				bool IsEnd = Chars == "--";
				if (IsEnd)
				{
					if (Segs.Length() == 0)
					{
						Resp->Error = LLoadString(IDS_GNUPG_ERR_NO_MIME);
						goto OnSigCheckError;
					}
					
					Segs.Last().Len = Next - Segs.Last().Start;
					
					i = k + 2;
				}
				else
				{
					if (Segs.Length() > 0)
					{
						// Finish last segment
						Segs.Last().Len = Next - Segs.Last().Start;
					}
					
					char *m = Msg;
					while (IsWhite(m[k]))
						k++;
					LRange &r = Segs.New();
					r.Start = k;
					r.Len = 0;
					
					i = k;
				}
			}
			else break;
		}
		
		if (Segs.Length() == 2)
		{
			// char *m = Msg;
			LRange Body, Sig;
			
			// Classify segments by content
			for (auto &range: Segs)
			{
				LString s = Msg(range.Start, range.End());
				auto ContentType = LGetHeaderField(s, "Content-Type");
				if (ContentType.Find(sApplicationPgpSignature) >= 0)
				{
					Sig = range;
				}
				else
				{
					Body = range;
				}
			}
			
			// Get the 2 parts from the whole message..
			LString SignedText = Msg(Body.Start, Body.Start + Body.Len);
			LString Signature = Msg(Sig.Start, Sig.Start + Sig.Len);
			ptrdiff_t Break = Signature.Find("\r\n\r\n");
			if (Break > 0)
				Signature = Signature(Break + 4, -1);
			
			// Save them to files:
			LFile::Path TextPath = ScribeTempPath(), SigPath = ScribeTempPath();
			TextPath += GpgIn.msg;
			SigPath  += GpgIn.sig;
			if (!Save(TextPath, SignedText.RStrip()) ||
				!Save(SigPath, Signature.RStrip()))
			{
				Resp->Error = LLoadString(IDS_GNUPG_ERR_SAVE_FAILED);
				goto OnSigCheckError;
			}
			
			// Now call GnuPG to verify the signature...
			LString Args;
			Args.Printf("--verify \"%s\" \"%s\"", SigPath.GetFull().Get(), TextPath.GetFull().Get());
			LString Output = RunGpg(Args);
			if (Output)
			{
				LString::Array a = Output.SplitDelimit("\n");
				for (unsigned i=0; i<a.Length(); i++)
				{
					LString &Line = a[i];
					if (Line.Lower().Find("signature from") >= 0)
					{
						LString::Array w = Line.SplitDelimit(" ");
						
						// Check if the signature is good...
						if (w.Length() > 2)
							Resp->SignatureMatch = w[1].Lower() == "good";
						
						// Get identity... 
						w = Line.SplitDelimit("\"");
						if (w.Length() >= 2 &&
							w[1].Find("@") >= 0)
						{
							Resp->Identity = w[1];
						}
					}
					else if (Line.Lower().Find("signature made") >= 0)
					{
						// Get date...
						LString::Array w = Line.SplitDelimit(" ");
						int MadeIdx = -1;
						LString::Array DateParts;
						for (unsigned i=0; i<w.Length(); i++)
						{
							if (w[i].Find("/") > 0)
								Resp->TimeStamp.SetDate(w[i]);
							else if (w[i].Find(":") > 0)
							{
								Resp->TimeStamp.SetTime(w[i]);
								MadeIdx = -1;
							}
							else if (w[i].Lower() == "made")
								MadeIdx = i;
							else if (MadeIdx >= 0 && (int)i > MadeIdx)
								DateParts.New() = w[i];
						}
						
						if (Resp->TimeStamp.Year() == 0 &&
							DateParts.Length() > 0)
						{
							LString s = LString(" ").Join(DateParts);
							Resp->TimeStamp.SetDate(s);
						}
					}
				}
			}
			else
			{
				Resp->Error = LLoadString(IDS_GNUPG_ERR_NO_OUTPUT);
			}

			#ifdef _DEBUG
				LgiTrace("%s:%i check sig files: txt='%s' sig='%s'\n",
					_FL,
					TextPath.GetFull().Get(),
					SigPath.GetFull().Get());
			#else
				// Clean up temporary files...
				FileDev->Delete(TextPath, NULL, false);
				FileDev->Delete(SigPath, NULL, false);
			#endif
		}
		else
		{
			Resp->Error.Printf(LLoadString(IDS_GNUPG_ERR_WRONG_SEGS), Segs.Length());
		}
		
	OnSigCheckError:
		PostObject(OwnerId, M_GNUPG_SIG_CHECK, Resp);
	}

	void Decrypt(GpgJob *j)
	{
		LAutoPtr<GpgDecryptResponse> Resp(new GpgDecryptResponse);
		if (!Resp || !j)
			return;
		
		Resp->UserValue = j->UserValue;
		
		LFile::Path InPath = ScribeTempPath();
		InPath += "encrypted.txt";

		LFile::Path OutPath = ScribeTempPath();
		OutPath += "decrypted.txt";

		if (!Save(InPath, j->Msg))
		{
			Resp->Error = "Failed to save file for decrypting.";
		}
		else
		{
			LString Args;
			Args.Printf("--batch --output \"%s\" --decrypt \"%s\"",
						OutPath.GetFull().Get(),
						InPath.GetFull().Get());
			LSubProcess Proc(GpgBinPath, Args);
			if (!Proc.Start(true, true))
			{
				Resp->Error = "Can't start the GnuPG sub-process.";
			}
			else
			{
				char Buf[256];
				LVariant v;
				if (Proc.GetValue(LDomPropToString(StreamReadable), v) &&
					v.CastInt32() != 0)
				{
					ssize_t r = Proc.Read(Buf, sizeof(Buf)-1);
					Buf[MAX(r, 0)] = 0;
				}

				ssize_t r = Proc.Read(Buf, sizeof(Buf)-1);
				Buf[MAX(r, 0)] = 0;
				int Result = Proc.Wait();
				LFile *f;
				if (Result)
				{
					if (Buf[0])
						Resp->Error = Buf[0];
					else
						Resp->Error.Printf("GnuPG encryption process failed with code: %i", Result);
				}
				else if
				(
					!Resp->Data.Reset(f = new LTempFile(OutPath)) ||
					!f->IsOpen())
				{
					Resp->Data.Reset();
					Resp->Error.Printf("Decrypt failed: Can't open '%s' for reading.", OutPath.GetFull().Get());
				}
				else
				{
					// Success?
				}
			}
		}
		
		// Clean up input file (encrypted)
		// The output file will be deleted by the M_GNUPG_DECRYPT handler.
		if (LFileExists(InPath))
		{
			FileDev->Delete(InPath, NULL, false);
		}
		
		PostObject(j->OwnerId, M_GNUPG_DECRYPT, Resp);
	}
	
	LMessage::Result OnEvent(LMessage *Msg) override
	{
		switch (Msg->Msg())
		{
			case GpgJob::JobGetKeys:
			{
				auto j = Msg->AutoA<GpgJob>();

				if (Keys.Length() == 0)
					GetKeys();
				
				LHashTbl<ConstStrKey<char,false>,bool> Map;
				for (unsigned i=0; i<j->Emails->Length(); i++)
				{
					Map.Add((*j->Emails)[i], true);
				}
				
				KeyArrAuto Inf(new KeyArr);
				for (auto &k: Keys)
				{
					if (!Map.Find(k.email))
						continue;

					// Make an explicit copy here, because we are passing the data back to the
					// calling thread, and LString's aren't thread safe.					
					Inf->New().Copy(k);
				}
						
				PostObject(j->OwnerId, M_GNUPG_KEY_INFO, Inf);
				break;
			}
			case GpgJob::JobCheckSig:
			{
				auto j = Msg->AutoA<GpgJob>();
				CheckSignature(j->OwnerId, j->Msg, j->UserValue);
				break;
			}
			case GpgJob::JobDecrypt:
			{
				auto j = Msg->AutoA<GpgJob>();
				Decrypt(j);
				break;
			}
			default:
			{
				LAssert(!"Invalid type.");
				break;
			}
		}
		
		return 0;
	}
};

bool GpgConnector::IsInstalled()
{
	#ifdef WINNATIVE
		char *Str = NULL;
		errno_t Err = _dupenv_s(&Str, NULL, "PATH");
		if (Err)
		{
			LgiTrace("%s:%i - _dupenv_s failed with %i\n", _FL, Err);
			return false;
		}
		LString Path = Str;
		free(Str);
	#else
		LString Path = getenv("PATH");
		#ifdef MAC
			Path += LGI_PATH_SEPARATOR"/opt/local/bin";
		#endif
	#endif
	LString::Array Parts = Path.Split(LGI_PATH_SEPARATOR);
	for (unsigned i = 0; i < Parts.Length(); i++)
	{
		LFile::Path p(Parts[i]);

		p += GpgBin;

		if (p.IsFile())
		{
			GpgBinPath = p;
			return true;
		}
	}

	return false;
}

GpgConnector::GpgConnector()
{
	d = new GpgConnectorPriv;
}

GpgConnector::~GpgConnector()
{
	delete d;
}

bool GpgConnector::GetKeyInfo(LViewI *Target, LString::Array &Emails)
{
	if (!Target)
	{
		LAssert(!"Invalid target.");
		return false;
	}

	LAutoPtr<GpgJob> j(new GpgJob);
	if (!j)
		return false;

	j->Emails.Reset(new LString::Array(Emails));
	j->OwnerId = Target->AddDispatch();
		
	return d->PostObject(d->GetHandle(), GpgJob::JobGetKeys, j);
}

bool GpgConnector::CheckSignature(LViewI *Target, LAutoStreamI Rfc822Msg, LMessage::Param UserVal)
{
	if (!Target)
	{
		LAssert(!"Invalid target.");
		return false;
	}

	LAutoPtr<GpgJob> j(new GpgJob);
	if (!j)
		return false;

	j->OwnerId = Target->AddDispatch();
	j->Msg = Rfc822Msg;
	j->UserValue = UserVal;

	return d->PostObject(d->GetHandle(), GpgJob::JobCheckSig, j);
}

bool GpgConnector::Decrypt(LViewI *Target, LAutoStreamI Data, LMessage::Param UserVal)
{
	if (!Target)
	{
		LAssert(!"Invalid target.");
		return false;
	}

	LAutoPtr<GpgJob> j(new GpgJob);
	if (!j)
		return false;

	j->OwnerId = Target->AddDispatch();
	j->Msg = Data;
	j->UserValue = UserVal;

	return d->PostObject(d->GetHandle(), GpgJob::JobDecrypt, j);
}

////////////////////////////////////////////////////////////////////////////////////////////////
struct MailUiGpgPriv
{
	struct UserPassword
	{
		uint64 Ts;
		LString Email;
		LString Password;
	};

	// Objs
	ScribeWnd *App = nullptr;
	MailUi *Ui = nullptr;
	
	// UI
	LCheckBox *Enc = nullptr;
	LCheckBox *Sign = nullptr;
	LCheckBox *Attach = nullptr;
	LTextLabel *Msg = nullptr;
	KeyArrAuto Inf;
	LTableLayout *Table = nullptr;
	LButton *Decrypt = nullptr;
	LButton *Install = nullptr;
	
	bool willEncrypt() { return Enc ? Enc->Value() != 0 : false; }
	bool willSign() { return Sign ? Sign->Value() != 0 : false; }

	// Options
	bool WritingEmail = false;
	bool Encrypted = false;
	bool Signed = false;
	
	// Passwords
	LArray<UserPassword> Psw;
	
	MailUiGpgPriv(ScribeWnd *app, MailUi *ui, bool writingEmail)
	{
		App = app;
		Ui = ui;
		WritingEmail = writingEmail;
	}

	AddressList *GetAddrLst()
	{		
		AddressList *AddrLst = NULL;
		Ui->GetViewById(IDC_TO, AddrLst);
		return AddrLst;
	}

	void NotInstalled()
	{
		SetError(LLoadString(IDS_GNUPG_ERR_NOT_INSTALLED));
		if (Install)
			Install->Visible(true);
		if (Enc)
			Enc->Enabled(false);
		if (Sign)
			Sign->Enabled(false);
		if (Attach)
			Attach->Enabled(false);
	}
	
	void Set(const char *Str, LColour &Col)
	{
		if (!Msg || !Table)
		{
			LgiTrace("%s:%i - Can't set message.\n", _FL);
			return;
		}
		Msg->GetCss(true)->Color(Col);
		Msg->Name(Str);

		LNotification note(LNotifyTableLayoutRefresh);
		Table->OnNotify(Msg, note);
	}

	void SetError(const char *Str)   { Set(Str, cError); }
	void SetWarning(const char *Str) { Set(Str, cWarn);  }
	void SetStatus(const char *Str)  { Set(Str, cTxt);   }
	void SetSuccess(const char *Str) { Set(Str, cGood);  }
	
	void GetPassword(LViewI *Parent, LString Addr, std::function<void(LString)> Callback)
	{
		for (unsigned i=0; i<Psw.Length(); i++)
		{
			UserPassword &p = Psw[i];
			if (p.Email == Addr)
			{
				if (Callback)
					Callback(p.Password);
				return;
			}
		}
	
		LString Msg;
		Msg.Printf(LLoadString(IDS_GNUPG_PSW_PROMPT), Addr.Get());
		
		// auto p = Parent->GetLView();
		auto Dlg = new LInput(Parent, "", Msg, LLoadString(IDC_PASSWORD), true);
		Dlg->DoModal([this, Dlg, Addr, Callback](auto dlg, auto ok)
		{
			if (ok)
			{
				UserPassword &p = Psw.New();
				p.Email = Addr;
				p.Password = Dlg->GetStr();
				p.Ts = LCurrentTime();
			
				if (Callback)
					Callback(p.Password);
			}
		});
	}

	Store3Addr GetFrom()
	{
		auto m = Ui->GetItem();
		auto obj = m ? m->GetObject() : nullptr;
		auto store = obj ? obj->GetStore() : nullptr;
		Store3Addr addr(store);

		LCombo *cbo;
		if (Ui && Ui->GetViewById(IDC_FROM, cbo))
			DecodeAddrName(cbo->Name(), addr.Name, addr.Addr, nullptr);

		return addr;
	}
};
	
static LCss::Len Px(int px)
{
	return LCss::Len(LCss::LenPx, (float)px);
}
	
MailUiGpg::MailUiGpg(ScribeWnd *App, MailUi *Ui, const char *ColX1, const char *ColX2, bool WritingEmail)
{
	d = new MailUiGpgPriv(App, Ui, WritingEmail);
	int TextY = 6;
	#ifdef MAC
	int ChkY = 1;
	#else
	int ChkY = 6;
	#endif
	
	if (AddView(d->Table = new LTableLayout(50)))
	{
		auto m = Ui->GetItem();
		LDataI *obj = m ? m->GetObject() : nullptr;

		d->Table->GetCss(true)->MarginLeft(ColX1);
		d->Table->GetCss(true)->MarginTop("0.4em");

		int x = 0;
		LTextLabel *Txt;
		auto c = d->Table->GetCell(x++, 0);
		c->Add(Txt = new LTextLabel(IDC_STATIC, 0, 0, -1, -1, "GnuPG:"));
		c->Width(ColX2);
		
		if (WritingEmail)
		{
			c = d->Table->GetCell(x++, 0);
			c->Add(d->Enc = new LCheckBox(IDC_ENCRYPT, LLoadString(IDS_GNUPG_ENCRYPT)));
			
			c = d->Table->GetCell(x++, 0);
			c->Add(d->Sign = new LCheckBox(IDC_SIGN, LLoadString(IDS_GNUPG_SIGN)));
			
			c = d->Table->GetCell(x++, 0);
			c->Add(d->Attach = new LCheckBox(IDC_ATTACH_PUB_KEY, LLoadString(IDS_GNUPG_ATTACH_PUB_KEY)));
		}
		else
		{
			c = d->Table->GetCell(x++, 0);
			c->Add(d->Decrypt = new LButton(IDC_DECRYPT, 0, 0, -1, -1, LLoadString(IDS_GNUPG_DECRYPT)));
			
			auto seg = obj ? obj->GetObj(FIELD_MIME_SEG) : NULL;
			if (auto mimeType = seg ? seg->GetStr(FIELD_MIME_TYPE) : nullptr)
			{
				if (!Stricmp(mimeType, sMultipartMixed))
				{
					// Check if it has a signing attachment
					LArray<LDataI*> files;
					if (m->GetAttachmentObjs(files))
					{
						for (auto f: files)
						{
							auto mt = f->GetStr(FIELD_MIME_TYPE);
							if (!Stricmp(mt, sApplicationPgpSignature))
								d->Signed = true;
						}
					}
				}
				else
				{
					d->Signed = !_stricmp(mimeType, sMultipartSigned);
					d->Encrypted = !_stricmp(mimeType, sMultipartEncrypted);
				}
			}

			if (d->Decrypt)
				d->Decrypt->Enabled(d->Encrypted);
		}

		c = d->Table->GetCell(x++, 0);
		c->PaddingLeft(LCss::Len(LCss::LenEm, 1.0f));
		c->Add(d->Msg = new LTextLabel(IDC_MSG, 0, 0, 300, -1, "..."));

		c = d->Table->GetCell(x++, 0);
		c->TextAlign(LCss::AlignRight);
		if ((d->Install = new LButton(IDC_INSTALL, 0, 0, -1, -1, LLoadString(IDS_GNUPG_GET_GPG))))
		{
			d->Install->Visible(false);
			c->Add(d->Install);
		}

		if (d->Encrypted)
		{
			d->SetWarning("Message is encrypted.");
		}
		
	}
}

MailUiGpg::~MailUiGpg()
{
	delete d;
	if (GetParent())
	{
		LNotification note(LNotifyItemDelete);
		GetParent()->OnNotify(this, note);
	}
}

void MailUiGpg::OnRecipientChange()
{
	if (!d->WritingEmail || !d->Enc || !d->Sign)
		return; // Don't care...
	
	auto AddrLst = d->GetAddrLst();
	if (!AddrLst)
	{
		d->SetError("No AddressList.");
		return;
	}

	auto Gpg = d->App->GetGpgConnector();
	if (!Gpg)
	{
		d->NotInstalled();
		return;
	}

	List<ListAddr> recipients;
	if (!AddrLst->GetAll(recipients))
	{
		d->SetStatus(LLoadString(IDS_GNUPG_ERR_NO_RECIP));
		return;
	}

	if (d->willEncrypt() || d->willSign())
	{
		d->SetWarning(LLoadString(IDS_GNUPG_CHECKING));
		
		LString::Array Emails;
		if (!d->Sign->Value())
		{
			// Encrypting: Do we have public keys for each of the recipients?
			for (auto i: recipients)
			{
				if (i->sAddr)
					Emails.New() = i->sAddr;
			}
		}
		
		// Encrypting+signing: Do we have a private key for the sender?
		auto from = d->GetFrom();
		if (from.Addr)
			Emails.New() = from.Addr;
		
		// Get the GpgConnector to do the work:
		Gpg->GetKeyInfo(this, Emails);
	}
	else
	{
		d->SetStatus(LLoadString(IDS_GNUPG_ERR_NO_SIGN_ENC));

		// Revert list items to no colour...
		for (auto i: recipients)
		{
			i->SetInt(FIELD_COLOUR, cDefaultListItemColour);
			i->Update();
		}
	}	
}

// This gets all the events from MailUi::OnNotify as well as any events
// from MailUiGpg's child controls.
// \returns non-zero if further processing in MailUi::OnNotify should be blocked.
int MailUiGpg::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_TO:
		{
			switch (n.Type)
			{
				case LNotifyItemInsert:
				case LNotifyItemDelete:
					// Recipients changed... check for keys...
					OnRecipientChange();
					break;
				default:
					break;
			}
			break;
		}
		case IDC_ENCRYPT:
		case IDC_SIGN:
		{
			OnRecipientChange();
			break;
		}
		case IDC_DECRYPT:
		{
			Decrypt(NULL);
			break;
		}
		case IDC_INSTALL:
		{
			LExecute(GpgInstall);
			break;
		}
	}
	
	return 0;
}

void InternalLogSegments(LStream &log, LDataPropI *seg, unsigned idx, int depth = 1)
{
	auto indent = LString(" ") * (depth * 2);
	auto iter = seg->GetList(FIELD_MIME_SEG);
	auto mt = seg->GetStr(FIELD_MIME_TYPE);
	auto cs = seg->GetStr(FIELD_CHARSET);
	LString headers = seg->GetStr(FIELD_INTERNET_HEADER);
	log.Print("%s[%i]=%p mt=%s cs=%s\n", indent.Get(), idx, seg, mt, cs);
	for (auto ln: headers.SplitDelimit("\n"))
		log.Print("%s        %s\n", indent.Get(), ln.Get());
	for (unsigned i=0; i<iter->Length(); i++)
	{
		auto c = (*iter)[i];
		InternalLogSegments(log, c, i, depth + 1);
	}
}

void LogSegments(LStream &log, const char *desc, LDataPropI *seg)
{
	log.Print("%s:\n", desc);
	InternalLogSegments(log, seg, 0);
	log.Print("\n");
}

void DeleteChildSegments(LStream &log, LDataPropI *d)
{
	auto It = d->GetList(FIELD_MIME_SEG);
	for (unsigned i=0; i<It->Length(); )
	{
		auto c = (*It)[i];
		
		DeleteChildSegments(log, c);
		
		if (auto cdi = dynamic_cast<LDataI*>(c))
		{
			// This deletes the on disk representation.
			LString mt = cdi->GetStr(FIELD_MIME_TYPE);
			auto result = cdi->Delete();
			log.Print("[%i] delete: %p = %i, mt=%s\n", i, cdi, result, mt.Get());

			// This removes the object from the segment tree and 
			// frees the memory
			delete cdi;
		}
		else
		{
			LgiTrace("%s:%i - DeleteChildSegments: Wrong object type.\n", _FL);
			LAssert(0);
			i++; // Skip this? Maybe we should break?
		}
	}
}

LString MailUiGpg::GetPublicKey(const char *Email)
{
	LString s;
	
	if (Email)
	{
		LString Args;
		Args.Printf("-a --export %s", Email);
		
		LSubProcess Proc(GpgBinPath, Args);
		if (!Proc.Start(true, false))
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_CANT_START));
			return 1;
		}
		
		ssize_t r;
		char Buf[256] = "";
		while ((r = Proc.Read(Buf, sizeof(Buf)-1)) > 0)
		{
			Buf[r] = 0;
			s += Buf;
		}

		int Result = Proc.Wait();
		if (Result)
		{
			LString Msg;
			Msg.Printf(LLoadString(IDS_GNUPG_ERR_CODE), Result);
			d->SetError(Buf[0] ? Buf : Msg);
			s.Empty();
		}
	}
	
	return s;
}

bool MailUiGpg::ReadFile(LArray<char> &Data, const char *Path)
{
	LFile f;
	if (!f.Open(Path, O_READ))
	{
		LString s;
		s.Printf(LLoadString(IDS_ERROR_CANT_READ), Path);
		d->SetError(s);
		return false;
	}
	
	if (f.GetSize() < 0)
	{
		LString s;
		s.Printf(LLoadString(IDS_GNUPG_ERR_NO_CONTENT), Path);
		d->SetError(s);
		return false;
	}
	
	if (!Data.Length((uint32_t)f.GetSize()))
	{
		d->SetError("Memory alloc failed.");
		return 1;
	}
	
	if (f.Read(&Data[0], Data.Length()) != Data.Length())
	{
		LString s;
		s.Printf(LLoadString(IDS_GNUPG_ERR_READ_FAIL), Path);
		d->SetError(s);
		return false;
	}
	
	return true;
}

void MailUiGpg::Decrypt(std::function<void(int)> callback)
{
	// Get the connector...
	auto Conn = d->App->GetGpgConnector();
	if (!Conn)
	{
		d->NotInstalled();
		DecryptStatus(1);
	}

	// Find the right attachment...
	auto m = d->Ui->GetItem();
	if (!m)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_MAIL));
		DecryptStatus(1);
	}
	auto Root = m->GetObject()->GetObj(FIELD_MIME_SEG);
	if (!Root)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_ROOT));
		DecryptStatus(1);
	}
	auto Mt = Root->GetStr(FIELD_MIME_TYPE);
	if (!Mt || _stricmp(Mt, sMultipartEncrypted))
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_WRONG_MIME));
		DecryptStatus(1);
	}
	LDataI *EncryptedObj = NULL;
	LArray<LDataI*> Objs;
	if (!m->GetAttachmentObjs(Objs))
		DecryptStatus(1);
		
	for (unsigned i=0; i<Objs.Length(); i++)
	{
		Mt = Objs[i]->GetStr(FIELD_MIME_TYPE);
		if (Mt && !_stricmp(Mt, sAppOctetStream))
		{
			EncryptedObj = Objs[i];
			break;
		}
	}	
	if (!EncryptedObj)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_ENC_MIME));
		DecryptStatus(1);
	}

	// Get the password for decryption...
	LHashTbl<ConstStrKey<char,false>,ScribeAccount*> Map;
	for (auto a : *m->App->GetAccounts())
	{
		LVariant v = a->Identity.Email();
		if (v.Str())
		{
			char *Email = v.Str();
			if (Email)
				Map.Add(Email, a);
		}
	}
	LDataIt ToLst = m->GetTo();
	LString::Array ToEmail;
	if (ToLst)
	{
		for (LDataPropI *t = ToLst->First(); t; t = ToLst->Next())
		{
			auto Email = t->GetStr(FIELD_EMAIL);
			if (Email)
			{
				if (Map.Find(Email))
					ToEmail.New() = Email;
			}
		}
	}
	if (ToEmail.Length() == 0)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_ID));
		DecryptStatus(1);
	}
	
	// Send the file to by decrypted...
	LAutoStreamI Data = EncryptedObj->GetStream(_FL);
	if (!Data)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_DATA));
		DecryptStatus(1);
	}

	if (!Conn->Decrypt(this, Data, (LMessage::Param) m))
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_DECRYPT_FAIL));
		DecryptStatus(1);
	}

	DecryptStatus(0);
}

void MailUiGpg::SignEncrypt(bool uSign, bool uEncrypt, bool uAttachPublicKey, std::function<void(int err)> callback)
{
	LFile log(LFile::Path(ScribeTempPath()) / "gpg.log", O_WRITE);

	// Save the message normally
	Mail *m = d->Ui->GetItem();
	bool IsInPublicFolder = m->GetFolder() && m->GetFolder()->IsPublicFolders();
	if (IsInPublicFolder)
	{
		LDateTime n;
		n.SetNow();
		m->SetDateSent(&n);
		m->Update();
	}
	d->Ui->OnDataEntered();
	d->Ui->OnSave();
	
	auto Root = dynamic_cast<LDataI*>(m->GetObject()->GetObj(FIELD_MIME_SEG));
	if (!Root)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_ROOT));
		DecryptStatus(1);
	}

	// Check that the send has a private key setup...
	LString FromEmail = m->GetFromStr(FIELD_EMAIL);
	if (!FromEmail)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_SENDER_EMAIL));
		DecryptStatus(1);
	}
	
	LString PrivKeyId;
	if (d->Inf)
	{
		for (unsigned i=0; i<d->Inf->Length(); i++)
		{
			GpgConnector::KeyInfo &ki = (*d->Inf)[i];
			if (FromEmail.Equals(ki.email))
			{
				PrivKeyId = ki.keyId;
				break;
			}
		}
	}			
	if (!PrivKeyId)
	{
		LString s;
		s.Printf(LLoadString(IDS_GNUPG_ERR_NO_PRIV_KEY), FromEmail.Get());
		d->SetError(s);
		DecryptStatus(1);
	}

	if (uAttachPublicKey)
	{
		LString PubKey = GetPublicKey(FromEmail);
		if (!PubKey)
		{
			LString s;
			s.Printf(LLoadString(IDS_GNUPG_ERR_NO_PUB_KEY), FromEmail.Get());
			d->SetError(s);
			DecryptStatus(1);
		}

		if (auto a = new Attachment(m->App))
		{
			LAutoStreamI Data(new LMemStream(PubKey, PubKey.Length()));
			if (Data)
			{
				if (!a->ImportStream("public-key.asc", "application/pgp-keys", Data))
				{
					d->SetError(LLoadString(IDS_GNUPG_ERR_IMPORT_FAIL));
					DecryptStatus(1);
				}
				else
				{				
					m->AttachFile(a);
				}
			}
			else
			{
				d->SetError("Allocation failed.");
				DecryptStatus(1);
			}
		}
	}

	// Re-write the MIME hierarchy to have the message and attachments encrypted

	// 1) Export the message to a file:
	const char *BaseName = GpgOut.in;
	LFile::Path p = ScribeTempPath();
	p += BaseName;
	LFile f;
	if (!f.Open(p, O_READWRITE))
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_TEMP_WRITE));
		DecryptStatus(1);
	}	
	f.SetSize(0);
	f.SetPos(0);
	LMime Mime(ScribeTempPath());
	Store3ToLMime(&Mime, Root);
	
	if (!Mime.LGetBoundary())
	{
		// No boundary... so set it and propagate the change back
		char b[64];
		CreateMimeBoundary(b, sizeof(b));
		Mime.SetBoundary(b);
		Root->SetStr(FIELD_INTERNET_HEADER, Mime.GetHeaders());
		
		// If we don't do this then LMime will create it again later
		// when we actually go to send the message, but it will be
		// different then and the signing will fail.
	}	
	
	if (!Mime.Text.Encode.Push(&f))
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_EXPORT_TEMP));
		DecryptStatus(1);
	}
	
	if (uSign)
	{
		// Remove any white space from the end of the file... this is
		// to make sure the signing process is standardized. See
		// https://www.ietf.org/rfc/rfc3156.txt
		// Part 5: OpenPGP signed data
		// This is probably not very efficient but it's usually only the
		// 2 bytes: "\r\n"
		int64 Size, Pos;
		while ( (Size = f.GetSize()) > 0)
		{
			Pos = f.SetPos(Size-1);
			if (Pos != Size - 1)
				break;
			
			char c;
			ssize_t Rd = f.Read(&c, 1);
			if (Rd == 1 &&
				strchr(LWhiteSpace, c))
			{
				f.SetSize(Size - 1);
			}
			else break;
		}		
	}
	
	f.Close();
	
	if (!uEncrypt)
	{
		// Just signing... move MIME tree into child node
		auto NewRoot = Root->GetStore()->Create(MAGIC_ATTACHMENT);
		if (!NewRoot)
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_NEW_ATTACH_FAIL));
			DecryptStatus(1);
		}
		
		// Copy over the root node headers
		NewRoot->SetStr(FIELD_INTERNET_HEADER, Root->GetStr(FIELD_INTERNET_HEADER));
		
		// Re-parent the old root to the new root, and then attach that to the message...
		if (!Root->Save(NewRoot) ||
			!m->GetObject()->SetObj(FIELD_MIME_SEG, NewRoot))
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_REPARENT));
			DecryptStatus(1);
		}
		
		Root = NewRoot;
	}
	
	// 2) Encrypt/sign the file:
	LString InFile(p);
	p = (p / ".." / GpgOut.out);
	LString OutFile(p);
	if (LFileExists(OutFile))
	{
		LError err;
		if (!FileDev->Delete(OutFile, &err, false))
		{
			d->SetError(err.ToString());
			DecryptStatus(1);
		}
	}
	
	LStringPipe args;
	args.Print("--batch -u 0x%s", PrivKeyId.Get());
	
	if (uEncrypt)
	{
		LDataIt To = m->GetTo();
		for (auto Recip = To->First(); Recip; Recip = To->Next())
		{
			auto Email = Recip->GetStr(FIELD_EMAIL);
			LAssert(Email != NULL);
			if (Email)
			{
				args.Print(" --recipient %s", Email);
			}
			else
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_RECIP_NO_EMAIL));
				DecryptStatus(1);
			}
		}
	}
	
	args.Print(	" --armor -o \"%s\" %s \"%s\"",
				OutFile.Get(),
				uSign && uEncrypt ? "-se" : (uSign ? "--detach-sign" : "-e"),
				InFile.Get());
		
	LSubProcess Proc(GpgBinPath, args.NewLStr());
	if (!Proc.Start(true, true))
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_CANT_START));
		DecryptStatus(1);
	}
	
	LStringPipe out;
	Proc.Communicate(&out);
	auto gpgOut = out.NewLStr();

	int Result = Proc.GetExitValue();
	if (Result)
	{
		d->SetError(gpgOut ? gpgOut.Get() : LLoadString(IDS_GNUPG_ERR_ENCRYPT_FAIL));
		DecryptStatus(1);
	}
	
	// 3) Import the encrypted message and replace contents of MIME tree.
	LArray<char> InData, OutData;
	if (!ReadFile(OutData, OutFile))
		DecryptStatus(1);

	#ifdef _DEBUG
		LgiTrace("Gpg finished: InFile='%s', OutFile='%s'\n", InFile.Get(), OutFile.Get());
	#else
		// Clean up temporary files...
		FileDev->Delete(InFile, NULL, false);
		FileDev->Delete(OutFile, NULL, false);
	#endif
	
	if (uEncrypt)
	{
		// Clear out all existing attachments...
		DeleteChildSegments(log, Root);
	}
	
	LogSegments(log, "post delete", Root);

	// Setup the root MIME node to have the right type and fields...
	LAutoStreamI Data;
	
	{
		// By using a LMime object we preserve the existing headers in the MIME
		// segment while still being able to change the MIME type and charset.
		LMime Tmp;
		Tmp.SetHeaders(Root->GetStr(FIELD_INTERNET_HEADER));
		Tmp.SetMimeType(uSign ? sMultipartSigned : sMultipartEncrypted);
		Tmp.SetCharset("utf-8");
		Tmp.SetSub(	"Content-Type",
					"protocol",
					uEncrypt ? sApplicationPgpEncrypted : sApplicationPgpSignature);
		Root->SetStr(FIELD_INTERNET_HEADER, Tmp.GetHeaders());
		
		// Set a body message
		const char *BodyMsg = "This is an OpenPGP/MIME encrypted message (RFC 4880 and 3156)\n";
		Data.Reset(new LMemStream(BodyMsg, strlen(BodyMsg)));
		Root->SetStream(Data);		
		Root->Save();
	}

	LogSegments(log, "saved", Root);

	if (uEncrypt)
	{
		// Attach some app info...
		auto AppInfo = Root->GetStore()->Create(MAGIC_ATTACHMENT);
		if (!AppInfo)
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_NEW_ATTACH_FAIL));
			DecryptStatus(1);
		}		
		AppInfo->SetStr(FIELD_MIME_TYPE, sApplicationPgpEncrypted);
		const char *AppInfoMsg = "Version: 1\n";
		Data.Reset(new LMemStream(AppInfoMsg, strlen(AppInfoMsg)));
		AppInfo->SetStream(Data);
		AppInfo->Save(Root);
	}

	{
		// Attach the new data to the email...
		auto File = Root->GetStore()->Create(MAGIC_ATTACHMENT);
		if (!File)
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_NEW_ATTACH_FAIL));
			DecryptStatus(1);
		}
		
		if (uEncrypt)
		{
			// Attach the encrypted data here...
			LMime Tmp;
			Tmp.SetMimeType(sAppOctetStream);
			Tmp.SetFileName(BaseName);
			Tmp.Set("Content-Disposition", "inline");
			Tmp.SetSub("Content-Disposition", "filename", BaseName);
			File->SetStr(FIELD_INTERNET_HEADER, Tmp.GetHeaders());

		}
		else // uSign
		{
			// Set up the signature attachment
			LMime Tmp;
			Tmp.SetMimeType(sApplicationPgpSignature);
			Tmp.SetFileName(BaseName);
			Tmp.Set("Content-Description", "OpenPGP digital signature");
			Tmp.Set("Content-Disposition", "attachment");
			Tmp.SetSub("Content-Disposition", "filename", BaseName);
			File->SetStr(FIELD_INTERNET_HEADER, Tmp.GetHeaders());
		}

		Data.Reset(new LMemStream(&OutData[0], OutData.Length()));
		File->SetStream(Data);
		File->Save(Root);
	}

	LogSegments(log, "file output", Root);

	// All items are already saved... don't resave
	// Otherwise new text/html body will be attached to the message before
	// being sent. Which is bad mkay?
	d->Ui->SetDirty(false, ThingUi::NoSave);
	Root->SetInt(FIELD_READONLY, true);
	
	// Tell the UI that the object has changed...
	LArray<LDataI*> ChangeArr { m->GetObject() };
	d->App->SetContext(_FL);
	d->App->OnChange(ChangeArr, 0);
	
	DecryptStatus(0);
}

void MailUiGpg::DoCommand(
	int Cmd,
	/// This callback will do the normal command processing...
	/// In the case where we encrypt / sign we replace the normal 
	/// processing and don't do it at all... thus NOT calling the
	/// callback.
	std::function<void(int)> callback)
{
	switch (Cmd)
	{
		case IDM_SEND_MSG:
		{
			if (!d ||
				!d->Enc ||
				!d->Sign ||
				!d->Attach ||
				!d->Ui)
			{
				// Resending an old mail.
				// There is no Enc/Sign UI.
				DecryptStatus(0);
			}
			
			if (!d->willEncrypt() && !d->willSign())
			{
				DecryptStatus(0); // No need to sign &| encrypt, but do the normal processing
			}

			auto m = d->Ui->GetItem();
			if (!m)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_NOMSG));
				DecryptStatus(1); // Don't do normal processing of the cmd
			}

			SignEncrypt(d->willSign(),
						d->willEncrypt(),
						d->Attach->Value() != 0,
						[this, m](auto error)
						{
							if (!error)
							{
								// Send the email..
								m->Send(true);
			
								// Close the window... but after we clean up.
								d->Ui->PostEvent(M_CLOSE);
							}
						});	
			
			// Don't call the callback as we're not doing the normal processing of this command.
			return;
		}
	}
	
	DecryptStatus(0); // Do the default processing
}

LMessage::Result MailUiGpg::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_GNUPG_KEY_INFO:
		{
			d->Inf = Msg->AutoA<KeyArr>();
			if (!d->Inf)
			{
				d->SetError("Missing param.");
				break;
			}

			auto AddrLst = d->GetAddrLst();
			if (!AddrLst)
			{
				d->SetError("No address list.");
				break;
			}

			auto from = d->GetFrom();
			int recipientsWithoutKey = 0;
			
			List<ListAddr> recipients;
			if (d->willEncrypt() && AddrLst->GetAll(recipients))
			{
				LHashTbl<ConstStrKey<char,false>, GpgConnector::KeyInfo*> Map;
				for (unsigned i=0; i<d->Inf->Length(); i++)
				{
					auto *ki = &(*d->Inf)[i];
					Map.Add(ki->email, ki);
				}
				
				for (auto r: recipients)
				{
					// Check if this recipient has a key...
					auto *ki = (r->sAddr) ? Map.Find(r->sAddr) : NULL;
					r->SetInt(FIELD_COLOUR, ki ? cDefaultListItemColour : cError.c32());
					r->Update();
					if (!ki)
						recipientsWithoutKey++;
				}
			}

			// Check for a private key for the sender...
			bool hasPrivate = false;
			if (from.Addr)
			{
				for (auto &i: *d->Inf)
				{
					if (from.Addr.Equals(i.email))
					{
						if (i.flags & GPG_HAS_PRIV_KEY)
							hasPrivate = true;
					}
				}
			}
			
			if (!hasPrivate)
				d->SetError(LString::Fmt("No private key for '%s'", from.Addr.Get()));
			else if (recipientsWithoutKey > 0)
				d->SetError(LLoadString(IDS_GNUPG_ERR_ONE_OR_MORE));
			else
			{
				if (d->willEncrypt() && d->willSign())
					d->SetSuccess(LLoadString(IDS_GNUPG_ENCRYPTED_AND_SIGNED));
				else if (d->willEncrypt())
					d->SetSuccess(LLoadString(IDS_GNUPG_ENCRYPTED));
				else if (d->willSign())
					d->SetSuccess(LLoadString(IDS_GNUPG_SIGNED));
				else
					LAssert(0);
			}
			break;
		}
		case M_GNUPG_SIG_CHECK:
		{
			Mail *m = d->Ui ? d->Ui->GetItem() : NULL;
			auto Resp = Msg->AutoA<GpgSigCheckResponse>();
			
			if (!Resp || m != (Mail*)Resp->UserValue)
			{
				d->SetError("M_GNUPG_SIG_CHECK: bad response");
				break;
			}
			
			if (Resp->Error)
			{
				d->SetError(Resp->Error);
				break;
			}

			LString Dt = Resp->TimeStamp.Get();
			
			const char *Type = Resp->SignatureMatch ? LLoadString(IDS_GNUPG_GOOD_SIG) : LLoadString(IDS_GNUPG_BAD_SIG);
			LString Msg;
			if (Resp->Identity)
				Msg.Printf(LLoadString(IDS_GNUPG_SIG_MSG), Type, Resp->Identity.Get(), Dt.Get());
			else
				Msg.Printf(LLoadString(IDS_GNUPG_SIG_NO_ID), Type, Dt.Get());

			if (Resp->SignatureMatch)
				d->SetSuccess(Msg);
			else
				d->SetError(Msg);
			break;
		}
		case M_GNUPG_DECRYPT:
		{
			auto Resp = Msg->AutoA<GpgDecryptResponse>();
			if (!Resp)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_INVALID_DECRYPTION));
				break;
			}
			if (!Resp->Data)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_NO_OUTPUT));
				break;
			}
			if (Resp->Error)
			{
				d->SetError(Resp->Error);
				break;
			}
			
			auto m = d->Ui->GetItem();
			if (!m || m != (Mail*)Resp->UserValue)
			{
				d->SetError("Incorrect mail object after decryption.");
				break;
			}

			d->SetSuccess(LLoadString(IDS_GNUPG_DECRYPT_OK));
			auto Obj = m->GetObject();
			if (!Obj)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_NOMSG));
				break;
			}
			
			// This turns of notification processing, the message is changing
			// but we don't want it to get dirty.
			d->Ui->_Running = false;

			// Unload the attachments, because they will point to stale back-end
			// objects, that "SetStream" will delete.
			m->UnloadAttachments();
			
			// Set the stream of the object (which will MIME parse the data, 
			// replacing the MIME Seg tree)
			Obj->SetStream(Resp->Data);
		
			// Re-enable the notifcation processing.
			d->Ui->_Running = true;
 
			// Get the UI to reload and display the object...
			LArray<LDataI*> Items;
			Items.Add(Obj);
			m->App->SetContext(_FL);
			m->App->OnChange(Items, 0);
			
			// Change the button to disabled... no need for it anymore.
			SetCtrlEnabled(IDC_DECRYPT, false);
			break;
		}
	}

	return LView::OnEvent(Msg);
}

bool MailUiGpg::Pour(LRegion &r)
{
	LRect lrg = FindLargest(r);
	if (!lrg.Valid())
		return false;

	int y = LSysFont->GetHeight() + 12;
	lrg.y2 = lrg.y1 + MIN(y, lrg.Y()) - 1;
	SetPos(lrg);
	
	if (d->Table)
	{
		LCssTools layout(d->Table);
		auto c = layout.ApplyMargin(GetClient());
		d->Table->SetPos(c);
	}

	return true;		
}

void MailUiGpg::OnCreate()
{
	AttachChildren();
	LResources::StyleElement(this);

	// This is in OnCreate because the CheckSignature needs a valid
	// view handle to post the message back to us.
	if (d->Signed)
	{
		GpgConnector *Conn = d->App->GetGpgConnector();
		if (Conn)
		{
			Mail *m = d->Ui->GetItem();
			LDataI *obj = m ? m->GetObject() : NULL;

			LAutoStreamI Msg;
			if (obj)
				Msg = obj->GetStream(_FL);

			if (Msg)
			{
				Conn->CheckSignature(this, Msg, (LMessage::Param)m);
				d->SetWarning(LLoadString(IDS_GNUPG_CHECK_MSG));
			}
			else
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_NOMSG));
			}
		}
		else
		{
			d->NotInstalled();
		}			
	}
}

void MailUiGpg::OnPaint(LSurface *pDC)
{
	LCssTools Tools(this);
	LRect c = GetClient();
	Tools.PaintBorder(pDC, c);
	Tools.PaintPadding(pDC, c);
	Tools.PaintContent(pDC, c);
}
