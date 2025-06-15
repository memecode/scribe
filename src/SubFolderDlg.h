#pragma once

////////////////////////////////////////////////////////////////////////////////////////
class SubFolderDlg : public LDialog, public LXmlTreeUi
{
public:
	using TResult = std::function<void(LString)>;
	using TCallback = std::function<void(LViewI *Parent, ScribeWnd *App, int Limit, TResult cb)>;

private:
	ScribeWnd *App = nullptr;
	LDom *OptionStore = nullptr;
	TCallback FolderSelectCallback;

	void FolderSelector(int OutputCtrl, int Limit)
	{
		if (!FolderSelectCallback)
			LAssert(!"missing callback");
		else
			FolderSelectCallback(this, App, Limit,
				[this, OutputCtrl](auto path)
				{
					SetCtrlName(OutputCtrl, path);
				});
	}

public:
	SubFolderDlg(LView *parent, ScribeWnd *app, LDom *optionStore, TCallback selectCallback) :
		App(app),
		OptionStore(optionStore),
		FolderSelectCallback(selectCallback)
	{
		SetParent(parent);

		if (!LoadFromResource(IDD_SUB_FOLDERS))
			LAssert(!"resource missing");
		else
		{
			Map(OPT_Inbox, IDC_INBOX, GV_STRING);
			Map(OPT_Outbox, IDC_OUTBOX, GV_STRING);
			Map(OPT_Sent, IDC_SENT, GV_STRING);
			Map(OPT_Trash, IDC_TRASH, GV_STRING);
			Map(OPT_Contacts, IDC_CONTACT_FLD, GV_STRING);
			Map(OPT_Templates, IDC_TEMPLATES, GV_STRING);
			Map(OPT_Calendar, IDC_CALENDER, GV_STRING);
			Map(OPT_Filters, IDC_FILTERS_FLD, GV_STRING);
			Map(OPT_Groups, IDC_GROUPS_FLD, GV_STRING);
			Map(OPT_SpamFolder, IDC_SPAM_FLD, GV_STRING);
			
			Map(OPT_HasTemplates, IDC_HAS_TEMPLATES, GV_BOOL);
			Map(OPT_HasGroups, IDC_HAS_GROUPS, GV_BOOL);
			Map(OPT_HasCalendar, IDC_HAS_CAL_EVENTS, GV_BOOL);
			Map(OPT_HasFilters, IDC_HAS_FILTERS, GV_BOOL);
			Map(OPT_HasSpam, IDC_HAS_SPAM, GV_BOOL);
			
			if (optionStore)
				Convert(optionStore, this, true);
			else
				LAssert(!"missing option store");
			MoveToCenter();
		}
	}

	int OnNotify(LViewI *Ctrl, const LNotification &n) override
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
				if (OptionStore)
					Convert(OptionStore, this, false);
				else
					LAssert(!"missing option store");
				EndModal(1);
				break;
			case IDCANCEL:
				EndModal(0);
				break;
			case IDC_SET_INBOX:
				FolderSelector(IDC_INBOX, MAGIC_MAIL);
				break;
			case IDC_SET_OUTBOX:
				FolderSelector(IDC_OUTBOX, MAGIC_MAIL);
				break;
			case IDC_SET_SENT:
				FolderSelector(IDC_SENT, MAGIC_MAIL);
				break;
			case IDC_SET_TRASH:
				FolderSelector(IDC_TRASH, MAGIC_ANY);
				break;
			case IDC_SET_CONTACTS:
				FolderSelector(IDC_CONTACT_FLD, MAGIC_CONTACT);
				break;
			case IDC_SET_TEMPLATES:
				FolderSelector(IDC_TEMPLATES, MAGIC_MAIL);
				break;
			case IDC_SET_CALENDER:
				FolderSelector(IDC_CALENDER, MAGIC_CALENDAR);
				break;
			case IDC_SET_FILTERS:
				FolderSelector(IDC_FILTERS_FLD, MAGIC_FILTER);
				break;
			case IDC_SET_GROUPS:
				FolderSelector(IDC_GROUPS_FLD, MAGIC_GROUP);
				break;
			case IDC_SET_SPAM:
				FolderSelector(IDC_SPAM_FLD, MAGIC_MAIL);
				break;
		}

		return 0;
	}
};
