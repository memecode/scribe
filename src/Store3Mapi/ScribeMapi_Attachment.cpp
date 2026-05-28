#include <WinSock2.h>
#include <shobjidl.h>
#include "ScribeMapi.h"
#include "lgi/common/Com.h"

LMapiAttachment::LMapiAttachment(LMapiStore *store) : Store3Attachment(store)
{
	MapiAttach = NULL;
	AttachNum = -1;
	DataSize = 0;
}

LMapiAttachment::~LMapiAttachment()
{
	if (MapiAttach)
	{
		MapiAttach->Release();
		MapiAttach = NULL;
	}
}

LPATTACH LMapiAttachment::Handle()
{
	if (!MapiAttach &&
		Mail &&
		Mail->Handle() &&
		AttachNum >= 0)
	{
		HRESULT res = Mail->Handle()->OpenAttach(AttachNum, NULL, MAPI_BEST_ACCESS, &MapiAttach);
		if (FAILED(res))
				Kit->Error("%s:%i - OpenAttach failed with 0x%x\n", _FL, res);
	}
	
	return MapiAttach;
}

bool LMapiAttachment::Set(LMapiMail *mail, LMapiList *Lst)
{
	SetMail(mail);
	
	AttachNum = (ULONG)MapiCastInt(Lst->GetField(PR_ATTACH_NUM));
	AttachMethod = (ULONG)MapiCastInt(Lst->GetField(PR_ATTACH_METHOD));
	DataSize = MapiCastInt(Lst->GetField(PR_ATTACH_SIZE));
	Name = MapiCastString(Lst->GetField(PR_ATTACH_LONG_FILENAME_W));
	if (!Name)
		Name = MapiCastString(Lst->GetField(PR_ATTACH_FILENAME_W));
	if (!Name)
		Name = MapiCastString(Lst->GetField(PR_DISPLAY_NAME_W));
	MimeType = MapiCastString(Lst->GetField(PR_ATTACH_MIME_TAG_W));
	ContentId = MapiCastString(Lst->GetField(PR_ATTACH_CONTENT_ID_W));
	
	if (!MimeType)
	{
		// Synthesize mime type from file name?
		auto Mt = LGetFileMimeType(Name);
		if (Mt)
			MimeType = Mt;
	}

	return true;
}

bool LMapiAttachment::Set(const char *content, const char *charset, const char *mimeType)
{
	Literal = content;
	Charset = charset;
	MimeType = mimeType;
	DataSize = Literal.Length();
	return true;
}

Store3CopyImpl(LMapiAttachment)
{
	LAssert(0);
	return false;
}

const char *LMapiAttachment::GetStr(int id)
{
	switch (id)
	{
		case FIELD_INTERNET_HEADER:
			if (!Headers)
				GenerateHeaders();
			return Headers;
		case FIELD_NAME:
			return Name;
		case FIELD_MIME_TYPE:
			return MimeType ? MimeType : sAppOctetStream;
		case FIELD_CONTENT_ID:
			return ContentId;
		case FIELD_CHARSET:
			return Charset;
		default:
			LAssert(0);
			break;
	}
	
	return NULL;
}

Store3Status LMapiAttachment::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_INTERNET_HEADER:
			Headers = str;
			return Store3Success;
		case FIELD_NAME:
			Name = str;
			break;
		case FIELD_MIME_TYPE:
			MimeType = str;
			break;
		case FIELD_CONTENT_ID:
			ContentId = str;
			break;
		case FIELD_CHARSET:
			Charset = str;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}
	
	return Store3Success;
}

int64 LMapiAttachment::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Mapi;
		case FIELD_SIZE:
			return DataSize;
		default:
			LAssert(0);
			break;
	}
	
	return -1;
}

Store3Status LMapiAttachment::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_SIZE:
			DataSize = i;
			break;
		default:
			LAssert(0);
			return Store3Error;
	}
	
	return Store3Success;
}

uint32_t LMapiAttachment::Type()
{
	return MAGIC_ATTACHMENT;
}

bool LMapiAttachment::IsOnDisk()
{
	return Mail != nullptr;
}

bool LMapiAttachment::IsOrphan()
{
	return Mail == nullptr;
}

uint64 LMapiAttachment::Size()
{
	LAssert(0);
	return 0;
}

Store3Status LMapiAttachment::Save(LDataI *Parent)
{
	LAssert(0);
	return Store3Error;
}

Store3Status LMapiAttachment::Delete(bool ToTrash)
{
	LAssert(0);
	return Store3Error;
}

LAutoStreamI LMapiAttachment::GetStream(const char *file, int line)
{
	LAutoStreamI s;
	
	if (Literal)
	{
		s.Reset(new LMemStream(Literal, Literal.Length(), false));
	}
	else if (Handle())
	{
		switch (AttachMethod)
		{
		    case ATTACH_BY_VALUE:
		    {
				IStream *Stream = NULL;
				HRESULT res = Handle()->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, 0, (IUnknown**)&Stream);
			    if (FAILED(res) || !Stream)
					break;

				s.Reset(new IStreamWrap(Stream));
			    break;
		    }
		    case ATTACH_BY_REFERENCE:
		    case ATTACH_BY_REF_RESOLVE:
		    case ATTACH_BY_REF_ONLY:
		    {
			    LString FileName = MapiGetPropStr(Handle(), PR_ATTACH_LONG_PATHNAME);
			    LAutoPtr<LFile> File;
			    if (FileName && File.Reset(new LFile))
			    {
					if (!File->Open(FileName, O_READ))
						File.Reset();
					else
						s.Reset(File.Release());
			    }																		
			    break;
		    }
			default:
		    case ATTACH_EMBEDDED_MSG:
		    {
				LAssert(!"Not impl.");
			    break;
		    }
	    }
	}
	
	return s;
}

void LMapiAttachment::OnSave()
{
}

