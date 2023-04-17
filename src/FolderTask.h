#pragma once

#include "resdefs.h"

class FolderTask : public LProgressDlg
{
protected:
	ScribeWnd *App = NULL;
	ScribeFolder *Folder = NULL;
	
	LString MimeType;
	
	LAutoPtr<LStreamI> Stream;

	ThingType::IoProgress Status;
	ThingType::IoProgressCallback onComplete;

public:
	// Minimum amount of time to do work.
	constexpr static int WORK_SLICE_MS		= 130;
	// This should be larger then WORK_SLICE_MS to allow message loop to process
	constexpr static int PULSE_MS			= 200;

	FolderTask(	ScribeFolder *folder,
				LAutoPtr<LStreamI> stream,
				LString mimeType,
				ThingType::IoProgressCallback cb) :
		LProgressDlg(folder->App),
		Folder(folder),
		Stream(stream),
		MimeType(mimeType),
		onComplete(cb),
		Status(Store3Success)
	{
		App = Folder->App;
		Ts = LCurrentTime();
		SetParent(Folder->GetTree());		
		SetPulse(PULSE_MS);
		SetAlwaysOnTop(true);

		App->OnFolderTask(this, true);
	}
	
	virtual ~FolderTask()
	{
		App->OnFolderTask(this, false);
		
		if (onComplete)
			onComplete(&Status, Stream);
	}

	bool OnRequestClose(bool OsClose)
	{
		return true;
	}

	void OnPulse()
	{
		LProgressDlg::OnPulse();

		// We aren't checking IsCancelled() here to allow the
		// TimeSlice implementation a chance to clean up anything relevant
		// before returning false to this caller.
		auto StartTs = LCurrentTime();
		while ((LCurrentTime() - StartTs) < WORK_SLICE_MS)
		{
			if (!TimeSlice())
			{
				Quit();
				break;
			}
		}
	}
	
	/// This should use around WORK_SLICE_MS of time and then
	/// \returns true if more work to do or false if finished.
	/// Do check for IsCancelled() while doing work and return
	/// false.
	virtual bool TimeSlice() = 0;
};

class ExportFolderTask : public FolderTask
{
	int Idx = 0;

public:
	ExportFolderTask(	ScribeFolder *folder,
						LAutoPtr<LStreamI> out,
						LString mimeType,
						ThingType::IoProgressCallback cb) :
		FolderTask(folder, out, mimeType, cb)
	{
		bool Mbox = _stricmp(MimeType, sMimeMbox) == 0;

		// Clear the files contents
		Stream->SetSize(0);

		// Setup progress UI
		SetDescription(Mbox ? LLoadString(IDS_MBOX_WRITING) : (char*)"Writing...");
		SetRange(Folder->Items.Length());
		
		switch (Folder->GetItemType())
		{
			case MAGIC_MAIL:
				SetType(LLoadString(IDS_EMAIL));
				break;
			case MAGIC_CALENDAR:
				SetType(LLoadString(IDS_CALENDAR));
				break;
			case MAGIC_CONTACT:
				SetType(LLoadString(IDS_CONTACT));
				break;
			case MAGIC_GROUP:
				SetType("Groups");
				break;
			default:
				SetType("Objects");
				break;
		}

		SetPulse(PULSE_MS);
		SetAlwaysOnTop(true);
	}
	
	bool TimeSlice()
	{
		auto Start = LCurrentTime();
		while (	LCurrentTime() - Start < WORK_SLICE_MS
				&&
		        !IsCancelled())
		{
			if (Idx >= (ssize_t)Folder->Items.Length())
				return false;

			// Process all the container's items
			Thing *t = Folder->Items[Idx++];
			if (!t)
				return false;

			LAutoPtr<LStreamI> wrapper(new LProxyStream(Stream));
			if (!t->Export(wrapper, MimeType))
			{
				Status.status = Store3Error;
				Status.errMsg = "Error exporting items.";
				return false;
			}
			
			Value(Idx);
		}
		
		return true;
	}
};



