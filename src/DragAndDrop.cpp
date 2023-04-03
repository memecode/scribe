/*
**	FILE:		DragAndDrop.cpp
**	AUTHOR:		Matthew Allen
**	DATE:		30/11/98
**	DESCRIPTION:	Drag and drop support
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

#include "defs.h"
#include "memdev.h"
#include "file.h"
#include "gdc2.h"
#include "gui.h"
#include "DragAndDrop.h"

/////////////////////////////////////////////////////////////////////////////////////////
ULONG STDMETHODCALLTYPE
GDDSource::AddRef()
{
	return 1;
}

ULONG STDMETHODCALLTYPE
GDDSource::Release()
{
	return 0;
}

HRESULT
STDMETHODCALLTYPE
GDDSource::QueryInterface(REFIID iid, void **ppv)
{
	*ppv=NULL;
	if (IID_IUnknown==iid)
	{
		*ppv=(void*)(IUnknown*)(IDataObject*)this;
	}

	if (IID_IEnumFORMATETC==iid)
	{
		*ppv=(void*)(IEnumFORMATETC*) this;
	}

	if (IID_IDataObject==iid)
	{
		*ppv=(void*)(IDataObject*) this;
	}

	if (IID_IDropSource==iid)
	{
		*ppv=(void*)(IDropSource*) this;
	}

	if (NULL==*ppv)
	{
		return E_NOINTERFACE;
	}

	AddRef();
	
	return NOERROR;
}

HRESULT STDMETHODCALLTYPE
GDDSource::Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched)
{
	if (rgelt AND Index == 0)
	{
		rgelt->cfFormat = DdFormat;
		rgelt->ptd = 0;
		rgelt->dwAspect = DVASPECT_CONTENT;
		rgelt->lindex = -1;
		rgelt->tymed = TYMED_HGLOBAL;
		Index++;

		if (pceltFetched)
		{
			*pceltFetched = 1;
			return (celt == *pceltFetched) ? S_OK : S_FALSE;
		}

		return S_OK;
	}

	return S_FALSE;
}

HRESULT STDMETHODCALLTYPE
GDDSource::Skip(ULONG celt)
{
	Index += celt;
	return S_FALSE;
}

HRESULT STDMETHODCALLTYPE
GDDSource::Reset()
{
	Index = 0;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE
GDDSource::Clone(IEnumFORMATETC **ppenum)
{
	if (ppenum)
	{
		*ppenum = new GDDSource();
		if (*ppenum)
		{
			return S_OK;
		}
		return E_OUTOFMEMORY;
	}
	return E_INVALIDARG;
}

HRESULT
STDMETHODCALLTYPE
GDDSource::QueryContinueDrag(BOOL fEscapePressed, DWORD InputState)
{
	if (fEscapePressed)
	{
		return DRAGDROP_S_CANCEL;
	}

	if (NOT (InputState & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
	{
		return DRAGDROP_S_DROP;
	}

	return S_OK;
}

HRESULT
STDMETHODCALLTYPE
GDDSource::GiveFeedback(DWORD dwEffect)
{
	return DRAGDROP_S_USEDEFAULTCURSORS;
}

HRESULT STDMETHODCALLTYPE
GDDSource::GetData(FORMATETC *pFormatEtc, STGMEDIUM *PMedium)
{
	return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE
GDDSource::GetDataHere(FORMATETC *pFormatEtc, STGMEDIUM *PMedium)
{
	return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE
GDDSource::QueryGetData(FORMATETC *pFormatEtc)
{
	if (pFormatEtc)
	{
		if (pFormatEtc->cfFormat != CF_HDROP)
		{
			return DV_E_FORMATETC;
		}

		if (pFormatEtc->tymed != TYMED_HGLOBAL)
		{
			return DV_E_TYMED;
		}

		return S_OK;
	}
	
	return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE
GDDSource::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppFormatEtc)
{
	if (ppFormatEtc)
	{
		*ppFormatEtc = (IEnumFORMATETC*) this;
		return (*ppFormatEtc) ? S_OK : E_OUTOFMEMORY;
	}

	return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE
GDDSource::GetCanonicalFormatEtc(FORMATETC * pFormatetcIn, FORMATETC * pFormatetcOut)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE
GDDSource::SetData(FORMATETC * pFormatetc, STGMEDIUM * pmedium, BOOL fRelease)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE
GDDSource::DAdvise(FORMATETC * pFormatetc, DWORD advf, IAdviseSink * pAdvSink, DWORD * pdwConnection)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE
GDDSource::DUnadvise(DWORD dwConnection)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE
GDDSource::EnumDAdvise(IEnumSTATDATA **ppenumAdvise)
{
	return E_UNEXPECTED;
}

GDDSource::GDDSource()
{
	Index = 0;
	// DdFormat = RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
	DdFormat = CF_HDROP;
}

BOOL GDDSource::DoDragDrop(uint Effect)
{
	DWORD dwEffect;
	return (::DoDragDrop(this, this, Effect, &dwEffect) == DRAGDROP_S_DROP);
}


////////////////////////////////////////////////////////////////////////////////////////////
GDDTarget::GDDTarget()
{
}

ULONG STDMETHODCALLTYPE GDDTarget::AddRef()
{
	return 1;
}

ULONG STDMETHODCALLTYPE GDDTarget::Release()
{
	return 0;
}

HRESULT STDMETHODCALLTYPE GDDTarget::QueryInterface(REFIID iid, void **ppv)
{
	*ppv=NULL;

	if (IID_IUnknown==iid)
	{
		*ppv=(void*)(IUnknown*)(IDataObject*) this;
	}

	if (IID_IDropTarget==iid)
	{
		*ppv=(void*)(IDropTarget*) this;
	}

	if (NULL==*ppv)
	{
		return E_NOINTERFACE;
	}

	AddRef();
	
	return NOERROR;
}

HRESULT STDMETHODCALLTYPE GDDTarget::DragEnter(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE GDDTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE GDDTarget::DragLeave(void)
{
	return E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE GDDTarget::Drop(IDataObject * pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	return E_UNEXPECTED;
}

