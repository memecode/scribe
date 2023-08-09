#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "ScribeImap.h"
#include "lgi/common/TextConvert.h"

ImapAttachment::ImapAttachment(ImapStore *store, ImapMail *mail, LMime *seg) :
	Store3Attachment<ImapStore, ImapMail, ImapAttachment>(store)
{
	Mail = mail;
	InMemoryOnly = false;
	if ((Seg = seg))
	{
		ImapAttachment *c;
		for (int i=0; i<Seg->Length(); i++)
		{
			if ((c = new ImapAttachment(Kit, 0, (*Seg)[i])))
			{
				c->AttachTo(this);
			}
		}
	}
}

ImapAttachment::ImapAttachment(ImapStore *store, ImapMail *mail, LDataPropI *att) :
	Store3Attachment<ImapStore, ImapMail, ImapAttachment>(store)
{
	Mail = mail;
	InMemoryOnly = false;
	if ((Seg = new LMime))
	{
		if (att)
		{
			LDataPropI *d = dynamic_cast<LDataPropI*>(att);
			if (d)
				CopyProps(*d);
			else
				LAssert(!"Not the right object.");
		}
	}
}

ImapAttachment::~ImapAttachment()
{
	// Deleting the seg here means that all the child mime segs will be deleted while
	// out attachment children still have pointers to them. So we clear the mime seg
	// pointers owned by our ImapAttachment children first... so that they don't have
	// dangling pointers.
	_ClearChildSegs();
	DeleteObj(Seg);
	_Delete();
}

void ImapAttachment::SetInMemoryOnly(bool b)
{
	InMemoryOnly = b;
}

void ImapAttachment::_ClearChildSegs()
{
	for (unsigned i=0; i<Children.Length(); i++)
	{
		ImapAttachment *c = Children.a[i];
		c->Seg = 0;
		c->_ClearChildSegs();
	}
}

void ImapAttachment::OnSave()
{
	if (!Mail)
	{
		LAssert(!"Segment is not attached to a mail!");
		return;
	}

	if (Dirty)
	{
		Save();
	}

	for (unsigned i=0; i<Children.Length(); i++)
	{
		Children.a[i]->OnSave();
	}
}

Store3CopyImpl(ImapAttachment)
{
	if (!Seg)
		Seg = new LMime;

	auto Hdrs = p.GetStr(FIELD_INTERNET_HEADER);
	if (Hdrs)
	{
	    SetStr(FIELD_INTERNET_HEADER, p.GetStr(FIELD_INTERNET_HEADER));
	}
	else
	{
	    SetStr(FIELD_MIME_TYPE, p.GetStr(FIELD_MIME_TYPE));
	    SetStr(FIELD_NAME, p.GetStr(FIELD_NAME));
	    SetStr(FIELD_CONTENT_ID, p.GetStr(FIELD_CONTENT_ID));
	    SetStr(FIELD_CHARSET, p.GetStr(FIELD_CHARSET));
	}

	LDataI *Data = dynamic_cast<LDataI*>(&p);
	if (Data)
	{
		LAutoStreamI tmp = Data->GetStream(_FL);
		SetStream(tmp);
	}

	return true;
}

uint32_t ImapAttachment::Type()
{
	return MAGIC_ATTACHMENT;
}

bool ImapAttachment::IsOnDisk()
{
	return Seg != 0;
}

uint64 ImapAttachment::Size()
{
	int64 Size = 0;

	if (Seg)
	{
		auto h = Seg->GetHeaders();
		Size += h ? strlen(h) : 0;
		LStreamI *s = Seg->GetData();
		if (s)
		{
			int64 sz = s->GetSize();
			if (sz > 0)
				Size += sz;
		}
	}

	return Size;
}

const char *ImapAttachment::GetStr(int id)
{
	switch (id)
	{
		case FIELD_NAME:
		{
			if (!Name)
			{
				Name = LDecodeRfc2047(Seg->LGetSub("Content-Type", "name"));
				if (!Name)
					Name = LDecodeRfc2047(Seg->LGetSub("Content-Disposition", "filename"));
			}
			return Name;
		}
		case FIELD_MIME_TYPE:
		{
			if (!MimeType)
				MimeType = Seg->LGetMimeType();
			return MimeType;
		}
		case FIELD_CONTENT_ID:
		{
			if (!ContentId)
				ContentId = Seg->LGet("Content-Id");
			return ContentId;
		}
		case FIELD_CHARSET:
		{
			if (!Charset)
			{
			    // Check the headers...
				Charset = Seg->LGetSub("Content-Type", "charset");				
		        if (!Charset)
		        {
			        // Maybe a parent segment has a charset?
			        for (ImapAttachment *p = GetParent(); p; p = p->GetParent())
			        {
			            auto Cs = p->GetStr(FIELD_CHARSET);
			            if (Cs)
			                return Cs;
			        }
                }
			}
			return Charset;
		}
		case FIELD_INTERNET_HEADER:
		{
			return Seg->GetHeaders();
			break;
		}
	}

	LAssert(0);
	return 0;
}

Store3Status ImapAttachment::SetStr(int id, const char *str)
{
	LAssert(Seg != NULL);
	if (!Seg)
		return Store3Error;
	switch (id)
	{
		case FIELD_INTERNET_HEADER:
		{
			Seg->SetHeaders(str);
			break;
		}
		case FIELD_NAME:
		{
			Seg->SetSub("Content-Type", "name", str, "application/octet");
			break;
		}
		case FIELD_MIME_TYPE:
		{
			Seg->SetMimeType(str);
			break;
		}
		case FIELD_CONTENT_ID:
		{
			Seg->Set("Content-Id", str);
			break;
		}
		case FIELD_CHARSET:
		{
			Seg->SetSub("Content-Type", "charset", str);
			break;
		}
		default:
			LAssert(0);
			return Store3Error;
	}

	return Store3Success;
}

int64 ImapAttachment::GetInt(int id)
{
	switch (id)
	{
		case FIELD_SIZE:
		{
			return Seg ? Seg->GetLength() : 0;
			break;
		}
		case FIELD_STORE_TYPE:
		{
			return Store3Imap;
		}
	}

	LAssert(0);
	return -1;
}

Store3Status ImapAttachment::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_SIZE:
		{
			break;
		}
	}

	return Store3Error;
}

Store3Status ImapAttachment::Save(LDataI *Folder)
{
	LAssert(0);
	return Store3Error;
}

Store3Status ImapAttachment::Delete(bool ToTrash)
{
	if (Mail)
		Mail->SegDirty = true;
	_Delete();
	
	return Store3Success;
}

LAutoStreamI ImapAttachment::GetStream(const char *file, int line)
{
	LStreamI *s = Seg->GetData();
	// int64 size = s->GetSize();
	// int64 pos = s->GetPos();
	return LAutoStreamI(s && s->GetSize() > 0 ? new LProxyStream(s) : 0);
}

bool ImapAttachment::SetStream(LAutoStreamI stream)
{
	return Seg->SetData(true, new LMemStream(stream, 0, -1));
}
