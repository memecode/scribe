#include "Scribe.h"
#include "CalendarView.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

/////////////////////////////////////////////////////////////////////////////////////
WebdavFolder::WebdavFolder(WebdavStore *store, WebdavFolder *parent)
{
	Store = store;
	ItemType = MAGIC_ANY;
	Field.State = Store3Loaded;
	Sort = 0;

	if ((Parent = parent))
	{
		Parent->Sub.State = Store3Loaded;
		Parent->Sub.Insert(this);
	}
}

bool WebdavFolder::CopyProps(LDataPropI &p)
{
	LAssert(!"Impl me.");
	return false;
}

const char *WebdavFolder::GetStr(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			return Name;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return NULL;
}

Store3Status WebdavFolder::SetStr(int id, const char *str)
{
	switch (id)
	{
		case FIELD_FOLDER_NAME:
			Name = str;
			break;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return Store3NotImpl;
}

int64 WebdavFolder::GetInt(int id)
{
	switch (id)
	{
		case FIELD_FOLDER_TYPE:
			return ItemType;
		case FIELD_UNREAD:
			return 0;
		case FIELD_STORE_TYPE:
			return Store3Webdav;
		case FIELD_FOLDER_OPEN:
			return true;
		case FIELD_FOLDER_INDEX:
			if (Parent)
				return Parent->Sub.IndexOf(this);
			return 0;
		case FIELD_FOLDER_PERM_READ:
		case FIELD_FOLDER_PERM_WRITE:
			return PermRequireNone;
		case FIELD_SORT:
			return Sort;
		case FIELD_FOLDER_THREAD:
			return false;
		case FIELD_SYSTEM_FOLDER:
			if (ItemType == MAGIC_CONTACT)
				return Store3SystemContacts;
			else if (ItemType == MAGIC_CALENDAR)
				return Store3SystemCalendar;
			return Store3SystemNone;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return -1;
}

Store3Status WebdavFolder::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_FOLDER_TYPE:
			ItemType = (Store3ItemTypes)i;
			break;
		case FIELD_FOLDER_OPEN:
			break;
		case FIELD_SORT:
			Sort = i;
			break;
		case FIELD_LOADED:
			break;
		default:
			LAssert(!"Not impl.");
			return Store3NotImpl;
	}

	return Store3Success;
}

Store3Status WebdavFolder::Save(LDataI *Parent)
{
	return Store3NotImpl;
}

Store3Status WebdavFolder::Delete(bool ToTrash)
{
	return Store3NotImpl;
}

LAutoStreamI WebdavFolder::GetStream(const char *file, int line)
{
	LAutoStreamI s;
	return s;
}

LDataIterator<LDataFolderI*> &WebdavFolder::SubFolders()
{
	return Sub;
}

LDataIterator<LDataI*> &WebdavFolder::Children()
{
	return Items;
}

LDataIterator<LDataPropI*> &WebdavFolder::Fields()
{
	return Field;
}

///////////////////////////////////////////////////////////////////
WebdavFld::WebdavFld(LDataStoreI *s, WebdavFolder *p, int id, int width)
{
	Store = s;
	Parent = p;
	Id = id;
	Width = width;
}

const char *WebdavFld::GetStr(int id)
{
	LAssert(!"Not impl.");
	return NULL;
}

int64 WebdavFld::GetInt(int id)
{
	switch (id)
	{
		case FIELD_ID:
			return Id;
		case FIELD_WIDTH:
			return Width;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return -1;
}

Store3Status WebdavFld::SetInt(int id, int64 i)
{
	switch (id)
	{
		case FIELD_ID:
			Id = (int)i;
			break;
		case FIELD_WIDTH:
			Width = (int)i;
			break;
		default:
			LAssert(!"Not impl.");
			break;
	}

	return Store3NotImpl;
}
