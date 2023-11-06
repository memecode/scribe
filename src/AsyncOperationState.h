#pragma once

class AsyncOperationState
{
	ScribeWnd *App = NULL;

	// Input
	ScribeFolder *Folder = NULL;
	LArray<Thing*> Items;
	bool CopyOnly;
	std::function<void(bool, LArray<Store3Status>&)> Callback;

	// Output
	bool Result = false; // Overall success/failure
	LArray<Store3Status> Status; // Per item status

								 // State

								 // Group all the mail in the same store into one operation:
	LArray<LDataI*> InStoreMove;

	LDataI *FolderObj = NULL;
	LDataStoreI *FolderStore = NULL;
	ScribeMailType NewBayesType = BayesMailUnknown;
	LHashTbl<PtrKey<Thing*>, int> Map;
	int NewFolderType = -1;
	bool BuildDynMenus = false;
	bool BayesInc = false;
	size_t Moves = 0;

	// Returns true the total operation is complete and
	// 'this' object has been deleted. Ie exit immediately
	// from the calling context.
	bool SetStatus(int i, Store3Status s)
	{
		LAssert(Status[i] == Store3NotImpl);
		Status[i] = s;

		LAssert(Moves > 0);
		Moves--;

		if (Moves > 0)
			return false;

		OnComplete();
		return true;
	}

public:
	AsyncOperationState(ScribeFolder *folder,
		LArray<Thing*> &items,
		bool copyOnly,
		std::function<void(bool, LArray<Store3Status>&)> callback) :
		Folder(folder),
		Items(items),
		CopyOnly(copyOnly),
		Callback(callback)
	{
		// Validate parameters
		if (Folder &&
			(App = Folder->App))
		{
			LVariant v;
			if (App->GetOptions()->GetValue(OPT_BayesIncremental, v))
				BayesInc = v.CastInt32() != 0;
		}
		else
		{
			delete this;
			return;
		}

		if ((FolderObj = Folder->GetObject()))
		{
			FolderStore = Folder->GetObject()->GetStore();
		}

		Status.Length(Moves = Items.Length());
		for (auto &s: Status)
			s = Store3NotImpl;

		auto FolderItemType = Folder->GetItemType();
		auto ThisFolderPath = Folder->GetPath();
		NewBayesType = App->BayesTypeFromPath(ThisFolderPath);
		NewFolderType = App->GetFolderType(Folder);
		ScribeFolder *TemplatesFolder = App->GetFolder(FOLDER_TEMPLATES);
		BuildDynMenus = Folder == TemplatesFolder;

		for (unsigned i=0; i<Items.Length(); i++)
		{
			auto t = Items[i];
			if (!t || !t->GetObject())
			{
				if (SetStatus(i, Store3Error))
					return;
				continue;
			}

			auto ThingItemType = t->Type();
			if (FolderItemType != ThingItemType && FolderItemType != MAGIC_ANY)
			{
				if (SetStatus(i, Store3Error))
					return;
				continue;
			}

			ScribeFolder *Old = t->GetFolder();
			LString Path;
			if (Old)
			{
				Path = Old->GetPath();

				if (Old == TemplatesFolder)
					// Moving to or from the templates folder... update the menu
					BuildDynMenus = true;
			}

			if (Old && Path)
			{
				bool IsDeleted = false;
				App->GetAccessLevel(App,
					Old->GetFolderPerms(ScribeWriteAccess),
					Path,
					[this, i, t, &IsDeleted](bool Allow)
					{
						if (Allow)
							IsDeleted = Move(i, t);
						else
							IsDeleted = SetStatus(i, Store3NoPermissions);
					});
				// If the callback has already been executed and the object is deleted, exit immediately.
				if (IsDeleted)
					return;
			}
			else
			{
				if (Move(i, t))
					return;
			}
		}

	}

	// This must call SetStatus once and only once for each item it's called with.
	// Returns true if the SetStatus call indicates deletion.
	// 'this' will be invalid after SetStatus returns true.
	bool Move(int i, Thing *t)
	{
		auto OldBayesType = BayesMailUnknown;
		if (BayesInc &&
			t->IsMail() &&
			TestFlag(t->IsMail()->GetFlags(), MAIL_READ))
		{
			OldBayesType = App->BayesTypeFromPath(t->IsMail());
		}

		auto Old = t->GetFolder();
		auto r = Store3NotImpl;

		auto OldFolderType = Old ? App->GetFolderType(Old) : -1;
		if ( (OldFolderType == FOLDER_TRASH || OldFolderType == FOLDER_SENT) &&
			NewFolderType == FOLDER_TRASH)
		{
			// Delete for good
			r = Old ? Old->DeleteThing(t, NULL) : Store3Error;
		}
		else
		{
			// If this folder is currently selected...
			if (Folder->Select())
			{
				// Insert item into list
				t->SetFieldArray(Folder->FieldArray);
			}

			if (CopyOnly)
			{
				LDataI *NewT = FolderStore->Create(t->Type());
				if (NewT)
				{
					NewT->CopyProps(*t->GetObject());
					r = NewT->Save(Folder->GetObject());
				}
				else
				{
					r = Store3Error;
				}
			}
			else
			{
				if (NewFolderType != FOLDER_TRASH &&
					OldBayesType != NewBayesType)
				{
					App->OnBayesianMailEvent(t->IsMail(), OldBayesType, NewBayesType);
				}

				// Move to this folder
				auto o = t->GetObject();
				if (o && o->GetStore() == FolderStore)
				{
					InStoreMove.Add(o);
					Map.Add(t, i);
					r = Store3Delayed;
				}
				else
				{
					// Out of store more... use the old single object method... for the moment..
					t->SetFolder(Folder,
						[this, Old, t, i](auto r)
						{
							if (r == Store3Success)
							{
								// Remove from the list..
								if (Old && Old->Select() && App->GetMailList())
									App->GetMailList()->Remove(t);
								t->OnMove();
							}

							return SetStatus(i, r);
						});
					return false; // SetFolder callback will call the SetStatus function;
				}
			}
		}

		if (r == Store3Success)
			t->OnMove();

		return SetStatus(i, r);
	}

	void OnComplete()
	{
		auto s = Store3NotImpl;

		if (InStoreMove.Length())
		{
			auto Fld = dynamic_cast<LDataFolderI*>(Folder->GetObject());
			if (!Fld)
				s = Store3Error;
			else
				s = FolderStore->Move(Fld, InStoreMove);

			for (auto p: Map)
			{
				Status[p.value] = s;

				if (s == Store3Success)
				{
					LAssert(p.key->GetFolder() == Folder);
					LAssert(Items.HasItem(p.key));

					p.key->OnMove();
				}
			}
		}

		// Calculate result based on the Status array:
		Result = true;
		for (auto s: Status)
		{
			if (s < Store3Delayed)
				Result = false;
		}

		if (BuildDynMenus)
			// Moving to or from the templates folder... update the menu
			App->BuildDynMenus();

		if (Callback)
			Callback(Result, Status);

		delete this;
	}
};

