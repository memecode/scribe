#include "ScribeMapi.h"

LMapiThing::LMapiThing(LMapiStore *store)
{
	Store = store;
}

LMapiThing::~LMapiThing()
{
	if (Store && IsDirty)
		Store->Dirty.Delete(this);
	ReleaseHandle();		
}

void LMapiThing::ReleaseHandle()
{
	if (MapiMsg)
	{
		MapiMsg->Release();
		MapiMsg = NULL;
	}
}

void LMapiThing::SetDirty()
{
	if (Store)
	{
		if (!IsDirty)
		{
			IsDirty = true;
			Store->Dirty.Add(this);
		}
	}
	else LAssert(0);
}

bool LMapiThing::MapiGetNamedPropLong(IMAPIProp *props, const GUID &guid, LONG dispid, LONG &value)
{
	if (!props)
		return false;

	MAPINAMEID nameid;
	nameid.lpguid = (LPGUID)&guid;
	nameid.ulKind = MNID_ID;
	nameid.Kind.lID = dispid;
	LPMAPINAMEID ids[1] = { &nameid };

	LPSPropTagArray tags = nullptr;
	auto res = props->GetIDsFromNames(1, ids, 0, &tags);
	if (FAILED(res) || !tags)
		return false;

	SPropTagArray req;
	req.cValues = 1;
	req.aulPropTag[0] = CHANGE_PROP_TYPE(tags->aulPropTag[0], PT_LONG);
	if (Store)
		Store->FreeMapiBuffer(tags);

	SPropValue *vals = nullptr;
	ULONG got = 0;
	res = props->GetProps(&req, 0, &got, &vals);
	if (FAILED(res) || got != 1 || !vals)
		return false;

	if (PROP_TYPE(vals->ulPropTag) != PT_LONG)
	{
		if (Store)
			Store->FreeMapiBuffer(vals);
		return false;
	}

	value = vals->Value.l;
	if (Store)
		Store->FreeMapiBuffer(vals);
	return true;
}

bool LMapiThing::MapiSetNamedPropLong(IMAPIProp *props, const GUID &guid, LONG dispid, LONG value)
{
	if (!props)
		return false;

	MAPINAMEID nameid;
	nameid.lpguid = (LPGUID)&guid;
	nameid.ulKind = MNID_ID;
	nameid.Kind.lID = dispid;
	LPMAPINAMEID ids[1] = { &nameid };

	LPSPropTagArray tags = nullptr;
	auto res = props->GetIDsFromNames(1, ids, MAPI_CREATE, &tags);
	if (FAILED(res) || !tags)
		return false;

	SPropValue p;
	p.ulPropTag = CHANGE_PROP_TYPE(tags->aulPropTag[0], PT_LONG);
	p.Value.l = value;
	if (Store)
		Store->FreeMapiBuffer(tags);

	res = props->SetProps(1, &p, 0);
	return SUCCEEDED(res);
}

bool LMapiThing::MapiGetNamedPropBinary(IMAPIProp *props, const GUID &guid, LONG dispid, LString &value)
{
	if (!props)
		return false;

	MAPINAMEID nameid;
	nameid.lpguid = (LPGUID)&guid;
	nameid.ulKind = MNID_ID;
	nameid.Kind.lID = dispid;
	LPMAPINAMEID ids[1] = { &nameid };

	LPSPropTagArray tags = nullptr;
	auto res = props->GetIDsFromNames(1, ids, 0, &tags);
	if (FAILED(res) || !tags)
		return false;

	SPropTagArray req;
	req.cValues = 1;
	req.aulPropTag[0] = CHANGE_PROP_TYPE(tags->aulPropTag[0], PT_BINARY);
	if (Store)
		Store->FreeMapiBuffer(tags);

	SPropValue *vals = nullptr;
	ULONG got = 0;
	res = props->GetProps(&req, 0, &got, &vals);
	if (FAILED(res) || got != 1 || !vals)
		return false;

	bool ok = PROP_TYPE(vals->ulPropTag) == PT_BINARY && vals->Value.bin.cb > 0;
	if (ok)
		value.Set((const char*)vals->Value.bin.lpb, vals->Value.bin.cb);

	if (Store)
		Store->FreeMapiBuffer(vals);
	return ok;
}

bool LMapiThing::MapiSetNamedPropBinary(IMAPIProp *props, const GUID &guid, LONG dispid, const void *data, ULONG size)
{
	if (!props || !data || size == 0)
		return false;

	MAPINAMEID nameid;
	nameid.lpguid = (LPGUID)&guid;
	nameid.ulKind = MNID_ID;
	nameid.Kind.lID = dispid;
	LPMAPINAMEID ids[1] = { &nameid };

	LPSPropTagArray tags = nullptr;
	auto res = props->GetIDsFromNames(1, ids, MAPI_CREATE, &tags);
	if (FAILED(res) || !tags)
		return false;

	SPropValue p;
	p.ulPropTag = CHANGE_PROP_TYPE(tags->aulPropTag[0], PT_BINARY);
	p.Value.bin.cb = size;
	p.Value.bin.lpb = (LPBYTE)data;
	if (Store)
		Store->FreeMapiBuffer(tags);

	res = props->SetProps(1, &p, 0);
	return SUCCEEDED(res);
}
