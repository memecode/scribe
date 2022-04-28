#include <atlbase.h>
extern CComModule _Module;
#include <atlcom.h>
#include <atlwin.h>
#include <atlhost.h>
#include <exdisp.h>
#include <comdef.h>
#include <ExDispid.h>

#include <time.h>
#include "Lgi.h"
#include "LDocView.h"
#include "../Scribe.h"


#undef ToLower
#undef ToUpper
#undef SkipWhiteSpace


CComModule _Module;
BEGIN_OBJECT_MAP(ObjectMap)
END_OBJECT_MAP()

//////////////////////////////////////////////////////////////////
class HtmlWrapper : public LDocView, public IDispatch
{
	CAxHostWindow *Host;
	CAxWindow Wnd;
	IUnknown *Ctrl;
	IWebBrowser2 *Browser;
	IHTMLDocument2 *Doc;

	char *Arg;
	LFile Temp;
	LString TempFile;
	bool UseMoz;
	char *Src;

	bool OpenTempFile();
	bool CloseTempFile();

	ULONG Refs;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
	{
		if (ppvObject == NULL)
			return E_POINTER;
		
		if (IsEqualIID(riid, IID_IDispatch))
		{
			*ppvObject = (IDispatch*)this;
			return S_OK;
		}
		
		// IID_IDispatchEx: do I need to implement this?

		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef()
	{
		return ++Refs;
	}

	ULONG STDMETHODCALLTYPE Release()
	{
		return --Refs;
	}

	HRESULT STDMETHODCALLTYPE GetTypeInfoCount(/* [out] */ __RPC__out UINT *pctinfo)
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE GetTypeInfo(	/* [in] */ UINT iTInfo,
											/* [in] */ LCID lcid,
											/* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo)
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE GetIDsOfNames(	/* [in] */ __RPC__in REFIID riid,
												/* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
												/* [range][in] */ UINT cNames,
												/* [in] */ LCID lcid,
												/* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId)
	{
		return E_NOTIMPL;
	}
	
	void OnLoad()
	{
		CComBSTR cs(GetCharset());
		cs.ToLower();
		HRESULT r = Doc->put_charset(cs);
		// LgiTrace("put_charset(%S)=%i\n", cs.m_str, r);
		Doc->Release();
		Doc = NULL;
		if (FAILED(r))
		{
			LgiTrace("%s:%i - put_charset failed.\n", _FL);
			return;
		}

		_variant_t rl = (short)1;
		r = Browser->Refresh2(&rl);
		// LgiTrace("Refresh2()=%i\n", r);
		if (FAILED(r))
		{
			LgiTrace("%s:%i - Refresh2 failed.\n", _FL);
		}
	}

	HRESULT STDMETHODCALLTYPE Invoke(	/* [in] */ DISPID dispIdMember,
										/* [in] */ REFIID riid,
										/* [in] */ LCID lcid,
										/* [in] */ WORD wFlags,
										/* [out][in] */ DISPPARAMS *pDispParams,
										/* [out] */ VARIANT *pVarResult,
										/* [out] */ EXCEPINFO *pExcepInfo,
										/* [out] */ UINT *puArgErr)
	{
		// LgiTrace("Invoke: %i\n", dispIdMember);
		
		switch (dispIdMember)
		{
			case DISPID_DOCUMENTCOMPLETE:
			{
				if (!Browser || !Doc)
				{
					LgiTrace("%s:%i - Invalid ptr?\n", _FL);
					break;
				}
				
				READYSTATE Rs;
				HRESULT r = Browser->get_ReadyState(&Rs);
				if (FAILED(r))
				{
					LgiTrace("%s:%i - get_ReadyState failed.\n", _FL);
					break;
				}
				
				if (Rs == READYSTATE_LOADING)
				{
					// LgiTrace("%s:%i - Still loading...\n", _FL);
					break;
				}
				
				OnLoad();
				break;
			}
		}
		
		return S_OK;        
    }

public:
	HtmlWrapper(int Id);
	~HtmlWrapper();

	const char *GetMimeType() { return "text/html"; }
	char *Name();
	bool Name(const char *s);
	void Visible(bool i);
	bool Visible();
	void Sunken(bool i);
	bool Sunken();
	bool SetPos(LRect &p, bool Repaint = false);
	LRect &GetPos();
	bool Attach(LViewI *Parent);
	bool Detach();

	bool Cut()
	{
		return false;
	}
	bool Copy()
	{
		return false;
	}
	bool Paste()
	{
		return false;
	}

	LMessage::Result OnEvent(LMessage *Msg);

};

///////////////////////////////////////////////////////////////////
HtmlWrapper::HtmlWrapper(int Id)
{
	Host = 0;
	Ctrl = 0;
	Browser = 0;
	Refs = 0;
	Doc = NULL;

	Src = 0;
	UseMoz = false;
	_View = 0;
	Arg = 0;

	SetId(Id);
	SetTabStop(true);
	srand(clock());
}

HtmlWrapper::~HtmlWrapper()
{
	Detach();
	DeleteArray(Src);
	CloseTempFile();
}

LMessage::Result HtmlWrapper::OnEvent(LMessage *Msg)
{
	/*
	switch (MsgCode(Msg))
	{
		case WM_VIEW_EMAIL_SUPPORTS_MAIL:
		{
			return true;
			break;
		}
		case WM_VIEW_EMAIL_RENDER_MAIL:
		{
			Mail *m=(Mail*)MsgA(Msg);
			int AttachIndex=MsgB(Msg);
			if (m &&
				AttachIndex < 0 &&
				OpenTempFile())
			{
			}
			break;
		}
	}
	*/

	return 0;
}

bool HtmlWrapper::OpenTempFile()
{
	CloseTempFile();

	char16 Str[256];
	DWORD Ch = GetTempPath(CountOf(Str)-1, Str);
	if (Ch > 0 && Ch <= CountOf(Str)-1) 
	{
		if (Str[Ch-1] != DIR_CHAR)
			Str[Ch++] = DIR_CHAR;

		swprintf_s(Str+Ch, CountOf(Str)-Ch, L"~%i.html", rand());
		TempFile = Str;
		
		int Status = Temp.Open(TempFile, O_WRITE);
		Temp.SetSize(0);
		return Status != 0;
	}

	return false;
}

bool HtmlWrapper::CloseTempFile()
{
	if (TempFile)
	{
		LAutoWString w(Utf8ToWide(TempFile));
		DeleteFile(w);
		TempFile.Empty();
		return true;
	}

	return false;
}

char *HtmlWrapper::Name()
{
	return Src;
}

bool HtmlWrapper::Name(const char *Name)
{
	DeleteArray(Src);
	Src = NewStr(Name);
	if (Doc)
	{
		Doc->Release();
		Doc = NULL;
	}

	if (Name)
	{
		const char *File = 0;

		if (_strnicmp(Name, "file://", 7) == 0)
		{
			File = Name + 7;
			TempFile = File;
		}
		else if (OpenTempFile())
		{
			/*
			const uint8 Utf8Bom[] = {0xEF,0xBB,0xBF};
			if (GetCharset() &&
				!_stricmp(GetCharset(), "utf-8"))
				Temp.Write(Utf8Bom, sizeof(Utf8Bom));
			*/
			
			char *e = 0;
			for (char *s = (char*)Name; s && *s; s = e)
			{
				e = strchr(s, '\n');
				if (e)
				{
					Temp.Write(s, e-s);
					Temp.Write("\r\n", 2);
					e++;
				}
				else
				{
					Temp.Write(s, strlen(s));
				}
			}
			Temp.Close();
			File = TempFile;
		}

		if (Browser && File)
		{
			_bstr_t Url(File);
			Browser->Navigate(Url, 0, 0, 0, 0);

			if (GetCharset())
			{
				IDispatch *pDisp = NULL;
				HRESULT r = Browser->get_Document(&pDisp);
				if (FAILED(r))
				{
					LgiTrace("%s:%i - get_Document failed.\n", _FL);
				}
				else
				{
					r = pDisp->QueryInterface(IID_IHTMLDocument2, (void**)&Doc);
					if (FAILED(r))
					{
						LgiTrace("%s:%i - QueryInterface(IID_IHTMLDocument2...) failed.\n", _FL);
					}
					else
					{
						READYSTATE Rs = READYSTATE_UNINITIALIZED;
						HRESULT r = Browser->get_ReadyState(&Rs);
						// LgiTrace("rs=%i\n", Rs);
						if (FAILED(r))
						{
							LgiTrace("%s:%i - get_ReadyState failed.\n", _FL);
						}
						else if (Rs != READYSTATE_LOADING)
						{
							OnLoad();
						}
					}
				}
			}

			return true;
		}
	}

	return false;
}

void HtmlWrapper::Visible(bool i)
{
	if (_View)
	{
		ShowWindow(_View, (i) ? SW_RESTORE : SW_HIDE);
	}
}

bool HtmlWrapper::Visible()
{
	return (_View) ? TestFlag(GetWindowLong(_View, GWL_STYLE), WS_VISIBLE) : false;
}

void HtmlWrapper::Sunken(bool i)
{
	if (_View)
	{
		long n = GetWindowLong(_View, GWL_EXSTYLE);
		if (i)
		{
			SetWindowLong(_View, GWL_EXSTYLE, WS_EX_CLIENTEDGE | n);
		}
		else
		{
			SetWindowLong(_View, GWL_EXSTYLE, ~WS_EX_CLIENTEDGE & n);
		}
	}
	else
	{
		LView::Sunken(i);
	}
}

bool HtmlWrapper::Sunken()
{
	return (_View) ? TestFlag(GetWindowLong(_View, GWL_EXSTYLE), WS_EX_CLIENTEDGE) : false;
}

bool HtmlWrapper::SetPos(LRect &p, bool Repaint)
{
	LDocView::SetPos(p);

	if ((HWND)Wnd)
	{
		RECT Rc = p;
		Wnd.MoveWindow(&Rc, Repaint);
	}

	return true;
}

LRect &HtmlWrapper::GetPos()
{
	static LRect r;

	r.ZOff(-1, -1);
	if (_View && GetParent())
	{
		RECT rc;
		GetWindowRect(_View, &rc);

		POINT p = { rc.left, rc.top };
		ScreenToClient(GetParent()->Handle(), &p);
		r.x1 = p.x;
		r.y1 = p.y;

		p.x = rc.right;
		p.y = rc.bottom;
		ScreenToClient(GetParent()->Handle(), &p);
		r.x2 = p.x;
		r.y2 = p.y;
	}

	return r;
}

bool HtmlWrapper::Attach(LViewI *Parent)
{
	bool Status = false;
	RECT Rc = GetPos();
	TCHAR *Ctrl = UseMoz ? _T("Mozilla.Browser.1") : _T("Shell.Explorer.2");

	if (Wnd.Create(	Parent->Handle(),
					Rc,
					Ctrl,
					WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
					0))
	{
		if (Wnd.QueryControl(IID_IWebBrowser2, (void**)&Browser) == S_OK)
		{
			_View = (HWND)Wnd;
			SetWindowLong(_View, GWL_STYLE, WS_TABSTOP | GetWindowLong(_View, GWL_STYLE));
			if (LView::Sunken())
			{
				SetWindowLong(_View, GWL_EXSTYLE, WS_EX_CLIENTEDGE | GetWindowLong(_View, GWL_EXSTYLE));
			}
			if (!Parent->HasView(this))
			{
				Parent->AddView(this);
				Parent->OnChildrenChanged(this, true);
			}
			SetParent(Parent);

			DWORD pdw;
			HRESULT hr = AtlAdvise(Browser, this, DIID_DWebBrowserEvents2, &pdw);
			// LgiTrace("AtlAdvise=%i\n", hr);

			CComVariant v;
			_bstr_t u = TempFile;
			Browser->Navigate(u, &v, &v, &v, &v);

			Status = true;
		}
	}
	
	return Status;
}

bool HtmlWrapper::Detach()
{
	if (Doc)
	{
		Doc->Release();
		Doc = NULL;
	}
	if (Browser)
	{
		Browser->Release();
		Browser = 0;
	}
	if ((HWND)Wnd)
	{
		Wnd.DestroyWindow();
	}
	if (GetParent())
	{
		GetParent()->DelView(this);
		GetParent()->OnChildrenChanged(this, false);
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
LDocView *CreateIeControl(int Id)
{
	return new HtmlWrapper(Id);
}