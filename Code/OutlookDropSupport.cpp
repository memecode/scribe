#ifdef WIN32
#include <WINSOCK2.H>
#include <windows.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <mapiutil.h>
#include "lgi/common/Com.h"
#include "lgi/common/NetTools.h"
#include "lgi/common/RtfHtml.h"
#include "lgi/common/Variant.h"

#ifndef PR_BODY_HTML
#define PR_BODY_HTML		PROP_TAG(PT_TSTRING, 0x1013)
#endif

class IDataStream : public GUnknownImpl<IStream>
{
	int Len;
	char *Data;
	int Pos;

public:
	IDataStream(LVariant &v)
	{
		Len = v.Value.Binary.Length;
		Data = new char[Len];
		if (Data)
		{
			memcpy(Data, v.Value.Binary.Data, Len);
		}
		Pos = 0;

		AddInterface(IID_IStream, this);
		AddInterface(IID_ISequentialStream, this);
	}

	HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead)
	{
		if (pv)
		{
			int r = min((unsigned)(Len-Pos), cb);
			if (r)
			{
				memcpy(pv, Data + Pos, r);
				Pos += r;
				if (pcbRead)
				{
					*pcbRead = r;
				}
				return S_OK;
			}
		}

		return S_FALSE;
	}

	HRESULT STDMETHODCALLTYPE Write(void const *pv, ULONG cb, ULONG *pcbWritten) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Revert(void) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Stat(STATSTG *pstatstg, DWORD grfStatFlag) { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) { return S_FALSE; }
};

class OutlookConverter
{
	static bool MapiLoaded;

protected:
	ScribeWnd *App;

public:
	OutlookConverter(ScribeWnd *app)
	{
		App = app;
	}

	virtual bool GetProp(LVariant &v, int Prop) { return 0; }
	virtual OutlookConverter *GetAttachment(int i) { return 0; }
	virtual char *GetHtml() { return 0; }

	char *GetSegment(char *ContentType)
	{
		if (!ContentType) ContentType = "text/plain";

		LVariant Section;
		if (_stricmp(ContentType, "text/plain") == 0)
		{
			GetProp(Section, PR_BODY);
		}
		else if (_stricmp(ContentType, "text/html") == 0)
		{	
			if (!GetProp(Section, PR_BODY_HTML))
			{
				Section = GetHtml();
			}
		}

		return NewStr(Section.Str());
	}

	int GetType()
	{
		LVariant Obj;
		
		if (GetProp(Obj, PR_BODY))
		{
			return MAGIC_MAIL;
		}
		if (GetProp(Obj, PR_GIVEN_NAME))
		{
			return MAGIC_CONTACT;
		}

		return MAGIC_NONE;
	}
	
	Mail *GetMail()
	{
		Mail *m = 0;
		LVariant Headers;
		if (GetProp(Headers, PR_TRANSPORT_MESSAGE_HEADERS) &&
			Headers.Str())
		{
			m = new Mail(App);
			if (m)
			{
				m->App = App;

				LAutoStreamI Mem( new LMemStream(Headers.Str(), strlen(Headers.Str())) );
				m->OnAfterReceive(Mem);

				m->SetBody(GetSegment("text/plain"));
				m->SetHtml(GetSegment("text/html"));

				for (int i=0; true; i++)
				{
					OutlookConverter *Oa = GetAttachment(i);
					if (Oa)
					{
						LVariant v;
						char *Name = 0;
						if (Oa->GetProp(v, PR_ATTACH_LONG_FILENAME_A) ||
							Oa->GetProp(v, PR_ATTACH_FILENAME_A))
						{
							Name = v.Str();
							if (ValidStr(Name))
							{
								Attachment *a = new Attachment(m->App);
								if (a)
								{
									a->App = m->App;
									a->SetName(Name);

									if (Oa->GetProp(v, PR_ATTACH_DATA_BIN))
									{
										if (v.Type == GV_BINARY)
										{
											a->Set((char*)v.Value.Binary.Data, v.Value.Binary.Length);
										}
									}
									if (Oa->GetProp(v, PR_ATTACH_MIME_TAG_A))
									{
										// We love MIME, We love MIME, We love MIME... ;)
										a->SetMimeType(v.Str());
									}

									m->AttachFile(a);
								}
							}
						}

						DeleteObj(Oa);
					}
					else break;
				}
			}
		}

		return m;
	}

	Contact *GetContact()
	{
		Contact *c = new Contact(App);
		if (c)
		{
			LVariant v;
			#define MapProp(s, o) \
				if (GetProp(v, o) && v.Str()) \
				{ c->Set(s, v.Str()); }

			// Simple props
			MapProp(OPT_First, PR_GIVEN_NAME);
			MapProp(OPT_Last, PR_SURNAME);
			MapProp(OPT_HomeStreet, PR_STREET_ADDRESS);
			MapProp(OPT_HomeSuburb, PR_LOCALITY);
			MapProp(OPT_HomePostcode, PR_POSTAL_CODE);
			MapProp(OPT_HomeState, PR_STATE_OR_PROVINCE);
			MapProp(OPT_HomeCountry, PR_COUNTRY);
			MapProp(OPT_WorkPhone, PR_BUSINESS_TELEPHONE_NUMBER);
			MapProp(OPT_HomePhone, PR_HOME_TELEPHONE_NUMBER);
			MapProp(OPT_HomeMobile, PR_CELLULAR_TELEPHONE_NUMBER);
			MapProp(OPT_HomeFax, PR_PRIMARY_FAX_NUMBER);
			MapProp(OPT_HomeWebPage, PR_PERSONAL_HOME_PAGE);
			MapProp(OPT_WorkWebPage, PR_BUSINESS_HOME_PAGE);
			MapProp(OPT_Nick, PR_NICKNAME);
			MapProp(OPT_Spouse, PR_SPOUSE_NAME);
			MapProp(OPT_Note, PR_BODY);
			
			// Email address... fucking M$ programmers try and 
			// hide the email in various undefined tags...
			// Just keep trying until we get a hit.
			uint32_t EmailTags[] = 
			{
				PR_EMAIL_ADDRESS,
				PR_CONTACT_EMAIL_ADDRESSES
			};

			bool GotEmail = false;
			int e;
			for (e=0; e<CountOf(EmailTags); e++)
			{
				if (GetProp(v, EmailTags[e]))
				{
					char *s = v.Str();
					if (s && strchr(s, '@'))
					{
						c->Set(OPT_Email, v.Str());
						GotEmail = true;
						break;
					}
				}
			}

			if (!GotEmail)
			{
				for (e=0x8000; e<0x8100; e++)
				{
					if (GetProp(v, PROP_TAG(PT_STRING8, e)))
					{
						char *s = v.Str();
						if (s && strchr(s, '@'))
						{
							c->Set(OPT_Email, v.Str());
							GotEmail = true;
							break;
						}
					}
				}

				if (!GotEmail)
				{
					/*
					// If the email address isn't getting through...
					// Try uncommenting this and see what values of i
					// it stops on
					for (int i=0x3000; i<= 0x9000; i++)
					{
						if (GetProp(v, PROP_TAG(PT_STRING8, i)) && v.Str())
						{
							if (strchr(v.Str(), '@'))
							{
								_asm int 3
							}
						}
					}
					*/
				}
			}
		}
		return c;
	}
};

class OutlookIStorage : public OutlookConverter
{
	static bool MapiLoaded;
	IStorage *Store;
	bool Release;

public:
	OutlookIStorage(ScribeWnd *App, IStorage *s, bool r) : OutlookConverter(App)
	{
		Store = s;
		Release = r;
	}

	~OutlookIStorage()
	{
		if (Release)
		{
			Store->Release();
		}
	}

	char *GetHtml()
	{
		char *Ret = 0;
		LVariant Html;
		GetProp(Html, PR_RTF_COMPRESSED);

		LLibrary Mapi("Mapi32");

		typedef HRESULT (STDAPICALLTYPE *p_MAPIInitialize)(LPVOID lpMapiInit);
		typedef void (STDAPICALLTYPE *p_MAPIUninitialize)();
		typedef HRESULT(STDAPICALLTYPE *p_WrapCompressedRTFStream)(LPSTREAM lpCompressedRTFStream, ULONG ulFlags, LPSTREAM FAR * lpUncompressedRTFStream);

		p_MAPIInitialize MAPIInitialize = (p_MAPIInitialize) Mapi.GetAddress("MAPIInitialize");
		p_MAPIUninitialize MAPIUninitialize = (p_MAPIUninitialize) Mapi.GetAddress("MAPIUninitialize");
		p_WrapCompressedRTFStream WrapCompressedRTFStream = (p_WrapCompressedRTFStream) Mapi.GetAddress("WrapCompressedRTFStream");

		if (MAPIInitialize &&
			MAPIUninitialize && 
			WrapCompressedRTFStream)
		{
			LDialog *Wait = 0;
			if (!MapiLoaded)
			{
				MapiLoaded = true;
				Wait = new LDialog;
				if (Wait)
				{
					Wait->Name(AppName);
					Wait->SetPos(LRect(0, 0, 150, 60));
					Wait->MoveToCenter();
					Wait->AddView(new LTextLabel(1, 10, 10, -1, -1, "Connecting to MAPI..."));
					Wait->DoModeless();
					Wait->AttachChildren();
				}
			}
			MAPIInitialize(0);
			if (Wait)
			{
				Wait->EndModeless();
			}

			IDataStream *i = new IDataStream(Html);
			if (i)
			{
				IStream *o;
				if (WrapCompressedRTFStream(i, 0, &o) == S_OK)
				{
					LStringPipe h;
					char Buf[256];
					ULONG r;
					HRESULT Err;
					while ((Err = o->Read(Buf, sizeof(Buf), &r)) == S_OK && r > 0)
					{
						h.Push(Buf, r);
					}
					o->Release();

					char *Text = h.NewStr();
					if (Text)
					{
						LFile f;
						if (f.Open("d:\\temp\\mail.rtf", O_WRITE))
						{
							f.SetSize(0);
							f.Write(Text, strlen(Text));
							f.Close();
						}

						Ret = MsRtfToHtml(Text);
						DeleteArray(Text);
					}
				}
			}

			MAPIUninitialize();
		}

		return Ret;
	}

	OutlookConverter *GetAttachment(int i)
	{
		OutlookConverter *Status = 0;

		if (Store)
		{
			IStorage *New = 0;
			char Part[256];
			sprintf_s(Part, sizeof(Part), "__attach_version1.0_#%8.8d", i);
			char16 *Name = Utf8ToWide(Part);

			HRESULT Err;
			if ((Err = Store->OpenStorage(Name, 0, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, 0, &New)) == S_OK)
			{
				Status = new OutlookIStorage(App, New, true);
			}

			DeleteArray(Name);
		}

		return Status;
	}

	bool GetProp(LVariant &v, int Prop)
	{
		bool Status = false;

		if (Store)
		{
			IStream *Str = 0;
			char Part[256];
			STATSTG Stat;

			ZeroObj(Stat);
			sprintf_s(Part, sizeof(Part), "__substg1.0_%08.8X", Prop);
			char16 *Name = Utf8ToWide(Part);

			HRESULT Err;
			if ((Err = Store->OpenStream(Name, 0, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &Str)) == S_OK)
			{
				if (Str->Stat(&Stat, STATFLAG_DEFAULT) == S_OK)
				{
					int Size = (int)Stat.cbSize.QuadPart;
					char *Buf = new char[Size + 1];
					if (Buf)
					{
						ulong Read;
						if (Str->Read(Buf, Size, &Read) == S_OK)
						{
							Buf[Size] = 0;

							Status = true;
							switch (PROP_TYPE(Prop))
							{
								case PT_STRING8:
								{
									v = Buf;
									break;
								}
								case PT_BINARY:
								{
									v.SetBinary(Size, Buf);
									break;
								}
								case PT_UNICODE:
								{
									LAssert(0);
									break;
								}
								case PT_I2:
								{
									LAssert(0);
									break;
								}
								case PT_I4:
								{
									LAssert(0);
									break;
								}
								case PT_I8:
								{
									LAssert(0);
									break;
								}
								default:
								{
									Status = false;
									LAssert(0);
									break;
								}
							}
						}
						DeleteArray(Buf);
					}
				}

				Str->Release();
			}

			DeleteArray(Name);
		}

		return Status;
	}
};

bool OutlookIStorage::MapiLoaded = false;
#endif

