#include "lgi/common/Lgi.h"
#include "mapi.h"
#include "..\..\Code\Scribe.h"
#include "..\..\Code\ScribeListAddr.h"
#include "lgi/common/OptionsFile.h"

#define LOG_FILE_NAME		"ScribeMapiLog.txt"

LOptionsFile *Options = 0;
HINSTANCE MyInst = 0;
char LogPath[MAX_PATH_LEN] = "";
char ExePath[MAX_PATH_LEN] = "";

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#if defined(_DEBUG) && !defined(_WIN64)
#define DebugBreak() _asm int 3
#else
#define DebugBreak() assert(0);
#endif

void Log(const char *Fmt, ...)
{
	LFile f;
	if (!f.Open(LogPath, O_WRITE))
	{
		// Maybe the default LogPath is read only?
		LString Ad = WinGetSpecialFolderPath(CSIDL_APPDATA);
		char p[MAX_PATH_LEN];
		if (LMakePath(p, sizeof(p), Ad, "Scribe\\" LOG_FILE_NAME))
		{
			strcpy_s(LogPath, sizeof(LogPath), p);
			f.Open(LogPath, O_WRITE);
		}
	}
	
	if (f.IsOpen())
	{
		f.SetPos(f.GetSize());
		
		va_list arg;
		va_start(arg, Fmt);
		LStreamPrintf(&f, 0, Fmt, arg);
		va_end(arg);
	}
}

struct GMapiLogger : public LStream
{
	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0)
	{
		Log("%.*s",  Size, Ptr);
		return Size;
	}
};

BOOL
DllMain(HINSTANCE hInst, DWORD Reason, LPVOID Reserved)
{
	if (Reason == DLL_PROCESS_ATTACH)
	{
		char16 DllPath[MAX_PATH_LEN];
		
		MyInst = hInst;
		LRegKey::AssertOnError = false;

		// Get the path of the DLL
		if (GetModuleFileNameW((HINSTANCE)&__ImageBase, DllPath, _countof(DllPath)))
		{
			// Create a log file from the DLL path...
			LAutoString u(WideToUtf8(DllPath));
			LMakePath(LogPath, sizeof(LogPath), u, "..\\" LOG_FILE_NAME);

			// Find the executable while we're here...
			LMakePath(ExePath, sizeof(ExePath), LogPath, "..\\Scribe.exe");
			if (!LFileExists(ExePath))
			{
				LRegKey s(false, "HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\Mail\\Scribe\\shell\\open\\command");
				if (s.IsOk())
				{
					const char *CmdPath = s.GetStr();
					if (CmdPath)
					{
						LAutoString Scribe(LTokStr(CmdPath));
						if (LFileExists(Scribe))
						{
							strcpy_s(ExePath, sizeof(ExePath), Scribe);
						}
						else
						{
							Log("%s:%i - Can't find Scribe executable.\n", _FL);
							return false;
						}
					}
				}
			}
		}
		else
		{
			Log("%s:%i - GetModuleFileNameW failed.\n", _FL);
			return false;
		}
		
		char OptsPath[MAX_PATH_LEN] = "";
		
		// Check portable mode...
		LMakePath(OptsPath, sizeof(OptsPath), ExePath, "..\\ScribeOptions.xml");
		if (!LFileExists(OptsPath))
		{
			LMakePath(OptsPath, sizeof(OptsPath), ExePath, "..\\..\\ScribeOptions.xml");
		}

		if (!LFileExists(OptsPath))
		{
			// Check app data for desktop mode...
			LString a = WinGetSpecialFolderPath(CSIDL_APPDATA);
			LMakePath(OptsPath, sizeof(OptsPath), a, "Scribe\\ScribeOptions.xml");
		}		

		if (LFileExists(OptsPath))
		{
			if (Options = new LOptionsFile(OptsPath)) // "ScribeOptions"
			{
				if (!Options->SerializeFile(false))
				{
					Log("Loading '%s' options failed.\n", OptsPath);
					return false;
				}
			}
		}
		else
		{
			Log("%s:%i - The file '%s' doesn't exist.\n", _FL, OptsPath);
			return false;
		}
	}

	return true;
}

ULONG FAR PASCAL
MAPILogon
(
	ULONG ulUIParam,
	LPSTR lpszProfileName,
	LPSTR lpszPassword,
	FLAGS flFlags,
	ULONG ulReserved,
	LPLHANDLE lplhSession
)
{
	if (lplhSession)
	{
		*lplhSession = (LHANDLE) MyInst;
	}

	return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL
MAPIAddress
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	LPTSTR lpszCaption,
	ULONG nEditFields,
	LPTSTR lpszLabels,
	ULONG nRecips,
	lpMapiRecipDesc lpRecips,
	FLAGS flFlags,
	ULONG ulReserved,
	LPULONG lpnNewRecips,
	lpMapiRecipDesc FAR * lppNewRecips
)
{
	Log("%s:%i - MAPIAddress not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL
MAPIDeleteMail
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	LPTSTR lpszMessageID,
	FLAGS flFlags,
	ULONG ulReserved
)
{
	Log("%s:%i - MAPIDeleteMail not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL
MAPIDetails
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	lpMapiRecipDesc lpRecip,
	FLAGS flFlags,
	ULONG ulReserved
)
{
	Log("%s:%i - MAPIDetails not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL MAPIFindNext
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	LPTSTR lpszMessageType,
	LPTSTR lpszSeedMessageID,
	FLAGS flFlags,
	ULONG ulReserved,
	LPTSTR lpszMessageID
)
{
	Log("%s:%i - MAPIFindNext not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL
MAPIFreeBuffer(
  LPVOID lpBuffer  
)
{
	Log("%s:%i - MAPIFreeBuffer not impl.\n", _FL);
	DebugBreak()
	return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPILogoff
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	FLAGS flFlags,
	ULONG ulReserved
)
{
	Log("%s:%i - MAPILogoff not impl.\n", _FL);
	DebugBreak()
	return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIReadMail
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	LPTSTR lpszMessageID,
	FLAGS flFlags,
	ULONG ulReserved,
	lpMapiMessage FAR * lppMessage
)
{
	Log("%s:%i - MAPIReadMail not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL MAPIResolveName
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	LPTSTR lpszName,
	FLAGS flFlags,
	ULONG ulReserved,
	lpMapiRecipDesc FAR * lppRecip
)
{
	Log("%s:%i - MAPIResolveName not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL MAPISaveMail
(
	LHANDLE lhSession,
	ULONG ulUIParam,
	lpMapiMessage lpMessage,
	FLAGS flFlags,
	ULONG ulReserved,
	LPTSTR lpszMessageID
)
{
	Log("%s:%i - MAPISaveMail not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL MAPISendDocuments
(
	ULONG ulUIParam,
	LPTSTR lpszDelimChar,
	LPTSTR lpszFullPaths,
	LPTSTR lpszFileNames,
	ULONG ulReserved
)
{
	Log("%s:%i - MAPISendDocuments not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

ULONG WINAPI MAPISendMailHelper(
  _In_  LHANDLE lhSession,
  _In_  ULONG_PTR ulUIParam,
  _In_  lpMapiMessage lpMessage,
  _In_  FLAGS flFlags,
  _In_  ULONG ulReserved
)
{
	Log("%s:%i - MAPISendMailHelper not impl.\n", _FL);
	DebugBreak()
	return MAPI_E_NOT_SUPPORTED;
}

template<typename T>
LString ConvertAddress(T r)
{
	LString Email;
	LString s;
	
	LString Name(r->lpszName);
	if (r->lpszAddress)
	{
		LString Addr(r->lpszAddress);
		char *Colon = strchr(Addr, ':');
		Email = Colon ? Colon + 1 : Addr.Get();
	}
	else if (Name && strchr(Name, '@'))
	{
		Email = Name;
	}

	if (Name)
		s.Printf("\"%s\" <%s>", Name, Email);
	else
		s.Printf("<%s>", Email);

	return s;
}

ULONG WINAPI MAPISendMailW(	LHANDLE lhSession,
							ULONG_PTR ulUIParam,
							lpMapiMessageW Msg,
							FLAGS flFlags,
							ULONG ulReserved
)
{
	ULONG Status = MAPI_E_FAILURE;

	if (!LFileExists(ExePath))
	{
		Log("%s:%i - No path to Scribe executable.\n", _FL);
		return Status;
	}

	LMime m;
	
	LString::Array To, Cc;
	for (int i=0; i<Msg->nRecipCount; i++)
	{
		lpMapiRecipDescW r = Msg->lpRecips + i;
		LString s = ConvertAddress(r);
		switch (r->ulRecipClass)
		{
			case MAPI_TO:
				To.Add(s);
				break;
			case MAPI_CC:
				Cc.Add(s);
				break;
			default:
			case MAPI_BCC:
				break;
		}
	}
	
	LString Sep(", ");
	if (To.Length())
	{
		LString s = Sep.Join(To);
		m.Set("To", s);
	}
	if (Cc.Length())
	{
		LString s = Sep.Join(Cc);
		m.Set("Cc", s);
	}

	LString Subj(Msg->lpszSubject);
	if (Subj)
		m.Set("Subject", Subj);

	LMime *Body = &m;
	if (Msg->nFileCount > 0)
	{
		Body = m.NewChild();
		m.SetMimeType("multipart/mixed");
		
		for (int i=0; i<Msg->nFileCount; i++)
		{
			LAutoPtr<LFile> File(new LFile);
			LString Path(Msg->lpFiles[i].lpszPathName);
			LString Mt = LGetFileMimeType(Path);
			if (File->Open(Path, O_READ))
			{
				LMime *a = m.NewChild();
				if (a)
				{
					a->SetMimeType(Mt ? Mt : "application/octet-stream");
					a->SetData(true, File.Release());
				}
			}
		}
	}
	
	if (Msg->lpszNoteText)
	{
		Body->SetMimeType("text/plain");
		LString Note(Msg->lpszNoteText);
		Body->SetData(true, new LMemStream(Note, Note.Length(), false));
	}

	bool HasFrom = false;
	if (Msg->lpOriginator)
	{
		LString s = ConvertAddress(Msg->lpOriginator);
		if (s)
			HasFrom = m.Set("From", s);
	}
	
	if (!HasFrom && Options)
	{
		LVariant Cur;
		if (!Options->GetValue(OPT_CurrentIdentity, Cur))
			Cur = 0;

		LXmlTag *Accounts = Options->LockTag(OPT_Accounts, _FL);
		if (Accounts)
		{
			LXmlTag *Account = Accounts->Children[Cur.CastInt32()];
			if (Account)
			{
				LVariant FromName, FromEmail;
				Account->GetValue(OPT_AccIdentName, FromName);
				Account->GetValue(OPT_AccIdentEmail, FromEmail);
				if (FromEmail.Str())
				{				
					LString s;
					if (FromName.Str())
						s.Printf("\"%s\" <%s>", FromName.Str(), FromEmail.Str());
					else
						s.Printf("<%s>", FromEmail.Str());					
					HasFrom = m.Set("From", s);
				}
				else Log("%s:%i - No email address for from header.\n", _FL);
			}
			else Log("%s:%i - No identity.\n", _FL);

			Options->Unlock();
		}
		else Log("%s:%i - No accounts element.\n", _FL);
	}
	else Log("%s:%i - No options file.\n", _FL);
	
	if (!HasFrom)
	{
		MessageBox((HWND)ulUIParam, L"No valid 'From' email address in parameters or options file.", L"Scribe MAPI Support", MB_OK);
		return MAPI_E_UNKNOWN_RECIPIENT;
	}
	
	char Tmp[MAX_PATH_LEN];
	if (LGetSystemPath(LSP_TEMP, Tmp, sizeof(Tmp)))
	{
		LMakePath(Tmp, sizeof(Tmp), Tmp, "_import_.eml");
		
		LFile f;
		if (f.Open(Tmp, O_WRITE))
		{
			f.SetSize(0);
			bool EncodeOk = m.Text.Encode.Push(&f);
			f.Close();
			if (EncodeOk)
			{
				bool UseDialog = TestFlag(flFlags, MAPI_DIALOG);

				char Args[400];
				sprintf(Args, "'%s' -send %i", Tmp, !UseDialog);
				if (LExecute(ExePath, Args))
				{
					Status = SUCCESS_SUCCESS;
				}
				else Log("%s:%i - LExecute(%s,%s) failed.\n", _FL, ExePath, Args);
			}
			else Log("%s:%i - Encoding failed.\n", _FL);
		}
		else Log("%s:%i - Can't open '%s' for writing.\n", _FL, Tmp);
	}
	else Log("%s:%i - Can't get temp path.\n", _FL);

	return Status;
}

ULONG FAR PASCAL MAPISendMail
(
	LHANDLE			lhSession,
	ULONG_PTR		ulUIParam,
	lpMapiMessage	Msg,
	FLAGS			flFlags,
	ULONG			ulReserved
)
{
	ULONG Status = MAPI_E_FAILURE;

	if (!LFileExists(ExePath))
	{
		Log("%s:%i - No path to Scribe executable.\n", _FL);
		return Status;
	}

	LMime m;
	
	LString::Array To, Cc;
	for (int i=0; i<Msg->nRecipCount; i++)
	{
		MapiRecipDesc *r = Msg->lpRecips + i;
		LString s = ConvertAddress(r);
		switch (r->ulRecipClass)
		{
			case MAPI_TO:
				To.Add(s);
				break;
			case MAPI_CC:
				Cc.Add(s);
				break;
			default:
			case MAPI_BCC:
				break;
		}
	}
	
	LString Sep(", ");
	if (To.Length())
	{
		LString s = Sep.Join(To);
		m.Set("To", s);
	}
	if (Cc.Length())
	{
		LString s = Sep.Join(Cc);
		m.Set("Cc", s);
	}

	if (ValidStr(Msg->lpszSubject))
		m.Set("Subject", Msg->lpszSubject);

	LMime *Body = &m;
	if (Msg->nFileCount > 0)
	{
		Body = m.NewChild();
		m.SetMimeType("multipart/mixed");
		
		for (int i=0; i<Msg->nFileCount; i++)
		{
			LAutoPtr<LFile> File(new LFile);
			const char *Path = Msg->lpFiles[i].lpszPathName;
			auto Mt = LGetFileMimeType(Path);
			if (File->Open(Path, O_READ))
			{
				LMime *a = m.NewChild();
				if (a)
				{
					a->SetMimeType(Mt ? Mt : "application/octet-stream");
					a->SetData(true, File.Release());
				}
			}
		}
	}
	
	if (Msg->lpszNoteText)
	{
		Body->SetMimeType("text/plain");
		Body->SetData(true, new LMemStream(Msg->lpszNoteText, strlen(Msg->lpszNoteText), false));
	}

	bool HasFrom = false;
	if (Msg->lpOriginator)
	{
		LString s = ConvertAddress(Msg->lpOriginator);
		if (s)
			HasFrom = m.Set("From", s);
	}
	
	if (!HasFrom && Options)
	{
		LVariant Cur;
		if (!Options->GetValue(OPT_CurrentIdentity, Cur))
			Cur = 0;

		LXmlTag *Accounts = Options->LockTag(OPT_Accounts, _FL);
		if (Accounts)
		{
			LXmlTag *Account = Accounts->Children[Cur.CastInt32()];
			if (Account)
			{
				LVariant FromName, FromEmail;
				Account->GetValue(OPT_AccIdentName, FromName);
				Account->GetValue(OPT_AccIdentEmail, FromEmail);
				if (FromEmail.Str())
				{				
					LString s;
					if (FromName.Str())
						s.Printf("\"%s\" <%s>", FromName.Str(), FromEmail.Str());
					else
						s.Printf("<%s>", FromEmail.Str());					
					HasFrom = m.Set("From", s);
				}
				else Log("%s:%i - No email address for from header.\n", _FL);
			}
			else Log("%s:%i - No identity.\n", _FL);

			Options->Unlock();
		}
		else Log("%s:%i - No accounts element.\n", _FL);
	}
	else Log("%s:%i - No options file.\n", _FL);
	
	if (!HasFrom)
	{
		MessageBox((HWND)ulUIParam, L"No valid 'From' email address in parameters or options file.", L"Scribe MAPI Support", MB_OK);
		return MAPI_E_UNKNOWN_RECIPIENT;
	}
	
	char Tmp[MAX_PATH_LEN];
	if (LGetSystemPath(LSP_TEMP, Tmp, sizeof(Tmp)))
	{
		LMakePath(Tmp, sizeof(Tmp), Tmp, "_import_.eml");
		
		LFile f;
		if (f.Open(Tmp, O_WRITE))
		{
			f.SetSize(0);
			bool EncodeOk = m.Text.Encode.Push(&f);
			f.Close();
			if (EncodeOk)
			{
				bool UseDialog = TestFlag(flFlags, MAPI_DIALOG);

				char Args[400];
				sprintf(Args, "'%s' -send %i", Tmp, !UseDialog);
				if (LExecute(ExePath, Args))
				{
					Status = SUCCESS_SUCCESS;
				}
				else Log("%s:%i - LExecute(%s,%s) failed.\n", _FL, ExePath, Args);
			}
			else Log("%s:%i - Encoding failed.\n", _FL);
		}
		else Log("%s:%i - Can't open '%s' for writing.\n", _FL, Tmp);
	}
	else Log("%s:%i - Can't get temp path.\n", _FL);

	return Status;
}
