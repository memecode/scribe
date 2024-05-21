/*
**	FILE:			ScribeFolderProp.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			17/5/1999
**	DESCRIPTION:	Scribe folder properties dialog
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/ProgressDlg.h"
#include "resdefs.h"
#include "lgi/common/TabView.h"
#include "lgi/common/LgiRes.h"

//////////////////////////////////////////////////////////////////////////////
class LFolderInfo : public LListItem
{
public:
	ScribeFolder *Folder;
	uint64 Size;
};

int FolderInfo_Compare(LListItem *a, LListItem *b, NativeInt Data)
{
	LFolderInfo *A = dynamic_cast<LFolderInfo*>(a);
	LFolderInfo *B = dynamic_cast<LFolderInfo*>(b);
	if (A && B)
	{
		return (int) ((int64)B->Size - (int64)A->Size);
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
#define M_INIT_DONE         (M_USER + 1000)

class FolderPropertiesDlg : public LDialog
{
	// Data
	ScribeFolder *Folder;

	// Controls
	LTabView *Tab;
	// LTabPage *DetailTab;
	LList *Usage;
	LView *Txt;

	// Scanning portion..
	bool Loop;

public:
	bool RePopulate;

	FolderPropertiesDlg(ScribeFolder *folder, int InitialTab)
	{
		RePopulate = false;
		Loop = true;
		Txt = 0;

		Folder = folder;
		if (!Folder)
		{
			LAssert(!"No folder.");
			return;
		}

		SetParent(Folder->App);

		if (LoadFromResource(IDD_FOLDER_PROPS))
		{
			MoveToCenter();

			GetViewById(IDC_TAB, Tab);
			GetViewById(IDC_FOLDER_MSG, Txt);
			GetViewById(IDC_USAGE, Usage);
		}

		auto Path = Folder->GetPath();
		SetCtrlName(IDC_PATH, Path);

		ScribePerm p = Folder->GetFolderPerms(ScribeReadAccess);
		if (p == PermRequireAdmin)
		{
			SetCtrlEnabled(IDC_FPR_NONE, false);
			SetCtrlEnabled(IDC_FPR_USER, false);
		}
		else
		{
			SetCtrlEnabled(IDC_FPR_ADMIN, false);
		}
		SetCtrlValue(IDC_FOLDER_READ, p);

		p = Folder->GetFolderPerms(ScribeWriteAccess);
		if (p == PermRequireAdmin)
		{
			SetCtrlEnabled(IDC_FPW_NONE, false);
			SetCtrlEnabled(IDC_FPW_USER, false);
		}
		else
		{
			SetCtrlEnabled(IDC_FPW_ADMIN, false);
		}
		SetCtrlValue(IDC_FOLDER_WRITE, p);
	}

	~FolderPropertiesDlg()
	{
	    LAssert(Loop == false);
	}
	
	void OnCreate()
	{
		PostEvent(M_INIT_DONE);
	}
	
	bool OnRequestClose(bool OsClose)
	{
	    if (Loop)
	    {
	        Loop = false;
	        return false;
	    }
	    
	    return true;
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
			{
				auto Err = [this]()
				{
					LgiMsg(this, "Failed to set permissions.", AppName);
					return false;
				};

				Folder->SetFolderPerms(	this,
										ScribeReadAccess,
										(ScribePerm) GetCtrlValue(IDC_FOLDER_READ),
										[this, Err](auto status)
				{
					if (!status)
						return Err();

					this->Folder->SetFolderPerms(this, ScribeWriteAccess, (ScribePerm) this->GetCtrlValue(IDC_FOLDER_WRITE), [this, Err](auto status)
					{
						if (!status)
							return Err();

						EndModal(1);

						return true;
					});

					return true;
				});
				break;
			}
			case IDCANCEL:
			{
	            if (Loop)
	                Loop = false;
                else
    				EndModal(0);
				break;
			}
		}

		return 0;
	}

	void AddMimeSeg(LDataPropI *Ptr, Counter &c, uint64 Size)
	{
		// Add this one...
		LDataI *Seg = dynamic_cast<LDataI*>(Ptr);
		if (Seg)
		{
			c.Inc(Seg->Type());
			Size += Seg->Size();

			// Add the children...
			LDataIt Children = Seg->GetList(FIELD_MIME_SEG);
			if (Children)
			{
				for (LDataPropI *Child = Children->First(); Child; Child = Children->Next())
				{
					AddMimeSeg(Child, c, Size);
				}
			}
		}
	}

	void Count(LDataFolderI *f, Counter &c, int depth = 0)
	{
		uint64 Size = f->Size();
		c.Inc(f->Type());
		
		LDataIterator<LDataI*> &fc = f->Children();
		for (unsigned i=0; Loop && i<fc.Length(); i++)
		{
			LDataI *t = fc[i];
			if (t)
			{
				c.Inc(t->Type());
				Size += t->Size();

				if (t->Type() == MAGIC_MAIL)
				{
					AddMimeSeg(t->GetObj(FIELD_MIME_SEG), c, Size);
				}
			}
		}

		c.Add(1, Size);

		#ifdef _DEBUG
		char s[256];
		memset(s, '\t', depth);
		s[depth] = 0;
		LgiTrace("%sCounting %s (size=%i)\n", s, f->GetStr(FIELD_FOLDER_NAME), Size);
		#endif

		for (unsigned n=0; Loop && n<f->SubFolders().Length(); n++)
		{
			LDataFolderI *s = f->SubFolders()[n];
			Count(s, c, depth + 1);
		}
	}

	void Run()
	{
		Counter c;

		// Do count
		LDataFolderI *f = Folder->GetFldObj();
		c.Inc(f->Type());
		c.Add(1, f->Size());
		
		LDataIterator<LDataI*> &Children = f->Children();
		for (unsigned i=0; Loop && i<Children.Length(); i++)
		{
			LDataI *t = Children[i];
			c.Inc(t->Type());
			c.Add(1, t->Size());
		}
		
		for (ScribeFolder *Child = Folder->GetChildFolder();
		    Loop && Child;
		    Child = Child->GetNextFolder())
		{
			uint64 Old = c.GetTypeCount(1);

			Count(Child->GetFldObj(), c);

			if (Usage)
			{
				LFolderInfo *i = new LFolderInfo;
				if (i)
				{
					i->Folder = Child;
					i->Size = c.GetTypeCount(1) - Old;

					char Size[32];
					LFormatSize(Size, sizeof(Size), i->Size);
					
					i->SetText(Child->GetText(), 0);
					i->SetText(Size, 1);

					Usage->Insert(i);
				}
			}
		}

	    int64 Used = c.GetTypeCount(1); // 64 bytes in the header

	    // post count tallying
	    if (Loop && Usage)
	    {
		    // set the percent
		    for (auto it = Usage->begin(); Loop && it != Usage->end(); it++)
		    {
		    	LFolderInfo *i = dynamic_cast<LFolderInfo*>(*it);
		    	if (!i) continue;
				
			    char Str[32];
			    sprintf_s(Str, sizeof(Str), "%.1f", (double)(int64)i->Size * 100 / Used );
			    i->SetText(Str, 2);
		    }

		    // sort the items
		    Usage->Sort(FolderInfo_Compare);
		    Usage->ResizeColumnsToContent();
	    }

		// other props
		LString MsgSize = LFormatSize(Used);
		char Msg[512];
		const char *Format = LLoadString(IDS_FOLDER_PROPERTIES_DLG);
		
		int ch = sprintf_s(Msg, sizeof(Msg), 
				Format?Format:"",
				(int) c.GetTypeCount(MAGIC_MAIL),
				(int) c.GetTypeCount(MAGIC_CONTACT),
				(int) c.GetTypeCount(MAGIC_FOLDER),
				MsgSize.Get());

		if (Loop && !Folder->GetParent())
		{
			LMailStore *Ms = Folder->App->GetDefaultMailStore();
			if (Ms)
			{
				char File[32], Unused[32];
				uint64 FileSize = Ms->Store->Size();
				LFormatSize(File, sizeof(File), FileSize);
				LFormatSize(Unused, sizeof(Unused), FileSize-Used);

				sprintf_s(Msg+ch, sizeof(Msg)-ch,
						LLoadString(IDS_FOLDER_PROPERTIES_COMPACT),
						File,
						(int) ((Used*100)/FileSize),
						Unused);
			}
		}

		if (Txt)
		{
			Txt->Name(Msg);
			Txt->SendNotify(LNotifyTableLayoutRefresh);
		}
		
		Loop = false;
	}

    LMessage::Param OnEvent(LMessage *Msg)
    {
        if (Msg->Msg() == M_INIT_DONE)
            Run();

        return LDialog::OnEvent(Msg);        
    }

};

//////////////////////////////////////////////////////////////////////////////
void OpenFolderProperties(ScribeFolder *Parent, int Tab, std::function<void(bool)> callback)
{
	auto Dlg = new FolderPropertiesDlg(Parent, Tab);
	Dlg->DoModal([callback, Dlg](auto dlg, auto code)
	{
		if (code && callback)
			callback(Dlg->RePopulate);
	});
}
