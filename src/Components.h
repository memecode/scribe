/// \file
#ifndef _COMPONENTS_H_
#define _COMPONENTS_H_

#include "lgi/common/RefCount.h"
#include "lgi/common/Progress.h"

class CapabilityInstaller;

struct InstallProgress : public LMutex, public LRefCount
{
	int64 CurrentPos = 0;
	int64 TotalSize = 0;
	bool Finished = false;
	bool HasError = false;

	// These are protected by the lock... 
	// DO NOT access without locking. Crashes happen.
	LViewI *Ui;
	LString Msg;
	
	InstallProgress() : LMutex("InstallProgress")
	{
	}
	
	~InstallProgress()
	{
	}

	void IncRef()
	{
		if (Lock(_FL))
		{
			LRefCount::IncRef();
			Unlock();
		}
	}

	bool DecRef()
	{
		if (Lock(_FL))
		{
			if (LRefCount::DecRef())
				return true;

			Unlock();
		}
		return false;
	}
};

/// This class displays a horizontal bar in the UI somewhere indicating 
/// there is missing functionality. The user has the ability to initiate
/// an install procedure to get the required libraries via an "install"
/// button
class MissingCapsBar : public LLayout
{
	struct MissingCapsBarPriv *d;

    CapabilityInstaller *Installer = nullptr;
    LCapabilityTarget::CapsHash *Caps = nullptr;
    LProgressView *ProgCtrl = nullptr;
    InstallProgress *Progress = nullptr;
    LCapabilityTarget *Owner = nullptr;

    LArray<LButton*> Btns;
    LArray<LString> Actions;

    bool IsFinished = false;
    
public:
    MissingCapsBar
    (
		/// Target to notify on install events.
		LCapabilityTarget *Owner,
        /// The capabilities that are missing
        LCapabilityTarget::CapsHash *a,
        /// The message to display after the capabilities, describing the
        /// effect of the missing capabilities
        const char *msg,
        /// The installer to action any installs
        CapabilityInstaller *inst,
        /// Possible actions to fix the situation
        LArray<const char*> &actions,
        /// The default background colour
        LColour *background = NULL
    );
    ~MissingCapsBar();

	void Empty();
	void SetMsg(const char *m);
    void OnCreate() override;
    void OnPosChange() override;
    void OnPaint(LSurface *pDC) override;
    bool Pour(LRegion &r) override;
    int OnNotify(LViewI *c, const LNotification &n) override;
    void OnPulse() override;
	LMessage::Param OnEvent(LMessage *Msg) override;
};

/// This class is the behind the scenes code to install functionality on
/// demand.
class CapabilityInstaller
{
    class CapabilityInstallerPriv *d;
    
public:
    CapabilityInstaller
    (
        /// The name of the application, as recognised by the web installer
        const char *App,
        /// The version of the application
        const char *Version,
        /// The URI to the web installer front end.
        const char *Uri,
        /// The temp folder to use for downloads...
        const char *TmpFolder,
        /// [Optional] The HTTP proxy to use, or NULL.
        const char *Proxy = NULL
    );
    virtual ~CapabilityInstaller();

    /// Gets the HTTP proxy to use.
    /// (As an alternative to passing it into the constructor)
    virtual LString GetHttpProxy() { return LString(); }
    
    /// Start the install for a set of components
    virtual InstallProgress *StartAction(MissingCapsBar *Bar, LCapabilityTarget::CapsHash *Components, const char *Action);
};

#endif
