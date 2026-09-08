#pragma once

class BayesianFilter
{
	friend class BuildSpamDB;
    class BayesianFilterPriv *d;
    ScribeWnd *App;

	/// Creates a word list from an email.
    Store3Status MakeMailWordList(Mail *m, LString &out);

	// Rebuilding the spam DB.
    void AddFolderToSpamDb(ScribeFolder *f);

	// Array of all storage and account folder roots:
	LArray<ScribeFolder*> RootFolders();

protected:
	void BuildStats();
	void CheckFolders();
    
public:
    BayesianFilter(ScribeWnd *app);
    virtual ~BayesianFilter();

    /// This event is called when the result of an analyze call 
    /// is returned to the user.
    virtual void OnBayesAnalyse(const char *Msg, const char *WhiteListEmail) {}

	/// This event is called when the Bayesian filter has finished
	/// classifying an email, and is telling the caller the result.
    virtual bool OnBayesResult(const char *MailRef, double Rating) { return false; }

    /// Re-builds the word lists from the source folders.
    bool BuildSpamDb();

    /// Starts an email classification job.
    Store3Status IsSpam(double &Result, Mail *m, bool Analyse = false);
    
    /// Increments the white list count for a particular work.
    void WhiteListIncrement(const char *Word);

    /// \returns the Bayesian type of folder
    ScribeMailType BayesTypeFromPath(LString Path);
    /// \returns the Bayesian type of folder
    ScribeMailType BayesTypeFromPath(Mail *m);

    /// Processes the change of an email from one Bayesian classification to another.
    Store3Status OnBayesianMailEvent(Mail *m, ScribeMailType OldType, ScribeMailType NewType);

	/// Remove an email from the white list...
	bool RemoveFromWhitelist(const char *Email);
    
    /// Impl
    void OnEvent(LMessage *Msg);
    
    /// The user changed the settings via the dialog
    void OnSettingsChange();
    
    //////////////////////////////////////////////////
    static bool UnitTests(ScribeWnd *app);
};
