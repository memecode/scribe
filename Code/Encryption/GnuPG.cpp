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
#include "Scribe.h"
#include "GnuPG.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Css.h"
#include "lgi/common/ThreadEvent.h"
#include "lgi/common/SubProcess.h"
#include "ScribeListAddr.h"
#include "Store3Common.h"
#include "lgi/common/LgiRes.h"
#include "resdefs.h"
#include "lgi/common/CssTools.h"

//////////////////////////////////////////////////////////////////////////////////////////
enum Ctrls
{
	IDC_SIGN = 700,
	IDC_ENCRYPT,
	IDC_ATTACH_PUB_KEY,
	IDC_DECRYPT,
	IDC_INSTALL
};

static LColour cGood					(0, 204, 0);
static LColour cWarn					(255, 154, 0);
static LColour cError					(255, 0, 0);
static LColour cTxt						(L_TEXT);
#define cDefaultListItemColour			0
#define PANEL_BORDER_PX					2

static const char *GpgInstall =			"https://www.gnupg.org/download/index.en.html";
static const char *GpgBin =				"gpg" LGI_EXECUTABLE_EXT;
static LString GpgBinPath;
#define SECONDS							* 1000
#define MINUTES							* 60
#define GPG_KEY_STALE_TIMEOUT			(10 MINUTES)

#define DecryptStatus(val) \
	if (callback) callback(val); \
	return;

typedef LArray<GpgConnector::KeyInfo>	KeyArr;
typedef LAutoPtr<KeyArr>				KeyArrAuto;

struct GpgJob
{
	enum JobType
	{
		JobGetKeys,
		JobCheckSig,
		JobDecrypt,
	} Type;	
	LViewI *Owner;
	
	GpgJob(JobType t)
	{
		Type = t;
		Owner = NULL;
	}

	// Get keys:
	LAutoPtr<LString::Array> Emails;

	// Check sig:
	LAutoStreamI Msg;
	LMessage::Param UserValue;
	
	// Decrypt
	LString Password;
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
			FileDev->Delete(Path, false);
		}
	}
};

struct GpgConnectorPriv : public LThread, public LMutex
{
	LThreadEvent Event;

private:
	bool Loop;
	KeyArr Keys;
	LArray<GpgJob*> Work;
	uint64 KeysTs;

public:	
	GpgConnectorPriv() :
		LThread("GpgConnectorPrivThread"),
		LMutex("GpgConnectorPrivMutex")
	{
		Loop = true;
		KeysTs = 0;
		Run();
	}
	
	~GpgConnectorPriv()	
	{
		Loop = false;
		Event.Signal();
		while (!IsExited())
			LSleep(1);
	}

	void AddWork(GpgJob *j)
	{
		if (Lock(_FL))
		{
			Work.Add(j);
			Unlock();
		}
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
		LString::Array a = str.Split(EOL_SEQUENCE);
		LString KeyId;
		for (unsigned i=0; i<a.Length(); i++)
		{
			LString &Ln = a[i];
			if (GotDash)
			{
				LString::Array b = Ln.SplitDelimit(" \t/");
				if (b.Length() > 0)
				{
					if (b[0].Equals("sec") ||
						b[0].Equals("pub"))
					{
						if (b.Length() == 4)
							KeyId = b[2];
					}
					else if (b[0].Equals("uid"))
					{
						LString Nm = Ln(21, -1).Strip();
						LAutoString Name, Addr;
						DecodeAddrName(Nm, Name, Addr, NULL);
						if (ValidStr(Name) && ValidStr(Addr))
						{
							GpgConnector::KeyInfo &Cur = k.New();
							Cur.KeyId = KeyId;
							Cur.Name = Name;
							Cur.Email = Addr;
						}
						else LgiTrace("%s:%i - Error parsing '%s'\n", _FL, Nm.Get());
					}
				}
			}
			else if (stristr(Ln, "--------"))
			{
				GotDash = true;
			}
		}
	}

	bool GetKeys()
	{
		uint64 Now = LCurrentTime();
		if (Now - KeysTs > GPG_KEY_STALE_TIMEOUT)
		{
			KeysTs = Now;		

			LString s = RunGpg("-k");
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
					if (k.Email)
						Hash.Add(k.Email, &k);
					else
						LgiTrace("%s:%i - No email for key?\n", _FL);
				}

				for (unsigned i=0; i<Priv.Length(); i++)
				{
					GpgConnector::KeyInfo *k = Hash.Find(Priv[i].Email);
					if (k)
						k->Flags |= GPG_HAS_PRIV_KEY;
				}
			}
		}

		return Keys.Length() > 0;
	}
	
	bool Save(const char *Path, LString &s)
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
	
	void CheckSignature(LViewI *Owner, LAutoStreamI UserMsg, LMessage::Param UserVal)
	{
		LString Msg;
		int64 Sz = 0;
		int64 Rd = 0;
		ptrdiff_t HdrSize = 0;
		LString Start;
		LArray<LRange> Segs;
		LMime Tmp;
		LString Hdrs;
		char *Boundary = NULL;
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
		Boundary = Tmp.GetBoundary();
		if (!Boundary)
		{
			Resp->Error = LLoadString(IDS_GNUPG_ERR_NO_BOUNDARY);
			goto OnSigCheckError;
		}
		
		// Now look through the message and find all the segments...
		Start.Printf("\r\n--%s", Boundary);
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
					while (IsWhiteSpace(m[k]))
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
			LRange &Body = Segs[0];
			LRange &Sig = Segs[1];
			
			// Get the 2 parts from the whole message..
			LString SignedText = Msg(Body.Start, Body.Start + Body.Len);
			LString Signature = Msg(Sig.Start, Sig.Start + Sig.Len);
			ptrdiff_t Break = Signature.Find("\r\n\r\n");
			if (Break > 0)
				Signature = Signature(Break + 4, -1);
			
			// Save them to files:
			LFile::Path TextPath = ScribeTempPath(), SigPath = ScribeTempPath();
			TextPath += "signed.txt";
			SigPath += "signature.txt";
			if (!Save(TextPath, SignedText) ||
				!Save(SigPath, Signature))
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

			#ifndef _DEBUG
			// Clean up temporary files...
			FileDev->Delete(TextPath, false);
			FileDev->Delete(SigPath, false);
			#endif
			
			printf("Txt=%s\nSig=%s\n", (const char*)TextPath, (const char*)SigPath);
		}
		else
		{
			Resp->Error.Printf(LLoadString(IDS_GNUPG_ERR_WRONG_SEGS), Segs.Length());
		}
		
	OnSigCheckError:
		Owner->PostEvent(M_GNUPG_SIG_CHECK, (LMessage::Param) Resp.Release());
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
			Args.Printf("--batch --passphrase-fd 0 --output \"%s\" --decrypt \"%s\"",
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

				LString PswStr;
				PswStr.Printf("%s\n", j->Password.Get());
				ssize_t w = Proc.Write(PswStr.Get(), PswStr.Length());
				if (w < 0)
				{
					ssize_t r = Proc.Read(Buf, sizeof(Buf)-1);
					Buf[MAX(r, 0)] = 0;

					Resp->Error = Buf[0] ? Buf : "Can't write to the GnuPG sub-process.";
				}
				else
				{
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
		}
		
		// Clean up input file (encrypted)
		// The output file will be deleted by the M_GNUPG_DECRYPT handler.
		if (LFileExists(InPath))
		{
			FileDev->Delete(InPath, false);
		}
		
		j->Owner->PostEvent(M_GNUPG_DECRYPT, (LMessage::Param) Resp.Release());
	}
	
	int Main()
	{
		LThreadEvent::WaitStatus s;
		while ((s = Event.Wait()) == LThreadEvent::WaitSignaled)
		{
			if (!Loop) break;
			
			LAutoPtr<GpgJob> j;
			if (Lock(_FL))
			{
				if (Work.Length())
				{
					j.Reset(Work[0]);
					Work.DeleteAt(0, true);
				}
				Unlock();
			}
			if (j)
			{
				switch (j->Type)
				{
					case GpgJob::JobGetKeys:
					{
						if (Keys.Length() == 0)
							GetKeys();
						
						LHashTbl<ConstStrKey<char,false>,bool> Map;
						for (unsigned i=0; i<j->Emails->Length(); i++)
						{
							Map.Add((*j->Emails)[i], true);
						}
						
						KeyArrAuto Inf(new KeyArr);
						for (unsigned i=0; i<Keys.Length(); i++)
						{
							GpgConnector::KeyInfo &in = Keys[i];
							if (!Map.Find(in.Email))
								continue;

							// Make an explicit copy here, because we are passing the data back to the
							// calling thread, and LString's aren't thread safe.					
							GpgConnector::KeyInfo &out = Inf->New();
							out.Email = in.Email.Get();
							out.Name = in.Name.Get();
							out.KeyId = in.KeyId.Get();
							out.Flags = in.Flags;
						}
						
						j->Owner->PostEvent(M_GNUPG_KEY_INFO, 0, (LMessage::Param)Inf.Release());
						break;
					}
					case GpgJob::JobCheckSig:
					{
						CheckSignature(j->Owner, j->Msg, j->UserValue);
						break;
					}
					case GpgJob::JobDecrypt:
					{
						Decrypt(j);
						break;
					}
					default:
					{
						LAssert(!"Invalid type.");
						break;
					}
				}
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
	if (!Target
		#if LGI_VIEW_HANDLE
		|| !Target->Handle()
		#endif
		)
	{
		LAssert(!"Invalid target.");
		return false;
	}
	if (!d->Lock(_FL))
		return false;

	GpgJob *j = new GpgJob(GpgJob::JobGetKeys);
	if (j)
	{
		j->Emails.Reset(new LString::Array(Emails));
		j->Owner = Target;
		d->AddWork(j);
	}
	
	d->Unlock();

	return d->Event.Signal();
}

bool GpgConnector::CheckSignature(LViewI *Target, LAutoStreamI Rfc822Msg, LMessage::Param UserVal)
{
	if (!Target
		#if LGI_VIEW_HANDLE
		|| !Target->Handle()
		#endif
		)
	{
		LAssert(!"Invalid target.");
		return false;
	}
	if (!d->Lock(_FL))
		return false;

	GpgJob *j = new GpgJob(GpgJob::JobCheckSig);
	if (j)
	{
		j->Owner = Target;
		j->Msg = Rfc822Msg;
		j->UserValue = UserVal;

		d->AddWork(j);
	}
	
	d->Unlock();

	return d->Event.Signal();
}

bool GpgConnector::Decrypt(LViewI *Target, LAutoStreamI Data, LString Password, LMessage::Param UserVal)
{
	if (!Target
		#if LGI_VIEW_HANDLE
		|| !Target->Handle()
		#endif
		)
	{
		LAssert(!"Invalid target.");
		return false;
	}
	if (!d->Lock(_FL))
		return false;

	GpgJob *j = new GpgJob(GpgJob::JobDecrypt);
	if (j)
	{
		j->Owner = Target;
		j->Msg = Data;
		j->Password = Password;
		j->UserValue = UserVal;

		d->AddWork(j);
	}
	
	d->Unlock();

	return d->Event.Signal();
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
	ScribeWnd *App;
	MailUi *Ui;
	
	// UI
	LCheckBox *Enc;
	LCheckBox *Sign;
	LCheckBox *Attach;
	LTextLabel *Msg;
	KeyArrAuto Inf;
	LTableLayout *Table;
	LButton *Decrypt, *Install;
	
	// Options
	bool WritingEmail;
	bool Encrypted;
	bool Signed;
	
	// Passwords
	LArray<UserPassword> Psw;
	
	MailUiGpgPriv(ScribeWnd *app, MailUi *ui, bool writingEmail)
	{
		App = app;
		Ui = ui;
		
		Enc = NULL;
		Sign = NULL;
		Attach = NULL;
		Msg = NULL;
		Table = NULL;
		Decrypt = NULL;
		Install = NULL;
		
		WritingEmail = writingEmail;
		Encrypted = false;
		Signed = false;
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

	void SetError(const char *Str) { Set(Str, cError); }
	void SetWarning(const char *Str) { Set(Str, cWarn); }
	void SetStatus(const char *Str) { Set(Str, cTxt); }
	void SetSuccess(const char *Str) { Set(Str, cGood); }
	
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
		Msg.Printf("Enter password for the user '%s':", Addr.Get());
		
		auto Dlg = new LInput(Parent, "", Msg, "GnuPG Password", true);
		Dlg->DoModal([this, Dlg, Addr, Callback](auto dlg, auto ctrlId)
		{
			if (ctrlId)
			{
				UserPassword &p = Psw.New();
				p.Email = Addr;
				p.Password = Dlg->GetStr();
				p.Ts = LCurrentTime();
			
				if (Callback)
					Callback(p.Password);
			}
			delete dlg;
		});
	}
};
	
LCss::Len Px(int px)
{
	return LCss::Len(LCss::LenPx, (float)px);
}
	
MailUiGpg::MailUiGpg(ScribeWnd *App, MailUi *Ui, int ColX1, int ColX2, bool WritingEmail)
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
		Mail *m = Ui->GetItem();
		LDataI *obj = m ? m->GetObject() : NULL;

		int x = 0;
		LTextLabel *Txt;
		auto *c = d->Table->GetCell(x++, 0);
		c->Position(LCss::PosAbsolute);
		c->Left(Px(ColX1 - PANEL_BORDER_PX));
		c->Top(Px(TextY - PANEL_BORDER_PX));
		c->Add(Txt = new LTextLabel(IDC_STATIC, ColX1, TextY, -1, -1, "GnuPG:"));
		
		if (WritingEmail)
		{
			c = d->Table->GetCell(x++, 0);
			c->Position(LCss::PosAbsolute);
			c->Left(Px(ColX2 - PANEL_BORDER_PX + 1));
			c->Top(Px(ChkY - PANEL_BORDER_PX));
			c->Add(d->Enc = new LCheckBox(IDC_ENCRYPT, 0, 0, -1, -1, LLoadString(IDS_GNUPG_ENCRYPT)));
			
			c = d->Table->GetCell(x++, 0);
			c->PaddingTop(Px(ChkY - PANEL_BORDER_PX));
			c->Add(d->Sign = new LCheckBox(IDC_SIGN, 0, 0, -1, -1, LLoadString(IDS_GNUPG_SIGN)));
			
			c = d->Table->GetCell(x++, 0);
			c->PaddingTop(Px(ChkY - PANEL_BORDER_PX));
			c->Add(d->Attach = new LCheckBox(IDC_ATTACH_PUB_KEY, 0, 0, -1, -1, LLoadString(IDS_GNUPG_ATTACH_PUB_KEY)));
		}
		else
		{
			c = d->Table->GetCell(x++, 0);
			c->Position(LCss::PosAbsolute);
			c->Left(Px(ColX2 - PANEL_BORDER_PX + 1));
			if (c->Add(d->Decrypt = new LButton(IDC_DECRYPT, 0, 0, -1, -1, LLoadString(IDS_GNUPG_DECRYPT))))
			{			
				LDataPropI *seg = obj ? obj->GetObj(FIELD_MIME_SEG) : NULL;
				auto mimeType = seg ? seg->GetStr(FIELD_MIME_TYPE) : NULL;				
				if (mimeType)
				{
					d->Signed = !_stricmp(mimeType, sMultipartSigned);
					d->Encrypted = !_stricmp(mimeType, sMultipartEncrypted);
				}				
				d->Decrypt->Enabled(d->Encrypted);
			}
		}

		c = d->Table->GetCell(x++, 0);
		c->PaddingTop(Px(TextY - PANEL_BORDER_PX));
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
	
	AddressList *AddrLst = d->GetAddrLst();
	if (!AddrLst)
	{
		d->SetError("No AddressList.");
		return;
	}

	GpgConnector *Gpg = d->App->GetGpgConnector();
	if (!Gpg)
	{
		d->NotInstalled();
		return;
	}

	List<ListAddr> a;
	if (!AddrLst->GetAll(a))
	{
		d->SetStatus(LLoadString(IDS_GNUPG_ERR_NO_RECIP));
		return;
	}

	if (d->Enc->Value() ||
		d->Sign->Value())
	{
		// Do we have public keys for each of the recipients?
		d->SetWarning(LLoadString(IDS_GNUPG_CHECKING));
		
		LString::Array Emails;
		for (auto i: a)
		{
			if (i->sAddr)
				Emails.New() = i->sAddr;
		}
		
		LCombo *cbo;
		if (d->Ui->GetViewById(IDC_FROM, cbo))
		{
			const char *Frm = cbo->Name();
			LAutoString Name, Email;
			DecodeAddrName(Frm, Name, Email, NULL);
			if (Email)
				Emails.New() = Email;
		}
		
		Gpg->GetKeyInfo(this, Emails);
	}
	else
	{
		d->SetStatus(LLoadString(IDS_GNUPG_ERR_NO_SIGN_ENC));

		// Revert list items to no colour...
		for (auto i: a)
		{
			i->SetInt(FIELD_COLOUR, cDefaultListItemColour);
			i->Update();
		}
	}	
}

// This gets all the events from MailUi::OnNotify as well as any events
// from MailUiGpg's child controls.
// \returns non-zero if further processing in MailUi::OnNotify should be blocked.
int MailUiGpg::OnNotify(LViewI *Ctrl, LNotification n)
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

void DeleteChildSegments(LDataPropI *d)
{
	LDataIt It = d->GetList(FIELD_MIME_SEG);
	for (unsigned i=0; i<It->Length(); )
	{
		LDataPropI *c = (*It)[i];
		
		DeleteChildSegments(c);
		
		LDataI *cdi = dynamic_cast<LDataI*>(c);
		if (cdi)
		{
			// This deletes the on disk representation.
			cdi->Delete();
			
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
	GpgConnector *Conn = d->App->GetGpgConnector();
	if (!Conn)
	{
		d->NotInstalled();
		DecryptStatus(1);
	}

	// Find the right attachment...
	Mail *m = d->Ui->GetItem();
	if (!m)
	{
		d->SetError(LLoadString(IDS_GNUPG_ERR_NO_MAIL));
		DecryptStatus(1);
	}
	LDataPropI *Root = m->GetObject()->GetObj(FIELD_MIME_SEG);
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
	
	d->GetPassword(d->Ui, ToEmail[0], [this, callback, EncryptedObj, Conn, m](auto Pass)
	{
		if (!Pass)
		{
			d->SetWarning(LLoadString(IDS_GNUPG_DECRYPT_CANCEL));
			DecryptStatus(1);
		}
	
		// Send the file to by decrypted...
		LAutoStreamI Data = EncryptedObj->GetStream(_FL);
		if (!Data)
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_NO_DATA));
			DecryptStatus(1);
		}

		if (!Conn->Decrypt(this, Data, Pass, (LMessage::Param) m))
		{
			d->SetError(LLoadString(IDS_GNUPG_ERR_DECRYPT_FAIL));
			DecryptStatus(1);
		}

		DecryptStatus(0);
	});
}

void MailUiGpg::SignEncrypt(bool uSign, bool uEncrypt, bool uAttachPublicKey, std::function<void(int)> callback)
{
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
	
	LDataI *Root = dynamic_cast<LDataI*>(m->GetObject()->GetObj(FIELD_MIME_SEG));
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
			if (!_stricmp(ki.Email, FromEmail))
			{
				PrivKeyId = ki.KeyId;
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

		Attachment *a = new Attachment(m->App);
		if (a)
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
	
	// 1) Get the password
	d->GetPassword(d->Ui, FromEmail.Get(),
		[this, callback, InputRoot=Root, uSign, uEncrypt, m, PrivKeyId](auto Psw)
		{
			LDataI *LocalRoot = InputRoot;

			if (!Psw)
			{
				d->SetStatus(LLoadString(IDS_GNUPG_ERR_SIGN_ENC_CANCEL));
				DecryptStatus(1);
			}
	
			// 1) Export the message to a file:
			const char *BaseName = "encrypted.asc";
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
			Store3ToLMime(&Mime, LocalRoot);
	
			if (!Mime.GetBoundary())
			{
				// No boundary... so set it and propagate the change back
				char b[64];
				CreateMimeBoundary(b, sizeof(b));
				Mime.SetBoundary(b);
				LocalRoot->SetStr(FIELD_INTERNET_HEADER, Mime.GetHeaders());
		
				// If we don't do this then LMime will create it again later
				// when we actually go to send the message, but it will be
				// different then and the signing will fail.
			}	
	
			if (!Mime.Text.Encode.Push(&f))
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_EXPORT_TEMP));
				DecryptStatus(1);
			}
	
			#if 1
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
						strchr(WhiteSpace, c))
					{
						f.SetSize(Size - 1);
					}
					else break;
				}		
			}
			#endif
	
			f.Close();
	
			if (!uEncrypt)
			{
				// Just signing... move MIME tree into child node
				LDataI *NewRoot = LocalRoot->GetStore()->Create(MAGIC_ATTACHMENT);
				if (!NewRoot)
				{
					d->SetError(LLoadString(IDS_GNUPG_ERR_NEW_ATTACH_FAIL));
					DecryptStatus(1);
				}
		
				// Copy over the root node headers
				NewRoot->SetStr(FIELD_INTERNET_HEADER, LocalRoot->GetStr(FIELD_INTERNET_HEADER));
		
				// Reparent the old root to the new root, and then attach that to the message...
				if (!LocalRoot->Save(NewRoot) ||
					!m->GetObject()->SetObj(FIELD_MIME_SEG, NewRoot))
				{
					d->SetError(LLoadString(IDS_GNUPG_ERR_REPARENT));
					DecryptStatus(1);
				}
		
				LocalRoot = NewRoot;
			}
	
			// 2) Encrypt/sign the file:
			LString InFile(p);
			p--;
			p += "encrypted.gpg";
			LString OutFile(p);
			if (LFileExists(OutFile))
			{
				FileDev->Delete(OutFile, false);
			}
	
			LString Args, s;
			Args.Printf("--batch --passphrase-fd 0 -u 0x%s",
				PrivKeyId.Get());
	
			if (uEncrypt)
			{
				LDataIt To = m->GetTo();
				for (LDataPropI *Recip = To->First(); Recip; Recip = To->Next())
				{
					auto Email = Recip->GetStr(FIELD_EMAIL);
					LAssert(Email != NULL);
					if (Email)
					{
						s.Printf(" --recipient %s", Email);
						Args += s;
					}
					else
					{
						d->SetError(LLoadString(IDS_GNUPG_ERR_RECIP_NO_EMAIL));
						DecryptStatus(1);
					}
				}
			}
	
			s.Printf(" --armor -o \"%s\" %s \"%s\"",
				OutFile.Get(),
				uSign && uEncrypt ? "-se" : (uSign ? "--detach-sign" : "-e"),
				InFile.Get());
			Args += s;
		
			LSubProcess Proc(GpgBinPath, Args);
			char Buf[256];
			if (!Proc.Start(true, true))
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_CANT_START));
				DecryptStatus(1);
			}
	
			LString PswStr;
			PswStr.Printf("%s\n", Psw.Get());
			ssize_t w = Proc.Write(PswStr.Get(), PswStr.Length());
			if (w < 0)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_WRITE));
				DecryptStatus(1);
			}
	
			ssize_t r = Proc.Read(Buf, sizeof(Buf)-1);
			Buf[MAX(r, 0)] = 0;
			int Result = Proc.Wait();
			if (Result)
			{
				d->SetError(Buf[0] ? Buf : LLoadString(IDS_GNUPG_ERR_ENCRYPT_FAIL));
				DecryptStatus(1);
			}
	
			// 3) Import the encrypted message and replace contents of MIME tree.
			LArray<char> InData, OutData;
			if (!ReadFile(OutData, OutFile))
				DecryptStatus(1);

			#ifndef _DEBUG
			// Clean up temporary files...
			FileDev->Delete(InFile, false);
			FileDev->Delete(OutFile, false);
			#endif
	
			if (uEncrypt)
			{
				// Clear out all existing attachments...
				DeleteChildSegments(LocalRoot);
			}
	
			// Setup the root MIME node to have the right type and fields...
			LAutoStreamI Data;
	
			{
				// By using a LMime object we preserve the existing headers in the MIME
				// segment while still being able to change the MIME type and charset.
				LMime Tmp;
				Tmp.SetHeaders(LocalRoot->GetStr(FIELD_INTERNET_HEADER));
				Tmp.SetMimeType(uSign ? sMultipartSigned : sMultipartEncrypted);
				Tmp.SetCharset("utf-8");
				Tmp.SetSub(	"Content-Type",
							"protocol",
							uEncrypt ? "application/pgp-encrypted" : "application/pgp-signature");
				LocalRoot->SetStr(FIELD_INTERNET_HEADER, Tmp.GetHeaders());
		
				// Set a body message
				const char *BodyMsg = "This is an OpenPGP/MIME encrypted message (RFC 4880 and 3156)\n";
				Data.Reset(new LMemStream(BodyMsg, strlen(BodyMsg)));
				LocalRoot->SetStream(Data);
		
				LocalRoot->Save();
			}

			if (uEncrypt)
			{
				// Attach some app info...
				LDataI *AppInfo = LocalRoot->GetStore()->Create(MAGIC_ATTACHMENT);
				if (!AppInfo)
				{
					d->SetError(LLoadString(IDS_GNUPG_ERR_NEW_ATTACH_FAIL));
					DecryptStatus(1);
				}		
				AppInfo->SetStr(FIELD_MIME_TYPE, "application/pgp-encrypted");
				const char *AppInfoMsg = "Version: 1\n";
				Data.Reset(new LMemStream(AppInfoMsg, strlen(AppInfoMsg)));
				AppInfo->SetStream(Data);
				AppInfo->Save(LocalRoot);
			}

			{
				// Attach the new data to the email...
				LDataI *File = LocalRoot->GetStore()->Create(MAGIC_ATTACHMENT);
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
					Tmp.SetMimeType("application/pgp-signature");
					Tmp.SetFileName(BaseName);
					Tmp.Set("Content-Description", "OpenPGP digital signature");
					Tmp.Set("Content-Disposition", "attachment");
					Tmp.SetSub("Content-Disposition", "filename", BaseName);
					File->SetStr(FIELD_INTERNET_HEADER, Tmp.GetHeaders());
				}

				Data.Reset(new LMemStream(&OutData[0], OutData.Length()));
				File->SetStream(Data);
		
				File->Save(LocalRoot);
			}
	
			// Tell the UI that the object has changed...
			LArray<LDataI*> ChangeArr;
			ChangeArr.Add(m->GetObject());
			d->App->SetContext(_FL);
			d->App->OnChange(ChangeArr, 0);
	
			DecryptStatus(0);
		});
}

void MailUiGpg::DoCommand(int Cmd, std::function<void(int)> callback)
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
			
			if (!d->Enc->Value() && !d->Sign->Value())
			{
				DecryptStatus(0); // No need to sign &| encrypt
			}

			Mail *m = d->Ui->GetItem();
			if (!m)
			{
				d->SetError(LLoadString(IDS_GNUPG_ERR_NOMSG));
				DecryptStatus(1);
			}

			SignEncrypt(d->Sign->Value() != 0,
						d->Enc->Value() != 0,
						d->Attach->Value() != 0,
						[this, m, callback](auto status)
			{
				if (!status)
				{
					// Send the email..
					m->Send(true);
			
					// Close the window...
					d->Ui->Quit();
				}

				// Bypass the normal code
				DecryptStatus(1);
			});			
		}
	}
	
	DecryptStatus(0);
}

LMessage::Result MailUiGpg::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_GNUPG_KEY_INFO:
		{
			d->Inf.Reset( (KeyArr*) Msg->B() );
			AddressList *AddrLst = d->GetAddrLst();
			if (d->Inf && AddrLst)
			{
				int NoKey = 0;
				
				List<ListAddr> a;
				if (AddrLst->GetAll(a))
				{
					LHashTbl<ConstStrKey<char,false>, GpgConnector::KeyInfo*> Map;
					for (unsigned i=0; i<d->Inf->Length(); i++)
					{
						GpgConnector::KeyInfo *ki = &(*d->Inf)[i];
						Map.Add(ki->Email, ki);
					}
					
					for (auto i: a)
					{
						// Check if this recipient has a key...
						GpgConnector::KeyInfo *ki = (i->sAddr) ? Map.Find(i->sAddr) : NULL;
						i->SetInt(FIELD_COLOUR, ki ? cDefaultListItemColour : cError.c32());
						i->Update();
						if (!ki)
							NoKey++;
					}
				}
				
				if (NoKey > 0)
					d->SetError(LLoadString(IDS_GNUPG_ERR_ONE_OR_MORE));
				else if (d->Enc && d->Sign)
				{
					if (d->Enc->Value() && d->Sign->Value())
						d->SetSuccess(LLoadString(IDS_GNUPG_ENCRYPTED_AND_SIGNED));
					else if (d->Enc->Value())
						d->SetSuccess(LLoadString(IDS_GNUPG_ENCRYPTED));
					else if (d->Sign->Value())
						d->SetSuccess(LLoadString(IDS_GNUPG_SIGNED));
					else
						LAssert(0);						
				}
				else LAssert(0);
			}
			else d->SetError("Parameter error.");
			break;
		}
		case M_GNUPG_SIG_CHECK:
		{
			Mail *m = d->Ui ? d->Ui->GetItem() : NULL;
			LAutoPtr<GpgSigCheckResponse> Resp((GpgSigCheckResponse*)Msg->A());
			
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
			LAutoPtr<GpgDecryptResponse> Resp((GpgDecryptResponse*)Msg->A());
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
			
			Mail *m = d->Ui->GetItem();
			if (!m || m != (Mail*)Resp->UserValue)
			{
				d->SetError("Incorrect mail object after decryption.");
				break;
			}

			d->SetSuccess(LLoadString(IDS_GNUPG_DECRYPT_OK));
			LDataI *Obj = m->GetObject();
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
		LRect c = GetClient();
		c.Inset(PANEL_BORDER_PX, PANEL_BORDER_PX);
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
