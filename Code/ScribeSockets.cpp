#include "Scribe.h"
#include "ScribePrivate.h"

//////////////////////////////////////////////////////////////////////////////
ILog::ILog(LStreamI *log)
{
	LogFmt = 0;
	Info = 0;
	Log = log;
	DeleteLog = false;
}

ILog::~ILog()
{
}

void ILog::WriteLog(const char *Buf, ssize_t Len)
{
	if (LogFmt != NET_LOG_ALL_BYTES &&
		LogFmt != NET_LOG_HEX_DUMP)
		return;

	LFile f;
	if (ValidStr(LogFileName.Str()) &&
		f.Open(LogFileName.Str(), O_WRITE))
	{
		f.SetPos(f.GetSize());

		if (LogFmt == NET_LOG_HEX_DUMP)
		{
			for (int i=0; i<Len;)
			{
				char s[256];
				char Ascii[32], *a = Ascii;
				int ch = sprintf_s(s, sizeof(s), "%8.8X\t", i);
				for (int n = i + 16; i<Len && i<n; i++)
				{
					*a = (uchar)Buf[i];
					if (*a < ' ') *a = '.';
					a++;

					ch += sprintf_s(s+ch, sizeof(s)-ch, "%2.2X ", (uchar)Buf[i]);
				}
				*a++ = 0;
				while (ch < 58)
				{
					s[ch++] = ' ';
				}
				ch += sprintf_s(s+ch, sizeof(s)-ch, "%s\r\n", Ascii);
				f.Write(s, ch);
			}
		}
		else
		{
			f.Write(Buf, Len);
		}
	}
}

char *ILog::EnumFileName(char *File)
{
	char *Status = 0;
	if (File)
	{
		char *Dot = strchr(File, '.');
		if (Dot)
		{
			char Str[256];
			ssize_t ch = Dot - File;
			memcpy(Str, File, ch);

			for (int i=0; true; i++)
			{
				sprintf_s(Str+ch, sizeof(Str)-ch, "_%4.4i%s", i, Dot);
				if (!LFileExists(Str))
				{
					break;
				}
			}

			Status = NewStr(Str);
		}
	}
	return Status;
}

void ILog::LogRead(char *Data, ssize_t Len)
{
	if (Info)
	{
		Info->Value(Info->Value() + Len);
	}

	if (Data)
	{
		WriteLog(Data, Len);
	}
}

void ILog::LogWrite(const char *Data, ssize_t Len)
{
	if (Info)
	{
		Info->Value(Info->Value() + Len);
	}

	if (Data)
	{
		WriteLog(Data, Len);
	}
}

void ILog::LogError(int ErrorCode, const char *ErrorDescription)
{
	char Str[512];
	int Ch = sprintf_s(Str, sizeof(Str), "SocketError(%i): %s\n", ErrorCode, ErrorDescription);
	Log->Write(Str, Ch, LSocketI::SocketMsgError);
}

void ILog::LogInfomation(const char *Info)
{
	char Str[512];
	int Ch = sprintf_s(Str, sizeof(Str), "Information: %s\n", Info);
	Log->Write(Str, Ch, LSocketI::SocketMsgInfo);
}

bool ILog::LogSetVariant(const char *Which, LVariant &What, const char *Arr)
{
	if (!Which)
		return false;

	if (!_stricmp(Which, OPT_LogFile))
	{
		LogFileName = What;
	}
	else if (!_stricmp(Which, OPT_LogFormat))
	{
		LogFmt = What.CastInt32();
	}
	else if (!_stricmp(Which, LSocket_Progress))
	{
		Info = (Progress*) What.Value.Ptr;
	}
	else if (!_stricmp(Which, LSocket_SetDelete))
	{
		DeleteLog = What.CastInt32() != 0;
	}
	else return false;

	return true;
}



