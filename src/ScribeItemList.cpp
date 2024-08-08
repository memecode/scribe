/*
**	FILE:			ScribeItemList.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			17/1/2000
**	DESCRIPTION:	Scribe Item List
**
**	Copyright (C) 2000, Matthew Allen
**		fret@memecode.com
*/

// Includes
#include "Scribe.h"
#include "lgi/common/ScrollBar.h"
#include "lgi/common/QuickSort.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/DropFiles.h"

#include "resdefs.h"

//////////////////////////////////////////////////////////////////////////////
int _Dir = 0;
int _Field = 0;

int ListCompare(LListItem *a, LListItem *b, NativeInt Data)
{
	return _Dir * a->Compare(b, _Field);
}
int ArrCompare(Thing **a, Thing **b)
{
	return _Dir * (*a)->Compare(*b, _Field);
}

inline int DateToInt(const LDateTime *dt)
{
	return (dt->Year() * 12) + dt->Month();
}

void DateSort(List<LListItem> &Lst)
{
	typedef LArray<Thing*> ThingArr;
	LArray<ThingArr*> Idx;
	int Base = 1970 * 12;

	for (auto i: Lst)
	{
		Thing *t = dynamic_cast<Thing*>(i);
		if (t)
		{
			const LDateTime *dt = t->GetObject()->GetDate(_Field);
			if (dt)
			{
				int i = DateToInt(dt) - Base;
				if (!Idx[i])
					Idx[i] = new ThingArr;
				Idx[i]->Add(t);
			}
		}
	}

	Lst.Empty();
	for (unsigned n=0; n<Idx.Length(); n++)
	{
		ThingArr *a = Idx[n];
		if (a)
		{
			a->Sort(ArrCompare);
			for (unsigned k=0; k<a->Length(); k++)
				Lst.Insert((*a)[k]);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
ThingList::ThingList(ScribeWnd *wnd) :
	LList(IDC_THING_LIST, 0, 0, 100, 100, "ThingList")
{
	App = wnd;

	Sunken(false);
	AskText(true);
	AskImage(true);
	SetImageList(App->GetIconImgList(), false);
}

ThingList::~ThingList()
{
	DeletePlaceHolders();
}

int ThingList::WillAccept(LDragFormats &Formats, LPoint Pt, int KeyState)
{
	Formats.SupportsFileDrops();
	return Formats.Length() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

int ThingList::OnDrop(LArray<LDragData> &Data, LPoint Pt, int KeyState)
{
	int Status = DROPEFFECT_NONE;

	for (auto &dd: Data)
	{
		if (Container && dd.IsFileDrop())
		{
			LDropFiles Files(dd);
			if (Files.Length())
			{
				Status = DROPEFFECT_COPY;
				Container->OnReceiveFiles(Files);
				break;
			}
		}
	}
	
	return Status;
}

void ThingList::DeletePlaceHolders()
{
	Thing *t;
	while ((t = PlaceHolders[0]))
	{
		PlaceHolders.Delete(t);
		t->DecRef();
	}
}

LFont *ThingList::GetFont()
{
	if (BoldUnread &&
		CurrentMail &&
		Container &&
		Container->App &&
		!TestFlag(CurrentMail->GetFlags(), MAIL_READ))
	{
		return Container->App->GetBoldFont();
	}

	return LList::GetFont();
}

LRect &ThingList::GetClient(bool ClientSpace)
{
	static LRect r;
	
	r = LLayout::GetClient(ClientSpace);
	for (auto c : Children)
	{
		if (!dynamic_cast<LScrollBar*>(c))
		{
			r.y1 = MAX(r.y1, c->GetPos().y2 + 1);
		}
	}

	return r;
}

void ThingList::OnPaint(LSurface *pDC)
{
	LVariant v;
	if (App->GetOptions()->GetValue(OPT_BoldUnread, v))
		BoldUnread = v.CastInt32();

	LList::OnPaint(pDC);
}

void ThingList::OnColumnClick(int Col, LMouse &m)
{
	if (m.IsContextMenu())
	{
		LSubMenu RClick;

		ItemFieldDef *Fields = 0;
		switch ((uint32_t)Container->GetItemType())
		{
			case MAGIC_MAIL:
			{
				Fields = MailFieldDefs;
				break;
			}
			case MAGIC_CONTACT:
			{
				Fields = ContactFieldDefs;
				break;
			}
			case MAGIC_GROUP:
			{
				Fields = GroupFieldDefs;
				break;
			}
			case MAGIC_FILTER:
			{
				Fields = FilterFieldDefs;
				break;
			}
			case MAGIC_CALENDAR:
			{
				Fields = CalendarFields;
				break;
			}
		}

		#define MENU_BASE	1000
		if (Fields)
		{
			int n=0, i;
			for (i=0; Fields[i].FieldId; i++)
			{
				const char *s = LLoadString(Fields[i].FieldId);
				auto Item = RClick.AppendItem(s?s:Fields[i].DisplayText, MENU_BASE+n++, true);
				if (Item)
				{
					if (Container->HasFieldId(Fields[i].FieldId))
					{
						Item->Checked(true);
					}
				}
			}

			if (!GetMouse(m))
				return;

			int FieldsIndex = RClick.Float(this, m);
			if (FieldsIndex >= MENU_BASE)
			{
				Container->SerializeFieldWidths();
				EmptyColumns();

				FieldsIndex -= MENU_BASE;
				LAssert(FieldsIndex >= 0 && FieldsIndex <= i);
				if (Container->HasFieldId(Fields[FieldsIndex].FieldId))
				{
					int Id = Fields[FieldsIndex].FieldId;
					LDataPropI *f;
					for (f = Container->GetFldObj()->Fields().First(); f;
						f = Container->GetFldObj()->Fields().Next())
					{
						if (f->GetInt(FIELD_ID) == Id)
							break;
					}

					if (f)
					{
						Container->GetFldObj()->Fields().Delete(f);
						DeleteObj(f);
					}
				}
				else
				{
					LDataPropI *Fld = Container->GetFldObj()->Fields().Create(Container->GetObject()->GetStore());
					if (Fld)
					{
						Fld->SetInt(FIELD_ID, Fields[FieldsIndex].FieldId);
						Fld->SetInt(FIELD_WIDTH, 100);
						Container->GetFldObj()->Fields().Insert(Fld);
					}
				}

				Container->SetDirty();
				Container->Populate(this);
				UpdateAllItems();
			}
		}
	}
	else
	{
		if (GetSortCol() != Col)
		{
			SetSort(Col, true);
		}
		else
		{
			SetSort(GetSortCol(), !GetSortAscending());
		}
	}
}

int ItemIndexer(LListItem *a, LListItem *b, NativeInt Data)
{
	Thing *ta = (Thing*)a->_UserPtr;
	Thing *tb = (Thing*)b->_UserPtr;

	Mail *A = ta->IsMail();
	Mail *B = tb->IsMail();
	if (A && B && A->Container && B->Container)
	{
		return A->Container->Index - B->Container->Index;
	}
	return 0;
}

template <class T> extern
int TrashCompare(T *a, T *b, NativeInt Data);

void ThingList::ReSort()
{
	if (App->GetCtrlValue(IDM_THREAD))
	{
		LArray<MContainer*> Root;
		for (auto i: Items)
		{
			Mail *m = IsMail(i);
			if (m)
			{
				MContainer *c = m->Container;
				if (c && c->Parent == 0)
				{
					Root.Add(c);
				}
			}
		}

		if (Root.Length())
		{
			// Sort the root items
			ThingSortParams Params;
			Params.SortAscend = Container->GetSortAscend();
			Params.SortField = Container->GetSortField();

            #if 0
			Root.Sort(ContainerCompare);
			#else
			if (Root.Length() > 1)
			    LQuickSort(&Root[0], Root.Length(), ContainerSorter, &Params);
			#endif

			for (int i=0; i<(int)Root.Length(); i++)
			{
				Root[i]->Pour(i, 0, 0, i<(int)Root.Length()-1, &Params);
			}

			Sort(ItemIndexer);
		}
	}
	else if (Container->GetItemType() == MAGIC_ANY)
	{
		Sort(TrashCompare<LListItem>, (NativeInt) Container);
	}
	else
	{
		Sort(ListCompare, (NativeInt) this);
	}

	#if 0
	if (Items.Length() == 4)
	{
		LgiTrace("Resort %i\n", Items.Length());
		for (LListItem *i = Items.First(); i; i = Items.Next())
		{
			Mail *m = dynamic_cast<Mail*>(i);
			if (m)
			{
				LDateTime *sent = m->GetObject()->GetDate(FIELD_DATE_SENT);
				if (sent)
				{
					char s[32];
					sent->Get(s);
					LgiTrace("    %p=%s\n", i, s);
				}
			}
		}
	}
	#endif
}

void ThingList::SetSort(int Col, int Ascend)
{
	if (!Container)
		return;

	Container->SetSort(Col, Ascend != 0);
	
	_Dir = GetSortAscending() ? 1 : -1;
	_Field = GetSortField();
	ReSort();
	
	LListItem *Sel = GetSelected();
	if (Sel)
	{
		LVariant Txt(Sel->GetText(Col));
		if (Txt.Str())
		{
			int Index = IndexOf(Sel);
			while (Index > 0)
			{
				LListItem *Prev = ItemAt(--Index);
				if (Prev)
				{
					const char *PrevTxt = Prev->GetText(Col);
					if (PrevTxt)
					{
						if (strcmp(PrevTxt, Txt.Str()) == 0)
						{
							// Prev list item is the same text as this one
							Sel = Prev;
						}
						else
						{
							// Prev item different so select this one and stop
							Select(Sel);
							Sel->ScrollTo();
							break;
						}
					}
					else break;
				}
				else
				{
					Select(Sel);
					Sel->ScrollTo();
					break;
				}
			}
		}
	}

	SetSortingMark(Col, Ascend ? false : true);
}

void ThingList::OnItemClick(LListItem *Item, LMouse &m)
{
	if (!Item)
	{
		LArray<LListItem*> s;
		OnItemSelect(s);
	}

	LList::OnItemClick(Item, m);
}

void ThingList::OnItemSelect(LArray<LListItem*> &Items)
{
	LList::OnItemSelect(Items);

	App->PostEvent(M_SCRIBE_ITEM_SELECT, 0, 0);
}

bool ThingList::OnKey(LKey &k)
{
	bool Status = false;

	switch (k.vkey)
	{
		case LK_RETURN:
		{
			if (k.Down() && !k.IsChar)
			{
				List<LListItem> Sel;
				LList::GetSelection(Sel);
				LListItem *i = Sel[0];
				if (i)
				{
					Thing *t = dynamic_cast<Thing*>(i);
					if (t)
					{
						t->DoUI();
					}
				}
			}
			Status = true;
			break;
		}
		#ifdef MAC
		case LK_SPACE:
		{
			if (!k.Down())
				// Catch the menu item for marking unread
				App->PostEvent(M_COMMAND, IDM_SET_READ);
			return true;
		}
		#endif
	}

	return LList::OnKey(k) || Status;
}

void ThingList::OnColumnDrag(int Col, LMouse &m)
{
	if (m.Left())
	{
		DragColumn(Col);
	}
}

void ThingList::OnCreate()
{
	SetWindow(this);
}

bool ThingList::OnColumnReindex(LItemColumn *Col, int OldIndex, int NewIndex)
{
	return Container->ReindexField(OldIndex, NewIndex);
}
