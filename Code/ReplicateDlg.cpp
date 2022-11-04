#include "Scribe.h"
#include "ReplicateDlg.h"
#include "lgi/common/Combo.h"
#include "lgi/common/List.h"
#include "resdefs.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/LgiRes.h"

#define SECONDS					* 1000
#define REPLICATE_TIMEOUT		(30 SECONDS)
#define REPLICATE_TRANS_LENGTH	10
#define MAX_DELAYED_UNITS		10
#define DEBUG_LOGGING			1

enum WorkType
{
	RNull,
	RCountFinish,

	RCountSource,		// Uses 'Folder1'
	RDeleteFolder,		// Uses 'Folder1'

	RCopySubFolder,		// Uses 'Folder2'
	RCopyFolder,		// Uses 'Folder2'
	RCreateFolder,		// Uses 'Folder2'

	RCopyData,			// Uses 'Data'
	RSaveData,			// Uses 'Data'

	RReloadFolders,		// Tells 'App' to load folders..
	ROpenMailStore,		// Uses 'Open'
	RCopyStore,			// Expects 'SrcStore' and 'DstStore' as arguments
	REndTransaction,
};

const char *ToString(WorkType t)
{
	switch (t)
	{
		case RNull: return "RNull";
		case RCountSource: return "RCountSource";
		case RCountFinish: return "RCountFinish";
		case RCopyFolder: return "RCopyFolder";
		case RCopySubFolder: return "RCopySubFolder";
		case RCreateFolder: return "RCreateFolder";
		case RCopyData: return "RCopyData";
		case RSaveData: return "RSaveData";
		case RReloadFolders: return "RReloadFolders";
		case ROpenMailStore: return "ROpenMailStore";
		case RCopyStore: return "RCopyStore";
		case REndTransaction: return "REndTransaction";
		case RDeleteFolder: return "RDeleteFolder";
	}
	return "#error";
}

static LString ObjToString(LDataI *d)
{
	LString s;
	switch ((unsigned)d->Type())
	{
		case MAGIC_MAIL:
			s = d->GetStr(FIELD_MESSAGE_ID);
			break;
		case MAGIC_CONTACT:
			s.Printf("%s %s",
				d->GetStr(FIELD_FIRST_NAME),
				d->GetStr(FIELD_LAST_NAME));
			break;
		case MAGIC_FILTER:
			s = d->GetStr(FIELD_FILTER_NAME);
			break;
		case MAGIC_CALENDAR:
			s = d->GetStr(FIELD_CAL_SUBJECT);
			break;
		case MAGIC_GROUP:
			s = d->GetStr(FIELD_GROUP_NAME);
			break;
		default:
			s.Printf("Error: Unknown type '%x'", d->Type());
			break;
	}
	return s;
}

struct ScribeReplicator : public LProgressDlg, public LDataEventsI
{
	bool CanRunConcurrent(WorkType t)
	{
		return	t == RCopyData ||
				t == RSaveData;
	}
	
	struct CopyStatus
	{
		int Total;
		int Errors;
		int Ok;
		
		CopyStatus()
		{
			Total = 0;
			Errors = 0;
			Ok = 0;
		}
	};
	
	LHashTbl<PtrKey<void*>, CopyStatus*> Status;
	
	struct WorkUnit
	{
		WorkType Type;
		uint64 Ts;
		bool Delayed;
		LArray<WorkUnit> Deferred;
		union
		{
			struct
			{
				LDataFolderI *Src, *Dst;
			}	Folder2; // Valid if Type == RCopyFolder

			struct
			{
				LDataFolderI *Folder;
			}	Folder1; // Valid if Type == RCountSource

			struct
			{
				LDataFolderI *DstFld;
				LDataFolderI *SrcFld;
				LDataI *Src;
			}	Data; // Valid if Type == RCopyData
			
			struct
			{
				ReplicateDlg::AccountSpec *Spec;
				LAutoPtr<LDataStoreI> *Store;
			}	Open; // Valid if Type == ROpenMailStore
		};
		
		LString ToString()
		{
			LString s;
			
			switch (Type)
			{
				// Folder2 users
				case RCopyFolder:
				case RCopySubFolder:
				case RCreateFolder:
					s.Printf("%s: %s <- %s",
						::ToString(Type),
						Folder2.Dst->GetStr(FIELD_FOLDER_NAME),
						Folder2.Src->GetStr(FIELD_FOLDER_NAME));
						break;

				// Folder1 users
				case RCountSource:
				case RDeleteFolder:
					s.Printf("%s: %s",
						::ToString(Type),
						Folder1.Folder->GetStr(FIELD_FOLDER_NAME));
						break;
						
				// Data users
				case RCopyData:
				case RSaveData:
					s.Printf("%s: %s <- %s",
						::ToString(Type),
						Data.DstFld->GetStr(FIELD_FOLDER_NAME),
						ObjToString(Data.Src).Get());
						break;
				
				default:
					s = ::ToString(Type);
					break;
			}
			
			return s;
		}
	};

	ScribeWnd *App;
	int Folders, Items;
	bool Types[MAGIC_MAX-MAGIC_BASE];
	int Copied[MAGIC_MAX-MAGIC_BASE];
	bool Recurse;
	bool DeleteSourceOnSuccess;
	LArray<WorkUnit> Work;
	LAutoPtr<LDataStoreI> SrcStore, DstStore;
	ReplicateDlg::AccountSpec SrcSpec, DstSpec;
	LDataStoreI::StoreTrans Trans;
	int TransLen;
	bool RestartOnPulse;
	bool PulseStarted;
	uint64 LastEvent;
	int UnitsTimedOut;
	LString OverviewMsg;
	LString StatusMsg;

	// Error handling..
	int FailedWork;
	LStringPipe ErrorLog;

	ScribeReplicator(ScribeWnd *app) : LProgressDlg(app)
	{
		App = app;
		Recurse = true;
		DeleteSourceOnSuccess = false;
		LastEvent = 0;
		Folders = 0;
		Items = 0;
		UnitsTimedOut = 0;
		PulseStarted = false;
		RestartOnPulse = false;
		FailedWork = 0;
		ZeroObj(Copied);
		SetDescription("Loading...");

		App->OnFolderTask(this, true);
		App->AddStore3EventHandler(this);
		
		SetAlwaysOnTop(true);
	}
	
	~ScribeReplicator()
	{
		App->OnFolderTask(this, false);
		App->RemoveStore3EventHandler(this);
		Status.DeleteObjects();
	}
	
	LMessage::Result OnEvent(LMessage *Msg)
	{
		switch (Msg->Msg())
		{
			case M_REPLICATE_NEXT:
				return DoNext();
			case M_STORAGE_EVENT:
			{
				LDataStoreI *Store = (LDataStoreI*)Msg->A();
				if (Store)
					Store->OnEvent((void*)Msg->B());
				break;
			}
		}
		
		return LProgressDlg::OnEvent(Msg);
	}
	
	LString ToString()
	{
		LString sep("\n");
		LString::Array s;
		for (unsigned i=0; i<Work.Length(); i++)
		{
			s.New() = Work[i].ToString();
		}
		return sep.Join(s);
	}
	
	int OnFinish()
	{
		if (FailedWork)
		{
			ErrorLog.Print("%i failed copies.\n", FailedWork);
		}
		
		if (ErrorLog.GetSize())
		{
			LAutoString a(ErrorLog.NewStr());
			LgiMsg(this, "Replication failed:\n%s", AppName, MB_OK, a.Get());
		}
		
		Quit();
		return true;		
	}
	
	void StartTransaction(LDataI *obj)
	{
		Trans = obj->GetStore()->StartTransaction();
		TransLen = 0;
	}
	
	// This is called after there is a valid object to save.
	// It may end up with a deferred save.
	int SaveObject(WorkUnit *w)
	{
		LDataFolderI *df = w->Data.DstFld;
		LDataFolderI *sf = w->Data.SrcFld;
		LDataI *s = w->Data.Src;

		LDataI *d = df->GetStore()->Create(s->Type());
		if (d)
		{
			d->CopyProps(*s);
			
			Store3Status Result = d->Save(df);
			if (Result == Store3Error)
			{
				ErrorLog.Print("%s:%i - Failed to save item.\n", _FL);
			}
			else if (Result == Store3Delayed)
			{
				w->Type = RSaveData;
				w->Data.Src = d;
				w->Delayed = true;
				w->Ts = LCurrentTime();
				return true;
			}
			
			CopyStatus *Cs = Status.Find(sf);
			if (Cs)
				Cs->Ok++;
			
			TransLen++;
		}
		else ErrorLog.Print("%s:%i - Failed to create object of type %i\n", _FL, s->Type());
		
		Pop(w);
		Value(Value() + 1);
		
		if (TransLen >= REPLICATE_TRANS_LENGTH)
		{
			Trans.Reset();
		}
		return true;
	}
	
	void Pop(WorkUnit *w)
	{
		if (Work.PtrCheck(w))
		{
			ptrdiff_t Idx = w - &Work.First();
			Work.DeleteAt((int)Idx, true);
		}
		else LAssert(0);
	}
	
	int DoNext()
	{
		if (Work.Length() == 0)
			return OnFinish();
		if (!PulseStarted)
		{
			SetPulse(60);
			PulseStarted = true;
		}
	
		uint64 Start = LCurrentTime();
		LastEvent = Start;

		WorkUnit *w = NULL;
		int DelayedCopies = 0, DelayedSaves = 0, DelayedCreate = 0;
		for (unsigned i=0; i<Work.Length(); i++)
		{
			w = &Work[i];
			if (w->Delayed)
			{
				if (w->Type == RCopyData)
					DelayedCopies++;
				else if (w->Type == RSaveData)
					DelayedSaves++;
				else if (w->Type == RCreateFolder)
					DelayedCreate++;
			}
		}
		
		if (DelayedCopies + DelayedSaves >= MAX_DELAYED_UNITS ||
			DelayedCreate > 0)
		{
			RestartOnPulse = true;
			return true;
		}
		
		for (ssize_t i=(ssize_t)Work.Length()-1; i>=0; i--)
		{
			w = &Work[i];
			if (CanRunConcurrent(w->Type))
			{
				if (w->Delayed)
					continue; // Find the last undelayed unit
				// else do this unit
			}
			else
			{
				if (w->Delayed)
					w = NULL; // We wait for it to complete..
				// else do this unit
			}
			break;
		}
		
		if (!w)
		{
			RestartOnPulse = true;
			#if DEBUG_LOGGING
			LgiTrace("%s:%i - DoNext all remaining delayed (%i/%i)\n", _FL, DelayedCopies, DelayedSaves);
			#endif
			return true;
		}

		#if DEBUG_LOGGING
		LString Str = w->ToString();
		LgiTrace("Do: %s\n", Str.Get());
		#endif
		
		switch (w->Type)
		{
			case RCreateFolder:
			{
				// Just wait for it to be created...
				LAssert(w->Delayed);
				RestartOnPulse = true;			
				return true;
			}
			case RDeleteFolder:
			{
				LDataFolderI *f = w->Folder1.Folder;
				if (!f)
				{
					ErrorLog.Print("%s:%i - Invalid folder.\n", _FL);
					Pop(w);
					break;
				}
				
				// Get the copy status
				CopyStatus *Cs = Status.Find(f);
				if (!Cs)
				{
					ErrorLog.Print("%s:%i - No copy status for '%s'.\n", _FL, f->GetStr(FIELD_FOLDER_NAME));
					Pop(w);
					break;
				}
				
				// Check that all the items have been copied across.
				if (Cs->Ok + Cs->Errors < Cs->Total)
				{
					// Go into wait mode...
					RestartOnPulse = true;
					return true;
				}
				
				if (Cs->Total != Cs->Ok)
				{
					// Error out if not
					ErrorLog.Print("%s:%i - Delete '%s' skipped: %i of %i ok (%i errors).\n",
						_FL,
						f->GetStr(FIELD_FOLDER_NAME),
						Cs->Ok,
						Cs->Total,
						Cs->Errors);				
					Pop(w);
					break;
				}
				
				Store3Status s = f->Delete();
				if (s == Store3Error)
				{
					ErrorLog.Print("%s:%i - Failed to delete folder.\n", _FL);
				}
				else if (s == Store3Delayed)
				{
					w->Delayed = true;
					// Leave the work unit on the stack...
					break;
				}

				Pop(w);				
				break;
			}
			case RCountSource:
			{
				LDataFolderI *f = w->Folder1.Folder;
				LAssert(f != NULL);
				
				if (!f->GetInt(FIELD_IS_ONLINE))
				{
					w->Delayed = true;
					w->Ts = LCurrentTime();
					
					StatusMsg.Printf("Waiting folder %s", f->GetStr(FIELD_FOLDER_NAME));
					UpdateMsg();
					RestartOnPulse = true;
					return true;
				}
				
				Folders++;
				Pop(w);

				for (LDataI *i = f->Children().First(); i && !IsCancelled(); i = f->Children().Next())
				{
					int Type = i->Type();
					if (Type && Types[Type-MAGIC_BASE])
						Items++;
				}

				for (LDataFolderI *c = f->SubFolders().First(); c && !IsCancelled(); c = f->SubFolders().Next())
				{
					WorkUnit &wu = Work.New();
					wu.Type = RCountSource;
					wu.Folder1.Folder = c;
				}
				break;
			}
			case RCountFinish:
			{
				// Update the UI
				Pop(w);
				
				OverviewMsg.Printf("%i items in %i folder(s)...", Items, Folders);
				UpdateMsg();
				
				SetRange(Items);
				break;
			}
			case REndTransaction:
			{
				Pop(w);
				Trans.Reset();
				break;
			}
			case RCopyFolder:
			{
				// Iterate over all the items and push work units onto the stack...
				LDataFolderI *d = w->Folder2.Dst;
				LDataFolderI *s = w->Folder2.Src;
				LAssert(d != NULL && d != NULL);
				Pop(w);

				auto Name = s->GetStr(FIELD_FOLDER_NAME);
				int64 Type = s->GetInt(FIELD_FOLDER_TYPE);

				StatusMsg.Printf("Copying folder '%s'", Name);
				UpdateMsg();
				
				CopyStatus *Cs = NULL;
				if (DeleteSourceOnSuccess)
				{
					// Setup a copy status structure..
					Cs = new CopyStatus;
					if (Cs)
						Status.Add(s, Cs);
					else
						LAssert(0);
					
					// Create a delete if needed
					WorkUnit &del = Work.New();
					del.Type = RDeleteFolder;
					del.Folder1.Folder = s;
				}
				
				// Setup an end transaction to commit outstanding data..
				Work.New().Type = REndTransaction;

				if (Type && Types[Type-MAGIC_BASE])
				{
					int ExistsInDestination = 0;
					
					switch (Type)
					{
						case MAGIC_MAIL:
						{
							// Scan existing items for UID's
							LHashTbl<ConstStrKey<char>,LDataI*> MsgMap;
							for (LDataI *e=d->Children().First(); e && !IsCancelled(); e=d->Children().Next())
							{
								auto MsgId = e->GetStr(FIELD_MESSAGE_ID);
								if (MsgId)
									MsgMap.Add(MsgId, e);
							}

							// Start replicate process
							for (LDataI *in=s->Children().First(); in && !IsCancelled(); in=s->Children().Next())
							{
								// Check if we have an existing item...
								auto SrcMsgId = in->GetStr(FIELD_MESSAGE_ID);
								if (!MsgMap.Find(SrcMsgId))
								{
									// Create a work unit to copy the data...
									WorkUnit &wu = Work.New();
									wu.Type = RCopyData;
									wu.Data.DstFld = d;
									wu.Data.SrcFld = s;
									wu.Data.Src = in;
									
									if (Cs) Cs->Total++;
								}
								else
								{
									ExistsInDestination++;
								}
							}
							break;
						}
						case MAGIC_CONTACT:
						{
							// Scan for UID's
							LHashTbl<ConstStrKey<char>,LDataI*> Uid, Email;
							const char *c;
							for (LDataI *e=d->Children().First(); e && !IsCancelled(); e=d->Children().Next())
							{
								if ((c = e->GetStr(FIELD_UID)))
									Uid.Add(c, e);
								if ((c = e->GetStr(FIELD_EMAIL)))
									Email.Add(c, e);
							}

							// Start replicate process
							for (LDataI *in=s->Children().First(); in && !IsCancelled(); in=s->Children().Next())
							{
								// Check if we have an existing item...
								if (!Uid.Find(in->GetStr(FIELD_UID)) &&
									!Email.Find(in->GetStr(FIELD_EMAIL)))
								{
									// Create a work unit to copy the data...
									WorkUnit &wu = Work.New();
									wu.Type = RCopyData;
									wu.Data.DstFld = d;
									wu.Data.SrcFld = s;
									wu.Data.Src = in;

									if (Cs) Cs->Total++;
								}
								else
								{
									ExistsInDestination++;
								}
							}
							break;
						}
						case MAGIC_FILTER:
						{
							// Scan for names
							LHashTbl<ConstStrKey<char,false>,LDataI*> Name;
							const char *c;
							for (LDataI *e=d->Children().First(); e && !IsCancelled(); e=d->Children().Next())
							{
								if ((c = e->GetStr(FIELD_FILTER_NAME)))
									Name.Add(c, e);
							}

							// Start replicate process
							for (LDataI *in=s->Children().First(); in && !IsCancelled(); in=s->Children().Next())
							{
								// Check if we have an existing item...
								if (!Name.Find(in->GetStr(FIELD_FILTER_NAME)))
								{
									// Create a work unit to copy the data...
									WorkUnit &wu = Work.New();
									wu.Type = RCopyData;
									wu.Data.DstFld = d;
									wu.Data.SrcFld = s;
									wu.Data.Src = in;

									if (Cs) Cs->Total++;
								}
								else
								{
									ExistsInDestination++;
								}
							}
							break;
						}
					}

					if (ExistsInDestination)
					{
						Value(Value() + ExistsInDestination);
					}
				}

				if (Recurse)
				{
					// Create a work unit to copy the child folders...
					//
					// By doing this at the end things like deletes get done
					// on child folders first. E.g. for a "move" operation, 
					// which is broken down to copy + delete.
					WorkUnit &wu = Work.New();
					wu.Type = RCopySubFolder;
					wu.Folder2.Dst = d;
					wu.Folder2.Src = s;
				}
				break;
			}
			case RCopySubFolder:
			{
				// Iterate over all the items and push work units onto the stack...
				LDataFolderI *d = w->Folder2.Dst;
				LDataFolderI *s = w->Folder2.Src;
				LAssert(d != NULL && d != NULL);
				Pop(w);

				LHashTbl<ConstStrKey<char,false>,LDataFolderI*> DestMap;
				for (LDataFolderI *dc = d->SubFolders().First(); dc; dc=d->SubFolders().Next())
				{
					auto DstName = dc->GetStr(FIELD_FOLDER_NAME);
					if (DstName)
						DestMap.Add(DstName, dc);
				}

				// Replicate sub-folders
				for (LDataFolderI *sc=s->SubFolders().First(); sc && !IsCancelled(); sc=s->SubFolders().Next())
				{
					auto SrcName = sc->GetStr(FIELD_FOLDER_NAME);
					if (SrcName)
					{
						Store3Status Status = Store3Success;
						LArray<WorkUnit> *Tasks = &Work;
						
						// Find matching dest folder...
						LDataFolderI *dc = DestMap.Find(SrcName);
						if (!dc)
						{
							// Not found, so create new destination sub-folder
							if ((dc = dynamic_cast<LDataFolderI*>(d->GetStore()->Create(MAGIC_FOLDER))))
							{
								dc->CopyProps(*sc);
								
								Status = dc->Save(d);
								if (Status == Store3Error)
								{
									ErrorLog.Print("%s:%i - Failed to create folder '%s'\n", _FL, SrcName);
								}
								else if (Status == Store3Delayed)
								{
									WorkUnit &wu = Work.New();
									wu.Type = RCreateFolder;
									wu.Folder2.Dst = d;
									wu.Folder2.Src = dc;
									wu.Delayed = true;
									Tasks = &wu.Deferred;
								}
							}
						}

						if (dc)
						{
							WorkUnit &wu = Tasks->New();
							wu.Type = RCopyFolder;
							wu.Folder2.Dst = dc;
							wu.Folder2.Src = sc;
						}
					}
				}
				break;
			}
			case RCopyData:
			{
				LDataFolderI *d = w->Data.DstFld;
				LDataI *s = w->Data.Src;
				LAssert(d != NULL && d != NULL);
				
				if (IsCancelled())
				{
					Pop(w);
					break;
				}
				
				if (!Trans)
					StartTransaction(d);
				
				Store3State State = (Store3State)s->GetInt(FIELD_LOADED);
				if (State != Store3Loaded)
				{
					// Ask for the object to load itself..
					s->GetStr(FIELD_TEXT);

					// Now check again to see what it's doing...
					State = (Store3State)s->GetInt(FIELD_LOADED);
					if (State == Store3Loading)
					{
						// We should get an OnChange event when it loads...
						w->Delayed = true;
						w->Ts = LCurrentTime();
						RestartOnPulse = true;
						return true;
					}
					else if (State != Store3Loaded)
					{
						// The error case... kill the task
						FailedWork++;
						Pop(w);
						break;
					}
				}

				SaveObject(w);
				break;
			}
			case RReloadFolders:
			{
				Pop(w);
				App->LoadFolders();
				break;
			}
			case ROpenMailStore:
			{
				Store3Status s = Open(w->Open.Store, *w->Open.Spec);
				if (s == Store3Error)
				{
					LString a = w->Open.Spec->Uri.ToString();
					ErrorLog.Print("%s:%i - Failed to open data store '%s'\n", _FL, a.Get());
				}
				else if (s == Store3Delayed)
				{
					w->Delayed = true;
					w->Ts = LCurrentTime();
					
					StatusMsg.Printf("Waiting for IMAP connection");
					UpdateMsg();
					RestartOnPulse = true;
					return true;
				}
				
				Pop(w);
				break;
			}
			case RCopyStore:
			{
				Pop(w);
				if (SrcStore && DstStore)
				{
					LDataFolderI *SrcRoot = SrcStore->GetRoot();
					LDataFolderI *DstRoot = DstStore->GetRoot();
					if (SrcRoot && DstRoot)
					{
						// Setup a job to copy the root sub-folders
						w = &Work.New();
						w->Type = RCopySubFolder;
						w->Folder2.Dst = DstRoot;
						w->Folder2.Src = SrcRoot;
						
						// Update the UI with the count results...
						Work.New().Type = RCountFinish;

						// Setup a copy store task
						w = &Work.New();
						w->Type = RCountSource;
						w->Folder1.Folder = SrcRoot;
					}
					else
					{
						ErrorLog.Print("%s:%i - Get roots failed %p/%p\n", _FL, SrcRoot, DstRoot);
					}
				}
				else
				{
					ErrorLog.Print("%s:%i - Copy store failed %p/%p\n", _FL, SrcStore.Get(), DstStore.Get());
				}
				break;
			}
			default:
			{
				LAssert(!"Invalid type.");
				break;
			}
		}

		uint64 Length = LCurrentTime() - Start;
		#if DEBUG_LOGGING
		if (Length > 20)
			LgiTrace("Work %i took " LPrintfInt64 "\n", w->Type, Length);
		#endif
		
		if (Length >= 50)
		{
			// This leaves a air gap for messages to be processed normally.
			RestartOnPulse = true;			
			return true;
		}
		
		return PostEvent(M_REPLICATE_NEXT);
	}
	
	void UpdateMsg()
	{
		LString s, m;
		
		if (OverviewMsg)
			s.Printf("%s\n", OverviewMsg.Get());
		
		s += StatusMsg;
		
		if (UnitsTimedOut > 0)
		{
			m.Printf(" (%i timed out)", UnitsTimedOut);
			s += m;
		}
		else s += "...";
		
		SetDescription(s);
	}
	
	void OnPulse()
	{
		// Check for timeouts...
		int Prev = UnitsTimedOut;
		UnitsTimedOut = 0;
		
		uint64 Now = LCurrentTime();
		for (unsigned i=0; i<Work.Length(); i++)
		{
			WorkUnit *w = &Work[i];
			if (w->Delayed)
			{
				if (Now - w->Ts > REPLICATE_TIMEOUT)
				{
					if (IsCancelled())
					{
						// Kill the work unit... user wants out.
						Work.DeleteAt(i--);
					}
					else
					{
						// Show the user there is an issue...
						UnitsTimedOut++;
					}
				}
				
				switch (w->Type)
				{
					case ROpenMailStore:
					{
						if (IsCancelled())
						{
							Work.DeleteAt(i--);
						}
						else
						{
							// Waiting for IMAP connection...
							int64 Online = (*w->Open.Store)->GetInt(FIELD_IS_ONLINE);
							if (Online)
							{
								Pop(w);
							}
							else
							{
								int64 Ms = LCurrentTime() - w->Ts;
								StatusMsg.Printf("Waiting %.1fsec for IMAP connection", ((double)Ms / 1000.0));
								UpdateMsg();
							}
						}
						break;
					}
					case RCountSource:
					{
						if (IsCancelled())
							Work.DeleteAt(i--);
						break;
					}
					default: break;
				}
			}
		}
		
		if (UnitsTimedOut != Prev)
			UpdateMsg();
		
		if (RestartOnPulse)
		{
			RestartOnPulse = false;
			PostEvent(M_REPLICATE_NEXT);
		}
	}

	bool StartProcess(LDataFolderI *Dst, LDataFolderI *Src, bool recurse, bool deleteSourceOnSuccess, LArray<uint32_t> *types)
	{
		if (!Dst)
		{
			ErrorLog.Print("%s:%i - No destination folder.\n", _FL);
			return false;
		}
		if (!Src)
		{
			ErrorLog.Print("%s:%i - No source folder.\n", _FL);
			return false;
		}

		Recurse = recurse;
		DeleteSourceOnSuccess = deleteSourceOnSuccess;
		if (types)
		{
			ZeroObj(Types);
			for (unsigned i=0; i<types->Length(); i++)
				Types[(*types)[i]-MAGIC_BASE] = true;
		}
		else
		{
			memset(Types, 1, sizeof(Types));
		}
		
		// Setup a task to copy the folders...
		WorkUnit *w = &Work.New();
		w->Type = RCopyFolder;
		w->Folder2.Src = Src;
		w->Folder2.Dst = Dst;
		
		// Update the UI with the count results...
		Work.New().Type = RCountFinish;

		// Setup a copy store task
		w = &Work.New();
		w->Type = RCountSource;
		w->Folder1.Folder = Src;
		
		// Start the event cycle
		return PostEvent(M_REPLICATE_NEXT);
	}

	bool StartProcess(ReplicateDlg::ReplicateSettings *Settings)
	{
		if (!Settings)
		{
			ErrorLog.Print("%s:%i - No settings.\n", _FL);
			return false;
		}
		
		// Set copy type flags
		ZeroObj(Types);
		for (unsigned i=0; i<Settings->Types.Length(); i++)
			Types[Settings->Types[i]-MAGIC_BASE] = true;

		// Setup a task to reload the Scribe folders at the end...
		WorkUnit *w = &Work.New();
		w->Type = RReloadFolders;

		// Setup a copy store task
		w = &Work.New();
		w->Type = RCopyStore;

		// Setup load tasks
		SrcSpec = Settings->Src;
		DstSpec = Settings->Dst;
		
		w = &Work.New();
		w->Type = ROpenMailStore;
		w->Open.Store = &SrcStore;
		w->Open.Spec = &SrcSpec;

		w = &Work.New();
		w->Type = ROpenMailStore;
		w->Open.Store = &DstStore;
		w->Open.Spec = &DstSpec;

		// Start the event cycle
		return PostEvent(M_REPLICATE_NEXT);
	}

	Store3Status Open(LAutoPtr<LDataStoreI> *Out, ReplicateDlg::AccountSpec &Acc)
	{
		if (!Out)
			return Store3Error;
			
		if (Acc.Uri.sProtocol && !_stricmp(Acc.Uri.sProtocol, "imap"))
		{
			MailProtocolProgress *prog[2] = { NULL, NULL };
			
			LAutoPtr<ProtocolSettingStore> SettingStore;			
			LDataStoreI *Store = OpenImap(	Acc.Uri.sHost,
											Acc.Uri.Port,
											Acc.Uri.sUser,
											Acc.Uri.sPass,
											Acc.SslFlags,
											this,
											0,
											prog,
											0,
											0,
											SettingStore);
			if (!Store)
				return Store3Error;
			
			Out->Reset(Store);
			return Store3Delayed;
		}
		else if (Acc.Uri.sProtocol && !_stricmp(Acc.Uri.sProtocol, "file"))
		{
			char Path[MAX_PATH_LEN];
			if (LIsRelativePath(Acc.Uri.sPath))
			{
				LMakePath(Path, sizeof(Path), App->GetOptions()->GetFile(), "..");
				LMakePath(Path, sizeof(Path), Path, Acc.Uri.sPath);
			}
			else
			{
				strcpy_s(Path, sizeof(Path), Acc.Uri.sPath);
			}

			char *Ext = LGetExtension(Path);
			if (LDirExists(Path))
			{
				if (Ext && !_stricmp(Ext, "mail3"))
				{
					LDataStoreI *Store = OpenMail3(Path, this);
					if (!Store)
						return Store3Error;
					
					Out->Reset(Store);
				}
			}
			else LAssert(!"Unknown store type.");
		}
		else LAssert(!"Unknown protocol.");

		return Store3Success;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// LDataEventsI impl
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	void Post(LDataStoreI *store, void *Param)
	{
		PostEvent(M_STORAGE_EVENT, (LMessage::Param)store, (LMessage::Param)Param);
	}

	bool GetSystemPath(int Folder, LVariant &Path)
	{
		return App->GetSystemPath(Folder, Path);
	}

	LOptionsFile *GetOptions(bool Create = false)
	{
		return App->GetOptions();
	}

	void OnNew(LDataFolderI *parent, LArray<LDataI*> &items, int pos, bool is_new)
	{
		#if DEBUG_LOGGING
		LgiTrace("%s:%i - OnNew %i\n", _FL, items.Length());
		#endif
		
		for (ssize_t i=(ssize_t)Work.Length()-1; i>=0; i--)
		{
			WorkUnit &w = Work[i];
			if (!w.Delayed)
				continue;

			switch (w.Type)
			{
				case RCreateFolder:
				{
					if (items.HasItem( (LDataI*)w.Folder2.Src ))
					{
						#if DEBUG_LOGGING
						LString Str = w.ToString();
						LgiTrace("OnNew.%s\n", Str.Get());
						#endif

						Work.DeleteAt(i);
					}
					break;
				}
				case RCopyData:
				case RSaveData:
				{
					if (items.HasItem( (LDataI*)w.Data.Src ))
					{
						CopyStatus *Cs = Status.Find(w.Data.SrcFld);
						if (Cs)
							Cs->Ok++;

						#if DEBUG_LOGGING
						LString Str = w.ToString();
						LgiTrace("OnNew.%s (Cs=%p)\n", Str.Get(), Cs);
						#endif

						Work.DeleteAt(i);
						Value(Value() + 1);
					}
					break;
				}
				default: break;
			}
		}
	}

	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items)
	{
		for (ssize_t i=(ssize_t)Work.Length()-1; i>=0; i--)
		{
			WorkUnit *w = &Work[i];
			if (!w->Delayed)
				continue;
			
			if (w->Type == RDeleteFolder)
			{
				if (items.HasItem((LDataI*)w->Folder1.Folder))
				{
					// Finished the delete...
					#if DEBUG_LOGGING
					LString Str = w->ToString();
					LgiTrace("OnDelete.%s\n", Str.Get());
					#endif

					Pop(w);
				}
			}
		}
		
		return true;
	}
	
	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &item)
	{
		return true;
	}
	
	bool OnChange(LArray<LDataI*> &items, int FieldHint)
	{
		#if DEBUG_LOGGING
		LgiTrace("%s:%i - OnChange %i\n", _FL, items.Length());
		#endif

		for (ssize_t i=(ssize_t)Work.Length()-1; i>=0; i--)
		{
			WorkUnit &w = Work[i];
			if (!w.Delayed)
				continue;

			switch (w.Type)
			{
				case RCopyData:
				{
					// This event fires when the delayed open has completed...
					if (items.HasItem(w.Data.Src))
					{
						// Finish the save...
						SaveObject(&w);
					}
					break;
				}
				default: break;
			}
		}
	
		return true;
	}

};

struct ReplicateDlgPriv
{
	ScribeWnd *App;
	LCombo *Src, *Dst;
	LList *Lst;
	LArray<ReplicateDlg::AccountSpec> Paths;
	LAutoPtr<ReplicateDlg::ReplicateSettings> Settings;
	LAutoString Msg;

	ReplicateDlgPriv()
	{
		App = 0;
		Src = Dst = 0;
		Lst = 0;
	}
};

class LType : public LListItem
{
	LListItemCheckBox *Chk;

public:
	int Type;

	LType(int t, int s)
	{
		Type = t;
		SetText((char*)LLoadString(s), 1);
		Chk = new LListItemCheckBox(this, 0, true);
	}

	bool Value() { return Chk->Value() != 0; }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplicateDlg::ReplicateDlg(ScribeWnd *app)
{
	d = new ReplicateDlgPriv;
	SetParent(d->App = app);

	if (LoadFromResource(IDD_REPLICATE))
	{
		MoveToCenter();

		if (GetViewById(IDC_SRC, d->Src) &&
			GetViewById(IDC_DST, d->Dst))
		{
			// Scan for file based mail stores...
			LXmlTag *Ms = d->App->GetOptions()->LockTag(OPT_MailStores, _FL);
			if (Ms)
			{
				for (auto c: Ms->Children)
				{
					char b[256];
					sprintf_s(b, sizeof(b), "%s (%s)", c->GetAttr(OPT_MailStoreName), c->GetAttr(OPT_MailStoreLocation));
					d->Src->Insert(b);
					d->Dst->Insert(b);

					ReplicateDlg::AccountSpec &a = d->Paths.New();
					a.Uri.sProtocol = "file";
					a.Uri.sPath = c->GetAttr(OPT_MailStoreLocation);
				}

				d->App->GetOptions()->Unlock();
			}			
			
			// Scan for IMAP stores...
			for (auto a : *d->App->GetAccounts())
			{
				LVariant Proto = a->Receive.Protocol();
				if (Proto.Str() &&
					!_stricmp(Proto.Str(), PROTOCOL_IMAP4) &&
					!a->Receive.Disabled())
				{
					LVariant Host = a->Receive.Server();
					LVariant User = a->Receive.UserName();
					LVariant Port = a->Receive.Port();
					
					char b[256];
					int Ch = sprintf_s(b, sizeof(b), "imap://%s@%s", User.Str(), Host.Str());
					if (Port.CastInt32())
						sprintf_s(b+Ch, sizeof(b)-Ch, ":%i", Port.CastInt32());
					d->Src->Insert(b);
					d->Dst->Insert(b);

					ReplicateDlg::AccountSpec &as = d->Paths.New();

					as.SslFlags = MakeOpenFlags(a, false);

					as.Uri.Empty();

					GPassword Pass;
					if (a->Receive.GetPassword(&Pass))
					{
						char Ps[256] = "";
						Pass.Get(Ps);
						as.Uri.sPass = Ps;
					}

					as.Uri.sProtocol = "imap";
					as.Uri.sUser = User.Str();
					as.Uri.sHost = Host.Str();
				}
			}
		}

		if (GetViewById(IDC_TYPES, d->Lst))
		{
			d->Lst->Insert(new LType(MAGIC_MAIL, IDS_EMAIL));
			d->Lst->Insert(new LType(MAGIC_CONTACT, IDS_CONTACT));
			d->Lst->Insert(new LType(MAGIC_GROUP, IDC_GROUP));
			d->Lst->Insert(new LType(MAGIC_CALENDAR, IDS_CALENDAR));
			d->Lst->Insert(new LType(MAGIC_FILTER, IDS_FILTER));
			d->Lst->ResizeColumnsToContent();
		}

		OnFolderChange();
	}
}

ReplicateDlg::~ReplicateDlg()
{
	DeleteObj(d);
}

void ReplicateDlg::OnFolderChange()
{
	SetCtrlEnabled(IDOK, GetCtrlValue(IDC_SRC) != GetCtrlValue(IDC_DST));
}

int ReplicateDlg::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDC_SRC:
		case IDC_DST:
		{
			OnFolderChange();
			break;
		}
		case IDOK:
		{
			d->Settings.Reset(new ReplicateSettings);

			List<LType> Types;
			if (d->Lst->GetAll(Types))
			{
				for (auto t: Types)
				{
					if (t->Value())
					{
						d->Settings->Types.Add(t->Type);
					}
				}
			}

			ReplicateDlg::AccountSpec &a = d->Paths[(int32_t)d->Dst->Value()];
			d->Settings->Dst = a;
			
			a = d->Paths[(uint32_t)d->Src->Value()];
			d->Settings->Src = a;
			// Fall through to EndModal
		}
		case IDCANCEL:
		{
			EndModal(c->GetId() == IDOK);
			break;
		}
	}

	return 0;
}

bool ReplicateDlg::StartProcess()
{
	ScribeReplicator *Rep = new ScribeReplicator(d->App);
	if (!Rep)
		return false;

	return Rep->StartProcess(d->Settings);
}

Store3Status Store3ReplicateFolders(	ScribeWnd *App,
										LDataFolderI *Dst,
										LDataFolderI *Src,
										bool Recurse,
										bool DeleteSourceOnSuccess,
										LArray<uint32_t> *Types)
{
	if (!Dst || !Src)
		return Store3Error;

	ScribeReplicator *Rep = new ScribeReplicator(App);
	if (!Rep)
		return Store3Error;
	
	if (!Rep->StartProcess(Dst, Src, Recurse, DeleteSourceOnSuccess, Types))
		return Store3Error;

	return Store3Delayed;
}
