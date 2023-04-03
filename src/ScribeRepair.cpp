// Scribe Folder Repair code


#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>

#include "Scribe.h"
#include "lgi/common/FileSelect.h"

#if 0 // FIXME
char ModuleName[] = "Storage Repair";

class RepairFile
{
	ScribeWnd *Parent;
	List<int> PossibleLocs;
	List<StorageItem> Items;
	StorageKit *StoreKit;
	ScribeFolder *MailThing;
	StorageItem *MailBox;

	char FolderName[256];
	char *GetFolderName(int i);

public:
	RepairFile(ScribeWnd *parent, char *FileName);

	void CreateFolder(char *Name, int Type, StorageItem *&Item);
	bool PatchChild(StorageItem *Item, StorageItem *Child = 0);
	bool PatchParent(StorageItem *Item, StorageItem *Parent = 0);
	bool PatchNext(StorageItem *Item, StorageItem *Next = 0);
	bool PatchPrev(StorageItem *Item, StorageItem *Prev = 0);
};

#define I_FOLDER			0
#define I_MAIL				1
#define I_CONTACT			2

RepairFile::RepairFile(ScribeWnd *parent, char *FileName)
{
	int ItemCount[3] = {0, 0, 0};

	// Init
	Parent = parent;
	MailThing = 0;

	#if 0
	// v1 storage

	// first lets do a general search for possible items in the file
	LFile F;
	if (F.Open(FileName, O_READ))
	{
		int Len = F.GetSize();
		uchar *Buf = new uchar[Len];
		if (Buf)
		{
			if (F.Read(Buf, Len) == Len)
			{
				for (int i=64; i<Len-4; i++)
				{
					if (*((ulong*) (Buf+i)) == STORAGE_ITEM_MAGIC)
					{
						PossibleLocs.Insert(new int(i));
					}
				}
			}
		}
		F.Close();
	}

	// Store kit init
	Storage1::StorageKitImpl Kit(FileName);
	StoreKit = &Kit;
	MailBox = Kit.GetRoot();
	if (NOT MailBox)
	{
		// Oh bugger, things are REALLY screwed up in there.
		// Start from scratch
		MailBox = Kit.CreateRoot(new ScribeFolder("MailBox", MAGIC_NONE));
	}

	if (MailBox)
	{
		MailThing = new ScribeFolder("MailBox", MAGIC_NONE);
		if (MailThing)
		{
			StorageItem *Inbox = 0;
			StorageItem *Outbox = 0;
			StorageItem *Sent = 0;
			StorageItem *Trash = 0;
			StorageItem *Contacts = 0;

			// Init
			MailBox->SetObject(MailThing);
			MailThing->Window = Parent;
			MailThing->Store = MailBox;

			// Detects and fixes broken link from the mail to child
			PatchChild(MailBox);

			// Ok now lets get down to the real business of locating items
			// and if necessary repatching them back into the tree

			// Now lets check all these possibles for actuals
			for (int *n = PossibleLocs.First(); n; n = PossibleLocs.Next())
			{
				StorageItem *Item = (*n > 64) ? Kit.LoadLocation(*n) : 0;
				if (Item)
				{
					StorageItem *Insert = 0;
					if (Item->StoreType == MAGIC_FOLDER)
					{
						ScribeFolder *t = new ScribeFolder(NULL, 0);
						t->SetObject(Item);

						LFile *f = Item->GotoObject();
						if (f)
						{
							if (t->Serialize(*f, FALSE))
							{
								ItemCount[I_FOLDER]++;
								Insert = Item;
							}
						}
					}
					else
					{
						Thing *t = Parent->LoadItem(Item);
						if (t)
						{
							switch (Item->StoreType)
							{
								case MAGIC_MAIL:
								{
									ItemCount[I_MAIL]++;
									break;
								}
								case MAGIC_CONTACT:
								{
									ItemCount[I_CONTACT]++;
									break;
								}
							}
							Insert = Item;
						}
					}

					if (Insert)
					{
						// None of the objects have been loaded correctly
						// So null em out. We use this later when we're
						// patching everything back together again. If a pointer
						// is non-null then we know we set it and it's ok.
						Insert->Prev = 0;
						Insert->Next = 0;
						Insert->Parent = 0;
						Insert->Child = 0;

						// It's an item of some sort
						Items.Insert(Insert);
					}
				}
			}

			// Lets loop through the items and see if any base
			// folders are there
			StorageItem *i;
			for (i = Items.First(); i; i = Items.Next())
			{
				if (i->StoreType == MAGIC_FOLDER)
				{
					ScribeFolder *c = dynamic_cast<ScribeFolder*>(Parent->LoadItem(i));
					if (c)
					{
						char *Text = c->LTreeItem::GetText();
						if (Text)
						{
							#define CheckName(n, v) if (stricmp(Text, n) == 0) v = i;
							CheckName(GetFolderName(FOLDER_INBOX), Inbox);
							CheckName(GetFolderName(FOLDER_OUTBOX), Outbox);
							CheckName(GetFolderName(FOLDER_SENT), Sent);
							CheckName(GetFolderName(FOLDER_TRASH), Trash);
							CheckName(GetFolderName(FOLDER_CONTACTS), Contacts);
						}
					}
				}
			}

			// if the folders don't exist then create them
			CreateFolder(GetFolderName(FOLDER_INBOX), MAGIC_MAIL, Inbox);
			CreateFolder(GetFolderName(FOLDER_OUTBOX), MAGIC_MAIL, Outbox);
			CreateFolder(GetFolderName(FOLDER_SENT), MAGIC_MAIL, Sent);
			CreateFolder(GetFolderName(FOLDER_TRASH), MAGIC_ANY, Trash);
			CreateFolder(GetFolderName(FOLDER_CONTACTS), MAGIC_CONTACT, Contacts);

			// follow links back to root
			// patching them as necessary... doesn't really matter where
			// as long as they show up.
			Iterator<StorageItem> ItemList(&Items);
			for (i = ItemList.First(); i; i = ItemList.Next())
			{
				int Index = Items.IndexOf(i);

				StorageItem *Find = i;
				while (	Find AND
						Find->StoreLoc != MailBox->StoreLoc)
				{
					// Walk prev list
					while (Find->StorePrev)
					{
						if (PatchPrev(Find))
						{
							// Keep walking... prev
							Find = Find->Prev;

							// Fall thru and walk parent
						}
						else
						{
							// It's dead
							// End of the line buddy, terminate here
							Find->StorePrev = 0;
							Kit.SerializeItem(Find, true, STORAGE_ITEM_NOSIZE);
						}
					}

					// Ok check the parent pointer
					if (Find->StoreParent)
					{
						if (PatchParent(Find))
						{
							// Keep walking... up
							Find = Find->Parent;
							
							// Go walking the next list again
							continue;
						}
						else
						{
							// It's dead
							// End of the line buddy, terminate here
							Find->StoreParent = 0;
							Kit.SerializeItem(Find, true, STORAGE_ITEM_NOSIZE);
						}
					}

					// Ok no parent... *sigh*
					if (NOT Find->StoreParent)
					{
						// this peice of the tree has been orphaned
						// so we have to attach it somewhere
						//
						// If it's a mail then dump it in the inbox
						// If it's a contact, the contacts folder
						// If it's a folder then straight off the mailbox itself
						// Otherwise we just have to chuck it.

						StorageItem *Link = 0;
						List<StorageItem> Attachments;

						switch (Find->StoreType)
						{
							case MAGIC_MAIL:
							{
								// Just to make it worse
								// Attachments were marked with the MAGIC_MAIL type
								// in versions before 1.25
								// So we need to make sure it's actually a real mail
								// Attachments will always have a parent of type
								// MAGIC_MAIL, whereas a real email will have a folder
								// for a parent... and because we don't know what the
								// parent is here we're stuffed.
								//
								// However the load function shouldn't of let any 
								// attachments get into the item list in the fist
								// place because it'll try loading the object using
								// an email object, which of course will [probably]
								// fail...

								Link = Inbox;

								/* This is broken???
								if (PatchChild(Find))
								{
									StorageItem *a = Find->Child;
									while (a)
									{
										if (a->StoreType == MAGIC_ATTACHMENT OR
											a->StoreType == MAGIC_MAIL)
										{
											Attachments.Insert(a);
										}

										if (PatchNext(a))
										{
											a = a->Next;
										}
										else
										{
											a = 0;
										}
									}
								}
								*/
								break;
							}
							case MAGIC_CONTACT:
							{
								Link = Contacts;
								break;
							}
							case MAGIC_FOLDER:
							{
								Link = MailBox;
								break;
							}
							case MAGIC_ATTACHMENT:
							{
								// ??? bugger
								// Create an email just to stick the attachment in it?
								// Maybe write it out as a file... at least it's not lost
								break;
							}
							default:
							case MAGIC_NONE:
							case MAGIC_ANY:
							{
								// Ahhh, dunno what to do with this
								// throw it away (by the default action
								// of not attaching it).
								break;
							}
						}

						if (Link)
						{
							// this chain, with it's top defined as Find
							// as a child of the parent store, which we
							// know is valid.

							// Ok check the child pointer
							if (Link->StoreChild AND
								PatchChild(Link))
							{
								// Keep walking... child
								Link = Link->Child;
							}
							else
							{
								// No child eh?
								// Link em here
								Link->Child = Find;
								Link->StoreChild = Find->StoreLoc;
								Kit.SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);

								Find->Parent = Link;
								Find->StoreParent = Link->StoreLoc;
								Kit.SerializeItem(Find, true, STORAGE_ITEM_NOSIZE);

								// exit out of this loop, our job here is done
								Find = 0;
							}

							if (Find)
							{
								// Walk next list
								while (Link->StoreNext)
								{
									if (PatchNext(Link))
									{
										// Keep walking... next
										Link = Link->Next;
									}
									else break;
								}

								// Link em here
								Link->Next = Find;
								Link->StoreNext = Find->StoreLoc;
								Kit.SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);

								Find->Prev = Link;
								Find->StorePrev = Link->StoreLoc;
								Kit.SerializeItem(Find, true, STORAGE_ITEM_NOSIZE);

								// exit out of this loop, our job here is done
								Find = 0;
							}
						}
					}

					if (Find)
					{
						Find = Find->Parent;
					}

				} // while (Find)
			} // for (All Items)


			char Str[512];
			sprintf(Str,
					"Repair complete\n"
					"\n"
					"Object Id's: %i\n"
					"Valid Items: %i\n"
					"\n"
					"Folders: %i\n"
					"Email: %i\n"
					"Contacts: %i\n",
					PossibleLocs.GetItems(),
					Items.GetItems(),
					ItemCount[I_FOLDER],
					ItemCount[I_MAIL],
					ItemCount[I_CONTACT]);
			LgiMsg(Parent, Str, "Repair", MB_OK);

		} // if (MailThing)
	}
	else
	{
		LgiMsg(Parent, "Couldn't create the root item.", ModuleName, MB_OK);
	}

	#endif
}

void RepairFile::CreateFolder(char *Name, int Type, StorageItem *&Item)
{
	#if 0
	if (MailThing)
	{
		if (Item)
		{
			// MailBox will always have valid 1st level children
			// Ok item exists but is it patched to it parent?
			StorageItem *i = MailBox->Child;
			while (i)
			{
				if (i->StoreLoc == Item->StoreLoc)
				{
					// already in the list...
					// we are cool
					return;
				}
				else if (NOT i->Next)
				{
					// we are at the end of the list and we havn't
					// found "Item" yet.. so patch it in.
					PatchNext(i, Item);

					// Patch any furthur items
					i = Item;
					while (PatchNext(i))
					{
						i = i->Next;
					}

					return;
				}
				i = i->Next;
			}
		}
		else
		{
			// No item exists... create one
			// Mailbox should be safe by now to create children from
			ScribeFolder *t = MailThing->CreateSubDirectory(Name, Type);
			if (t)
			{
				Item = t->Store;
			}
		}
	}
	#endif
}

bool RepairFile::PatchChild(StorageItem *Item, StorageItem *Link)
{
	#if 0

	#define ItemLinkPtr			Child
	#define LinkItemPtr			Parent
	#define ItemLinkLoc			StoreChild
	#define LinkItemLoc			StoreParent

	if (Item AND
		NOT Item->ItemLinkPtr)
	{
		if (NOT Link)
		{
			for (StorageItem *p = Items.First(); p; p = Items.Next())
			{
				if (p->StoreLoc == Item->ItemLinkLoc)
				{
					Link = p;
					break;
				}
			}
		}

		if (Link)
		{
			Link->LinkItemPtr = Item;
			Link->LinkItemLoc = Item->StoreLoc;
			StoreKit->SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);
		}

		Item->ItemLinkPtr = Link;
		Item->ItemLinkLoc = (Link) ? Link->StoreLoc : 0;
		StoreKit->SerializeItem(Item, true, STORAGE_ITEM_NOSIZE);
	}

	return (Item) ? (Item->ItemLinkPtr != 0) : (false);

	#undef ItemLinkPtr
	#undef LinkItemPtr
	#undef ItemLinkLoc
	#undef LinkItemLoc

	#else
	return 0;
	#endif
}

bool RepairFile::PatchParent(StorageItem *Item, StorageItem *Link)
{
	#if 0

	#define ItemLinkPtr			Parent
	#define LinkItemPtr			Child
	#define ItemLinkLoc			StoreParent
	#define LinkItemLoc			StoreChild

	if (Item AND
		NOT Item->ItemLinkPtr)
	{
		if (NOT Link)
		{
			for (StorageItem *p = Items.First(); p; p = Items.Next())
			{
				if (p->StoreLoc == Item->ItemLinkLoc)
				{
					Link = p;
					break;
				}
			}
		}

		if (Link)
		{
			Link->LinkItemPtr = Item;
			Link->LinkItemLoc = Item->StoreLoc;
			StoreKit->SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);
		}

		Item->ItemLinkPtr = Link;
		Item->ItemLinkLoc = (Link) ? Link->StoreLoc : 0;
		StoreKit->SerializeItem(Item, true, STORAGE_ITEM_NOSIZE);
	}

	return (Item) ? (Item->ItemLinkPtr != 0) : (false);

	#undef ItemLinkPtr
	#undef LinkItemPtr
	#undef ItemLinkLoc
	#undef LinkItemLoc

	#else
	return 0;
	#endif
}

bool RepairFile::PatchNext(StorageItem *Item, StorageItem *Link)
{
	#if 0

	#define ItemLinkPtr			Next
	#define LinkItemPtr			Prev
	#define ItemLinkLoc			StoreNext
	#define LinkItemLoc			StorePrev

	if (Item AND
		NOT Item->ItemLinkPtr)
	{
		if (NOT Link)
		{
			// Search for item
			for (StorageItem *p = Items.First(); p; p = Items.Next())
			{
				if (p->StoreLoc == Item->ItemLinkLoc)
				{
					Link = p;
					break;
				}
			}

			/*
			// check the validity of that item
			StorageItem *i = Item->Prev;
			while (i AND p)
			{
				while (i->Prev AND p)
				{
					if (i->StoreLoc == p->StoreLoc)
					{
						p = 0;
					}
					i = i->Prev;
				}

				if (i->Parent AND p)
				{
					if (i->StoreLoc == p->StoreLoc)
					{
						p = 0;
					}
					i = i->Parent;
				}
			}
			*/
		}

		if (Link)
		{
			Link->LinkItemPtr = Item;
			Link->LinkItemLoc = Item->StoreLoc;
			StoreKit->SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);
		}

		Item->ItemLinkPtr = Link;
		Item->ItemLinkLoc = (Link) ? Link->StoreLoc : 0;
		StoreKit->SerializeItem(Item, true, STORAGE_ITEM_NOSIZE);
	}

	return (Item) ? (Item->ItemLinkPtr != 0) : (false);

	#undef ItemLinkPtr
	#undef LinkItemPtr
	#undef ItemLinkLoc
	#undef LinkItemLoc

	#else
	return 0;
	#endif
}

bool RepairFile::PatchPrev(StorageItem *Item, StorageItem *Link)
{
	#if 0

	#define ItemLinkPtr			Prev
	#define LinkItemPtr			Next
	#define ItemLinkLoc			StorePrev
	#define LinkItemLoc			StoreNext

	if (Item AND
		NOT Item->ItemLinkPtr)
	{
		if (NOT Link)
		{
			// Search for possible item
			for (StorageItem *p = Items.First(); p; p = Items.Next())
			{
				if (p->StoreLoc == Item->ItemLinkLoc)
				{
					Link = p;
					break;
				}
			}
		}

		if (Link)
		{
			Link->LinkItemPtr = Item;
			Link->LinkItemLoc = Item->StoreLoc;
			StoreKit->SerializeItem(Link, true, STORAGE_ITEM_NOSIZE);
		}

		Item->ItemLinkPtr = Link;
		Item->ItemLinkLoc = (Link) ? Link->StoreLoc : 0;
		StoreKit->SerializeItem(Item, true, STORAGE_ITEM_NOSIZE);
	}

	return (Item) ? (Item->ItemLinkPtr != 0) : (false);

	#undef ItemLinkPtr
	#undef LinkItemPtr
	#undef ItemLinkLoc
	#undef LinkItemLoc

	#else
	return 0;
	#endif
}

char *RepairFile::GetFolderName(int i)
{
	FolderName[0] = 0;
	if (Parent)
	{
		LVariant v;
		char Str[32], *s;
		sprintf(Str, "Folder-%i", i);
		if (Parent->GetOptions()->GetValue(Str, v) AND (s = v.Str()) != 0)
		{
			char *n = 0;
			for (n = s+strlen(s)-1; n>=s; n--)
			{
				if (*n == '/')
				{
					strcpy(FolderName, n+1);
					break;
				}
			}
			if (n<s)
			{
				strcpy(FolderName, s);
			}
		}
	}
	return FolderName;
}
#endif

//////////////////////////////////////////////////////////////////
void Scribe_Repair(ScribeWnd *Parent)
{
	auto Select = new LFileSelect(Parent);
	Select->Type("Mail folders", "*.mail");
	Select->Open([&](auto dlg, auto status)
	{
		// FIXME RepairFile Worker(Parent, Select.Name());
		delete dlg;
	});
}

