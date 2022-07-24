#ifndef _SCRIBE_UTILS_H_
#define _SCRIBE_UTILS_H_

#include "lgi/common/DateTime.h"
#include "lgi/common/Variant.h"
#include "lgi/common/DocView.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/Store3.h"
#include "lgi/common/OAuth2.h"

#include "ScribeInc.h"

ScribeFunc const char *MimeToUti(const char *Mime);
ScribeFunc char *ScribeTempPath();
ScribeFunc char *ScribeInsertFields(const char *Template, LDom *Source);
ScribeFunc char *MakeFileName(const char *ContentUtf, const char *Ext);
ScribeClass LColour SocketMsgTypeToColour(LSocketI::SocketMsgType flags);
ScribeFunc class ContactGroup *LookupContactGroup(class ScribeWnd *App, const char *Name);
ScribeExtern LString AskOverwriteMsg(const char *FileName);
ScribeFunc void PatternBox(LSurface *pDC, const LRect &r);
ScribeExtern LOAuth2::Params GetOAuth2Params(const char *Host, Store3ItemTypes Context);
ScribeFunc const char *ScribeResourcePath();
ScribeExtern LString::Array ScribeThemePaths();
ScribeExtern LString DetectCharset(LString s);
extern LAutoString ConvertThreadIndex(char *ThreadIndex, int TruncateChars = 0);

/// Parses HTML into a tree and then evaluates 'SearchExp' on each node.
/// On the list of matching nodes it evaluates 'ResultExp' using the scripting
/// engine. The resulting variables are stored in 'ReturnValue' as a list.
extern bool SearchHtml
(
	/// List of values created from evaluating 'ResultExp' on the matching elements.
	LVariant *ReturnValue,
	/// The input HTML to parse.
	const char *Html,
	/// A LGI scripting expression. The element being inspected has the following
	/// variables:
	///		'element' - the name of the element. e.g. 'div', 'body' etc.
	///		'content' - any textual content after the element in the document.
	///		'attr[name]' - the value of an attribute called 'name'.
	/// If the expression evaluates to non-zero the element is stored in a result
	/// array.
	const char *SearchExp,
	/// The result array is then evaluated into values that can be passed back to
	/// scripts via this expression. It uses the same available fields as the
	/// 'SearchExp'.
	const char *ResultExp
);

extern void LHtmlMsg
(
	/// The callback to receive the status
	std::function<void(int)> Callback,
	/// The parent view or NULL if none available
	LViewI *Parent,
	/// The message's text. This is a printf format string that you can pass arguments to
	const char *Html,
	/// The title of the message box window
	const char *Title = 0,
	/// The type of buttons below the message. Can be one of:
	/// #MB_OK, #MB_OKCANCEL, #MB_YESNO or #MB_YESNOCANCEL.
	int Type = MB_OK,
	...
);

extern void ClearTempPath();
extern char *RemoveAmp(const char *s);
extern LString AddAmp(const char *menu, int shortcut);

class HttpImageThread : public LThreadWorker, public LCancel
{
	class ScribeWnd *App;
	LString Proxy, Cache;
	LHashTbl<StrKey<char, false>,LString> UriMap;
	LAutoPtr<class Zlib> z;

public:
	HttpImageThread(ScribeWnd *app, const char *proxy, LThreadTarget *First);
	~HttpImageThread();

	void DoJob(LThreadJob *j);
};

class Store3Progress : public LProgressDlg, public LDataPropI
{
    bool Interact;
    int NewFormat;
    LString Err, Cache;

public:
    Store3Progress(LView *parent, bool interact);

	const char *GetStr(int id);
    Store3Status SetStr(int id, const char *str);
    
    int64 GetInt(int id);
    Store3Status SetInt(int id, int64 i);
};

ScribeFunc void TraceTime(char *s);

class CountItem
{
public:
	int Type;
	int64 Count;

	CountItem()
	{
		Type = 0;
		Count = 0;
	}
};

class Counter : public List<CountItem>
{
	CountItem *FindType(int Type);

public:
	Counter() {}
	~Counter();

	void Inc(int Type);
	void Dec(int Type);
	void Add(int Type, int64 n);
	void Sub(int Type, int64 n);
	int64 GetTypeCount(int Type);
};

class Mailto
{
public:
	ScribeWnd *App;
	List<AddressDescriptor> To;
	char *Subject;
	char *Body;

	Mailto(ScribeWnd *app, const char *s);
	~Mailto();

	void Apply(class Mail *m);
};

class LStringStream : public LStringPipe
{
	LStream *s;

	ssize_t Read(void *Ptr, ssize_t Size, int Flags = 0)
	{
		return s->Read(Ptr, Size, Flags);
	}

	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0)
	{
		return s->Write(Ptr, Size, Flags);
	}

public:
	LStringStream(LStream *str)
	{
		s = str;
		s->SetPos(0);
	}

	bool IsOpen() { return s->IsOpen(); }
	int Close() { return s->Close(); }
	bool IsEmpty() { return s->GetSize() == 0; }
	void Empty() { s->SetSize(0); }
	int64 GetSize() { return s->GetSize(); }

	void *New(int AddBytes = 0)
	{
		char *Buf = 0;
		int Len = (int)s->GetSize();
		if (Len > 0)
		{
			Buf = new char[Len + AddBytes];
			if (Buf)
			{
				s->Read(Buf, Len);
				memset(Buf+Len, 0, AddBytes);
			}
		}
		else
		{
			LAssert(0);
		}

		return Buf;
	}

	int64 Peek(uchar *Ptr, int Size)
	{
		LAssert(0);
		return 0;
	}
	int64 Peek(LStreamI *Ptr, int Size)
	{
		LAssert(0);
		return 0;
	}
};

class TabDialog : public LDialog
{
    int TabCtrlId;
    int HelpBtnId;

	void IdealSize(LButton *b);

public:
    TabDialog(int tabCtrlId, int helpBtnId)
    {
        TabCtrlId = tabCtrlId;
        HelpBtnId = helpBtnId;
    }
    
    void OnCreate();
    void OnPosChange();
};

class ProtocolSettingStore : public LDom
{
	LOptionsFile *Opts;
	LString AccountTag;
	
public:
	ProtocolSettingStore(LOptionsFile *opts, const char *accountTag)
	{
		Opts = opts;
		AccountTag = accountTag;
	}
	
	LOptionsFile *GetOptions()
	{
		return Opts;
	}
	
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override
	{
		char s[256];
		sprintf_s(s, sizeof(s), "%s.%s", AccountTag.Get(), Name);
		LAssert(Array == NULL); // Shouldn't need this
		return Opts->GetValue(s, Value);
	}
	
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override
	{
		char s[256];
		sprintf_s(s, sizeof(s), "%s.%s", AccountTag.Get(), Name);
		LAssert(Array == NULL); // Shouldn't need this
		return Opts->SetValue(s, Value);
	}
};

class ScriptDownloadContentThread : public LThread, public LCancel
{
	ScribeWnd *App;
	LString Uri;
	LString CallbackName;
	LStringPipe Out;
	LString Err;
	LVariant UserData;
	bool Result = false;

public:
	ScriptDownloadContentThread(ScribeWnd *App, LString Uri, LString CallbackName, LVariant *userData = NULL);
	
	int Main();
	void OnComplete();
};

#endif
