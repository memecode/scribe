#pragma once

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


