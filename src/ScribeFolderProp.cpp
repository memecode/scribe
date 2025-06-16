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

#include "lgi/common/Lgi.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/TabView.h"
#include "lgi/common/LgiRes.h"

#include "Scribe.h"
#include "resdefs.h"
#include "Store3Imap/ScribeImap.h"

//////////////////////////////////////////////////////////////////////////////
class LFolderInfo : public LListItem
{
public:
	enum Col {
		ColName,
		ColSize,
		ColPercent,
	};

	LDataFolderI *folder = nullptr;
	uint64 size = 0;

	void OnChange()
	{
		if (folder)
			SetText(folder->GetStr(FIELD_FOLDER_NAME), ColName);
		SetText(LFormatSize(size), ColSize);
	}
};

//////////////////////////////////////////////////////////////////////////////
class FolderPropertiesDlg : public LDialog
{
	// Data
	ScribeFolder *Folder = nullptr;

	// Controls
	LTabView *Tab = nullptr;
	LList *Usage = nullptr;
	LView *Txt = nullptr;

	// Scanning portion..
	Counter c;
	LArray<LDataFolderI*> inFolders;
	LUnrolledList<LDataI*> inData;
	uint64_t resortTs = 0;
	LHashTbl<PtrKey<LDataFolderI*>,LFolderInfo*> infoMap;
	LFolderInfo *rootInfo = nullptr;

public:
	bool RePopulate = false;
	constexpr static int TIMESLICE = 300; // ms

	FolderPropertiesDlg(ScribeFolder *folder, int InitialTab) :
		inData(nullptr)
	{
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

		auto bayesType = folder->App->BayesTypeFromPath(Path);
		SetCtrlName(ID_BAYES_TYPE, ToString(bayesType));

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

		if (auto f = Folder->GetFldObj())
		{
			inFolders.Add(f);

			if (rootInfo = new LFolderInfo)
			{
				rootInfo->SetText(".");
				infoMap.Add(f, rootInfo);
				Usage->Insert(rootInfo);
			}
		}

		SetPulse(TIMESLICE);
	}

	int OnNotify(LViewI *Ctrl, const LNotification &n) override
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
    			EndModal(0);
				break;
			}
		}

		return 0;
	}

	void AddMimeSeg(LDataPropI *Ptr, Counter &c, uint64 &Size)
	{
		// Add this one...
		auto Seg = dynamic_cast<LDataI*>(Ptr);
		if (!Seg)
		{
			LAssert(!"wrong obj");
			return;
		}

		c.Inc(Seg->Type());
		Size += Seg->Size();

		// Add the children...
		LDataIt Children = Seg->GetList(FIELD_MIME_SEG);
		if (Children)
		{
			for (LDataPropI *Child = Children->First(); Child; Child = Children->Next())
				AddMimeSeg(Child, c, Size);
		}
	}

	void Count(LDataFolderI *f, Counter &c, int depth = 0)
	{
		uint64 Size = f->Size();
		c.Inc(f->Type());
		
		LDataIterator<LDataI*> &fc = f->Children();
		for (unsigned i=0; i<fc.Length(); i++)
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

		for (unsigned n=0; n<f->SubFolders().Length(); n++)
		{
			if (auto s = f->SubFolders()[n])
				Count(s, c, depth + 1);
		}
	}

	void SortUsage()
	{
		Usage->Sort([](auto a, auto b)
			{
				auto A = dynamic_cast<LFolderInfo*>(a);
				auto B = dynamic_cast<LFolderInfo*>(b);
				if (!A || !B)
					return 0;

				if (A->size != B->size)
					return (int) ((int64)B->size - (int64)A->size);

				return Stricmp(A->GetText(0), B->GetText(0));
			});
		Usage->ResizeColumnsToContent();
	}

	void Finished()
	{
		SetPulse();

	    int64 Used = c.GetTypeCount(1); // 64 bytes in the header

	    // post count tallying
	    if (Usage)
	    {
		    // set the percent
		    for (auto item: *Usage)
		    {
		    	LFolderInfo *i = dynamic_cast<LFolderInfo*>(item);
		    	if (!i) continue;
				
			    char Str[32];
			    sprintf_s(Str, sizeof(Str), "%.1f", (double)(int64)i->size * 100 / Used );
			    i->SetText(Str, LFolderInfo::ColPercent);
		    }

		    // sort the items
			SortUsage();
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

		if (!Folder->GetParent())
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
	}

	void OnPulse()
	{
		auto startTs = LCurrentTime();
		#define IN_TIMESLICE() ((LCurrentTime() - startTs) < (TIMESLICE * 0.8))

		if (inFolders.Length() == 0 &&
			inData.Length() == 0)
			return Finished();

		LHashTbl<PtrKey<LFolderInfo*>,bool> dirty;

		auto rootFolder = Folder->GetFldObj();
		LDataFolderI *f;
		while (	IN_TIMESLICE() &&
				(f = inFolders.PopFirst()))
		{
			bool isRoot = rootFolder == f;
			auto info = infoMap.Find(f);

			c.Inc(f->Type());
			auto fSize = f->Size();
			c.Add(1, fSize);
			if (info)
				info->size += fSize;

			auto &subs = f->SubFolders();
			if (subs.GetState() == Store3Loaded)
			{
				for (auto s = subs.First(); s; s = subs.Next())
				{
					auto name = s->GetStr(FIELD_FOLDER_PATH);
					if (!name)
						s->GetStr(FIELD_FOLDER_NAME);

					if (isRoot)
					{
						// Create a list item for the top level sub-folder:
						if (auto i = new LFolderInfo)
						{
							i->folder = s;
							i->OnChange();
							infoMap.Add(s, i);
							LgiTrace("Adding child %p: '%s' with info %p\n", s, name, i);
							Usage->Insert(i);
						}
					}
					else if (info)
					{
						// Child of direct sub-folder..
						infoMap.Add(s, info);
						LgiTrace("Adding sub %p: '%s' with info %p\n", s, name, info);
					}
					else LAssert(!"no info map");
					
					inFolders.Add(s);
				}
			}

			auto &children = f->Children();
			if (children.GetState() == Store3Loaded)
			{
				for (auto c = children.First(); c; c = children.Next())
					inData.Add(c);
			}
			else
			{
				if (auto imapFld = dynamic_cast<ImapFolder*>(f))
				{
					imapFld->WhenLoaded([this, f](auto status)
						{
							auto &children = f->Children();
							for (auto c = children.First(); c; c = children.Next())
								inData.Add(c);
						});
				}
				else LAssert(!"what type of folder isn't loaded?");
			}
		}

		LDataI *d;
		while (IN_TIMESLICE() &&
			(d = inData.PopFirst()))
		{
			auto dSize = d->Size();
			auto folder = dynamic_cast<LDataFolderI*>(d->GetObj(FIELD_PARENT));
			LAssert(folder);
			bool isRoot = rootFolder == folder;
			auto info = isRoot ? rootInfo : infoMap.Find(folder);

			if (d->Type() == MAGIC_MAIL)
				if (auto seg = d->GetObj(FIELD_MIME_SEG))
					AddMimeSeg(seg, c, dSize);

			c.Inc(d->Type());
			c.Add(1, dSize);

			// LgiTrace("child obj of %s, isroot=%i, info=%p\n", folder->GetStr(FIELD_IMAP_PATH), isRoot, info);
			info->size += dSize;
			dirty.Add(info, true);
		}

		for (auto p: dirty)
		{
			p.key->OnChange();
			p.key->Update();
		}

		auto now = LCurrentTime();
		if (now - resortTs >= 2000)
		{
			resortTs = now;
			SortUsage();
		}
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
