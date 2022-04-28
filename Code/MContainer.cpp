#include "Scribe.h"
#include "ScribePrivate.h"
#include "lgi/common/LgiQuickSort.h"
#include "lgi/common/DisplayString.h"

MContainer::MContainer(const char *Id, Mail *m)
{
    #ifdef _DEBUG
    MsgId.Reset(NewStr(Id));
    #endif
	Index = -1;
	Message = 0;
	Parent = 0;
	Open = true;
	Lines = 0;
	Next = false;
	Depth = 0;

	SetMail(m);
}

MContainer::~MContainer()
{
	if (Message)
	{
		Message->Container = 0;
	}

	while (Children.Length())
	{
	    MContainer *c = Children[0];
		LAssert(LgiCanReadMemory(c, 1));
		c->Parent = 0;
		Children.DeleteAt(0);
	}

	if (Parent)
	{
		Parent->Children.Delete(this, true);
	}

	RefCache.DeleteArrays();
}

Mail *MContainer::GetTop()
{
	MContainer *t;
	for (t=this; t->Parent; t=t->Parent)
	    ;
	
	if (t->Message)
	{
		return t->Message;
	}
	
	if (t->Children.Length())
	{
		return t->Children[0]->Message;
	}

	return Message;
}

bool MContainer::HasChild(MContainer *m)
{
	for (unsigned i=0; i<Children.Length(); i++)
	{
	    MContainer *c = Children[i];
		if (this == c)
		{
			Children.DeleteAt(i--);
		}
		else
		{
			if (c == m || c->HasChild(m))
				return true;
		}
	}

	return false;
}

void MContainer::RemoveChild(MContainer *m)
{
	if (Children.Delete(m))
	{
		m->Parent = 0;
	}
}

void MContainer::AddChild(MContainer *m)
{
	if (m)
	{
		Children.Add(m);
		m->Parent = this;
	}
}

int MContainer::CountMessages()
{
    int c = Message ? 1 : 0;
    for (unsigned i=0; i<Children.Length(); i++)
    {
        c += Children[i]->CountMessages();
    }
    return c;
}

void MContainer::SetMail(Mail *m)
{
	RefCache.DeleteArrays();

	if ((Message = m))
	{
		Message->GetReferences(RefCache);
		m->Container = this;
	}
}

void MContainer::OnPaint(LSurface *pDC, LRect &r, LItemColumn *c, LColour Fore, LColour Back, LFont *Font, const char *Txt)
{
	pDC->Colour(Back);
	pDC->Rectangle(&r);

	pDC->Colour(L_MED);
	if (Lines)
	{
		int x = 0;
		for (int i=Lines; i; i>>=1, x += 16)
		{
			if (i & 1)
			{
				int X = r.x1 + x + 7;
				pDC->Line(X, r.y1, X, r.y2);
			}
		}
	}

	int x = r.x1 + (Depth * 16);
	pDC->Line(x + 7, r.y1, x + 7, Next ? r.y2 : r.y1 + 7);
	pDC->Line(x + 7, r.y1 + 7, x + 14, r.y1 + 7);

	if (Txt)
	{
		int x = r.x1 + ((Depth + 1) * 16);
		Font->Colour(Fore, Back);
		Font->Transparent(true);
		LDisplayString ds(Font, Txt);
		ds.Draw(pDC, x, r.y1+1);
	}
}

int ContainerCompare(MContainer **a, MContainer **b)
{
	if ((*a)->Message && (*b)->Message)
	{
	    NativeInt ni = (NativeInt) (*a)->Message->GetFolder();
		return ThingCompare((*a)->Message, (*b)->Message, ni);
	}

	#ifdef _DEBUG
	LgiMsg(0, "ContainerCompare error.", "MContainer");
	#endif
	return 0;
}

int ContainerSorter(MContainer *&a, MContainer *&b, ThingSortParams *Params)
{
	if (a->Message && b->Message)
	{
	    return ThingSorter(a->Message, b->Message, Params);
	}

	return 0;
}

int ContainerIndexer(Thing *a, Thing *b, NativeInt Data)
{
	Mail *Ma = a->IsMail();
	Mail *Mb = b->IsMail();
	if (Ma && Mb && Ma->Container && Mb->Container)
	{
		return Ma->Container->Index - Mb->Container->Index;
	}

	return 0;
}

void MContainer::Pour(int &index, int depth, int tree, bool next, ThingSortParams *params)
{
	Index = index++;
	Lines = tree;
	Depth = depth;
	Next = next;

    #if 0
	char *Str1 = Message ? Message->GetFieldText(params->SortField) : NULL;
	char msg[256];
	int ch = depth*4;
	memset(msg, ' ', ch);
	msg[ch] = 0;
	LgiTrace("%s[%i] = %p,%s (has_next=%i)\n", msg, Index, Message, Str1, next);	
	
	LAssert(!Message || Message->Container == this);
	#endif

	// Children.Sort(ContainerCompare);
	if (Children.Length() > 1)
	    LgiQuickSort(&Children[0], Children.Length(), ContainerSorter, params);

	int Flag = (Open && Next) ? 1 << Depth : 0;
	ssize_t Last = Children.Length() - 1;
	for (int i=0; i<(int)Children.Length(); i++)
	{
		Children[i]->Pour(index, Depth+1, tree | Flag, i < Last, params);
	}

	// Implement ignore flag
	if (Message && Parent && Parent->Message)
	{
		if (!TestFlag(Message->GetFlags(), MAIL_READ) &&
			TestFlag(Parent->Message->GetFlags(), MAIL_IGNORE))
		{
			Message->SetFlags(Message->GetFlags() | MAIL_READ | MAIL_IGNORE);
		}
	}
}

void MContainer::Prune(int &ParentIndex, LArray<MContainer*> &L)
{
	for (int i=0; i<(int)L.Length(); i++)
	{
    	MContainer *c = L[i];
	    
	    if (c->Message)
		{
		    // This node has a message, prune children
		    Prune(i, c->Children);
		}
		else if (c->Children.Length()) // No message at this node, we have to deal with it.
		{
		    // There are children, so promote them up to this level
		    Prune(i, c->Children);
				
			// 4 B
			if (c->Parent || c->Children.Length() == 1)
			{
				// Promote children
				for (unsigned k=0; k<c->Children.Length(); k++)
				{
    				MContainer *sub = c->Children[k];

					// and add it to the parent
					sub->Parent = c->Parent;
					
					if (k)
					    L.AddAt(++i, sub);
    				else
					    L[i] = sub;
				}

			    c->Children.Length(0);
			    DeleteObj(c);
			}
		}
		else
		{
		    // No children, so nuke this node all together.
			if (c->Parent)
				c->Parent->RemoveChild(c);

			DeleteObj(c);
			L.DeleteAt(i--, true);
		}
	}
}

void MContainer::Thread(List<Mail> &In, LArray<MContainer*> &Root)
{
	// Taken from http://www.jwz.org/doc/threading.html
	LHashTbl<ConstStrKey<char>, MContainer*> IdTable((int)In.Length() << 2);
	
	// This holds a mapping between the Outlook 'Thread-Index' values and the respective Message-Id's.
	LHashTbl<StrKey<char>, char*> ThreadToMsgID;

	// Stage 1: For each message
	for (auto m: In)
	{
		auto MsgId = m->GetMessageId(true);
		if (!MsgId)
		{
		    // LAssert(!"No message ID for this email.");
		    continue;
		}

        // If the message-id still has '<>' characters around it, then trim them off.		
		LAutoString Mem;
		if (strchr(MsgId, '<') &&
		    Mem.Reset(TrimStr(MsgId, "<>")))
		{
		    MsgId = Mem;
		}

		// Add message to thread-index map
		LAutoString Thread = m->GetThreadIndex();
        if (Thread)
        {
			if (!ThreadToMsgID.Find(Thread))
				ThreadToMsgID.Add(Thread, NewStr(MsgId));
        }

		// 1 A
		MContainer *c = IdTable.Find(MsgId);
		if (c)
		{
			if (!c->Message)
			{
			    // This happens when a reference to the email is seen before the actual email
				c->SetMail(m);
			}
			else
			{
				// Huh? Duplicate msg-id
				// LAssert(!"Dupe msg");
				continue;
			}
		}
		else
		{
			IdTable.Add(MsgId, c = new MContainer(MsgId, m));
		}

		// 1 B
		if (!c) continue;
		MContainer *p = NULL, *r = NULL;
	    for (auto Ref: c->RefCache)
	    {
		    r = IdTable.Find(Ref);

            // Do outlook thread-index handling
	        if (!r && !strchr(Ref, '@'))
	        {
	            char *Id = ThreadToMsgID.Find(Ref);
	            if (Id)
	                r = IdTable.Find(Id);
	        }

		    // No container yet?
		    if (!r)
		    {
		        // Create an empty container for the reference that we haven't seen yet.
			    IdTable.Add(Ref, r = new MContainer(Ref));
		    }
			
		    if (r && p)
		    {
			    // Check for loop..
			    if (!p->HasChild(r) &&
			        !r->HasChild(p) &&
			        r->Parent == NULL)
			    {
			        p->AddChild(r);
			    }
		    }
		    
		    p = r;
	    }

		// 1 C
		if (c->Parent)
		{
			c->Parent->RemoveChild(c);
		}

		if (r &&
		    !r->HasChild(c) &&
		    !c->HasChild(r))
		{
			r->AddChild(c);
		}
	}

	// Stage 2: Find the root set
	// MContainer *c;
	// for (c = IdTable.First(); c; c = IdTable.Next())
	for (auto c : IdTable)
	{
		if (!c.value->Parent)
			Root.Add(c.value);
	}

	// Stage 3: Discard memory
	ThreadToMsgID.DeleteArrays();

	// Stage 4: Prune empty containers
	int Idx = 0;
	Prune(Idx, Root);

    #if 0
	for (int i=0; i<Root.Length(); i++)
	    Root[i]->RemoveFromTable(IdTable);
	#elif 0
	LFile Out;
	if (Out.Open("c:\\temp\\thread.txt", O_WRITE))
	{
		for (unsigned i=0; i<Root.Length(); i++)
		{
			Root[i]->Dump(Out);
		}
		Out.Close();
	}
	#endif
	
	// Stage 5: Group root set by subject

	// Stage 6: Done

	// Stage 7: Sort siblings

    // Clean up memory

	#if 0
	if (Out.Open("c:\\temp\\thread.txt", O_WRITE))
	{
	    for (int i=0; i<Root.Length(); i++)
	    {
		    Root[i]->Dump(Out);
	    }
	}
	#endif
}

void MContainer::Dump(LStream &s, int Depth)
{
	#ifdef _DEBUG
	for (int i=0; i<Depth; i++)
		s.Print("    ");

	s.Print("%p, %s, %s\n", this, MsgId.Get(), Message ? Message->GetSubject() : "<NULL>");

	for (unsigned i=0; i<Children.Length(); i++)
		Children[i]->Dump(s, Depth+1);
	#endif
}

///////////////////////////////////////////////////////////
class MailLeaf : public LTreeItem
{
	MContainer *Msg;

public:
	MailLeaf(MContainer *m)
	{
		Msg = m;
		for (unsigned i=0; i<Msg->Children.Length(); i++)
		{
			Insert(new MailLeaf(Msg->Children[i]));
		}

		Expanded(true);
	}

	const char *GetText(int i)
	{
		if (Msg->Message)
		{
			return Msg->Message->GetSubject();
		}

		return 0;
	}

	int GetImage(int i)
	{
		if (Msg->Message)
		{
			return Msg->Message->GetImage();
		}

		return ICON_OPEN_FOLDER;
	}

	void OnMouseClick(LMouse &m)
	{
		if (Msg->Message)
		{
			Msg->Message->OnMouseClick(m);
			Update();
		}
	}
};

class ThreadWnd : public LWindow
{
	ScribeWnd *App;

public:
	ThreadWnd(ScribeWnd *app)
	{
		App = app;
		Attach(0);
	}
};

void Threads(ScribeWnd *App)
{
	ScribeFolder *f = App->GetCurrentFolder();
	if (f)
	{
		List<Mail> m;
		for (auto t: f->Items)
		{
			if (t->IsMail())
			{
				m.Insert(t->IsMail());
			}
		}

		if (m.Length())
		{
			LArray<MContainer*> Containers;
			MContainer::Thread(m, Containers);
		}
	}
}


