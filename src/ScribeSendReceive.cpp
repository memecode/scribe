/*
**	FILE:			ScribeAccount.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			19/10/99
**	DESCRIPTION:	Scribe account
**
**	Copyright (C) 2002, Matthew Allen
**		fret@memecode.com
*/

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "lgi/common/OpenSSLSocket.h"
#include "ScribePrivate.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"
#include "ScribeAccountPreview.h"
#include "ScribeUtils.h"
#include "lgi/common/TextConvert.h"

#define MAIL_POSTED_TO_GUI			0x0001
#define MAIL_EXPLICIT				0x0002

#define SECONDS(i)					((i)*1000)
#define TRANSFER_WAIT_TIMEOUT		SECONDS(30)
#define DEFAULT_SOCKET_TIMEOUT		SECONDS(15)

LColour SocketMsgTypeToColour(LSocketI::SocketMsgType flags)
{
	LColour col;
	switch (flags)
	{
		case LSocketI::SocketMsgInfo:
		case LSocketI::SocketMsgNone:
			col.Set(MAIL_INFO_COLOUR, 24);
			break;
		case LSocketI::SocketMsgSend:
			col.Set(MAIL_SEND_COLOUR, 24);
			break;
		case LSocketI::SocketMsgReceive:
			col.Set(MAIL_RECEIVE_COLOUR, 24);
			break;
		case LSocketI::SocketMsgError:
			col.Set(MAIL_ERROR_COLOUR, 24);
			break;
		case LSocketI::SocketMsgWarning:
			col.Set(MAIL_WARNING_COLOUR, 24);
			break;
		default:
			LAssert(0);
			break;
	}
	return col;
}

int MakeOpenFlags(ScribeAccount *a, bool Send)
{
	int OpenFlags = 0;
	Accountlet *l = Send ? (Accountlet*)&a->Send : (Accountlet*)&a->Receive;

	auto Nam = a->Identity.Name();
	auto Em = a->Identity.Email();
	// auto SslSmtp = a->Send.UseSSL();

	ScribeSslMode SslMode = (ScribeSslMode)l->UseSSL();
	switch (SslMode)
	{
		case SSL_STARTTLS:
			OpenFlags |= MAIL_USE_STARTTLS;
			break;
		case SSL_DIRECT:
			OpenFlags |= MAIL_SSL;
			break;
		default:
			break;
	}

	if (Send)
	{
		if (a->Send.RequireAuthentication())
		{
			OpenFlags |= MAIL_USE_AUTH;
		}

		switch (a->Send.AuthType())
		{
			case 1:
				OpenFlags |= MAIL_USE_PLAIN;
				break;
			case 2:
				OpenFlags |= MAIL_USE_LOGIN;
				break;
			case 3:
				OpenFlags |= MAIL_USE_CRAM_MD5;
				break;
			case 4:
				OpenFlags |= MAIL_USE_OAUTH2;
				break;
		}
	}
	else
	{
		switch (a->Receive.AuthType())
		{
			case 1:
				OpenFlags |= MAIL_USE_PLAIN;
				break;
			case 2:
				OpenFlags |= MAIL_USE_LOGIN;
				break;
			case 3:
				OpenFlags |= MAIL_USE_NTLM;
				break;
			case 4:
				OpenFlags |= MAIL_USE_OAUTH2;
				break;
		}
	}

	return OpenFlags;
}

/*
class LProtocolLogger : public LStreamI
{
	LViewI *Wnd;
	int Msg;
	List<LogEntry> *Log;

public:
	LProtocolLogger(LViewI *wnd, int msg, List<LogEntry> *log)
	{
		Wnd = wnd;
		Msg = msg;
		Log = log;
	}

	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0)
	{
		return -1;
	}
	
	ssize_t Write(const void *b, ssize_t len, int f = 0)
	{
		LogEntry *l = new LogEntry(SocketMsgTypeToColour((GSocketI::SocketMsgType)f));
		if (l)
		{
			l->Add((const char*)b, len);
            Wnd->PostEvent(Msg, (LMessage::Param)Log, (LMessage::Param)l);
		}
		return len;
	}
};
*/

void MakeTempPath(char *Path, int PathSize)
{
	// Create unique temporary filename
	char r[32];

	do
	{
		sprintf_s(r, sizeof(r), "%X.eml", LRand());
		LMakePath(Path, PathSize, ScribeTempPath(), r);
	}
	while (LFileExists(Path));
}

const char *AccountThreadStateName(AccountThreadState i)
{
	switch (i)
	{
		case ThreadIdle: return "ThreadIdle";
		case ThreadSetup: return "ThreadSetup";
		case ThreadConnecting: return "ThreadConnecting";
		case ThreadTransfer: return "ThreadTransfer";
		case ThreadWaiting: return "ThreadWaiting";
		case ThreadDeleting: return "ThreadDeleting";
		case ThreadDone: return "ThreadDone";
		case ThreadCancel: return "ThreadCancel";
		case ThreadError: return "ThreadError";
	}

	return "(none)";
}

const char *ReceiveActionName(ReceiveAction i)
{
	switch (i)
	{
		case MailNoop:				return "MailNoop";
		case MailDelete:			return "MailDelete";
		case MailDownloadAndDelete:	return "MailDownloadAndDelete";
		case MailDownload:			return "MailDownload";
		case MailUpload:			return "MailUpload";
		case MailHeaders:			return "MailHeaders";
		default:					break;
	}

	return "(error)";
}

const char *ReceiveStatusName(ReceiveStatus i)
{
	switch (i)
	{
		case MailReceivedNone:		return "ReceiveNone";
		case MailReceivedWaiting:	return "ReceiveWaiting";
		case MailReceivedOk:		return "ReceiveOk";
		case MailReceivedError:		return "ReceiveError";
		default:					break;
	}
	
	return "(none)";
}


bool Accountlet::WaitForTransfers(List<MailTransferEvent> &Files)
{
	LArray<int> Counts;
	auto StartTs = LCurrentTime();
	do
	{
		Counts.Length(0);

		for (auto e: Files)
			Counts[e->Status]++;

		LSleep(100);
		
		if (LCurrentTime() - StartTs > TRANSFER_WAIT_TIMEOUT)
		{
			LgiTrace("%s:%i - WaitForTransfers timed out.\n", _FL);
			for (ReceiveStatus rs = MailReceivedNone; rs < MailReceivedMax; rs = (ReceiveStatus)(((int)rs)+1))
				LgiTrace("...%s=%i\n", ReceiveStatusName(rs), Counts[rs]);
			return false;
		}
		else if (Thread && Thread->IsCancelled())
		{
			LgiTrace("%s:%i - WaitForTransfers exiting because thread is cancelled.\n", _FL);
			return false;
		}
	}
	while (Counts[MailReceivedWaiting] > 0);
	
	return true;
}

////////////////////////////////////////////////////////////////////////////
char *NewStrCat(char *a, char *b)
{
	size_t Len = ((a)?strlen(a):0) + ((b)?strlen(b):0);
	char *s = new char[Len+1];
	if (s)
	{
		s[0] = 0;
		if (a) strcat(s, a);
		if (b) strcat(s, b);
	}
	DeleteArray(a);
	return s;
}

class AccountletThread : public AccountThread
{
	friend class SendAccountlet;
	friend class ReceiveAccountlet;
	friend bool AfterReceived(MailTransaction *Trans, void *Data);

	List<MailTransferEvent> Files;

	void MakeTempPath(char *Buf);
	void SetState(AccountThreadState s);
	AccountThreadState GetState();

public:
	// Object - Api
	AccountletThread(Accountlet *acc, List<char> *Input);
	~AccountletThread();

	// Methods
	void Complete();		// Called by the app when the all the files have been processed
	
	// Thread
	int Main();
};

////////////////////////////////////////////////////////////////////////////
AccountletThread::AccountletThread(Accountlet *acc, List<char> *Input) :
	AccountThread(acc)
{
}

AccountletThread::~AccountletThread()
{
	Files.DeleteObjects();
}

void AccountletThread::SetState(AccountThreadState s)
{
	Acc->State = s;
}

AccountThreadState AccountletThread::GetState()
{
	return Acc->GetState();
}

void AccountletThread::Complete()
{
	for (auto f: Files)
	{
		f->Rfc822Msg.Reset();
	}
	Acc->State = ThreadDeleting;
}

int AccountletThread::Main()
{
	// We're running...
	SetState(ThreadSetup);

	// Do all the connect, transfer and clean up
	Acc->Main(this);

	// This tells the app we're all done
	SetState(ThreadIdle);
	
	return 0;
}

////////////////////////////////////////////////////////////////////////////
LList *MailTransferEvent::GetList()
{	
	if (!Account)
	{
		LAssert(!"No account.");
		return NULL;
	}

	// As the list is set in the GUI thread, we should only ever use it in the
	// GUI thread... so check for that here. Otherwise we need to lock it while it's
	// in use. Which is not currently done.
	#ifdef _DEBUG
	ScribeWnd *App = Account->GetApp();
	#endif
	LAssert(App->InThread());

	ReceiveAccountlet *ra = dynamic_cast<ReceiveAccountlet*>(Account);
	return ra ? ra->GetItems() : NULL;
}

////////////////////////////////////////////////////////////////////////////
Accountlet::Accountlet(ScribeAccount *a) : PrivLock("Accountlet")
{
	Account = a;
	State = ThreadIdle;

	// Update sig to new format...
	auto opt = OptionName("AccSig");
	LVariant Old;
	if (GetApp()->GetOptions()->GetValue(opt, Old))
	{
		if (LFileExists(Old.Str()))
		{
			auto Xml = LReadFile(Old.Str());
			if (Xml)
				GetAccount()->Identity.TextSig(Xml);
		}
		
		GetApp()->GetOptions()->DeleteValue(opt);
	}
}

Accountlet::~Accountlet()
{
	I Lck = Lock(_FL);
	if (Lck)
	{
		Lck->d->Log.DeleteObjects();
		Lck.Reset();
	}
	DeleteObj(Root);
	DeleteObj(DataStore);
}

void Accountlet::OnBeforeDelete()
{
	// This has to be before ~ScribeAccount is finished, so that the
	// account's index is still valid, if we are going to save the "Expanded"
	// setting.
	LString p;
	if (DataStore && Root)
	{
		p = Root->GetPath();
		bool e = Root->Expanded();
		Expanded(e);
	}
}

bool Accountlet::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	auto k = OptionName(Name);
	bool mapped = Account->IsMapped(k);
	auto Opts = GetApp()->GetOptions();
	Value.Empty();
	if (mapped)
		Opts->GetValue(k, Value);

	return true;
}

bool Accountlet::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	auto k = OptionName(Name);
	bool mapped = Account->IsMapped(k);
	auto Opts = GetApp()->GetOptions();
	if (mapped)
		Opts->SetValue(k, Value);

	return true;
}

void Accountlet::StrOption(const char *Opt, LVariant &v, const char *Set)
{
	auto Key = OptionName(Opt);
	if (Set)
		GetApp()->GetOptions()->SetValue(Key, v = Set);
	else
		GetApp()->GetOptions()->GetValue(Key, v);
}

void Accountlet::IntOption(const char *Opt, LVariant &v, int Set)
{
	auto Key = OptionName(Opt);
	if (Set >= 0)
		GetApp()->GetOptions()->SetValue(Key, v = Set);
	else
		GetApp()->GetOptions()->GetValue(Key, v);
}

const char *Accountlet::GetStateName()
{
	switch (State)
	{
		case ThreadIdle: return LLoadString(IDS_ACCSTAT_IDLE);
		case ThreadSetup: return LLoadString(IDS_ACCSTAT_SETUP);
		case ThreadConnecting: return LLoadString(IDS_ACCSTAT_CONNECT);
		case ThreadTransfer: return LLoadString(IDS_ACCSTAT_TRANS);
		case ThreadWaiting: return LLoadString(IDS_ACCSTAT_WAIT);
		case ThreadDeleting: return LLoadString(IDS_ACCSTAT_DELETING);
		case ThreadDone: return LLoadString(IDS_ACCSTAT_FINISHED);
		case ThreadCancel: return LLoadString(IDS_ACCSTAT_CANCELLED);
		case ThreadError: return LLoadString(IDS_ERROR);
	}

	return "(none)";
}

void Accountlet::OnThreadDone()
{
	Thread.Reset();

	if (DataStore && Root)
		Expanded(Root->Expanded());

	DeleteObj(Root);
}

ScribeWnd *Accountlet::GetApp()
{
	LAssert(Account->Parent != NULL);
	return Account->Parent;
}

LSocketI *Accountlet::CreateSocket(bool Sending, LCapabilityClient *Caps, bool RawLFCheck)
{
	LSocketI *Socket = 0;
	LVariant File;
	LVariant Socks5Proxy;
	LVariant UseSocks;
	LVariant Format = (int)NET_LOG_NONE;

	GetApp()->GetOptions()->GetValue(OPT_LogFile, File);
	GetApp()->GetOptions()->GetValue(OPT_LogFormat, Format);

	int SslMode = UseSSL();
	if (SslMode > 0)
	{
		Socket = new SslSocket(this, Caps, SslMode == SSL_DIRECT, RawLFCheck);
	}
	else if (	GetApp()->GetOptions()->GetValue(OPT_UseSocks, UseSocks) &&
				UseSocks.CastInt32() &&
				GetApp()->GetOptions()->GetValue(OPT_Socks5Server, Socks5Proxy) &&
				ValidStr(Socks5Proxy.Str()))
	{
		LVariant Socks5UserName;
		LVariant Socks5Password;
		GetApp()->GetOptions()->GetValue(OPT_Socks5UserName, Socks5UserName);
		GetApp()->GetOptions()->GetValue(OPT_Socks5Password, Socks5Password);
		LUri Server(Socks5Proxy.Str());

		Socket = new ILogSocks5Connection(	Server.sHost,
											Server.Port?Server.Port:1080,
											Socks5UserName.Str(),
											Socks5Password.Str(),
											this);
	}
	else
	{
		Socket = new ILogConnection(this);
	}

	if (Socket)
	{
		Socket->SetTimeout(DEFAULT_SOCKET_TIMEOUT);

		if (File.Str())
		{
			Socket->SetValue(OPT_LogFile, File);
			Socket->SetValue(OPT_LogFormat, Format);
		}

		Socket->SetCancel(Thread);
	}

	return Socket;
}

void Accountlet::Disconnect()
{
	if (Thread)
	{
		Thread->Disconnect();
	}
	else
	{
		if (MailStore)
			Account->Parent->OnMailStore(&MailStore, false);
		if (Root)
		{
			if (DataStore && Root)
				Expanded(Root->Expanded());
			Root->Remove();
			DeleteObj(Root);
		}
		DeleteObj(DataStore);
	}
}

void Accountlet::Kill()
{
	if (Thread)
	{
		Thread->Kill();
	}
}

bool Accountlet::Lock()
{
	return Account->Parent->Lock(_FL);
}

void Accountlet::Unlock()
{
	Account->Parent->Unlock();
}

ssize_t Accountlet::Write(const void *buf, ssize_t size, int flags)
{
	if (!Account->GetApp())
		return 0;

	LColour col = SocketMsgTypeToColour((LSocketI::SocketMsgType)flags);
	I Lck = Lock(_FL);
	if (Lck)
	{
		bool Match = false;
		LArray<LogEntry*> &Log = Lck->d->Log;
		if (Log.Length() > 0)
			Match = Log.Last()->GetColour() == col;

		if (Match)
		{
			Log.Last()->Add((const char*)buf, size);
		}
		else
		{
			LogEntry *le = new LogEntry(col);
			if (le)
			{
				le->Add((const char*)buf, size);
				Log.Add(le);
			}
		}
	}

	return size;
}

void Accountlet::OnOnlineChange(bool Online)
{
	if (DataStore && Root && Expanded())
	{
		// uint64 s = LCurrentTime();
		Root->Expanded(true);
		// uint64 e = LCurrentTime();
		// LgiTrace("Expanded took: %i\n", (int)(e-s));
	}
}

bool Accountlet::Connect(LView *p, bool quiet)
{
	bool Status = false;

	I Lck = Lock(_FL);
	if (Lck)
	{
		Lck->d->Log.DeleteObjects();
		Lck.Reset();
	}

	if (Lock())
	{
		Parent = p;
		Quiet = quiet;
		if (!Thread)
		{
			ReceiveAccountlet *Receive = NULL;
			if (IsReceive() &&
				(Receive = &GetAccount()->Receive) &&
				Receive->IsPersistant())
			{
				auto Protocol = GetAccount()->Receive.ProtocolType();
				auto Wnd = GetAccount()->Parent;

				#ifdef ImapSupport
				LString HostName = Server().Str();
				
				if (!DataStore &&
					(Protocol == ProtocolImapFull || Protocol == ProtocolGoogle) &&
					ValidStr(HostName))
				{
					char Password[256] = "";
					LPassword p;
					if (GetPassword(&p))
					{
						p.Get(Password);
					}

					int OpenFlags = MakeOpenFlags(Account, false);
					MailProtocolProgress *Prog[2] = { &Group, &Item };
					LAutoPtr<ProtocolSettingStore> Store;
					Store.Reset(new ProtocolSettingStore(Wnd->GetOptions(), OptionName(NULL)));
					
					DataStore = OpenImap(HostName,
										Receive->Port(),
										UserName().Str(),
										Password,
										OpenFlags,
										Account->GetApp(),
										GetAccount(),
										Prog,
										this,
										Id(),
										Store);
					if (DataStore)
					{
   						DeleteObj(Root);
    					if ((Root = new ScribeFolder))
    					{
    						Root->App = Account->GetApp();
    						Root->SetLoadOnDemand();

						    LDataFolderI *r = DataStore->GetRoot();
							if (r)
    							Root->SetObject(r, false, _FL);

    						Wnd->Tree->Insert(Root);
    					}
					}
				}
				#endif

				#ifdef WIN32
				LString profile = Server().Str();
				if (!DataStore &&
					Protocol == ProtocolMapi)
				{
					char Password[256] = "";
					LPassword p;
					if (GetPassword(&p))
						p.Get(Password);

					DataStore = OpenMapiStore(	profile,
												UserName().Str(),
												Password,
												Account->Receive.Id(),
												Account->GetApp());
					if (DataStore)
					{
					    if (auto r = DataStore->GetRoot())
					    {					    
    						DeleteObj(Root);
    						if ((Root = new ScribeFolder))
    						{
    							Root->App = Account->GetApp();
    							Root->SetLoadOnDemand();
    							Root->SetObject(r, false,_FL);
    							Wnd->Tree->Insert(Root);
    						}
    					}
    					else LgiTrace("%s:%i - Failed to get data store root.\n", _FL);
					}
				}
				#endif
			}
			else
			{
				// Setup
				Enabled(false);

				I Lck = Lock(_FL);
				if (Lck)
				{
					Lck->d->Log.DeleteObjects();
					Lck.Reset();
				}

				auto StartThread = [this]()
				{
					if (Thread.Reset(new AccountletThread(this, 0)))
					{
						Thread->Run();
						Account->Parent->OnBeforeConnect(Account, IsReceive());
					}
				};

				// Do we need the password?
				LString Password;
				LPassword Psw;
				GetPassword(&Psw);
				Password = Psw.Get();
				auto User = UserName();

				if (User.Str() && !ValidStr(Password))
				{
					auto RemoteHost = Server();

					LString Msg;
					Msg.Printf(LLoadString(IDS_ASK_ACCOUNT_PASSWORD), User.Str(), RemoteHost.Str());
					GetApp()->GetUserInput(	Parent ? Parent : GetApp(),
											Msg,
											true,
											[this, StartThread](auto Psw)
											{
												this->TempPsw = Psw;
												StartThread();
											});
				}
				else StartThread();		
			}
		}

		Unlock();
	}

	return Status;
}

void Accountlet::IsCancelled(bool b)
{
	if (Thread)
		Thread->Cancel(b);
}

bool Accountlet::IsCancelled()
{
	return Thread ? Thread->IsCancelled() : false;
}

LString Accountlet::OptionName(const char *Opt, ssize_t Index)
{
	LString opt;
	auto Idx = Index >= 0 ? Index : Account->GetIndex();
	LAssert(Idx >= 0);
	if (Opt)
		opt.Printf("Accounts.Account-" LPrintfSSizeT ".%s", Idx, Opt);
	else
		opt.Printf("Accounts.Account-" LPrintfSSizeT, Idx);
	return opt;
}

bool Accountlet::GetPassword(LPassword *p)
{
	bool Status = false;

	if (p &&
		OptPassword &&
		Lock())
	{
		Status = p->Serialize(Account->GetApp()->GetOptions(), OptionName(OptPassword), false);
		Unlock();
	}

	return Status;
}

void Accountlet::SetPassword(LPassword *p)
{
	if (OptPassword &&
		Lock())
	{
		auto Name = OptionName(OptPassword);
		if (p)
			p->Serialize(Account->GetApp()->GetOptions(), Name, true);
		else
			Account->GetApp()->GetOptions()->DeleteValue(Name);
		Unlock();
	}
}

bool Accountlet::IsCheckDialup()
{
	LVariant CheckDialUp;
	Account->GetApp()->GetOptions()->GetValue(OptionName(OPT_CheckForDialUp), CheckDialUp);
	return CheckDialUp.CastInt32() != 0;
}

////////////////////////////////////////////////////////////////////////////
AccountIdentity::AccountIdentity(ScribeAccount *a) : Accountlet(a)
{
}

bool AccountIdentity::IsValid()
{
	return	ValidStr(Email().Str()) &&
			ValidStr(Name().Str());
}

void AccountIdentity::CreateMaps()
{
	Account->Map(OptionName(OPT_AccIdentName), IDC_NAME, GV_STRING);
	Account->Map(OptionName(OPT_AccIdentEmail), IDC_EMAIL, GV_STRING);
	Account->Map(OptionName(OPT_AccIdentReply), IDC_REPLY_TO, GV_STRING);
	Account->Map(OptionName(OPT_AccIdentTextSig), IDC_SIG, GV_STRING);
	Account->Map(OptionName(OPT_AccIdentHtmlSig), IDC_SIG_HTML, GV_STRING);
}

bool AccountIdentity::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	/*
	case SdName:			// Type: String
	case SdEmail:			// Type: String
	case SdReply:			// Type: String
	case SdSig:				// Type: String
	case SdHtmlSig:			// Type: String
	*/
	char n[128];
	sprintf_s(n, sizeof(n), "Identity.%s", Name);
	return Accountlet::GetVariant(n, Value, Array);
}

bool AccountIdentity::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	char n[128];
	sprintf_s(n, sizeof(n), "Identity.%s", Name);
	return Accountlet::SetVariant(n, Value, Array);
}

////////////////////////////////////////////////////////////////////////////
SendAccountlet::SendAccountlet(ScribeAccount *a) : Accountlet(a)
{
	SendItem = 0;

	// Options
	OptPassword = OPT_EncryptedSmtpPassword;
}

SendAccountlet::~SendAccountlet()
{
}

bool SendAccountlet::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	/*
	case SdServer:		// Type: String
	case SdPort:		// Type: Integer
	case SdDomain:		// Type: String
	case SdName:		// Type: String
	case SdAuth:		// Type: Bool
	case SdAuthType:	// Type: Integer
	case SdPrefCharset1: // Type: String
	case SdPrefCharset2: // Type: String
	case SdOnlySendThis: // Type: Bool
	case SdSSL:			// Type: Integer
	*/

	char n[128];
	sprintf_s(n, sizeof(n), "Send.%s", Name);
	return Accountlet::GetVariant(n, Value, Array);
}

bool SendAccountlet::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	char n[128];
	sprintf_s(n, sizeof(n), "Send.%s", Name);
	return Accountlet::SetVariant(n, Value, Array);
}

void SendAccountlet::CreateMaps()
{
	// Fields
	Account->Map(OptionName(OPT_SmtpServer), IDC_SMTP_SERVER, GV_STRING);
	Account->Map(OptionName(OPT_SmtpPort), IDC_SMTP_PORT, GV_INT32);
	Account->Map(OptionName(OPT_SmtpDomain), IDC_SMTP_DOMAIN, GV_STRING);
	Account->Map(OptionName(OPT_SmtpName), IDC_SMTP_NAME, GV_STRING);
	Account->Map(OptionName(OPT_SmtpAuth), IDC_SMTP_AUTH, GV_BOOL);
	Account->Map(OptionName(OPT_SmtpAuthType), IDC_SEND_AUTH_TYPE, GV_INT32);
	Account->Map(OptionName(OPT_SendCharset1), IDC_SEND_CHARSET1, GV_STRING);
	Account->Map(OptionName(OPT_SendCharset2), IDC_SEND_CHARSET2, GV_STRING);
	Account->Map(OptionName(OPT_OnlySendThroughThis), IDC_ONLY_SEND_THIS, GV_BOOL);
	Account->Map(OptionName(OPT_SmtpSSL), IDC_SEND_SSL, GV_INT32);
}

ScribeAccountletStatusIcon SendAccountlet::GetStatusIcon()
{
	return IsOnline() ? STATUS_ONLINE : STATUS_OFFLINE;
}

bool SendAccountlet::InitMenus()
{
	// Menus
	LVariant n = Name();
	LVariant s = Server();
	LVariant u = UserName();
	if (s.Str())
	{
		char Name[256];

		// Base name of menu
		if (n.Str())
		{
			strcpy_s(Name, sizeof(Name), n.Str());
		}
		else if (u.Str())
		{
			sprintf_s(Name, sizeof(Name), "%s@%s", u.Str(), s.Str());
		}
		else
		{
			strcpy_s(Name, sizeof(Name), s.Str());
		}

		// Add send menuitem
		if (GetApp()->SendMenu)
		{
			LVariant Default;
			const char *ShortCut = nullptr;
			GetApp()->GetOptions()->GetValue(OPT_DefaultSendAccount, Default);
			if (Default.CastInt32() == Account->GetIndex())
			{
				ShortCut = "Ctrl+S";
			}

			SendItem = GetApp()->SendMenu->AppendItem(Name, IDM_SEND_FROM+(int)Account->GetIndex(), true, -1, ShortCut);
		}

		return true;
	}

	return false;
}

void SendAccountlet::Enabled(bool b)
{
	if (Account->GetIndex() == 0)
	{
		GetApp()->CmdSend.Enabled(b);
	}
	else if (SendItem)
	{
		SendItem->Enabled(b);
	}
}

void SendAccountlet::Main(AccountletThread *Thread)
{
	bool Status = false;
	bool MissingConfig = false;
	LStringPipe Err;

	if (!GetApp())
	{
		err.Set(LErrorInvalidParam, "no app.");
		Thread->SetState(ThreadError);
	}
	else
	{
		LVariant v;
		GetApp()->GetOptions()->GetValue(OPT_DebugTrace, v);
		int DebugTrace = v.CastInt32();

		LastOnline = LCurrentTime();

		{
			MailSink *Sink;
			// LProtocolLogger Logger(GetApp(), M_SCRIBE_LOG_MSG, &Log);

			LVariant SendHotFolder = HotFolder();
			if (SendHotFolder.Str() && LDirExists(SendHotFolder.Str()))
			{
				Client = Sink = new MailSendFolder(SendHotFolder.Str());
			}
			else
			{
				Client = Sink = new MailSmtp;
			}

			if (Client)
			{
				LVariant s;
				if (GetApp()->GetOptions()->GetValue(OPT_ExtraHeaders, s))
				{
					Client->ExtraOutgoingHeaders = s.Str();
				}

				s = PrefCharset1();
				if (s.Str())
				{
					Sink->CharsetPrefs.Add(s.Str());
				}
				
				s = PrefCharset2();
				if (s.Str())
				{
					Sink->CharsetPrefs.Add(s.Str());
				}
			}

			if (Outbox.Length())
			{
				int n = 0;
				
				for (unsigned Idx=0; Idx<Outbox.Length(); Idx++)
				{
					ScribeEnvelope *m = Outbox[Idx]; 
					char Buf[256];
					MakeTempPath(Buf, sizeof(Buf));
					MailTransferEvent *t = new MailTransferEvent;
					if (t)
					{
						t->Action = MailUpload;
						t->Index = n++;
						t->Send = m;

						Thread->Files.Insert(t);
					}
				}
				
if (DebugTrace) LgiTrace("Send(%i) got %i mail to send\n", Account->GetIndex(), Thread->Files.Length());

				if (Sink &&
					Thread->Files.Length())
				{
					Sink->Logger = this;
					Sink->Transfer = &Item;

					if (Client)
					{
						LVariant _HotFld = HotFolder();
						LVariant _Server = Server();
						LVariant _Domain = Domain();
						LVariant _UserName = UserName();
						LString _Password;
						int Sent = 0;
						// int Total = Outbox.Length();
						LVariant HideId = false;
						auto Opts = GetApp()->GetOptions();
						Opts->GetValue(OPT_HideId, HideId);
						if (!HideId.CastInt32())
						{
							Opts->GetValue(OPT_HideId, HideId);
							Client->ProgramName = GetFullAppName(!HideId.CastInt32());
						}
						Client->SetSettingStore(Opts);

						if (ValidStr(_HotFld.Str()) ||
							ValidStr(_Server.Str()))
						{
							bool Error = false;

							if (RequireAuthentication())
							{
								LPassword p;
								if (GetPassword(&p))
									_Password = p.Get();
							}

if (DebugTrace) LgiTrace("Send(%i) connecting to SMTP server\n", Account->GetIndex());

							Thread->SetState(ThreadConnecting);
							
							int OpenFlags = MakeOpenFlags(Account, true);
							auto Params = GetOAuth2Params(_Server.Str(), MAGIC_MAIL);
							if (Params.IsValid())
							{
								Sink->SetOAuthParams(Params);
							}
							
							if (!Sink->Open(CreateSocket(true, GetAccount(), true),
											_Server.Str(),
											_Domain.Str(),
											_UserName.Str(),
											_Password,
											Port(),
											OpenFlags))
							{
if (DebugTrace) LgiTrace("Send(%i) %s:%i\n", Account->GetIndex(), _FL);
								Err.Push(LLoadString(IDS_NO_CONNECTION_TO_SERVER));
								Err.Push("\n");
							}
							else
							{
if (DebugTrace) LgiTrace("Send(%i) connected\n", Account->GetIndex());

								Group.Value = 0;
								Group.Range = Thread->Files.Length();

								Thread->SetState(ThreadTransfer);
								
								MailTransferEvent *e = NULL;
								for (auto FileIt = Thread->Files.begin();
									FileIt != Thread->Files.end() &&
										(e = dynamic_cast<MailTransferEvent*>(*FileIt)) &&
										!Thread->IsCancelled();
									FileIt++, Group.Value++)
								{
if (DebugTrace) LgiTrace("Send(%i) Item(%i) starting send\n", Account->GetIndex(), Group.Value);
									
									AddressDescriptor From;
									From.sAddr = e->Send->From;

									List<AddressDescriptor> ToLst;
									for (unsigned n=0; n<e->Send->To.Length(); n++)
									{
										AddressDescriptor *ad = new AddressDescriptor;
										ad->sAddr = e->Send->To[n];
										ToLst.Insert(ad);
									}
									
									MailProtocolError SendErr;
									LStringPipe *Out = Sink->SendStart(ToLst, &From, &SendErr);
									List<AddressDescriptor> *To = &ToLst;

									bool BadAddress = false;
									for (auto a: *To)
									{
										if (!a->Status)
										{
											if (a->sAddr.Find(",") >= 0)
											{
												auto t = a->sAddr.SplitDelimit(",");
												for (unsigned i=0; i<t.Length(); i++)
													Err.Print("%s\n", t[i].Get());
											}
											else
											{
												Err.Print("%s\n", a->Print().Get());
											}
											BadAddress = true;
										}
									}
									if (BadAddress)
									{
										LString m;
										m.Printf(LLoadString(Sink->ErrMsgId, Sink->ErrMsgFmt), Sink->ErrMsgParam.Get());
										Err.Push(m.Get());
									}

									if (Out)
									{
										LStringPipe InetHeaders;
										ssize_t Len = e->Send->Rfc822.Length();
										ssize_t Written = 0;
										
										Item.Range = Len;
										Item.Start = LCurrentTime();
										
										for (ssize_t Pos = 0; Pos < Len; )
										{
											ssize_t Remaining = Len - Pos;
											ssize_t Block = MIN(Remaining, 4 << 10);
											ssize_t Wr = Out->Write(e->Send->Rfc822.Get() + Pos, Block);
											if (Wr <= 0)
												break;
											
											Pos += Wr;
											Written += Wr;
											Item.Value = Pos;
										}
										
										Item.Start = 0;
										
										if (Written == Len)
										{
											if (Sink->SendEnd(Out))
											{
if (DebugTrace) LgiTrace("Send(%i) Item(%i) success\n", Account->GetIndex(), Group.Value);
												Status = true;
												Sent++;
												e->OutgoingHeaders = InetHeaders.NewStr();
												
												if (e->Send->References)
												{
													GetApp()->PostEvent(M_SCRIBE_SET_MSG_FLAG,
																		(LMessage::Param)NewStr(e->Send->References),
																		MAIL_REPLIED);
												}
												if (e->Send->FwdMsgId)
												{
													GetApp()->PostEvent(M_SCRIBE_SET_MSG_FLAG,
																		(LMessage::Param)NewStr(e->Send->FwdMsgId),
																		MAIL_FORWARDED);
												}
												if (e->Send->BounceMsgId)
												{
													GetApp()->PostEvent(M_SCRIBE_SET_MSG_FLAG,
																		(LMessage::Param)NewStr(e->Send->BounceMsgId),
																		MAIL_BOUNCED);
												}

												e->Account = this;
												e->Status = MailReceivedWaiting;
												GetApp()->OnMailTransferEvent(e);
											}
											else
											{
												Write("SendEnd failed.\n", -1, LSocketI::SocketMsgError);
												Error = true;
												break;
											}
										}
										else
										{
											Write("Encoding failed.\n", -1, LSocketI::SocketMsgError);
											Error = true;
											break;
										}
									}
									else
									{
										LString s;
										s.Printf("SendStart failed: %i, %s\n", SendErr.Code, SendErr.ErrMsg.Get());
										Write(s, -1, LSocketI::SocketMsgError);
										Error = true;
										break;
									}
								}

if (DebugTrace) LgiTrace("Send(%i) closing, Error=%i\n", Account->GetIndex(), Error);
								if (Error)
								{
									// we can't call Client->Close() on error because it
									// sends a quit command and expects a reply. Under
									// error conditions that can fail because the server
									// might not be in a state to handle that request.
									// So just close the socket and call it a day.
if (DebugTrace) LgiTrace("Send(%i) %s:%i\n", Account->GetIndex(), _FL);
									Sink->CloseSocket();
								}
								else
								{
									// Normal quit
if (DebugTrace) LgiTrace("Send(%i) %s:%i\n", Account->GetIndex(), _FL);
									Sink->Close();
								}

if (DebugTrace) LgiTrace("Send(%i) %s:%i\n", Account->GetIndex(), _FL);
								Group.Value = 0;
								Group.Range = 0;
								Item.Value = 0;
								Item.Range = 0;

								Status &= WaitForTransfers(Thread->Files);
							}

if (DebugTrace) LgiTrace("Send(%i) %s:%i, status=%i\n", Account->GetIndex(), _FL, Status);
							if (!Status)
							{
if (DebugTrace) LgiTrace("Send(%i) %s:%i, Client=%p\n", Account->GetIndex(), _FL, Client);
								if (ValidStr(Client->ErrMsgFmt))
								{
									LString m;
									m.Printf(LLoadString(Sink->ErrMsgId, Sink->ErrMsgFmt), Sink->ErrMsgParam.Get());

									Err.Push("\n");
									Err.Push(m.Get());
									Err.Push("\n");
								}
							}
						}
						else
						{
							MissingConfig = true;
							Err.Push(LLoadString(IDS_NO_SMTP_SERVER_SETTINGS));
							Err.Push("\n");
						}
					}

if (DebugTrace) LgiTrace("Send(%i) %s:%i, Thread=%p\n", Account->GetIndex(), _FL, Thread);
if (DebugTrace) LgiTrace("Send(%i) %s:%i\n", Account->GetIndex(), _FL);
				}

				Outbox.DeleteObjects();
			}

			DeleteObj(Sink);
		}

if (DebugTrace) LgiTrace("Send(%i) exit\n", Account->GetIndex());
	}

	char *e = 0;
	if (MissingConfig)
	{
		Err.Push(LLoadString(IDS_ASK_ABOUT_OPTIONS));
		Err.Push("\n");
		e = Err.NewStr();
		GetApp()->PostEvent(M_SCRIBE_MSG, (LMessage::Param)e, 1);
	}
	else if ((e = Err.NewStr()))
	{
		GetApp()->PostEvent(M_SCRIBE_MSG, (LMessage::Param)e);
	}
}

////////////////////////////////////////////////////////////////////////////
ReceiveAccountlet::ReceiveAccountlet(ScribeAccount *a) : Accountlet(a)
{
	// Init
	SecondsTillOnline = -1;

	LVariant Ct = CheckTimeout();
	if (Ct.Str())
	{
		SecondsTillOnline = GetCheckTimeout();
	}

	ReceiveItem = 0;
	PreviewItem = 0;
	Items = 0;
	IdTemp = 0;

	Msgs.Reset(new MsgList(a->GetApp()->GetOptions(), OptionName("Messages")));
	Spam.Reset(new MsgList(a->GetApp()->GetOptions(), OptionName("Spam")));

	// Upgrade props
	LVariant OldType = -1;
	LOptionsFile *Options = GetApp()->GetOptions();
	auto typeOpt = OptionName(OPT_Pop3Type);
	if (Options->GetValue(typeOpt, OldType))
	{
		// Old Account types: 
		#define MAIL_SOURCE_POP3			0
		#define MAIL_SOURCE_IMAP4			1
		#define MAIL_SOURCE_MAPI			2
		#define MAIL_SOURCE_SCP				3
		#define MAIL_SOURCE_HTTP			4

		switch (OldType.CastInt32())
		{
			case MAIL_SOURCE_POP3:
				Protocol(PROTOCOL_POP3); break;
			case MAIL_SOURCE_IMAP4:
				Protocol(PROTOCOL_IMAP4_FETCH); break;
			case MAIL_SOURCE_SCP:
				Protocol(PROTOCOL_CALENDAR); break;
			case MAIL_SOURCE_HTTP:
				Protocol(PROTOCOL_POP_OVER_HTTP); break;
			case MAIL_SOURCE_MAPI:
				Protocol(PROTOCOL_MAPI); break;
		}
		
		Options->DeleteValue(typeOpt);
	}

	LVariant Auto;
	if (!Options->GetValue(OptionName(OPT_Pop3AutoReceive), Auto))
	{
		LVariant s;
		Options->GetValue(OptionName(OPT_Pop3CheckEvery), s);
		if (s.Str())
		{
			int Timeout = atoi(s.Str());
			Options->SetValue(OptionName(OPT_Pop3AutoReceive), s = (Timeout > 0));
		}
	}


	// Options
	OptPassword = OPT_EncryptedPop3Password;
}

ReceiveAccountlet::~ReceiveAccountlet()
{
}

bool ReceiveAccountlet::GetVariant(const char *Name, LVariant &Value, const char *Array)
{	
	char n[128];
	sprintf_s(n, sizeof(n), "Receive.%s", Name);
	return Accountlet::GetVariant(n, Value, Array);
}

bool ReceiveAccountlet::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	char n[128];
	sprintf_s(n, sizeof(n), "Receive.%s", Name);
	return Accountlet::SetVariant(n, Value, Array);
}

ScribeAccountletStatusIcon ReceiveAccountlet::GetStatusIcon()
{
	if (DataStore)
	{
		int64 s = DataStore->GetInt(FIELD_STORE_STATUS);
		if (s >= STATUS_OFFLINE && s < STATUS_MAX)
			return (ScribeAccountletStatusIcon)s;
	}

	if (IsOnline())
		return STATUS_ONLINE;

	if (!GetStatus())
		return STATUS_ERROR;

	if (AutoReceive())
		return STATUS_WAIT;

	return STATUS_OFFLINE;
}

void ReceiveAccountlet::CreateMaps()
{
	// Fields
	Account->Map(OptionName(OPT_Pop3Protocol),			IDC_REC_TYPE, GV_STRING);
	Account->Map(OptionName(OPT_Pop3Server),			IDC_REC_SERVER, GV_STRING);
	Account->Map(OptionName(OPT_Pop3Port),				IDC_REC_PORT, GV_INT32);
	Account->Map(OptionName(OPT_Pop3Name),				IDC_REC_NAME, GV_STRING);
	Account->Map(OptionName(OPT_Pop3AutoReceive),		IDC_CHECK_EVERY, GV_BOOL);
	Account->Map(OptionName(OPT_Pop3CheckEvery),		IDC_REC_CHECK, GV_STRING);
	
	Account->Map(OptionName(OPT_Pop3LeaveOnServer),		IDC_POP3_LEAVE, GV_BOOL);
	Account->Map(OptionName(OPT_DeleteAfter),			IDC_DELETE_AFTER, GV_BOOL);
	Account->Map(OptionName(OPT_DeleteDays),			IDC_DELETE_DAYS, GV_INT32);
	Account->Map(OptionName(OPT_DeleteIfLarger),		IDC_DELETE_LARGER, GV_BOOL);
	Account->Map(OptionName(OPT_DeleteIfLargerSize),	IDC_DELETE_SIZE, GV_INT32);

	Account->Map(OptionName(OPT_Pop3Folder),			IDC_FOLDER, GV_STRING);
	Account->Map(OptionName(OPT_MaxEmailSize),			IDC_MAX_SIZE, GV_INT32);
	Account->Map(OptionName(OPT_Receive8BitCs),			IDC_REC_8BIT_CS, GV_STRING);
	Account->Map(OptionName(OPT_ReceiveAsciiCs),		IDC_REC_ASCII_CP, GV_STRING);
	Account->Map(OptionName(OPT_ReceiveAuthType),		IDC_RECEIVE_AUTH_TYPE, GV_INT32);
	Account->Map(OptionName(OPT_Pop3SSL),				IDC_RECEIVE_SSL, GV_INT32);
	Account->Map(OptionName(OPT_ReceiveFilterIncoming), IDC_FILTER_INCOMING, GV_BOOL);
}

int ReceiveAccountlet::GetCheckTimeout()
{
	int Sec = -1;
	LVariant Timeout = CheckTimeout();
	if (Timeout.Str())
	{
		auto t = Timeout.LStr().SplitDelimit(":");
		if (t.Length() == 2)
		{
			Sec = (atoi(t[0]) * 60) + atoi(t[1]);
		}
		else
		{
			Sec = atoi(Timeout.Str()) * 60;
		}
	}
	return Sec;
}

bool ReceiveAccountlet::InitMenus()
{
	// Menus
	LVariant n = Name();
	LVariant s = Server();
	LVariant u = UserName();
	if (IsConfigured())
	{
		char Name[256];

		// Base name of menu
		if (n.Str())
			strcpy_s(Name, sizeof(Name), n.Str());
		else if (u.Str() && s.Str())
			sprintf_s(Name, sizeof(Name), "%s@%s", u.Str(), s.Str());
		else if (u.Str())
			strcpy_s(Name, sizeof(Name), u.Str());
		else if (s.Str())
			strcpy_s(Name, sizeof(Name), s.Str());
		else
			strcpy_s(Name, sizeof(Name), "(untitled)");

		// Add preview menuitem
		if (GetApp()->PreviewMenu)
		{
			PreviewItem = GetApp()->PreviewMenu->AppendItem(Name, IDM_PREVIEW_FROM+(int)Account->GetIndex(), !Disabled());
		}

		// Add shortcut
		LString ShortCut;
		if (Account->GetIndex() < 9)
		{
			ShortCut.Printf("Ctrl+" LPrintfSSizeT, Account->GetIndex()+1);
		}

		// Add receive menuitem
		if (GetApp()->ReceiveMenu)
		{
			ReceiveItem = GetApp()->ReceiveMenu->AppendItem(Name, IDM_RECEIVE_FROM+(int)Account->GetIndex(), !Disabled(), -1, ShortCut);
		}

		return true;
	}

	return false;
}

bool ReceiveAccountlet::RemoveFromSpamIds(const char *Id)
{
	if (!Id || !Spam)
		return false;

	if (!Lock())
		return false;

	Spam->Delete(Id);
	Spam->Dirty = true;

	Unlock();

	return true;
}

void ReceiveAccountlet::DeleteAsSpam(const char *Id)
{
	if (HasMsg(Id))
	{
		RemoveMsg(Id);

		if (Lock())
		{
			if (Spam)
			{
				if (!Spam->Find(Id))
				{
					Spam->Add(Id);
					Spam->Dirty = true;
				}
			}

			Unlock();
		}
	}
}

bool ReceiveAccountlet::IsSpamId(const char *Id, bool Delete)
{
	bool Status = false;

	if (ValidStr(Id))
	{
		if (Lock())
		{
			if (Spam)
			{
				Status = Spam->Find(Id);
			}

			Unlock();
		}
	}	

	return Status;
}

bool ReceiveAccountlet::SetActions(LArray<ReceiveAction> *a)
{
	bool Status = false;

	if (Lock())
	{
		if (!IsOnline())
		{
			if (a)
				Actions = *a;
			else
				Actions.Length(0);

			Status = true;
		}
		else LAssert(!"Trying to set actions when online is forbidden.");

		Unlock();
	}

	return Status;
}

bool ReceiveAccountlet::SetItems(LList *l)
{
	bool Status = false;

	if (Lock())
	{
		Items = l;
		Status = true;

		Unlock();
	}

	return Status;
}

void ReceiveAccountlet::Enabled(bool b)
{
	if (Account->GetIndex() == 0)
	{
		GetApp()->CmdReceive.Enabled(b);
		GetApp()->CmdPreview.Enabled(b);
	}

	if (ReceiveItem)
	{
		ReceiveItem->Enabled(b);
	}
	if (PreviewItem)
	{
		PreviewItem->Enabled(b);
	}
}

bool ReceiveAccountlet::IsPersistant()
{
	switch (ProtocolType())
	{
		case ProtocolImapFull:
		case ProtocolGoogle:
		#ifdef WIN32
		case ProtocolMapi:
		#endif
			return true;
	}

	return false;
}

int64 ConvertRelativeSize(char *Size, int64 Total)
{
	int64 Status = -1;
	if (Size)
	{
		double f = atof(Size);
		if (strchr(Size, '%'))
		{
			Status = (int64)(Total * f) / 100;
		}
		else if (stristr(Size, "KB"))
		{
			Status = (int64)(f * 1024);
		}
		else if (stristr(Size, "MB"))
		{
			Status = (int64)(f * 1024 * 1024);
		}
		else if (stristr(Size, "GB"))
		{
			Status = (int64)(f * 1024 * 1024 * 1024);
		}
		else
		{
			Status = (int64)f;
		}
	}

	return Status;
}

struct MailReceiveParams
{
	ScribeWnd *App;
	AccountletThread *Thread;

	uint64 MaxSize;
	uint64 NoAttachLimit;
	uint64 NoDownloadLimit;
	char *KitFileName;
	int DownloadLines;

	MailReceiveParams()
	{
		App = 0;
		Thread = 0;

		MaxSize = 0;
		NoAttachLimit = 0;
		NoDownloadLimit = 0;
		KitFileName = 0;
		DownloadLines = 0;
	}
};

bool AfterReceived(MailTransaction *Trans, void *Data)
{
	if (Trans->Oversize)
	{
	}
	else if (Trans->Status)
	{
		MailReceiveParams *p = (MailReceiveParams*) Data;
		if (!p)
			return false;

		MailTransferEvent *t = p->Thread->Files[Trans->Index];
		if (!t)
			return false;

		Trans->Flags |= MAIL_POSTED_TO_GUI;

		if (t->Rfc822Msg)
		{
			// t->Data->Parse();
		}

		// Set the event status
		t->Status = MailReceivedWaiting;
		t->Account = p->Thread->GetAccountlet();

		// Ask the gui thread to load the mail in
		p->App->OnMailTransferEvent(t);
	}

	return true;
}

MailSrcStatus ReceiveCallback(MailTransaction *Trans, uint64 Size, int *TopLines, void *Data)
{
	MailReceiveParams *Params = (MailReceiveParams*) Data;

	if (Params->KitFileName)
	{
		uint64 Free;
		if (LGetDriveInfo(Params->KitFileName, &Free))
		{
			if (Free <= Params->NoDownloadLimit)
			{
				return DownloadNone;
			}
			else if (Free <= Params->NoAttachLimit)
			{
				if (TopLines && Params->DownloadLines > 0)
				{
					*TopLines = Params->DownloadLines;
				}

				return DownloadTop;
			}
		}
	}

	if (Params->MaxSize)
	{
		if (Size > Params->MaxSize)
		{
			if (!TestFlag(Trans->Flags, MAIL_EXPLICIT))
			{
				return DownloadNone;
			}
		}
	}

	return DownloadAll;
}

bool ReceiveAccountlet::OnIdle()
{
 	return DataStore ? DataStore->OnIdle() : false;
}

void ReceiveAccountlet::Main(AccountletThread *Thread)
{
	LVariant v;

	err.Empty();
	LastOnline = LCurrentTime();
	#define TimeDelta() ((int) (LCurrentTime() - LastOnline))
	SecondsTillOnline = -1;

int DebugTrace = false;
GetApp()->GetOptions()->GetValue(OPT_DebugTrace, v);
DebugTrace = v.CastInt32();
if (DebugTrace) LgiTrace("Receive(%i) starting, %i\n", Account->GetIndex(), TimeDelta());

	auto RemoteHost = Server();
	auto RemotePort = Port();
	auto User = UserName();

	// int LeaveCopy = LeaveOnServer();
	int DeleteIfLargerThan = DeleteLarger() ? DeleteSize() << 10 : 0;

	MailReceiveParams Params;
	Params.App = GetApp();
	Params.Thread = Thread;
	Params.MaxSize = DownloadLimit() << 10;

	auto MailSourceType = ProtocolToEnum(Protocol().Str());

	LString Password;

	LPassword Psw;
	GetPassword(&Psw);
	Password = Psw.Get();

	MailSource *Source = NULL;
	LVariant RecHotFolder = HotFolder();
	if (RecHotFolder.Str() && LDirExists(RecHotFolder.Str()))
	{
		Client = Source = new MailReceiveFolder(RecHotFolder.Str());
	}
	else
	{
		switch (MailSourceType)
		{
			case ProtocolPop3:
			{
				Client = Source = new MailPop3;
				break;
			}
			case ProtocolImapFetch:
			{
				Client = Source = new MailIMap;
				break;
			}
			case ProtocolPopOverHttp:
			{
				Client = Source = new MailPhp;
				break;
			}
			default:
			{
				err.Set(LErrorNotSupported, "Unsupported protocol.");
				return;
			}
		}
	}

if (DebugTrace) LgiTrace("Receive(%i) protocol=%i client=%p, time=%i\n", Account->GetIndex(), MailSourceType, Client, TimeDelta());

	if (!Source)
	{
		err.Set(LErrorNoMem, "alloc source failed.");
		Thread->SetState(ThreadError);
		Actions.Length(0);
		return;
	}

	auto HttpProxy = GetApp()->GetHttpProxy();
	if (HttpProxy)
	{
		LUri Host(HttpProxy);
		if (Host.sHost)
			Source->SetProxy(Host.sHost, Host.Port?Host.Port:80);
	}
	
	// Setup logging
	Source->Logger = this;
	Source->Items = &Group;
	Source->Transfer = &Item;

	auto OpenFlags = MakeOpenFlags(Account, false);
	auto NeedsPassword = !ValidStr(Password) && ValidStr(User.Str());
	if (NeedsPassword)
	{
		if (TempPsw)
			Password = TempPsw;
		else
			LAssert(!"Need to ask user for password BEFORE we're in the worker thread.");
	}

if (DebugTrace) LgiTrace("Receive(%i) opening connection..., time=%i\n", Account->GetIndex(), TimeDelta());
	Thread->SetState(ThreadConnecting);

	LHashTbl<StrKey<char>,bool> Uids;
	ReceiveAccountlet *Receive = dynamic_cast<ReceiveAccountlet*>(Thread->Acc);

	if (!RecHotFolder.Str() && !ValidStr(Password))
	{
		// Status = true;
	}
	else if (!Source->Open(	CreateSocket(false, GetAccount(), false),
							RemoteHost.Str(),
							RemotePort,
							User.Str(),
							Password,
							SettingStore,
							OpenFlags))
	{
		err.Set(LErrorIoFailed, "connection failed.");
		Thread->SetState(ThreadError);
	}
	else
	{
if (DebugTrace) LgiTrace("Receive(%i) connected, time=%i\n", Account->GetIndex(), TimeDelta());
		SecondsTillOnline = -1;

		// Get all the messages..
		Thread->SetState(ThreadTransfer);
		auto Msgs = Source->GetMessages();
		if (Msgs)
		{
			bool LeaveOnServer = Receive->LeaveOnServer() != 0;
			bool DeleteAfter = Receive->DeleteAfter() != 0;
			int DeleteDays = Receive->DeleteDays();
			bool GetUids = LeaveOnServer;
			bool HasGetHeaders = false;

			if (DeleteDays < 1)
			{
				Receive->DeleteDays(DeleteDays = 1);
			}

			for (int i=0; i<Msgs; i++)
			{
				if (auto t = new MailTransferEvent)
				{
					t->Rfc822Msg.Reset(new LTempStream(ScribeTempPath()));
					t->Index = i;

					if (Actions.Length())
					{
						t->Action = (unsigned)i < Actions.Length() ? Actions[i] : MailNoop;
						t->Explicit = true;
					}
					else if (Receive->Items)
					{
						t->Action = MailHeaders;
						GetUids = true;
					}
					else
					{
						t->Action = LeaveOnServer ? MailDownload : MailDownloadAndDelete;
					}

					switch (t->Action)
					{
						default:
							break;
						case MailHeaders:
						{
							HasGetHeaders = true;
							// fall thru
						}
						case MailDownloadAndDelete:
						case MailDownload:
						case MailUpload:
						{
							Group.Range++;
							break;
						}
					}

					Thread->Files.Insert(t);
				}
			}

if (DebugTrace) LgiTrace("Receive(%i) got %i actions, time=%i\n", Account->GetIndex(), Thread->Files.Length(), TimeDelta());

			if (HasGetHeaders || DeleteIfLargerThan)
			{
				LArray<int64_t> Sizes;
				if (Source->GetSizes(Sizes))
				{
					unsigned i = 0;
					for (auto t: Thread->Files)
					{
						if (i >= Sizes.Length())
							break;

						t->Size = Sizes[i];

						if (t->Action == MailHeaders)
						{
							t->Msg = new AccountMessage(Account);
							if (t->Msg)
							{
								t->Msg->Size = t->Size;
							}
						}
						i++;
					}
				}
			}

			// Getting the UID's of the messages on the server
			if (GetUids || Receive->Msgs->Length() > 0)
			{
if (DebugTrace) LgiTrace("Receive(%i) getting UID's, time=%i\n", Account->GetIndex(), TimeDelta());
				LString::Array UidLst;
				Source->GetUidList(UidLst);
				for (auto u: UidLst)
					Uids.Add(u, true);

				// Assign all the ID strings to the transfer events
				LDateTime Now;
				Now.SetNow();
				for (auto t: Thread->Files)
				{
					auto k = UidLst[0];
					if (k)
					{
						UidLst.DeleteAt(0, true);
						t->Uid = k;
if (DebugTrace) LgiTrace("\tUid[%i]='%s' (time=%i)\n", t->Index, t->Uid.Get(), TimeDelta());
					}
					else break;

					if (!t->Explicit && DeleteAfter)
					{
						// Check how long the message has been on the server.
						LDateTime MsgDate;
						if (Receive->Msgs->GetDate(t->Uid, &MsgDate))
						{
							LDateTime Days = Now - MsgDate;
							if (Days.Day() > DeleteDays)
							{
								if (t->Action == MailDownload)
									t->Action = MailDownloadAndDelete;
							}
						}
					}
				}
			}

if (DebugTrace) LgiTrace("Receive(%i) starting main action loop, time=%i\n", Account->GetIndex(), TimeDelta());
				
			Group.Start = LCurrentTime();
			bool Error = false;
			LArray<MailTransaction*> Trans;
			char NotLoaded[256];
			sprintf_s(	NotLoaded, sizeof(NotLoaded),
						"%s: %s",
						LLoadString(FIELD_SUBJECT),
						LLoadString(IDS_NOT_LOADED));	

			for (auto it = Thread->Files.rbegin();
				it != Thread->Files.end() && !Thread->IsCancelled();
				it--)
			{
				MailTransferEvent *t = *it;
				if (!t)
					continue;
						
				bool Ok = false;
				switch (t->Action)
				{
					case MailDownload:
					case MailDownloadAndDelete:
					{
						if (!t->Explicit &&
							t->Uid &&
							Receive->Msgs->Find(t->Uid))
						{
							// Already got it...
							Group.Value++;

							if (DeleteIfLargerThan > 0 &&
								t->Size > 0 &&
								t->Size > DeleteIfLargerThan)
							{
								t->Action = MailDelete;
							}
							continue;
						}
						else
						{
							if (t->Uid && Receive->IsSpamId(t->Uid))
							{
								// Delete the spam
								t->Action = MailDelete;
								Group.Value++;
								continue;
							}
							else
							{
								// Download it
								MailTransaction *Get = new MailTransaction;
								if (Get)
								{
									if (t->Explicit)
										Get->Flags |= MAIL_EXPLICIT;

									Get->Index = t->Index;
									Get->Stream = t->Rfc822Msg;
									Trans.Add(Get);
									continue;										
								}
							}
							Group.Value++;
						}
						break;
					}
					case MailHeaders:
					{
if (DebugTrace) LgiTrace("Receive(%i) Item(%i) Getting headers..., time=%i\n", Account->GetIndex(), t->Index, TimeDelta());
						auto Headers = Source->GetHeaders(t->Index);
if (DebugTrace) LgiTrace("Receive(%i) Item(%i) headers=%p, time=%i\n", Account->GetIndex(), t->Index, Headers.Get(), TimeDelta());

                        if (!Headers)
							Headers = NotLoaded;
							
						if (Headers)
						{
							if (!t->Msg)
								t->Msg = new AccountMessage(Account);
							if (t->Msg)
							{
								t->Msg->Index = t->Index;
								t->Msg->ServerUid = t->Uid;
								t->Msg->From = LDecodeRfc2047(LGetHeaderField(Headers, "From"));
								t->Msg->Subject = LDecodeRfc2047(LGetHeaderField(Headers, "Subject")).Replace("\n");
									
								auto date = LGetHeaderField(Headers, "Date");
								if (date)
									t->Msg->Date.Decode(date);
									
								t->Msg->Attachments = LGetHeaderField(Headers, "Content-Type").Find("multipart/mixed") >= 0;
								if (IsSpamId(t->Uid))
								{
									t->Msg->Download->Value(false);
									t->Msg->Delete->Value(true);
									t->Msg->New = false;
								}
								else
								{
									t->Msg->New = !HasMsg(t->Uid);
								}
									
								Ok = true;
							}
						}

						Group.Value++;
						break;
					}
					default:
					{
						continue;
					}
				}

				if (Ok)
				{
					t->Account = this;
					t->Status = MailReceivedWaiting;							
					GetApp()->OnMailTransferEvent(t);
				}
				else
				{
if (DebugTrace) LgiTrace("Receive(%i) Item(%i) Error, time=%i\n", Account->GetIndex(), t->Index, TimeDelta());
					Error = true;
					break;
				}
			}

			if (Trans.Length() > 0)
			{
				MailCallbacks Callbacks;
				ZeroObj(Callbacks);
				Callbacks.CallbackData = &Params;
				Callbacks.OnSrc = ReceiveCallback;
				Callbacks.OnReceive = AfterReceived;

				Error = !Source->Receive(Trans, &Callbacks);

				for (unsigned i=0; i<Trans.Length(); i++)
				{
					MailTransaction *Tran = Trans[i];
					MailTransferEvent *t = Thread->Files[Tran->Index];
					if (t)
					{
						/*
						LgiTrace("%s:%i - Trans[%i]: No 't' ptr for idx=%i files.len=%i.\n",
							_FL,
							i,
							Tran->Index,
							(int)Thread->Files.Length());
						*/
					}
					else
					{
						if (Tran->Oversize)
						{
							// Ignore
							t->Action = MailNoop;
							t->Status = MailReceivedOk;
						}
						else if (Tran->Status)
						{
							if (t->Status == MailReceivedNone)
							{
if (DebugTrace) LgiTrace("Receive(%i) Item(%i) Posting WM_SCRIBE_THREAD_ITEM, time=%i\n", Account->GetIndex(), t->Index, TimeDelta());
								if (!TestFlag(Tran->Flags, MAIL_POSTED_TO_GUI))
								{
									// Ask the gui thread to load the mail in
									t->Status = MailReceivedWaiting;
									GetApp()->OnMailTransferEvent(t);
								}
							}
						}
						else
						{
							LgiTrace("%s:%i - Error: Bad Status on download %i of %i.\n", _FL, i, Trans.Length());
							// Don't delete a mail that failed to download
							t->Action = MailNoop;
						}
					}
				}
				Trans.DeleteObjects();
			}

			// Done... wait for main thread to finish processing
if (DebugTrace) LgiTrace("Receive(%i) Waiting for main thread, time=%i\n", Account->GetIndex(), TimeDelta());
			Thread->SetState(ThreadWaiting);

			// Wait for the main thread to finish with all the items we sent over
			if (!WaitForTransfers(Thread->Files))
				Error = true;

			if (Error)
			{
				LgiTrace("%s:%i - Error receiving mail.\n", _FL);
			}
			else
			{
				// Do delete's
if (DebugTrace) LgiTrace("Receive(%i) Delete phase, time=%i\n", Account->GetIndex(), TimeDelta());

				Group.Empty();
				{
					for (auto d: Thread->Files)
					{
						if (d->Action == MailDelete ||
							d->Action == MailDownloadAndDelete)
						{
							Group.Range++;
						}
					}
				}
					
				Group.Start = LCurrentTime();
				Thread->SetState(ThreadDeleting);
				for (auto It = Thread->Files.rbegin();
					It != Thread->Files.end() && Thread->GetState() == ThreadDeleting;
					It--)
				{
					MailTransferEvent *d = *It;

					if (d->Action == MailDelete ||
						d->Action == MailDownloadAndDelete)
					{
if (DebugTrace) LgiTrace("Receive(%i) Delete(%i) Deleting, time=%i\n", Account->GetIndex(), d->Index, TimeDelta());

						if (Source->Delete(d->Index))
						{
							if (d->Uid)
								Uids.Delete(d->Uid);
						}
						Group.Value++;
					}
				}
			}

			WaitForTransfers(Thread->Files);
			Thread->Files.DeleteObjects();
			Group.Empty();
		}
		else
		{
			Receive->RemoveAllMsgs();
		}

		// Clear out the UID's
		if (Uids.Length() && Receive->Msgs)
		{
			// ssize_t UidsLen = Uids.Length();
			// ssize_t AllMsgIdsLen = Receive->Msgs->Length();

			auto MsgKeys = Receive->Msgs->CopyKeys();
			for (auto &k : MsgKeys)
			{
				if (!Uids.Find(k))
					RemoveMsg(k);
			}

			if (Spam)
			{
				auto SpamKeys = Spam->CopyKeys();
				for (auto &s : SpamKeys)
	 			{
					if (!Uids.Find(s))
						Spam->Delete(s);
				}
			}
		}

if (DebugTrace) LgiTrace("Receive(%i) Closing the connection, time=%i\n", Account->GetIndex(), TimeDelta());
		// Close the connection.
		Source->Close();
	}

	DeleteObj(Source);

	// Clean up, notify the app
	Actions.Length(0);

if (DebugTrace) LgiTrace("Receive(%i) Exit, time=%i\n", Account->GetIndex(), TimeDelta());
}

void ReceiveAccountlet::OnPulse(char *s, int s_len)
{
	// this is called every second
	if (Lock())
	{
		LVariant Offline;

		bool On = IsOnline();
		LString Serv = Server().Str();
		if (On)
		{
			strcat(s, "Online");
			SecondsTillOnline = -1;
			
            if (Thread && Thread->GetState() == LThread::THREAD_EXITED)
			{
				#ifdef _DEBUG
				LgiTrace("Caught exited thread state, Accountlet[%i]=%p, Thread=%p, &Thread=%p\n",
					GetAccount()->GetIndex(),
					this,
					Thread.Get(),
					&Thread);
				#endif
				Thread.Reset();
			}
		}
		else if ((GetApp()->GetOptions()->GetValue(OPT_WorkOffline, Offline) && Offline.CastInt32()) ||
				Disabled() > 0)
		{
			strcat(s, "Offline");
			SecondsTillOnline = -1;
		}
		else
		{
			if (AutoReceive())
			{
				if (SecondsTillOnline < 0)
				{				
					int To = GetCheckTimeout();
					SecondsTillOnline = To > 0 ? To : 10 * 60;
				}
			}
			else
			{
				SecondsTillOnline = -1;
			}

			if (SecondsTillOnline == 0)
			{
				SecondsTillOnline = -1;

				if ((!IsCheckDialup() || LHaveNetConnection()) &&
					!Offline.CastInt32())
				{
					Connect(0, false);
				}
			}
			else if (SecondsTillOnline > 0)
			{
				SecondsTillOnline--;

				if (CheckTimeout().Str())
				{
					sprintf_s(s, s_len, "%i:%2.2i", SecondsTillOnline/60, SecondsTillOnline%60);
				}
			}
		}

		Unlock();
	}
}

bool ReceiveAccountlet::HasMsg(const char *Id)
{
	if (!Id || !Msgs || !Lock())
		return false;

	auto Status = Msgs->Find(Id);
	Unlock();

	return Status;
}

void ReceiveAccountlet::AddMsg(const char *Id)
{
	if (!Msgs || !Lock())
		return;

	if (!Msgs->Find(Id))
		Msgs->Add(Id);

	Unlock();
}

void ReceiveAccountlet::RemoveMsg(const char *Id)
{
	if (!Id || !Msgs || !Lock())
		return;

	Msgs->Delete(Id);

	Unlock();
}

void ReceiveAccountlet::RemoveAllMsgs()
{
	if (!Msgs || !Lock())
		return;

	Msgs->Empty();

	Unlock();
}

int ReceiveAccountlet::GetMsgs()
{
	int Status = 0;

	if (Lock())
	{
		if (Msgs)
		{
			Status = Msgs->Length();
		}

		Unlock();
	}

	return Status;
}

//////////////////////////////////////////////////////////////////////////////////
#define OPT_MsgDate			"Date"

MsgList::MsgList(LOptionsFile *opts, char *tag)
{
	Opts = opts;
	Tag = tag;
	Loaded = false;
	Dirty = false;
}

MsgList::~MsgList()
{
}

bool MsgList::Load()
{
	if (Opts && Tag && !Loaded)
	{
		LXmlTag *Msg = Opts->LockTag(Tag, _FL);

		if (!Msg)
		{
			Opts->CreateTag(Tag);
			Msg = Opts->LockTag(Tag, _FL);
		}

		if (Msg)
		{
			LArray<LXmlTag*> postDelete;
			for (auto t: Msg->Children)
			{
				if (!t->GetAttr(OPT_MsgDate))
				{
					LDateTime Now;
					char n[64];

					Now.SetNow();
					Now.Get(n, sizeof(n));

					t->SetAttr(OPT_MsgDate, n);
				}

				auto id = t->GetContent();
				if (id)
					Parent::Add(t->GetContent(), t);
				else
					postDelete.Add(t);
			}

			postDelete.DeleteObjects();

			Opts->Unlock();
		}

		Loaded = true;
	}

	return Loaded;
}

LXmlTag *MsgList::LockId(const char *id, const char *file, int line)
{
	if (Load())
	{
		LXmlTag *r = Opts->LockTag(Tag, file, line);
		if (r)
		{
			LXmlTag *m = (LXmlTag*) Parent::Find(id);
			if (m)
			{
				// deliberately leave the options locked.
				return m;
			}

			Opts->Unlock();
		}
	}

	return 0;
}

void MsgList::Unlock()
{
	Opts->Unlock();
}

bool MsgList::Add(const char *id)
{
	bool Status = false;
	
	LXmlTag *r = Opts->LockTag(Tag, _FL);
	if (r)
	{
		if (!Parent::Find(id))
		{
			LXmlTag *t = new LXmlTag("Message");
			if (t)
			{
				LDateTime Now;
				char n[64];

				Now.SetNow();
				Now.Get(n, sizeof(n));

				t->SetContent(id);
				t->SetAttr(OPT_MsgDate, n);				
				r->InsertTag(t);

				Status = Parent::Add(id, t);
			}
		}

		Unlock();
	}
	
	return Status;
}

bool MsgList::Delete(const char *id)
{
	bool Status = false;

	LXmlTag *r = Opts->LockTag(Tag, _FL);
	if (r)
	{
		LXmlTag *t = Parent::Find(id);
		if (t)
		{			
			// LgiTrace("DelMsg '%s' = %p\n", id, t);

			Parent::Delete(id);
			t->RemoveTag();
			DeleteObj(t);

			Status = true;
		}

		Opts->Unlock();
	}

	return Status;
}

int MsgList::Length()
{
	if (Load())
	{
		return (int)Parent::Length();
	}

	return 0;
}

bool MsgList::Find(const char *id)
{
	if (Load())
	{
		return Parent::Find(id) != 0;
	}

	return 0;
}

LString::Array MsgList::CopyKeys()
{
	LString::Array a(Length());
	for (auto i : *this)
	{
		a.Add(i.key);
	}
	return a;
}

void MsgList::Empty()
{
	if (!Load())
		return;

	LXmlTag *Msg = Opts->LockTag(Tag, _FL);
	if (!Msg)
		return;

	LXmlTag *t;
	while (	Msg->Children.Length() &&
			(t = Msg->Children[0]))
	{
		DeleteObj(t);
	}

	Opts->Unlock();
	Parent::Empty();
}

bool MsgList::SetDate(char *id, LDateTime *dt)
{
	bool Status = false;

	if (id && dt)
	{
		LXmlTag *t = LockId(id, _FL);
		if (t)
		{
			char s[64];

			LAssert(dt->IsValid());

			// Force the date to save in a guessable format.
			// yyyy/m/d is always easy to sniff.
			dt->SetFormat(GDTF_YEAR_MONTH_DAY);
			dt->Get(s, sizeof(s));

			t->SetAttr(OPT_MsgDate, s);
			Status = true;

			Unlock();
		}
	}

	return Status;
}

bool MsgList::GetDate(char *id, LDateTime *dt)
{
	if (!id || !dt)
		return false;

	LXmlTag *t = LockId(id, _FL);
	if (!t)
		return false;

	char *s = t->GetAttr(OPT_MsgDate);
	if (s)
	{
		// This is where I fucked up... previous versions of the software
		// saved the date in whatever the currently selected date format.
		// Which could've been anything... and the user can change that.
		// So if they do change it, the saved dates are now in the wrong
		// format to load back in (or mixed). Setting the format to '0' 
		// here puts the LDateTime object into "guess" mode based on the 
		// data. Which is going to be the closest we get to recovering 
		// from the ugly situation of not saving the date in a specific
		// format.
		dt->SetFormat(0);
		dt->Set(s);
		LAssert(dt->IsValid());
	}

	Unlock();

	return s != NULL;
}

///////////////////////////////////////////////////////////////////////////////
LMimeStream::LMimeStream() : LTempStream(ScribeTempPath(), 4 << 20)
{
}

#ifdef _DEBUG
#define DEBUG_MIME_STREAM	1
#else
#define DEBUG_MIME_STREAM	0
#endif

bool LMimeStream::Parse()
{
	if (Tmp)
		Tmp->SetPos(0);

	#if DEBUG_MIME_STREAM
	LMemStream *Src = dynamic_cast<LMemStream*>(s);
	#endif

	bool Status = Text.Decode.Pull(s) != 0;
	if (!Status)
	{
		#if DEBUG_MIME_STREAM
		LFile f;
		if (Src && f.Open("c:\\temp\\mime_error.txt", O_WRITE))
		{
			Src->SetSize(0);
			Src->SetPos(0);
			Src->Write(&f, (int)Src->GetSize());
			f.Close();
		}

		s->SetPos(0);
		#endif
		LAssert(!"Mime Parsing Failed.");
	}
	
	LTempStream::Empty();

	return Status;
}
