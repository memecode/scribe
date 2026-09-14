/*
**	FILE:			ScribeApp.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			22/10/1998
**	DESCRIPTION:	Scribe email application
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

// Debug defines
// #define PRINT_OUT_STORAGE_TREE
// #define TEST_OBJECT_SIZE

#define USE_SPELLCHECKER			1
#define USE_INTERNAL_BROWSER		1		// for help
#define RUN_STARTUP_SCRIPTS			1
#define PROFILE_ON_PULSE			0
#define TRAY_CONTACT_BASE			1000
#define TRAY_MAIL_BASE				10000

// Includes
#include <cstddef>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"

#include "lgi/common/StoreConvert1To2.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/OpenSSLSocket.h"
#include "lgi/common/SoftwareUpdate.h"
#include "lgi/common/Html.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/Store3.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Box.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/SpellCheck.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/CssTools.h"
#include "lgi/common/Charset.h"
#include "lgi/common/RefCount.h"
#include "lgi/common/PopupNotification.h"
#include "lgi/common/Base64.h"
#include "lgi/common/Html2.h"
#include "lgi/common/LiteHtmlView.h"

#include "ScribePrivate.h"
#include "PreviewPanel.h"
#include "ScribeStatusPanel.h"
#include "ScribeFolderDlg.h"
#include "ScribePageSetup.h"
#include "Calendar.h"
#include "CalendarView.h"
#include "ScribeSpellCheck.h"
#include "Store3Common.h"
#include "PrintContext.h"
#include "resource.h"
#include "ManageMailStores.h"
#include "ReplicateDlg.h"
#include "ScribeAccountPreview.h"
#include "Encryption/GnuPG.h"
#include "resdefs.h"
#include "ScribeIpc.h"
#include "DynamicHtml.h"

#define DEBUG_STORE_EVENTS			0
#if DEBUG_STORE_EVENTS
#define LOG_STORE(...)				LgiTrace(__VA_ARGS__)
#else
#define LOG_STORE(...)
#endif

#define IDM_LOAD_MSG				2000
#define RAISED_LOOK					0
#define SUNKEN_LOOK					false
#ifdef MAC
#define SUNKEN_CTRL					false
#else
#define SUNKEN_CTRL					true
#endif

enum TrayIconIndex
{
	TRAY_ICON_NORMAL = 0,
	TRAY_ICON_ERROR,
	TRAY_ICON_MAIL,
	TRAY_ICON_NONE
};

#include "ScribeWndPrivate.h"

/// Received new items from a storage backend.
void ScribeWnd::OnNew
(
	/// The parent folder of the new item
	LDataFolderI *Parent,
	/// All the new items
	LArray<LDataI*> &NewItems,
	/// The position in the parent folder or -1
	int Pos,
	/// Non-zero if the object is a new email.
	bool IsNew,
	/// Account requests filtering of email.
	bool FilterIncoming
)
{
	THREAD_UNSAFE();

	auto Fld = CastFolder(Parent);
	int UnreadDiff = 0;

	if (!Fld)
	{
		// When this happens the parent hasn't been loaded by the UI thread
		// yet, so if say the IMAP back end notices a new sub-folder then we
		// can safely ignore it until such time as the UI loads the parent
		// folder. At which point the child we got told about here will be 
		// loaded anyway.

		#if DEBUG_NEW_MAIL
		LDataFolderI *p = dynamic_cast<LDataFolderI*>(Parent);
		LgiTrace("%s:%i - NewMail.OnNew no UI object for '%s'\n", _FL, p ? p->GetStr(FIELD_FOLDER_NAME) : NULL);
		#endif
		return;
	}
	
	if (!Parent || !NewItems.Length())
	{
		LAssert(!"Param error");
		return;
	}

	for (auto cb: d->Store3EventCallbacks)
		cb->OnNew(Parent, NewItems, Pos, IsNew, FilterIncoming);

	List<Mail> NewMail;
	List<LListItem> ToInsert;
	for (auto Item: NewItems)
	{
		if (Item->Type() == MAGIC_FOLDER)
		{
			// Insert new folder into the right point on the tree...
			LDataFolderI *SubObject = dynamic_cast<LDataFolderI*>(Item);
			if (!SubObject)
			{
				LAssert(!"Not a valid folder.");
				continue;
			}
			
			
			ScribeFolder *Sub = CastFolder(SubObject);
			// LgiTrace("OnNew '%s', Sub=%p Children=%i\n", SubObject->GetStr(FIELD_FOLDER_NAME), Sub, SubObject->SubFolders().Length());

			if (!Sub)
			{
				// New folder...
				if ((Sub = new ScribeFolder))
				{
					Sub->App = this;
					Sub->SetObject(SubObject, false, _FL);

					Fld->Insert(Sub);
				}
			}
			else
			{
				// Existing folder...
				Fld->Insert(Sub, Pos);
			}
		}
		else
		{
			auto t = CastThing(Item);
			if (!t)
			{
				// Completely new thing...
				t = CreateThingOfType((Store3ItemTypes) Item->Type(), Item);
			}
			
			if (t)
			{
				if (t->DeleteOnAdd.Obj)
				{
					// Complete a delayed move
					auto OldFolder = t->App->GetFolder(t->DeleteOnAdd.Path);
					if (!OldFolder)
					{
						LgiTrace("%s:%i - Couldn't resolve old folder '%s'\n", _FL, t->DeleteOnAdd.Path.Get());
						if (t->DeleteOnAdd.Callback)
							t->DeleteOnAdd.Callback(Store3Error);
					}
					else
					{
						auto OldItem = t->DeleteOnAdd.Obj;
						if (!OldFolder->Items.HasItem(OldItem))
						{
							LgiTrace("%s:%i - Couldn't find old obj.\n", _FL);
							if (t->DeleteOnAdd.Callback)
								t->DeleteOnAdd.Callback(Store3Error);
						}
						else
						{
							OldItem->OnDelete();
							if (t->DeleteOnAdd.Callback)
								t->DeleteOnAdd.Callback(Store3Success); // I guess?
						}
					}
				}

				t->SetParentFolder(Fld);
				Fld->Update();

				auto fldPath = Fld->GetPath();
				if (Fld->Select())
				{
					// Existing thing...
					t->SetFieldArray(Fld->GetFieldArray());

					auto Filter = GetThingFilter();
					if (!Filter || Filter->TestThing(t))
					{
						ToInsert.Insert(t);
					}
				}

				auto m = t->IsMail();
				if (m)
				{
					bool read = TestFlag(m->GetFlags(), MAIL_READ);
					UnreadDiff += read ? 0 : 1;

					#if 0 // DEBUG_NEW_MAIL
					LgiTrace("%s:%i - NewMail.OnNew t=%p uid=%s read=%i IsNew=%i\n", _FL,
						t, m->GetServerUid().ToString().Get(),
						read, IsNew);
					#endif

					if (IsNew && FilterIncoming)
					{
						LAssert(!NewMail.HasItem(m));
						NewMail.Insert(m);
					}
				}

				t->Update();
			}
		}
	}

	if (ToInsert.Length())
		MailList->Insert(ToInsert, -1, false);

	if (UnreadDiff)
		Fld->OnUpdateUnRead(UnreadDiff, false);

	if (MailList && Fld->Select())
	{
		#if 1
			MailList->Sort();
		#else
			auto fieldSort = Fld->GetFieldSort();
			auto dir = fieldSort.Ascend ? 1 : -1;
			MailList->Sort([Fld, dir, fieldSort](auto *a, auto *b)
				{
					return dir * a->Compare(b, fieldSort.Col);
				});
		#endif
	}

	if (NewMail.Length())
		OnNewMail(NewMail, true);
}

void ScribeWnd::OnPropChange(LDataStoreI *store, int Prop, LVariantType Type)
{
	THREAD_UNSAFE();

	switch (Prop)
	{
		case FIELD_IS_ONLINE:
		{
			// This is in case we receive a message after the app has shutdown and
			// deleted 'this'.
			if (ScribeState > ScribeRunning)
				break;
			
			if (StatusPanel)
				StatusPanel->Invalidate();
			
			for (auto a: Accounts)
			{
				if (a->Receive.GetDataStore() == store)
				{
					int64 Online = store->GetInt(FIELD_IS_ONLINE);

					auto Old = ScribeState;

					// This prevents the folders unloading during this call.
					// Which causes crashes.
					ScribeState = ScribeLoadingFolders;

					a->Receive.OnOnlineChange(Online != 0);
					ScribeState = Old;
					break;
				}
			}
			break;
		}
		case FIELD_MAPI_PROFILES:
		{
			// Store the available profiles in the account, so the user can pick from them later:
			auto profiles = store->GetStr(Prop);
			LString userName = store->GetStr(FIELD_NAME);

			for (auto a : Accounts)
			{
				if (a->Receive.ProtocolType() == ProtocolMapi &&
					userName.Equals(a->Receive.UserName().Str()))
				{
					a->Receive.MapiProfiles(profiles);
				}
			}
			break;
		}
	}
}

// This checks the certificate store to see if the user has selected to ALLOW 
// a specific certificate for a host name.
//
// The code that writes to that store is:
// - ScribeWnd::NeedsCapability
// - ScribeWnd::StartAction
// - ScribeWndPrivate::AllowCert
bool ScribeWnd::AllowSslCert(const char *certHost, LArray<uint8_t> *certId)
{
	THREAD_SAFE();
	
	if (!certHost || !certId)
	{
		LAssert(!"Param error");
		return false;
	}
	
	// Get the options and lock the cert info tag:
	if (!d->Options)
	{
		LgiTrace("%s:%i - no options loaded?\n", _FL);
		LAssert(!"No options");
		return false;
	}
	
	auto certIdHex = LHex(LString((const char*)certId->AddressOf(), certId->Length()));
	bool status = false;
	if (auto certOpts = d->Options->LockTag(OPT_SavedCerts, _FL))
	{
		for (auto t: certOpts->Children)
		{
			if (!t->IsTag(OPT_Cert))
				continue;
			
			auto host = t->GetAttr(OPT_Host);
			if (Stricmp(host, certHost))
				continue;
			
			if (auto id = t->GetContent())
			{
				if (certIdHex.Equals(id))
				{
					switch ((TSslAccept)t->GetAsInt(OPT_Accept))
					{
						case SslAcceptOnce:
						{
							// Delete the entry but return true:
							if (t->RemoveTag())
								delete t;
							status = true;
							break;
						}
						case SslAcceptAlways:
						{
							status = true;
							break;
						}
						default:
							break;
					}
				}
			}
			
			if (status)
				break;
		}
		
		d->Options->Unlock();
	}

	return status;
}

// This stores a certificate in the options along with is host and acceptable level.
bool ScribeWndPrivate::AllowCert(TSslAccept accept)
{
	THREAD_SAFE();
	
	auto certOpts = Options->LockTag(OPT_SavedCerts, _FL);
	if (!certOpts)
	{
		Options->CreateTag(OPT_SavedCerts);
		certOpts = Options->LockTag(OPT_SavedCerts, _FL);
	}
	if (!certOpts)
	{
		LAssert(0);
		return false;
	}

	LXmlTag *certTag = nullptr;
	
	for (auto t: certOpts->Children)
	{
		if (!t->IsTag(OPT_Cert))
			continue;
		
		auto host = t->GetAttr(OPT_Host);
		if (Stricmp(host, SslCertHost.Get()))
			continue;
			
		certTag = t;
		break;
	}
	
	if (!certTag)
	{
		// Create a new cert tag...
		certTag = new LXmlTag(OPT_Cert);
		certOpts->InsertTag(certTag);
	}

	bool status = certTag != nullptr;
	if (status)
	{
		certTag->SetAttr(OPT_Accept, (int64_t)accept);
		certTag->SetAttr(OPT_Host, SslCertHost);
		certTag->SetContent(SslCertId);
	}
	else LAssert(0);

	Options->Unlock();
	
	return status;
}

void ScribeWnd::SetContext(const char *file, int line)
{
	THREAD_UNSAFE();

	d->CtxFile = file;
	d->CtxLine = line;
}

bool ScribeWnd::OnChange(LArray<LDataI*> &items, int FieldHint)
{
	THREAD_UNSAFE(false);

	bool UpdateSelection = false;
	List<Mail> NewMail;
	ScribeFolder *Parent = 0;

	// LOG_STORE("OnChange(%i, %i)\n", (int)items.Length(), FieldHint);
	LAssert(d->CtxFile != NULL);

	for (unsigned c=0; c<d->Store3EventCallbacks.Length(); c++)
	{
		d->Store3EventCallbacks[c]->OnChange(items, FieldHint);
	}

	for (unsigned i=0; i<items.Length(); i++)
	{
		LDataI *Item = items[i];
		Thing *t;

		if (Item->Type() == MAGIC_FOLDER)
		{
			auto ItemFolder = dynamic_cast<LDataFolderI*>(Item);
			ScribeFolder *fld = CastFolder(ItemFolder);
			if (fld)
			{
				if (FieldHint == FIELD_STATUS)
				{
					// This is the delayed folder load case:
					fld->IsLoaded(true);
					if (fld->Select())
						fld->Populate(GetMailList(), nullptr);
				}
				else
				{
					fld->Update();
				}
			}
			else
			{
				// LAssert(!"Can't cast to folder?");
			}
		}
		else if ((t = CastThing(Item)))
		{
			ThingUi *Ui = t->GetUI();
			if (Ui)
				Ui->OnChange();
				
			Mail *m = t->IsMail();
			if (m)
			{
				auto StoreFlags = Item->GetInt(FIELD_FLAGS);

				#if 0
				LgiTrace("App.OnChange(%i) handler %p: %s -> %s\n", 
					FieldHint, m,
					EmailFlagsToStr(m->FlagsCache).Get(),
					EmailFlagsToStr(StoreFlags).Get());
				#endif
				
				if (TestFlag(m->FlagsCache, MAIL_NEW) &&
					!TestFlag(StoreFlags, MAIL_NEW))
				{
					Mail::NewMailLst.Delete(m);
					Parent = m->GetFolder();
				}
					
				if (m->FlagsCache != StoreFlags)
				{
					// LgiTrace("%s:%i - OnChange mail flags changed.\n", _FL);
					m->SetFlagsCache(StoreFlags, false, false);
					Parent = m->GetFolder();
				}

				if (m->NewEmail == Mail::NewEmailLoading)
				{
					auto Loaded = m->GetLoaded();
					if (Loaded < Store3Loaded)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnChange.GetBody t=%p, uid=%s, mode=%s, loaded=%s (%s:%i)\n",
							_FL, (Thing*)m, m->GetServerUid().ToString().Get(),
							toString(m->NewEmail), toString(Loaded),
							d->CtxFile, d->CtxLine);
						#endif

						m->GetBody();
					}
					else
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnChange.NewMail t=%p, uid=%s, mode=%s, loaded=%s (%s:%i)\n",
							_FL, (Thing*)m, m->GetServerUid().ToString().Get(),
							toString(m->NewEmail), toString(Loaded),
							d->CtxFile, d->CtxLine);
						#endif

						NewMail.Insert(m);
					}
				}
			}
			else if (t->IsCalendar())
			{
				for (auto cv: CalendarView::CalendarViews)
					cv->OnContentsChanged();
			}

			#ifdef _DEBUG
			if (FieldHint == FIELD_MIME_SEG)
			{
				// This was a hack to fix a dumb bug in a dev build... so so dumb.
				t->SetDirty();
			}
			else 
			#endif
			if (FieldHint != FIELD_FLAGS)
			{
				// Call the on load handler...
				t->IsLoaded(true);
			}

			if (t->GetList())
			{
				t->Update();
				if (FieldHint != FIELD_FLAGS)
					UpdateSelection |= t->Select();
			}
		}
	}

	if (MailList && UpdateSelection)
	{
		List<Thing> Sel;
		if (MailList->GetSelection(Sel))
		{
			OnSelect(&Sel, true);
		}
	}

	if (Parent)
		Parent->OnUpdateUnRead(0, true);

	if (NewMail.Length())
		OnNewMail(NewMail, true);

	d->CtxFile = NULL;
	d->CtxLine = 0;
	return true;
}

ContactGroup *ScribeWnd::FindGroup(char *SearchName)
{
	THREAD_UNSAFE(NULL);

	ScribeFolder *g = GetFolder(FOLDER_GROUPS);
	if (!g || !SearchName)
		return 0;

	g->LoadThings();
	for (Thing *t: g->Items)
	{
		ContactGroup *Grp = t->IsGroup();
		if (!Grp)
			continue;

		auto Name = Grp->GetObject()->GetStr(FIELD_GROUP_NAME);
		if (Name)
		{
			if (!_stricmp(Name, SearchName))
			{
				return Grp;
			}
		}
	}

	return 0;
}

bool ScribeWnd::Match(LDataStoreI *Store, LDataPropI *Address, int ObjType, LArray<LDom*> &Matches)
{
	THREAD_UNSAFE(false);

	if (!Store || !Address)
		return 0;

	// char *Addr = Address->GetStr(ObjType == MAGIC_CONTACT ? FIELD_EMAIL : FIELD_NAME);
	auto Addr = Address->GetStr(FIELD_EMAIL);
	if (!Addr)
		return 0;

	List<Contact> *l = GetEveryone();
	if (!l)
		return 0;

	Matches.Length(0);

	if (ObjType == MAGIC_CONTACT)
	{
		for (auto c: *l)
		{
			if (strchr(Addr, '@'))
			{
				auto Emails = c->GetEmails();
				for (auto e: Emails)
				{
					if (e.Equals(Addr))
					{
						Matches.Add(c);
						break;
					}
				}
			}
		}
	}
	else if (ObjType == MAGIC_GROUP)
	{
		ScribeFolder *g = GetFolder(FOLDER_GROUPS);
		if (!g)
			return false;

		g->LoadThings();
		for (auto t: g->Items)
		{
			ContactGroup *Grp = t->IsGroup();
			if (!Grp)
				continue;

			List<char> GrpAddr;
			if (!Grp->GetAddresses(GrpAddr))
				continue;

			for (auto a: GrpAddr)
			{
				if (_stricmp(a, Addr) == 0)
				{
					Matches.Add(t);
					break;
				}
			}

			GrpAddr.DeleteArrays();
		}
	}


	return Matches.Length() > 0;
}

bool ScribeWnd::OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
{
	THREAD_UNSAFE(false);

	if (!new_parent || items.Length() == 0)
		return false;

	bool Status = false;

	for (auto cb: d->Store3EventCallbacks)
		cb->OnMove(new_parent, old_parent, items);

	ScribeFolder *New = CastFolder(new_parent);
	ScribeFolder *Old = old_parent ? CastFolder(old_parent) : NULL;
	if (New)
	{
		ssize_t SelIdx = -1;
		for (unsigned n=0; n<items.Length(); n++)
		{
			switch ((uint32_t)items[n]->Type())
			{
				case MAGIC_FOLDER:
				{
					ScribeFolder *i = CastFolder(items[n]);
					if (i)
					{
						i->Detach();
						New->Insert(i);
						Status = true;
					}
					break;
				}
				default:
				{
					Thing *t = CastThing(items[n]);
					if (t)
					{
						int UnreadMail = t->IsMail() ? !TestFlag(t->IsMail()->GetFlags(), MAIL_READ) : false;

						if (Old && UnreadMail)
							Old->OnUpdateUnRead(-1, false);

						if (t->GetList())
						{
							if (t->Select() && SelIdx < 0)
								SelIdx = t->GetList()->IndexOf(t);
							t->GetList()->Remove(t);
						}
						
						// This closes the user interface if the object is being moved to the trash...
						if (New->GetSystemFolderType() == Store3SystemTrash)
							t->SetUI();

						t->SetParentFolder(New);
						if (New->Select())
						{
							MailList->Insert(t);
							MailList->Sort();
						}
						if (UnreadMail) New->OnUpdateUnRead(1, false);
						if (New->GetItemType() == MAGIC_ANY &&
							t->IsMail())
						{
							Mail::NewMailLst.Delete(t->IsMail());
						}

						Status = true;
					}
					break;
				}
			}
		}

		if (MailList && SelIdx >= 0 && MailList->Length() > 0)
		{
			if (SelIdx >= (ssize_t)MailList->Length())
				SelIdx = MailList->Length()-1;
			MailList->Value(SelIdx);
		}
	}

	return Status;
}

bool ScribeWnd::OnDelete(LDataFolderI *Parent, LArray<LDataI*> &Items)
{
	THREAD_UNSAFE(false);

	int UnreadAdjust = 0;
	int SelectIdx = -1;

	LOG_STORE("OnDelete(%s, %i)\n", Parent->GetStr(FIELD_FOLDER_NAME), (int)Items.Length());

	if (!Items.Length())
		return false;

	for (unsigned c=0; c<d->Store3EventCallbacks.Length(); c++)
	{
		d->Store3EventCallbacks[c]->OnDelete(Parent, Items);
	}

	ScribeFolder *Fld = NULL;
	for (unsigned i=0; i<Items.Length(); i++)
	{
		LDataI *Item = Items[i];

		if (Item->Type() == MAGIC_FOLDER)
		{
			// Insert new folder into the right point on the tree...
			ScribeFolder *Sub = CastFolder(Item);
			if (!Sub)
				return true;

			LgiTrace("OnDelete folder %p (%i)\n", (ThingType*)Sub, ThingType::DirtyThings.HasItem(Sub));
			// Remove the deleted object from the dirty queue...
			ThingType::DirtyThings.Delete(Sub);
			// And make sure it can't get dirty again...
			Sub->SetWillDirty(false);

			// Remove all the children
			LDataIterator<LDataI*> &SubItems = Sub->GetFldObj()->Children();
			LArray<LDataI*> Children;
			for (LDataI *c = SubItems.First(); c; c = SubItems.Next())
			{
				Children.Add(c);
			}
			OnDelete(Sub->GetFldObj(), Children);

			// Remove the UI element
			Sub->Remove();
			
			// Free the memory for the object
			DeleteObj(Sub);
		}
		else
		{
			Fld = Parent ? CastFolder(Parent) : 0;
			if (!Fld)
				return false;

			Thing *t = CastThing(Item);
			if (!t)
			{
				// This happens when the item moves from one store to another
				// of a different type and a copy of the old object is made. The
				// 'Thing' is detached from 'Item' and attached to the new LDataI
				// object.

				// However if the object is an unread email... we should still decrement the 
				// folder's unread count.
				if (Item->Type() == MAGIC_MAIL &&
					!(Item->GetInt(FIELD_FLAGS) & MAIL_READ))
				{
					Fld->OnUpdateUnRead(-1, false);
				}
			}
			else
			{
				LAssert(!Fld || Fld == t->GetFolder());
				
				// LgiTrace("OnDelete thing %p (%i)\n", (ThingType*)t, ThingType::DirtyThings.HasItem(t));
				
				// Remove the deleted object from the dirty queue...
				ThingType::DirtyThings.Delete(t);
				// And make sure it can't get dirty again...
				t->SetWillDirty(false);
				
				// Was the thing currently being previewed?
				if (PreviewPanel && PreviewPanel->GetCurrent() == t)
					PreviewPanel->OnThing(0, false);

				// Was the object selected in the thing list?
				if (Fld && Fld->Select())
				{
					// If so, select an adjacent item.
					if (SelectIdx < 0 && t->Select())
						SelectIdx = MailList->IndexOf(t);
					MailList->Remove(t);
				}

				// Was the object an unread email?
				Mail *m = t->IsMail();
				if (m)
				{
					if (!TestFlag(m->GetFlags(), MAIL_READ) && Fld)
						UnreadAdjust--;
					Mail::NewMailLst.Delete(m);
				}

				t->SetUI();
				t->SetParentFolder(NULL);
				t->SetObject(NULL, false, _FL);

				if (Fld)
					Fld->Update();

				if (!t->DecRef())
				{
					t->SetDirty(false);
					t->SetWillDirty(false);
				}
			}
		}
	}

	if (Fld)
		Fld->OnUpdateUnRead(UnreadAdjust, false);

	if (SelectIdx >= 0)
	{
		LListItem *i = MailList->ItemAt(SelectIdx);
		if (!i && MailList->Length() > 0)
			i = MailList->ItemAt(MailList->Length()-1);
		if (i)
			i->Select(true);
	}

	return true;
}

bool ScribeWnd::AddStore3EventHandler(LDataEventsI *callback)
{
	THREAD_UNSAFE(false);

	if (!d->Store3EventCallbacks.HasItem(callback))
		d->Store3EventCallbacks.Add(callback);

	return true;
}

bool ScribeWnd::RemoveStore3EventHandler(LDataEventsI *callback)
{
	THREAD_UNSAFE(false);

	d->Store3EventCallbacks.Delete(callback);

	return true;
}

bool ScribeWnd::OnMailTransferEvent(MailTransferEvent *t)
{
	THREAD_SAFE();

	if (!Lock(_FL))
		return false;

	LAssert(t);
	d->Transfers.Add(t);
	Unlock();
	
	return true;
}

bool ScribeWnd::OnTransfer()
{
	THREAD_UNSAFE(false);

	LVariant v;
	LArray<MailTransferEvent*> Local;
	
	// Lock the transfer list
	if (Lock(_FL))
	{
		// Take out a bunch of emails...
		for (int i=0; i<5 && d->Transfers.Length() > 0; i++)
		{
			auto t = d->Transfers[0];
			LAssert(t);
			d->Transfers.DeleteAt(0, true);

			// Save them to a local array
			Local.Add(t);
		}

		Unlock();

		if (Local.Length() == 0)
			// Didn't get any
			return false;
	}

	LArray<LDataStoreI::StoreTrans> Trans;
	for (auto &f: Folders)
	{
		if (f.Store)
			Trans.New() = f.Store->StartTransaction();
	}

	for (auto Transfer: Local)
	{
		ReceiveStatus NewStatus = MailReceivedError;

		if (!Transfer)
		{
			LAssert(0);
			continue;
		}
		
		// We have to set Transfer->Status to something other than "waiting"
		// in all branches of this loop. Otherwise the account thread will hang.
		
		Accountlet *Acc = Transfer->Account;
		#if DEBUG_NEW_MAIL
		LgiTrace(	"%s:%i - NewMail.OnTransfer t=%p receive=%i act=%i\n",
					_FL, Transfer, Acc->IsReceive(), Transfer->Action);
		#endif
		if (Acc->IsReceive())
		{
			switch (Transfer->Action)
			{
				default: break;
				case MailDownload:
				case MailDownloadAndDelete:
				{
					// Import newly received mail from file
					ReceiveAccountlet *Receive = dynamic_cast<ReceiveAccountlet*>(Acc);
					if (Receive)
					{
						LVariant Path = Receive->DestinationFolder();
						ScribeFolder *Inbox = Path.Str() ? GetFolder(Path.Str()) : NULL;
						if (!Inbox)
							Inbox = GetFolder(FOLDER_INBOX);

						if (Inbox)
						{
							Mail *m = new Mail(this, Inbox->GetObject()->GetStore()->Create(MAGIC_MAIL));
							if (m)
							{
								// Just in case
								m->SetParentFolder(Inbox);
								
								// Set the new flag...
								m->SetFlags(m->GetFlags() | MAIL_NEW, true, false);

								// Set the account to, which is used to map the email back
								// to the incoming account in the case that the headers
								// don't have the correct "To" information.
								m->SetAccountId(Receive->Id());

								// Decode the email
								m->OnAfterReceive(Transfer->Rfc822Msg);
								
								LVariant v;
								m->SetServerUid(v = Transfer->Uid);

								// Save to permanent storage
								if (m->Save(Inbox))
								{
									m->SetDirty(false);
									
									NewStatus = MailReceivedOk;

									// Only after we've safely stored the email can we 
									// actually mark it as downloaded.
									if (Transfer->Uid)
									{
										Receive->AddMsg(Transfer->Uid);
									}
								}
								else LgiTrace("%s:%i - Error: Couldn't save mail to folders.\n", _FL);
							}
							else LgiTrace("%s:%i - Error: Memory alloc failed.\n", _FL);
						}
						else LgiTrace("%s:%i - Error: No Inbox.\n", _FL);
					}
					else LgiTrace("%s:%i - Error: Bad ptr.\n", _FL);
					
					break;
				}
				case MailHeaders:
				{
					LList *Lst;
					if (Transfer->Msg &&
						(Lst = Transfer->GetList()))
					{
						//LgiTrace("Using Lst=%p\n", Lst);
						Lst->Insert(Transfer->Msg);
						NewStatus = MailReceivedOk;
					}
					break;
				}
			}
		}
		else if (Transfer->Send)
		{
			ScribeFolder *Outbox = GetFolder(Transfer->Send->SourceFolder);
			if (!Outbox)
				Outbox = GetFolder(FOLDER_OUTBOX);
			if (!Outbox)
				break;
				
			Outbox->GetMessageById(Transfer->Send->MsgId, [this, Transfer](auto m)
			{
				if (!m)
				{
					LAssert(!"Where is the email?");
					LgiTrace("%s:%i - Can't find outbox for msg id '%s'\n", _FL, Transfer->Send->MsgId.Get());
				}
				else
				{
					if (Transfer->OutgoingHeaders)
					{
						m->SetInternetHeader(Transfer->OutgoingHeaders);
						DeleteArray(Transfer->OutgoingHeaders);
					}
					
					m->OnAfterSend();
					m->Save();

					// Do filtering
					LVariant DisableFilters;
					GetOptions()->GetValue(OPT_DisableUserFilters, DisableFilters);
					if (!DisableFilters.CastInt32())
					{
						// Run the filters
						List<Filter> Filters;
						GetFilters(Filters, false, true, false);
						if (Filters[0])
						{													
							List<Mail> In;
							In.Insert(m);
							
							Filter::ApplyFilters(0, Filters, In);
						}
					}

					// Add to bayesian spam whitelist...
					LVariant v;
					ScribeBayesianFilterMode FilterMode = BayesOff;
					GetOptions()->GetValue(OPT_BayesFilterMode, v);
					FilterMode = (ScribeBayesianFilterMode) v.CastInt32();

					if (FilterMode != BayesOff &&
						m->GetObject())
					{
						LDataIt To = m->GetObject()->GetList(FIELD_TO);
						if (To)
						{
							for (LDataPropI *a = To->First();
								a;
								a = To->Next())
							{
								if (a->GetStr(FIELD_EMAIL))
									WhiteListIncrement(a->GetStr(FIELD_EMAIL));
							}
						}
					}

					// FIXME:
					// NewStatus = MailReceivedOk;
				}
			});
		}

		#if DEBUG_NEW_MAIL
		LgiTrace(	"%s:%i - NewMail.OnTransfer t=%p NewStatus=%i\n",
					_FL, Transfer, NewStatus);
		#endif

		if (NewStatus != MailReceivedOk)
		{
			// So tell the thread not to delete it from the server
			LgiTrace("%s:%i - Mail[%i] error: %s\n", _FL, Transfer->Index, ReceiveStatusName(Transfer->Status));
			Transfer->Action = MailNoop;
		}

		Transfer->Status = NewStatus;
	}

	return Local.Length() > 0;
}

bool ScribeWnd::OnIdle()
{
	THREAD_UNSAFE(false);

	bool Status = false;
	
	for (auto a : Accounts)
		Status |= a->Receive.OnIdle();

	Status |= OnTransfer();

	LMessage m(M_SCRIBE_IDLE);
	BayesianFilter::OnEvent(&m);

	SaveDirtyObjects();	

	#ifdef _DEBUG
	static uint64_t LastTs = 0;
	auto Now = LCurrentTime();
	if (Now - LastTs >= 1000)
	{
		LastTs = Now;
		if (Thing::DirtyThings.Length() > 0)
			LgiTrace("%s:%i - Thing::DirtyThings=" LPrintfInt64 "\n", _FL, Thing::DirtyThings.Length());
	}
	#endif

	return Status;
}

void ScribeWnd::OnScriptCompileError(const char *Source, Filter *f)
{
	THREAD_UNSAFE();

	const char *CompileMsg = LLoadString(IDS_ERROR_SCRIPT_COMPILE);
	LArray<const char*> Actions;
	Actions.Add(LLoadString(IDS_SHOW_CONSOLE));
	Actions.Add(LLoadString(IDS_OPEN_SOURCE));
	Actions.Add(LLoadString(IDS_OK));
	if (!d->Bar)
	{
		// FYI Capabilities are handled in ScribeWnd::StartAction.
		d->ErrSource = Source;
		d->ErrFilter = f;
		d->Bar = new MissingCapsBar(this, &d->MissingCaps, CompileMsg, this, Actions);
		AddView(d->Bar, 2);
		AttachChildren();
		OnPosChange();
	}
}

////////////////////////////////////////////////////////////////////////////////////
ScribeFolder *LMailStore::GetRoot() const
{
	return Root;
}

void LMailStore::SetRoot(ScribeFolder *f)
{
	if ((Root = f))
	{
		// Something is deleting the ScribeFolder object that root points to outside of the
		// DeleteRoot function. I want to catch that case and make sure there is no dangling
		// pointer here to cause a crash.
		Root->BeforeDelete = [this]()
		{
			LAssert(InRootDelete);
			if (!InRootDelete)
				Root = NULL;
		};
	}
}

void LMailStore::DeleteRoot()
{
	InRootDelete = true;
	DeleteObj(Root);
	InRootDelete = false;
}

bool LMailStore::IsOk() const
{
	return	Store != NULL &&
			Root  != NULL;
}

int LMailStore::Priority()
{
	return Store ? (int)Store->GetInt(FIELD_VERSION) : 0;
}

void LMailStore::Empty()
{
	Store.Reset();
	Name.Empty();
	Path.Empty();
	Root = NULL;
}

LMailStore &LMailStore::operator =(LMailStore &a)
{
	LAssert(0);
	return *this;
}

ScribeWndPrivate::ScribeWndPrivate(ScribeWnd *app) :
	App(app),
	TextControlFactory(app),
	TrayIcon(new LTrayIcon(app))
{
	htmlStatic = new LHtmlStaticInst;
	NoContact = new NoContactType(app);
	NoContact->DecRef(); // 2->1
	AppWndHnd = LEventSinkMap::Dispatch.AddSink(App);

#ifdef WIN32
	ClipboardFormat = RegisterClipboardFormat(
#ifdef UNICODE
		L"Scribe.Item"
#else
		"Scribe.Item"
#endif
	);
#endif

	LScribeScript::Inst = new LScribeScript(App);
	if (Engine.Reset(new LScriptEngine(App, LScribeScript::Inst, this)))
		Engine->SetConsole(LScribeScript::Inst->GetLog());
}

ScribeWndPrivate::~ScribeWndPrivate()
{
	// Why do we need this? ~LView will take care of it?
	// LEventSinkMap::Dispatch.RemoveSink(App);
	Options.Reset();
	Scripts.DeleteObjects();
	DeleteObj(ImageLoader);
	Engine.Reset();
	DeleteObj(LScribeScript::Inst);
	DeleteObj(htmlStatic);
}

