
#include "Lgi.h"
#include "LSegmentTree.h"
#include "ScribeDefs.h"
#include "Store2.h"
#include "resdefs.h"
#include "LTextLog.h"

#define VERSION						0.3

using namespace Storage2;

#define OLD_FIELD_FOLDER_TYPE		1000
#define OLD_FIELD_FOLDER_NAME		1001
#define OLD_FIELD_UNREAD			1002
#define OLD_FIELD_SORT				1003

///////////////////////////////////////////////////////////////////////////////
// Storage defs
const char *ItemTypeName(Store3ItemTypes t)
{
	switch (t)
	{
		case MAGIC_BASE: return "MAGIC_BASE";
		case MAGIC_MAIL: return "MAGIC_MAIL";
		case MAGIC_CONTACT: return "MAGIC_CONTACT";
		case MAGIC_FOLDER_OLD: return "MAGIC_FOLDER_OLD";
		case MAGIC_MAILBOX: return "MAGIC_MAILBOX";
		case MAGIC_ATTACHMENT: return "MAGIC_ATTACHMENT";
		case MAGIC_ANY: return "MAGIC_ANY";
		case MAGIC_FILTER: return "MAGIC_FILTER";
		case MAGIC_FOLDER: return "MAGIC_FOLDER";
		case MAGIC_CONDITION: return "MAGIC_CONDITION";
		case MAGIC_ACTION: return "MAGIC_ACTION";
		case MAGIC_CALENDAR: return "MAGIC_CALENDAR";
		case MAGIC_ATTENDEE: return "MAGIC_ATTENDEE";
		case MAGIC_GROUP: return "MAGIC_GROUP";
		default:
		{
			static char s[256];
			sprintf(s, "Unknown(0x%x)", t);
			return s;
		}
	}
	
	return 0;
}

struct DumpStats
{
	int Nodes;
	int Email;
	int Contact;
	int Folder;
};

int32 freadint32(LFile *f)
{
	int32 i;
	f->Read(&i, sizeof(i));
	return i;
}

int16 freadint16(LFile *f)
{
	int16 i;
	f->Read(&i, sizeof(i));
	return i;
}

bool DumpNodeData(LFile *f, int Pos, LStream *Log)
{
	bool Status = false;
	
	if (f && Pos >= sizeof(StorageHeader))
	{
		f->SetPos(Pos);
		
		StorageItemHeader Node;
		f->Read(&Node, sizeof(Node));
		if (Node.Magic == STORAGE2_ITEM_MAGIC)
		{
			if (f->SetPos(Node.DataLoc) == Node.DataLoc)
			{
				Log->Print("Dumping node (at %i) data (at %i):\n", Pos, Node.DataLoc);
				int Magic = freadint32(f);
				if (Magic == Node.Type)
				{
					int Fields = freadint32(f);
					Log->Print("%i fields:\n\n", Fields);
					for (int i=0; i<Fields; i++)
					{
						short Id = freadint16(f);
						int Size = freadint32(f);
						Log->Print("Field: Id=%i Size=%i DataOffset=%i\n", Id, Size, (uint32)f->GetPos());
						f->Seek(Size, SEEK_CUR);						
					}
					
					Log->Print("\n");
				}
				else
				{
					Log->Print("Data for node (at %i) had wrong magic.\n", Pos);
				}
			}
			else
			{
				Log->Print("%s,%i - fseek failed.\n", __FILE__, __LINE__);
			}			
		}
		else
		{
			Log->Print("Error: No node at %i\n", Pos);
		}
	}
	
	return Status;	
}

bool DumpTreeNode(LFile *f, int Pos, DumpStats *Stats, LStream *Log)
{
	bool Status = false;
	
	if (f && Pos >= sizeof(StorageHeader))
	{
		f->SetPos(Pos);
		
		StorageItemHeader Node;
		f->Read(&Node, sizeof(Node));
		if (Node.Magic != STORAGE2_ITEM_MAGIC)
		{
			Log->Print("Error: node magic number wrong (at %i)\n", Pos);
		}
		else
		{
			Stats->Nodes++;
			Status = true;
			
			switch (Node.Type)
			{
				case MAGIC_MAIL:
					Stats->Email++;
					break;
				case MAGIC_FOLDER:
				case MAGIC_FOLDER_OLD:
					Stats->Folder++;
					break;
				case MAGIC_CONTACT:
					Stats->Contact++;
					break;
			}
			
			Log->Print("Node at %i: Type=%X(%s) Data=%i Dir=%i\n", Pos, Node.Type, ItemTypeName((Store3ItemTypes)Node.Type), Node.DataSize, Node.DirCount);
			
			if (Node.DirCount > 0)
			{
				for (int i=0; i<Node.DirCount && Status; i++)
				{
					Status &= DumpTreeNode(f, Node.DirLoc + (i * sizeof(StorageItemHeader)), Stats, Log);
				}
			}
		}
	}
	
	return Status;
}

void DumpTree(LFile *f, LStream *Log)
{
	StorageHeader Header;
	f->Read(&Header, sizeof(Header));
	if (Header.Magic != STORAGE2_MAGIC)
	{
		Log->Print("Error: storage header magic number wrong.\n");
	}
	else
	{
		DumpStats Stats;
		memset(&Stats, 0, sizeof(Stats));
		DumpTreeNode(f, 64, &Stats, Log);
		
		Log->Print(	"\nSummary:\n"
					"\tEmails: %i\n"
					"\tContacts: %i\n"
					"\tFolders: %i\n"
					"\tTotal: %i\n"
					"\n",
					Stats.Email,
					Stats.Contact,
					Stats.Folder,
					Stats.Nodes);
	}
}

class GFolderInfo
{
public:
	uint32 Type;
	char *Name;
};

class Node
{
public:
	uint32 Loc;
	uint32 NewLoc;
	StorageItemHeader Header;
	GFolderInfo *Fld;
	Node *Owner;
	LArray<Node*> Children;

	int DataLen;
	char *Data;
	
	Node(int l, StorageItemHeader *h)
	{
		Loc = l;
		Header = *h;
		Fld = 0;
		Owner = 0;
		NewLoc = 0;
		DataLen = 0;
		Data = 0;
	}

	Node(const char *Name, int Type)
	{
		Loc = 0;
		Data = 0;
		DataLen = 0;
		NewLoc = 0;
		memset(&Header, 0, sizeof(Header));
		Owner = 0;
		Header.Type = MAGIC_FOLDER;

		Fld = new GFolderInfo;
		if (Fld)
		{
			Fld->Type = Type;
			Fld->Name = NewStr(Name);
			if (Fld->Name)
			{

				int NameLen = strlen(Fld->Name);
				DataLen =	sizeof(uint32) + // magic
							sizeof(uint32) + // # of items
							sizeof(uint16) + // ItemType
								sizeof(uint32) +
								sizeof(uint32) +
							sizeof(uint16) + // Name
								sizeof(uint32) +
								NameLen;
				Data = new char[DataLen];
				if (Data)
				{
					uint32 *p = (uint32*)Data;
					*p++ = MAGIC_FOLDER;
					*p++ = 2;
					
					// Item type
					*((uint16*&)p)++ = FIELD_FOLDER_TYPE;
					*p++ = sizeof(Fld->Type);
					*p++ = Fld->Type;

					// Name
					*((uint16*&)p)++ = FIELD_FOLDER_NAME;
					*p++ = NameLen;
					memcpy(p, Fld->Name, NameLen);
				}
			}
		}
	}
	
	~Node()
	{
		DeleteObj(Fld);
		DeleteArray(Data);
	}

	bool Overlap(Node *Seg)
	{
		if (Header.DataLoc + Header.DataSize <= Seg->Header.DataLoc
			||
			Seg->Header.DataLoc >= Header.DataLoc + Header.DataSize)
		{
			return false;
		}

		return true;
	}
	
	int Type()
	{
		return Header.Type;
	}
	
	bool IsFolder()
	{
		return
		(
			Header.Type == MAGIC_FOLDER
			||
			Header.Type == MAGIC_FOLDER_OLD
		);
	}
	
	void SetOwner(Node *n)
	{
		if (Type() &&
			n->Type())
		{
			if (IsFolder())
			{
				assert(n->IsFolder());
			}
			if (Owner)
			{
				assert(0);
			}
			
			Owner = n;
			if (Owner)
			{
				Owner->Children.Add(this);
			}
		}
	}
	
	const char *TypeName()
	{
		return ItemTypeName((Store3ItemTypes) Type());
	}
	
	bool Read(LFile *f, uint16 &i)
	{
		return f->Read(&i, 2) == 2;
	}
	
	bool Read(LFile *f, uint32 &i)
	{
		return f->Read(&i, 4) == 4;
	}

	bool Read(LFile *f, char *&s)
	{
		uint32 Size;
		if (Read(f, Size))
		{
			s = new char[Size+1];
			if (s)
			{
				if (f->Read(s, Size) == Size)
				{
					s[Size] = 0;
					return true;
				}
				DeleteArray(s);
			}
		}
		return false;
	}
	
	bool ReadFolder(LFile *f)
	{
		bool Status = false;
	
		if (Header.DataLoc &&
			f->SetPos(Header.DataLoc) == Header.DataLoc)
		{
			uint32 Magic;
			if (Read(f, Magic))
			{
				if (Magic == MAGIC_FOLDER_OLD)
				{
					if (!Fld)
						Fld = new GFolderInfo;
	
					if (Fld)
					{
						Read(f, Fld->Type);
						uint32 u;
						Read(f, u);
						Read(f, Fld->Name);
						
						Status = ValidStr(Fld->Name);
					}
				}
				else if (Magic == MAGIC_FOLDER)
				{
					uint32 Count;
					if (Read(f, Count))
					{
						int i = 0;
						while
						(
							f->GetPos() < Header.DataLoc + Header.DataSize
							&&
							i < Count
						)
						{
							uint16 Id;
							
							if (Read(f, Id))
							{
								switch (Id)
								{
									case OLD_FIELD_FOLDER_TYPE:
									case FIELD_FOLDER_TYPE:
									{
										uint32 Size;
										if (Read(f, Size))
										{
											assert(Size == 4);
											if (!Fld)
												Fld = new GFolderInfo;
											if (Fld)
											{
												Read(f, Fld->Type);
											}										
										}
										break;
									}
									case OLD_FIELD_FOLDER_NAME:
									case FIELD_FOLDER_NAME:
									{
										char *s = 0;
										if (Read(f, s))
										{
											if (!Fld)
												Fld = new GFolderInfo;
											if (Fld)
											{
												Fld->Name = s;
												Status = true;
											}
										}
										break;
									}
									default:
									{
										uint32 Size;
										if (Read(f, Size))
										{
											f->Seek(Size, SEEK_CUR);
										}
										break;
									}
								}
							}
							 
							i++;
						}
					}
				}
			}
		}
		
		/*
		if (Fld && stricmp(Fld->Name, "Spam") == 0)
		{
			Spam = true;
		}
		*/
		
		return Status;
	}
};

int NodeStartCmp(Node **a, Node **b)
{
	int64 Diff = (int64)(*a)->Header.DataLoc - (int64)(*b)->Header.DataLoc;
	if (Diff < 0)
		return -1;
	return Diff > 0 ? 1 : 0;
}

int FindNode(int Loc, LArray<Node*> &n)
{
	int Low = 0, High = n.Length()-1;
	while (true)
	{
		if (n[Low]->Loc == Loc)
		{
			return Low;
		}
		if (n[High]->Loc == Loc)
		{
			return High;
		}

		int i = (High + Low) >> 1;
		assert(i != Low);
		assert(i != High);
		
		Node *Cur = n[i];

		if (Cur->Loc == Loc)
		{
			return i;
		}
		else if (Cur->Loc > Loc)
		{
			// Choose lower half
			High = i;
		}
		else
		{
			// Choose upper half
			Low = i;
		}

		if (High - Low < 2)
		{
			return -1;
		}
	}
}

void PrintTree(Node *n, LStream *Log, int Depth = 0)
{
	if (n && n->IsFolder())
	{
		int i;
		for (i=0; i<Depth << 2; i++) Log->Print(" ");
		Log->Print("[-] %s\n", n->Fld ? n->Fld->Name : 0);

		for (i=0; i<n->Children.Length(); i++)
		{
			Node *c = n->Children[i];
			if (c->IsFolder())
			{
				PrintTree(c, Log, Depth+1);
			}
		}
	}
}

void ExportData(LFile *Out, LFile *In, Node *n, StorageItemHeader *h, LStream *Log)
{
	if (Out && In && n && h)
	{
		uint32 Pos = Out->GetPos();
		if (Pos == h->DataLoc)
		{
			if (n->Data)
			{
				Out->Write(n->Data, n->DataLen);
			}
			else
			{
				// Log->Print("Data write starting at %i for %i bytes ", ftell(Out), n->Header.DataSize);
				In->Seek(n->Header.DataLoc, SEEK_SET);

				char Buf[1024];
				for (int i=0; i<n->Header.DataSize;)
				{
					int Len = min(n->Header.DataSize - i, sizeof(Buf));
					int r = In->Read(Buf, Len);
					if (r == Len)
					{
						int w = Out->Write(Buf, r);
						if (w == r)
						{
							i += w;
						}
						else
						{
							assert(0);
						}
					}
					else
					{
						assert(0);
						break;
					}
				}

				// Log->Print("finishing at %i\n", ftell(Out));
			}
		}
		else
		{
			Log->Print("Export data error: Pos=%i != DataLoc=%i\n", Pos, h->DataLoc);
			assert(0);
		}
	}
}

void ExportNode(LFile *Out, LFile *In, Node *n, StorageItemHeader *h, LStream *Log)
{
	if (Out && In && n)
	{
		h->Magic = STORAGE2_ITEM_MAGIC;
		h->Type = n->Type();
		if (!h->Type)
			return;
		
		assert(h->Type);

		// Write directory...
		StorageItemHeader *Dir = n->Children.Length() ? new StorageItemHeader[n->Children.Length()] : 0;
		if (Dir)
		{
			// Write a blank directory
			h->DirLoc = Out->GetPos();
			h->DirCount = h->DirAlloc = n->Children.Length();
			
			int DirSize = h->DirCount * sizeof(StorageItemHeader);

			memset(Dir, 0, DirSize);
			Out->Write(Dir, DirSize);
			
			// Write children
			for (int i=0; i<n->Children.Length(); i++)
			{
				n->Children[i]->NewLoc = h->DirLoc + (i * sizeof(StorageItemHeader));
				Dir[i].ParentLoc = n->NewLoc;
				ExportNode(Out, In, n->Children[i], Dir + i, Log);
			}

			// Write the directory
			Out->Seek(h->DirLoc, SEEK_SET);
			// Log->Print("Dir write start at %i for %i bytes ", ftell(Out), DirSize);
			Out->Write(Dir, DirSize);
			// Log->Print("ending at %i\n", ftell(Out));
			Out->Seek(0, SEEK_END);
		}

		if (n->Header.DataLoc || n->Data != 0)
		{
			h->DataLoc = Out->GetPos();
			h->DataSize = n->Data ? n->DataLen : n->Header.DataSize;
			
			// Write data...
			ExportData(Out, In, n, h, Log);
		}

		DeleteArray(Dir);
	}
}

/*
class GNodeSeg : public LSegment
{
public:
	Node *n;

	GNodeSeg(Node *node)
	{
		n = node;
		Start = n->Header.DataLoc;
		Length = n->Header.DataSize;
	}
};
*/

class Worker : public LThread
{
	LAutoString FileName;
	LStream *Log;
	LProgressView *Prog;
	bool Loop;

public:
	Worker(char *File, LStream *log, LProgressView *prog) : LThread("Worker")
	{
		FileName.Reset(NewStr(File));
		Log = log;
		Prog = prog;
		Loop = true;
		Run();
	}
	
	void Scan(LFile *f, LArray<Node*> &Nodes)
	{
		// Skip header...
		uint32 Offset = sizeof(StorageHeader);
		uint32 FileSize = f->GetSize();
		f->SetPos(Offset);
		
		int Len = 2 << 20;
		char *Buf = new char[Len];
		if (Buf)
		{
			uint32 Used = 0;
			
			Prog->SetLimits(0, FileSize);
			
			while (Loop && !f->Eof())
			{
				Offset = f->GetPos() - Used;
				int r = f->Read(Buf + Used, Len - Used);
				Used += r;
				
				Prog->Value(f->GetPos());
				
				if (Used > 0)
				{
					int i;
					for (i=0; i<Used - sizeof(StorageItemHeader); i++)
					{
						if ( ((uint32*)(Buf+i))[0] == STORAGE2_ITEM_MAGIC)
						{
							// Possible match
							StorageItemHeader *h = (StorageItemHeader*) (Buf + i);
							const char *Type;
							if ((Type = ItemTypeName((Store3ItemTypes) h->Type)) != 0 &&
								(h->DataLoc != 0 || h->DirLoc != 0))
							{
								// Type is valid
								int ObjectLoc = Offset + i;

								if
								(
									h->DataLoc == 0
									||
									(
										h->DataLoc >= 64 &&
										h->DataLoc < FileSize &&
										h->DataSize >= 0 &&
										h->DataSize < 40 << 20
									)
								)
								{
									if
									(
										h->DirLoc == 0
										||
										(
											h->DirLoc >= 64 &&
											h->DirLoc < FileSize &&
											h->DirCount >= 0 &&
											h->DirCount < 15000 &&
											h->DirAlloc >= 0 &&
											h->DirAlloc < 15000
										)
									)
									{
										// Valid node
										Node *n = new Node(ObjectLoc, h);
										if (n)
										{
											Nodes.Add(n);
										}
										else LAssert(0);
									}
									else
									{
										Log->Print("\t%s @ %i\n", Type, ObjectLoc);
										Log->Print("\tInvalid Directory Loc/Count/Alloc @ Loc %i: %i,%i,%i\n",
											ObjectLoc, h->DirLoc, h->DirCount, h->DirAlloc);
									}
								}
								else
								{
									Log->Print("\t%s @ %i\n", Type, ObjectLoc);
									Log->Print("\tInvalid DataSeg Loc/Size @ Loc %i: %i,%i\n", ObjectLoc, h->DataLoc, h->DataSize);
								}
							}
						}
					}
					
					if (i < Used)
					{
						memmove(Buf, Buf + i, Used - i);
						Used = Used - i;
					}

				}
				else break;
				
			}
		
			DeleteArray(Buf);
		}
	}

	void Export(LFile *f, LArray<Node*> &Nodes)
	{
		// Build list of folders first...
		LArray<Node*> Folders;
		Node OrphanEmail("Orphan Email", MAGIC_MAIL);
		Node OrphanContacts("Orphan Contacts", MAGIC_CONTACT);
		Node OrphanCalendars("Orphan Calendar", MAGIC_CALENDAR);
		
		Log->Print("\tScanning for folders...\n");
		int i;

		Prog->SetLimits(0, Nodes.Length());
		for (i=0; Loop && i<Nodes.Length(); i++)
		{
			Node *n = Nodes[i];
			if (n->IsFolder())
			{
				Folders[Folders.Length()] = n;
				if (n->ReadFolder(f))
				{
					Log->Print("\t\t%i: Folder '%s' with %i children (%s containing %s)\n",
						n->Loc,
						n->Fld->Name,
						n->Header.DirCount,
						ItemTypeName((Store3ItemTypes) n->Type()),
						ItemTypeName((Store3ItemTypes) n->Fld->Type));
				}
				else
				{
					Log->Print("\t\t%i: Failed folder load. (%s)\n", n->Loc, ItemTypeName((Store3ItemTypes) n->Type()));
					n->Header.Type = 0;
				}
			}
			
			if (i % 10 == 0)
				Prog->Value(i);
		}
		Log->Print("\t%i folders found.\n", Folders.Length());

		// Check that node list is sorted
		bool Sorted = true;
		int CurLoc = 0;
		for (i=0; i<Nodes.Length(); i++)
		{
			int Here = Nodes[i]->Loc;
			if (Here > CurLoc)
			{
				CurLoc = Here;
			}
			else
			{
				Sorted = false;
				break;
			}
		}
		Log->Print("\tNodes sorted: %i\n", Sorted);
		if (!Sorted)
		{
			return;
		}

		// Fix up as many parent pointers as we can
		Log->Print("\tCorrecting tree errors... (%i nodes)\n", Nodes.Length());
		int ParentPtrCorrect = 0;
		int DirsRemoved = 0;
		int NodesChecked = 0;

		Prog->SetLimits(0, Nodes.Length());
		for (i=0; Loop && i<Nodes.Length(); i++)
		{
			if (i % 10 == 0)
				Prog->Value(i);

			Node *n = Nodes[i];
			if (n->Loc > 0 && n->Header.DirLoc)
			{
				int Pos = n->Header.DirLoc;
				int Dir = FindNode(Pos, Nodes);
				if (Dir >= 0)
				{
					int j;
					for (j=0; j<n->Header.DirCount; j++)
					{
						Node *c = (Dir + j) < Nodes.Length() ? Nodes[Dir + j] : 0;
						if (c)
						{
							if (Pos != c->Loc)
							{
								break;
							}

							if (n->Loc != c->Header.ParentLoc)
							{
								c->Header.ParentLoc = n->Loc;
								ParentPtrCorrect++;
							}
							
							NodesChecked++;
						}

						Pos += sizeof(StorageItemHeader);
					}
					
					if (j < n->Header.DirCount)
					{
						Log->Print("\tError didn't scan all of %s\n", n->TypeName());
					}
				}
				else
				{
					// Dir doesn't exist
					n->Header.DirLoc = 0;
					n->Header.DirCount = 0;
					n->Header.DirAlloc = 0;
					DirsRemoved++;
				}
			}
		}
		Log->Print("\t\tNodes Checked: %i, Parent ptrs fixed: %i, Missing directories removed: %i\n",
			NodesChecked, ParentPtrCorrect, DirsRemoved);

		// Assign all non-folder nodes to a folder
		Log->Print("\tAssigning nodes to folders...\n");
		int Orphans = 0;
		int Owned = 0;
		int Unassigned = 0;
		
		Prog->SetLimits(0, Nodes.Length());
		for (i=0; Loop && i<Nodes.Length(); i++)
		{
			if (i % 10 == 0)
				Prog->Value(i);

			Node *n = Nodes[i];
			if (n &&
				n != Folders[0] &&
				n->Type())
			{
				if (n->Type() == MAGIC_ATTACHMENT)
				{
					int Index = FindNode(n->Header.ParentLoc, Nodes);
					if (Index > 0)
					{
						n->SetOwner(Nodes[Index]);
					}
				}
				else
				{
					int Index = FindNode(n->Header.ParentLoc, Folders);

					if (n->IsFolder())
					{
					}
					
					if (Index >= 0)
					{						
						n->SetOwner(Folders[Index]);
					}
				}
				
				if (n->Owner)
				{
					// Log->Print("\t\t%s @ %i Ok\n", ItemTypeName((Store3ItemTypes) n->Header.Type), n->Loc);
					Owned++;
				}
				else if (n->Header.DataLoc)
				{
					// Log->Print("\t\t%s @ %i doesn't have a owner (Parent=%i)\n",
					//	ItemTypeName((Store3ItemTypes) n->Header.Type), n->Loc, n->Header.ParentLoc);
					
					int Magic = 0;
					f->Seek(n->Header.DataLoc, SEEK_SET);
					f->Read(&Magic, sizeof(Magic));
					if (ItemTypeName((Store3ItemTypes) Magic))
					{
						switch (n->Type())
						{
							case MAGIC_FOLDER:
							case MAGIC_FOLDER_OLD:
							{
								n->SetOwner(Folders[0]);
								break;
							}
							case MAGIC_MAIL:
							{
								n->SetOwner(&OrphanEmail);
								break;
							}
							case MAGIC_CONTACT:
							{
								n->SetOwner(&OrphanContacts);
								break;
							}
							case MAGIC_CALENDAR:
							{
								n->SetOwner(&OrphanCalendars);
								break;
							}
							default:
							{
								/*
								Log->Print("\t\tOrphaned %s @ %i not assigned (parent=%i).\n",
									ItemTypeName((ScribeItemTypes) n->Type()), n->Loc, n->Header.ParentLoc);
								*/
								
								Unassigned++;
								break;
							}
						}
					}
					else
					{
						Unassigned++;
					}

					Orphans++;
				}
			}
		}
		Log->Print("\t\tOwned: %i, Orphans: %i of which %i are unassigned.\n", Owned, Orphans, Unassigned);

		// Cull duplicates from folders
		Log->Print("\tCulling duplicate nodes from folders...\n");
		int CulledNodes = 0;

		Prog->SetLimits(0, Folders.Length());
		for (i=0; Loop && i<Folders.Length(); i++)
		{
			if (i % 1000 == 0)
				Prog->Value(i);

			Node *Folder = Nodes[i];
			if (Folder->IsFolder())
			{
				// Make a tree of all the nodes, discarding nodes that point to the same
				// data
				Folder->Children.Sort(NodeStartCmp);
				
				// Check for overlaps
				Node **n = &Folder->Children[0];
				for (int s=0; s<Folder->Children.Length()-1; s++)
				{
					if (n[s]->Header.DataLoc + n[s]->Header.DataSize
						>
						n[s+1]->Header.DataLoc)
					{
						n[s] = NULL;
					}
				}
				
				// Now compact the array
				int Out = 0;
				for (int In = 0; In < Folder->Children.Length(); In++)
				{
					if (n[In])
						n[Out++] = n[In];
				}
				CulledNodes += Folder->Children.Length() - Out;
				Folder->Children.Length(Out);
			}
		}
		Log->Print("\t\tCulled %i nodes.\n", CulledNodes);
		Prog->Value(0);
		
		// Export nodes to new folder file
		LDumpMemoryStats("memory.mem");

		Log->Print("\tWriting export folders...\n");
		LFile Export;
		if (Loop && Export.Open("export.mail2", O_WRITE))
		{
			Export.SetSize(0);
			Export.SetPos(0);

			// Write the header.
			StorageHeader Header;
			memset(&Header, 0, sizeof(Header));
			Header.Magic = STORAGE2_MAGIC;
			if (Export.Write(&Header, sizeof(Header)))
			{
				// Write out the nodes...				
				Node *Mailbox = Folders[0];
				if (OrphanEmail.Children.Length())
				{
					OrphanEmail.SetOwner(Mailbox);
				}
				if (OrphanContacts.Children.Length())
				{
					OrphanContacts.SetOwner(Mailbox);
				}
				if (OrphanCalendars.Children.Length())
				{
					OrphanCalendars.SetOwner(Mailbox);
				}

				Mailbox->NewLoc = Export.GetPos();

				StorageItemHeader Root;
				memset(&Root, 0, sizeof(Root));
				Root.Magic = STORAGE2_ITEM_MAGIC;
				Root.Type = MAGIC_FOLDER;
				Root.DirCount = Root.DirAlloc = Mailbox->Children.Length();
				Root.ParentLoc = 0;
				Export.Write(&Root, sizeof(Root));

				ExportNode(&Export, f, Folders[0], &Root, Log);
				
				Export.Seek(Mailbox->NewLoc, SEEK_SET);
				Export.Write(&Root, sizeof(Root));
			}
		}
	}

	int Main()
	{
        LFile f;
        if (!f.Open(FileName, O_READ))
        {
			Log->Print("Error: Failed to open '%s'.\n", FileName.Get());
			return -1;
        }

        LArray<Node*> Nodes;
        Log->Print("Starting scan...\n");
        Scan(&f, Nodes);
        Log->Print("Scan complete: %i possible nodes found...\n", Nodes.Length());
		
        Log->Print("Starting export of all nodes...\n");
        Export(&f, Nodes);
        Log->Print("Export finished.\n");
        
        return 0;
	}
};

class App : public LWindow
{
    LAutoString FileName;
    LTextLog *Log;
    LAutoPtr<LThread> Thread;
    LProgressView *Prog;

public:
	App()
	{
		LRect r(0, 0, 1200, 800);
		SetPos(r);
		MoveToCenter();
		Name("Scribe Folder Dump");
		SetQuitOnClose(true);
		
		#ifdef _DEBUG
		FileName.Reset(NewStr("H:\\Mail\\Test\\Folders.mail2"));
		#endif
		
		if (Attach(0))
		{
			Prog = new LProgressView(101, 0, 0, 200, 20, 0);
			Prog->Attach(this);
			
			Log = new LTextLog(100);
			Log->Sunken(false);
			Log->Attach(this);
			Log->Print("Scribe Folder Dumper v%.2f\n", VERSION);
			
			if (Menu = new GMenu)
			{
				Menu->Attach(this);
				Menu->Load(this, "IDM_MENU");
			}
			Visible(true);
		}
	}
	
	~App()
	{
		int asd=0;
	}
	
	int OnCommand(int Cmd, int Event, OsView Wnd)
	{
		switch (Cmd)
		{
		    case IDM_OPEN:
		    {
		        LFileSelect s;
		        s.Parent(this);
		        s.Type("Mail2 Folders", "*.mail2");
		        if (s.Open())
		        {
		            FileName.Reset(NewStr(s.Name()));
		            Log->Print("Loaded '%s'\n", FileName.Get());
		        }
		        break;
		    }
		    case IDM_CLOSE:
		    {
		        FileName.Reset();
		        break;
		    }
		    case IDM_DUMP:
		    {
		        if (FileName)
		        {
		            LFile f;
		            if (f.Open(FileName, O_READ))
		            {
   				        DumpTree(&f, Log);
   				    }
	   				else Log->Print("Error: Failed to open '%s'.\n", FileName.Get());
   				}
   				else Log->Print("Error: No file loaded.\n");
   			    break;
   			}
   			case IDM_RECOVER:
   			{
		        if (!FileName)
		        {
	   				Log->Print("Error: No file loaded.\n");
	   				break;
	   			}

	            Thread.Reset(new Worker(FileName, Log, Prog));
   			    break;
   			}
			case IDM_EXIT:
			{
				LCloseApp();
				break;
			}
		}
		
		return LWindow::OnCommand(Cmd, Event, Wnd);
	}
	
	void OnReceiveFiles(LArray<char*> &Files)
	{
		if (Files.Length() > 0)
		{
            FileName.Reset(NewStr(Files[0]));
            Log->Print("Loaded '%s'\n", FileName.Get());
		}
	}
};

int LgiMain(OsAppArguments &AppArgs)
{
	#ifdef LINUX
	setvbuf(stdout,(char *)NULL,_IONBF,0); // print mesgs immediately.
	#endif
	
	assert(sizeof(StorageHeader) == 64);
	assert(sizeof(StorageItemHeader) == 32);

	LApp a(AppArgs, "ScribeFolderDump");
	if (a.IsOk())
	{
		a.AppWnd = new App();
		a.Run();
	}

	return 0;
}
