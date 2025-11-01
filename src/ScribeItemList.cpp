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
inline int DateToInt(const LDateTime *dt)
{
	return (dt->Year() * 12) + dt->Month();
}

/*
int ArrCompare(Thing **a, Thing **b)
{
	return _Dir * (*a)->Compare(*b, _Field);
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
*/

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
	if (!Container)
		return DROPEFFECT_NONE;

	// Check if this item ALREADY belongs to this container...
	for (auto &dd: Data)
	{
		if (dd.IsFormat(ScribeThingList) &&
			dd.Data.Length() > 0)
		{
			auto &v = dd.Data[0];
			if (v.Type == GV_BINARY &&
				ScribeClipboardFmt::IsThing(v.Value.Binary.Data, v.Value.Binary.Length))
			{
				auto fmt = (ScribeClipboardFmt*) v.Value.Binary.Data;
				for (uint32_t i=0; i<fmt->Length(); i++)
				{
					if (auto t = fmt->ThingAt(i))
					{
						if (auto f = t->GetFolder())
						{
							if (f == Container)
							{
								// Dropping item on the same container it's already in is a Noop:
								return DROPEFFECT_NONE;
							}
						}
					}
				}
			}
		}
	}

	// Handle dropped file:
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
		auto sort = GetSort();
		if (sort.Col != Col)
		{
			SetSort({Col, true});
		}
		else
		{
			SetSort({sort.Col, !sort.Ascend});
		}
	}
}

void ThingList::Sort()
{
	auto fieldSort = GetFieldSort();
	int direction = fieldSort.Ascend ? 1 : -1;

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
			auto Params = Container->GetFieldSort();

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

			LList::Sort([this](auto *a, auto *b)
				{
					auto ta = (Thing*)a->User.Ptr;
					auto tb = (Thing*)b->User.Ptr;

					auto A = ta->IsMail();
					auto B = tb->IsMail();
					if (A && B && A->Container && B->Container)
					{
						return A->Container->Index - B->Container->Index;
					}
					return 0;
				});
		}
	}
	else if (Container->GetItemType() == MAGIC_ANY)
	{
		auto colSort = Container->GetColumnSort();
		LList::Sort([this, colSort](auto *pa, auto *pb)
			{
				auto a = dynamic_cast<Thing*>(pa);
				auto b = dynamic_cast<Thing*>(pb);
				if (!a || !b)
					return 0;

				int type = (int) (a->Type() - b->Type());
				if (type)
					return type;

				auto defs = a->GetDefaultFields();
				if (!defs || !defs[colSort.Col])
					return 0;

				return (colSort.Ascend ? 1 : -1) * a->Compare(b, defs[colSort.Col]);

			});
	}
	else
	{
		/*
		auto dumpLst = [&]()
			{
				LArray<Mail*> all;
				if (GetAll(all))
				{
					for (auto m: all)
						LgiTrace("	%p = %s\n", m, m->GetDateSent()->Get().Get());
				}
			};

		LgiTrace("Before sort:\n");
		dumpLst();
		*/

		LList::Sort([this, direction, fieldSort](auto *a, auto *b)
			{
				return direction * a->Compare(b, fieldSort.Col);
			});

		// LgiTrace("After sort:\n");
		// dumpLst();
	}
}

bool ThingList::SetSort(SortParam sort, bool reorderItems, bool setMark)
{
	if (!Container)
		return false;

	sortParam = sort;
	Container->SetSort(sort);
	
	auto fieldSort = GetFieldSort();
	Sort();
	
	if (auto Sel = GetSelected())
	{
		LVariant Txt(Sel->GetText(sort.Col));
		if (Txt.Str())
		{
			int Index = IndexOf(Sel);
			while (Index > 0)
			{
				LListItem *Prev = ItemAt(--Index);
				if (Prev)
				{
					const char *PrevTxt = Prev->GetText(sort.Col);
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

	SetSortingMark(sort);
	return true;
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

bool ThingList::OnColumnReindex(LItemColumn *Col, int OldIndex, int NewIndex)
{
	return Container->ReindexField(OldIndex, NewIndex);
}
