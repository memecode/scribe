
#ifndef __SCRIBE_DEFS_H
#define __SCRIBE_DEFS_H

////////////////////////////////////////////////////////////////////////////////////////////
// Options
#define OPT_UserName				"UserName"		//(char*)
#define OPT_IsPortableInstall		"IsPortable"		//(bool)

#define OPT_AccountName				"AccName"			//(char*)
#define OPT_AccountDisabled			"AccDisable"		//(char*)
#define OPT_AccountUID				"AccId"				//(int)
#define OPT_AccountExpanded			"AccExpand"			//(bool)

#define OPT_Accounts				"Accounts"			// (LXmlTag*)

#define OPT_AccIdentName			"Identity.Name"
#define OPT_AccIdentEmail			"Identity.Email"
#define OPT_AccIdentReply			"Identity.Reply"
#define OPT_AccIdentTextSig			"Identity.Sig"
#define OPT_AccIdentHtmlSig			"Identity.HtmlSig"
#define OPT_OnlySendThroughThis		"Identity.OnlySendThis"
#define OPT_AccountSort				"Identity.Sort"

#define OPT_SmtpServer				"Send.Server"		//(char*)
#define OPT_SmtpPort				"Send.Port"			//(int)
#define OPT_SmtpDomain				"Send.Domain"		//(char*)
#define OPT_SmtpName				"Send.Name"			//(char*)
#define OPT_SmtpAuth				"Send.Auth"			//(bool)
#define OPT_SmtpAuthType			"Send.AuthType"		//(int)
#define OPT_SmtpSSL					"Send.SSL"			//(int)
#define OPT_EncryptedSmtpPassword	"Send.Password"		//(void*)
#define OPT_SendHotFolder			"Send.HotFolder"	//(char*)
#define OPT_SendCharset1			"Send.PrefCharset1"
#define OPT_SendCharset2			"Send.PrefCharset2"

#define	OPT_Pop3Type				"Receive.Type"			//(int)
#define	OPT_Pop3Protocol			"Receive.Protocol"		//(char*) one of the PROTOCOL_??? defs
#define OPT_Pop3Server				"Receive.Server"		//(char*)
#define OPT_Pop3Port				"Receive.Port"
#define OPT_Pop3Name				"Receive.Name"			//(char*)
#define OPT_Pop3AutoReceive			"Receive.AutoReceive"	//(bool)
#define OPT_Pop3CheckEvery			"Receive.CheckEvery"	//(char*)
#define OPT_Pop3LeaveOnServer		"Receive.LeaveOnServer"	//(bool)(int)
#define OPT_Pop3Folder				"Receive.Folder"		//(char*)
#define OPT_Pop3SSL					"Receive.SSL"			//(int)
#define OPT_EncryptedPop3Password	"Receive.Password"		//(void*)
#define OPT_DeleteAfter				"Receive.DeleteAfter"	// (bool)
#define OPT_DeleteDays				"Receive.DeleteDays"	// (int)
#define OPT_DeleteIfLarger			"Receive.DelIfLarger"	// (bool)
#define OPT_DeleteIfLargerSize		"Receive.DelIfLargerSize"
#define OPT_ReceiveHotFolder		"Receive.HotFolder"		//(char*)
#define OPT_Receive8BitCs			"Receive.8BitCharSet"
#define OPT_ReceiveAsciiCs			"Receive.AsciiCharset"
#define OPT_ReceiveAuthType			"Receive.AuthType"		//(int)
#define OPT_MaxEmailSize			"Receive.MaxSize"		//(int)
#define OPT_ReceiveSecAuth			"Receive.SecAuth"		//(int)

#define OPT_SplitterPos				"ScribeUI.SplitPos"	//(int)	
#define OPT_SubSplitPos				"ScribeUI.SubSplitPos" //(int)	
#define OPT_UiLanguage				"ScribeUI.Lang"		//(char*)
#define OPT_UiFontSize				"ScribeUI.FontSize"	//(int)
#define OPT_ScribeWndPos			"ScribeUI.Pos"		//(char*)
#define OPT_ScribeWndToolbar		"ScribeUI.Toolbar"
#define OPT_ShowScriptConsole		"ScribeUI.ShowScriptConsole"
#define OPT_ShowFolderTotals		"ScribeUI.ShowFldTotals"
#define OPT_Theme					"ScribeUI.Theme"		//(char*)

#define OPT_LocalCalendarColour		"CalendarUI.LocCol"	  //(int)Rgb24(x, x, x)
#define OPT_CalendarViewPos			"CalendarUI.ViewPos"  //(char*)
#define OPT_CalendarViewMode		"CalendarUI.Mode"	  //(int)
#define OPT_CalendarViewTodo		"CalendarUI.Todo"	  //(int)
#define OPT_CalendarEventPos		"CalendarUI.EventPos" //(char*)
#define OPT_CalendarCreateIn		"CalendarUI.Create"
#define OPT_CalendarSources			"CalendarUI.Sources"
#define OPT_CalendarFirstDayOfWeek	"CalendarUI.FirstDayOfWeek"

#define OPT_CreateFoldersIfMissing	"CreateIfMissing"	//(int)
#define OPT_PersonalFolders			"Folders"			//(char*)
#define OPT_MailStores				"MailStores"		// XML Tag with OPT_MailStore children
#define OPT_MailStore				"MailStore"			// XML Tag
#define OPT_MailStoreName			"Name"				//(char*)
#define OPT_MailStoreLocation		"Path"				//(char*)
#define OPT_MailStoreDisable		"Disable"			//(bool)
#define OPT_MailStoreFormat			"StoreFormat"		//(int)
#define OPT_MailStoreExpanded		"Expanded"			//(bool)
#define OPT_MailStoreContactUrl		"ContactUrl"		//(char*)
#define OPT_MailStoreCalendarUrl	"CalUrl"			//(char*)
#define OPT_MailStoreUserName		"User"				//(char*)
#define OPT_MailStorePassword		"Password"			//(char*)

#define OPT_SpellCheck				"SpellCheck"
#define OPT_SpellCheckLanguage		"SpellCheckLang"
#define OPT_SpellCheckDictionary	"SpellCheckDict"
#define OPT_PreferAspell			"PrefAspell"
#define OPT_ReceiveSpamIds			"SpamIds"			//(char*)
#define OPT_QuoteReply				"ReplyQuote"		//(bool)(int)
#define OPT_QuoteReplyStr			"ReplyQuoteStr"		//(char*)
#define OPT_ReplyWithSig			"ReplyWithSig"		//(bool)(int)
#define OPT_LogFile					"LogFile"			//(char*)
#define OPT_LogFormat				"LogFmt"			//(int)
#define OPT_SoftwareUpdate			"SoftwareUpdate"
#define OPT_SoftwareUpdateTime		"SoftwareUpdateTime"
#define OPT_SoftwareUpdateLast		"SoftwareUpdateLast"
#define OPT_SoftwareUpdateIncBeta	"SoftwareUpdateIncBeta"
#define OPT_AdjustDateTz			"AdjustDateTz"
#define OPT_NoEmoji					"NoEmoji"			//(bool)
#define OPT_MinimizeToTray			"MinToTray"			//(bool)(int)
#define OPT_WordWrap				"WordWrap"			//(bool)(int)
#define OPT_EditControl				"EditControl"		//(int)
#define OPT_CheckForDialUp			"CheckDialUp"		//(bool)(int)
#define OPT_EditorFont				"EditorFont"		//(binary)
#define OPT_HtmlFont				"HtmlFont"			//(binary)
#define OPT_WrapAtColumn			"WrapAtColumn"		//(int)
#define OPT_SoftwareKey				"SoftwareKey"		//(char*)
#define OPT_NewMailSoundFile		"NewMailSound"		//(char*)
#define OPT_RegisterWindowsClient	"RegWinClient"		//(bool)IDC_REGISTER_CLIENT
#define OPT_CheckDefaultEmail		"DefMailer"			//(bool)
#define OPT_UseSocks				"UseSocks5"			//(bool)
#define OPT_Socks5Server			"Socks5Server"		//(char*)
#define OPT_Socks5UserName			"Socks5Name"		//(char*)
#define OPT_Pop3OnStart				"Pop3OnStart"		//(bool)
#define OPT_Pop3DefAction			"Pop3DefAction"		//(bool)
#define OPT_RecipientFromClipboard	"ClipRecip"			//(bool)
#define OPT_GridLines				"GridLines"			//(bool)
#define OPT_PreviewLines			"PreviewLines"		//(bool)
#define OPT_ToolbarText				"ToolText"			//(bool)
#define OPT_DefCodePage				"DefCodePage"		//(int)
#define OPT_ConfirmDelete			"ConfirmDel"		//(bool)
#define OPT_DelDirection			"DelDirection"		//(ScribeDeleteAction)
#define OPT_DefaultSendAccount		"DefSend"			//(int)
#define OPT_NewMailNotify			"NewMailNotify"		//(bool)
#define OPT_CurrentIdentity			"CurId"				//(int)	
#define OPT_DefaultAlternative		"DefAlt"			//(int) 0=text/plain, 1=text/html
#define OPT_BoldUnread				"BdUnread"			//(int)
#define OPT_DateFormat				"DateFormat"		//(int)
#define OPT_HttpProxy				"HttpPr"			//(char*)
#define OPT_GlyphSub				"GlyphSub"			//(bool)
#define OPT_WorkOffline				"Offline"			//(bool)
#define OPT_DebugTrace				"DbgTrc"			//(bool)
#define OPT_DebugSSL				"DebugSSL"			//(bool)
#define OPT_ScriptDebugger			"ScriptDebugger"	//(bool)
#define OPT_AccPermRead				"AccPermRd"			//(ScribePerm)
#define OPT_AccPermWrite			"AccPermWr"			//(ScribePerm)
#define OPT_SizeInKiB				"SizeInKiB"			//(bool)
#define OPT_RelativeDates			"RelativeDates"		//(bool)

#define OPT_BayesFilterMode			"BayesMode"			//(ScribeBayesianFilterMode)
#define OPT_BayesMoveTo				"BayesMvTo"			//(char*)
#define OPT_BayesUserWhiteList		"BayesWhiteLst"		//(char*)
#define OPT_BayesHam				"BayesHam"			//(int)
#define OPT_BayesSpam				"BayesSpam"			//(int)
#define OPT_BayesFalsePositives		"BayesFalsePos"		//(int)
#define OPT_BayesFalseNegitives		"BayesFalseNeg"		//(int)
#define OPT_BayesThreshold			"BayesThres"		//(char*)
#define OPT_BayesIncremental		"BayesInc"			//(int)
#define OPT_BayesDebug				"BayesDbg"			//(int)
#define OPT_BayesDeleteAttachments	"BayesDelAttach"	//(bool)
#define OPT_BayesDeleteOnServer		"BayesDelOnServer"	//(bool)
#define OPT_BayesSetRead			"BayesSetRead"		//(bool)

#define OPT_PrintSettings			"PrintOpts"			//(char*)
#define OPT_AutoDeleteExe			"AutoDelExe"		//(bool)(int)
#define OPT_MarkReadAfterPreview	"PreRead"			//(bool)(int)
#define OPT_MarkReadAfterSeconds	"PreReadSec"		//(int)
#define OPT_ExtraHeaders			"XtrHeaders"		//(char*)
#define OPT_DefaultReplyAllSetting	"DefReplayAll"		//(int)
#define OPT_HideId					"HideId"			//(bool)
#define OPT_LayoutMode				"Layout"			//(int)
#define OPT_BlinkNewMail			"Blink"				//(bool)
#define OPT_StartInFolder			"StartFdr"			//(char*)
#define OPT_OutlookImportSrc		"OiSrc"				//(char*)
#define OPT_OutlookImportDst		"OiDst"				//(char*)
#define OPT_OutlookImportAll		"OiAll"				//(int)
#define OPT_OutlookExportSrc		"OutlookExpFld"		//(char*)
#define OPT_OutlookExportDst		"OeDst"				//(char*)
#define OPT_OutlookExportAll		"OeAll"				//(int)
#define OPT_OutlookExportExclude	"OeExc"				//(int)
#define OPT_DisableUserFilters		"DisFilt"			//(int)
#define OPT_ScribeExpSrcPaths		"ExpSrcPaths"		//(char*)
#define OPT_ScribeExpDstPath		"ExpDstPath"		//(char*)
#define OPT_ScribeExpFolders		"ExpFlds"			//(char*)
#define OPT_ScribeExpAll			"ExpAll"			//(bool)
#define OPT_ScribeExpExclude		"ExpExc"			//(bool)
#define OPT_MailShowFrom			"MailUI.ShowFrom"	//(bool)
#define OPT_GrowlEnabled			"GrowlEnabled"		//(bool)
#define OPT_FileTypes				"FileTypes"			// list of elements used to do custom file typing
#define OPT_PreviewWndPos			"PreviewWndPos"		//(char*)
#define OPT_PreviewWndCols			"PreviewWndCols"	//(char*)

#define OPT_HtmlLoadImages			"HtmlImgs"			//(bool)
#define OPT_RemoteContentWhiteList	"RemoteContentWhitelist" // (char*)
#define OPT_RemoteContentBlackList	"RemoteContentBlacklist" // (char*)

// New options
//#define OPT_Signature				"Sig"				//(char*)
#define OPT_TextReplyFormat			"ReplyFmt"			//(char*)
#define OPT_HtmlReplyFormat			"HtmlReplyFmt"		//(char*)
#define OPT_TextForwardFormat		"FwdFmt"			//(char*)
#define OPT_HtmlForwardFormat		"HtmlFwdFmt"		//(char*)

// Passwords
#define OPT_Socks5Password			"Socks5Psw"			//(char*)
#define OPT_UserPermPassword		"UsrPermPwd"		//(binary)

// Lgi conf options
#define OPT_AdminPassword			"AdminPsw"			// in the tag "scribe"

// Image attachment resizing options:
#define OPT_ResizeImgAttachments	"ResizeImgAttach"	//(bool)
#define OPT_ResizeJpegQual			"ResizeJpegQual"	//(int)
#define OPT_ResizeMaxPx				"ResizeMaxPx"		//(int)
#define OPT_ResizeMaxKb				"ResizeMaxSize"		//(int)

// Encryption options
#define OPT_HideGnuPG				"HideGnuPG"			//(bool)

// Folder opt's
#define OPT_Inbox					"Folder-0"
#define OPT_Outbox					"Folder-1"
#define OPT_Sent					"Folder-2"
#define OPT_Trash					"Folder-3"
#define OPT_Contacts				"Folder-4"
#define OPT_Templates				"Folder-5"
#define OPT_Filters					"Folder-6"
#define OPT_Calendar				"Folder-7"
#define OPT_Groups					"Folder-8"
#define OPT_SpamFolder				"Folder-9"

#define OPT_HasTemplates			"HasTemplates"
#define OPT_HasFilters				"HasFilters"
#define OPT_HasCalendar				"HasCalEvents"
#define OPT_HasGroups				"HasGroups"
#define OPT_HasSpam					"HasSpam"

#define OPT_Title					"Title"
#define OPT_First					"FirstName"
#define OPT_Last					"SurName"
#define OPT_Email					"Email"
#define OPT_Nick					"Nick"
#define OPT_Spouse					"Spouse"
#define OPT_Note					"Note"
#define OPT_Uid						"Uid"
#define OPT_TimeZone				"TZ"

#define OPT_HomeStreet				"Street"
#define OPT_HomeSuburb				"Suburb"
#define OPT_HomePostcode			"Postcode"
#define OPT_HomeState				"State"
#define OPT_HomeCountry				"Country"
#define OPT_HomePhone				"Home"
#define OPT_HomeMobile				"Mobile"
#define OPT_HomeIM					"ICQ"
#define OPT_HomeFax					"Fax"
#define OPT_HomeWebPage				"WebPage"

#define OPT_WorkStreet				"WkStreet"
#define OPT_WorkSuburb				"WkSuburb"
#define OPT_WorkPostcode			"WkPostcode"
#define OPT_WorkState				"WkState"
#define OPT_WorkCountry				"WkCountry"
#define OPT_WorkPhone				"Work"
#define OPT_WorkMobile				"WkMobile"
#define OPT_WorkIM					"WkIM"
#define OPT_WorkFax					"WkFax"
#define OPT_WorkWebPage				"WkWebPage"
#define OPT_Company					"Company"
#define OPT_CustomFields			"CustomFields"

// Clipboard formats
#define CFSTR_SCRIBE_THING			"Scribe-MailItem"
#define CFSTR_SCRIBE_FOLDER			"Scribe-Folder"

// Protocol names
#define PROTOCOL_POP3				"POP3"
#define PROTOCOL_IMAP4_FETCH		"IMAP4(fetch)"
#define PROTOCOL_IMAP4				"IMAP4(full)"
#define PROTOCOL_CALENDAR			"Calendar"
#define PROTOCOL_POP_OVER_HTTP		"PopOverHttp"
#define PROTOCOL_MAPI				"MAPI"
#define PROTOCOL_SMTP				"SMTP"

enum ScribeProtocol
{
	ProtocolNone,
	ProtocolPop3,
	ProtocolPopOverHttp,
	ProtocolImapFetch,
	ProtocolImapFull,
	ProtocolCalender,
	ProtocolMapi,
	ProtocolSmtp,
};

extern ScribeProtocol ProtocolStrToEnum(const char *str);

enum SribeResourceType
{
	ResNone,
	ResToolbarFile,
	ResIconsFile,
	ResMax,
};

// File names
#define ABOUT_IMG_FILE				"About.png"

////////////////////////////////////////////////////////////////////////////////////////////
// Defines

// Window messages
#define ShareMemName				"ScribeSharedMem"
enum ScribeMessages
{
	M_SCRIBE_CMD = M_USER + 0x301,
	M_SCRIBE_OPEN_THING,
	M_SCRIBE_ITEM_SELECT,
	M_SCRIBE_NEW_MAIL,
	M_SCRIBE_THREAD_DONE,		// Accountlet *a=MsgB(m)
	M_SCRIBE_MSG,				// char *Msg=MsgA(m), bool AskOptions=MsgB(m);
	M_SCRIBE_LOADED,
	M_SCRIBE_DEL_THING,			// (Thing*)MsgA(m)
	M_SCRIBE_SET_MSG_FLAG,		// (Mail*)MsgA(m), Flag = MsgB(m)
	M_SCRIBE_ACC_ONLINE_UPDATE,
	M_SCRIBE_BAYES_RESULT,
	M_NEEDS_CAP,				// (char*)MsgA(m)
	M_UPDATE,
	M_NEW_CONSOLE_MSG,
	M_GNUPG_KEY_INFO,			// LAutoPtr< LArray<GpgConnector::KeyInfo> > Inf( (LArray<GpgConnector::KeyInfo>*) Msg->B() )
	M_GNUPG_SIG_CHECK,			// LAutoPtr<GpgSigCheckResponse> Resp( (GpgSigCheckResponse*)Msg->A() );
	M_GNUPG_DECRYPT,			// LAutoPtr<GpgDecryptResponse> Resp( (GpgDecryptResponse*)Msg->A() );
	M_RESIZE_IMAGE,				// LAutoPtr<ImageResizeThread::Job> Job((ImageResizeThread::Job*)Msg->A());
	M_SCRIBE_IDLE,
	M_EXPORT_NEXT,
	M_DELETE_STYLE,
	M_GET_USER_INPUT,			// (UserInput*)MsgA(m)
	M_SET_HTML,
	M_UNIT_TEST,				// (LJson*)m->A()
	M_CALENDAR_SOURCE_EVENT,	// (CalendarSource*)m->a, (LMessage*)m->b
	M_CALENDAR_SOURCE_FINISH,
	M_LOAD_NEXT_MAIL_STORE,
	M_UNIT_TEST_TICK,
};

enum ScribeControls
{
	IDC_STATIC = -1,
	IDC_ATTACHMENT = 1100,
	IDC_INTERNET_HEADER,
	IDC_TEST,
	IDC_SET_TO,
	IDC_ENTRY,
	IDC_REPLYALL,
	IDC_FORWARD,
	IDC_TO,
	IDC_SEND,
	IDC_REPLY,
	IDC_REMOVE_STR,
	IDC_FROM,
	IDC_LAUNCH_HTML,
	IDC_SET_FROM,
	IDC_THING_LIST,
	IDC_SHOW_FROM,
	IDC_KILL,
	IDC_USE_REPLY_TO,
	IDC_REPLY_TO_ADDR,
	IDC_TEXT_VIEW,
	IDC_HTML_VIEW,
	IDC_MAIL_UI_TABS,
	IDC_MONTH_VIEW,
	IDC_ADD_CAL_EVENT,
	IDC_ADD_CAL_EVENT_POPUP,
};

enum {
	IDM_NULL = 39000,
	#undef IDM_OPEN
	IDM_OPEN,
	IDM_LOAD,
	IDM_SAVE_CLOSE,
	#undef IDM_CLOSE
	IDM_CLOSE,
	IDM_UNDO,
	IDM_REDO,
	IDM_TO,
	IDM_CC,
	IDM_RENAME,
	IDM_EMPTY,
	IDM_EDIT,
	IDM_BCC,
	IDM_CREATE_SUB,
	IDM_PROPERTIES,
	IDM_EMAIL_GROUP,
	IDM_ATTACH_FILE,
	IDM_IMPORT_BEOS_PPL,
	IDM_IMPORT_BEOS_MAIL,
	IDM_MARK_SEND,
	IDM_IMPORT_OUTLOOK_CONTACTS,
	IDM_CALENDAR,
	IDM_COLLECT_MAIL,
	IDM_DELETE_AS_SPAM,
	IDM_ITEM_FILTER,
	IDM_HIGH_PRIORITY,
	IDM_LOW_PRIORITY,
	IDM_READ_RECEIPT,
	IDM_THREAD,
	IDM_SELECT_THREAD,
	IDM_DELETE_THREAD,
	IDM_IGNORE_THREAD,
	IDM_BROWSE_FOLDER,
	IDM_MERGE_FILE,
	IDM_PREV_MSG,
	IDM_NEXT_MSG,
	IDM_SEND_MSG,
	IDM_DELETE_MSG,
	IDM_RECEIVE_AND_SEND,
	IDM_SORT_SUBFOLDERS,
	IDM_CREATE_LIST_FILTER,
	IDM_MARK_ALL_READ,
	IDM_EXPUNGE,
	IDM_IMPORT_EXPORT_TEST,
	IDM_UNDELETE,
	IDM_ADD_LOCAL_CAL,
	IDM_ADD_CAL_URL,
	IDM_ADD_TO_CAL,
	IDM_SHOW_CONSOLE,
	IDM_INSPECT,
	IDM_RESIZE,
	IDM_LOCAL_FOLDERS,
	IDM_WEBDAV_FOLDER,
	IDM_COPY_PATH,
	IDM_REPARSE
};

#define IDM_MERGE_TEMPLATE_BASE		2000
#define IDM_TOOL_SCRIPT_BASE		5000
#define IDM_SEND_FROM				6000
#define IDM_NEW_FROM_TEMPLATE		7000
#define IDM_RECEIVE_FROM			8000
#define IDM_RECEIVE_MAIL			8998
#define IDM_RECEIVE_ALL				8999
#define IDM_PREVIEW_FROM			9000
#define IDM_PREVIEW_POP3			9999

// Icons indexs
#define ICON_CLOSED_FOLDER			0
#define ICON_OPEN_FOLDER			1
#define ICON_READ_MAIL				2
#define ICON_UNREAD_MAIL			3
#define ICON_CONTACT				4
#define ICON_UNSENT_MAIL			5
#define ICON_TO						6
#define ICON_CC						7
#define ICON_UNKNOWN				8
#define ICON_READ_ATT_MAIL			9
#define ICON_UNREAD_ATT_MAIL		10
#define ICON_BCC					11
#define ICON_TRASH					12
#define ICON_FILTER					13
#define ICON_PAPER_CLIP				14
#define ICON_PRIORITY_BLACK			15
#define ICON_PRIORITY_HIGH			16
#define ICON_PRIORITY_LOW			17
#define ICON_FLAGS_BLACK			18
#define ICON_FLAGS_REPLY			19
#define ICON_FLAGS_FORWARD			20
#define ICON_FLAGS_MARK				21
#define ICON_GREY					22
#define ICON_GREEN					23
#define ICON_YELLOW					24
#define ICON_RED					25
#define ICON_UP_ARROW				26
#define ICON_DOWN_ARROW				27
#define ICON_UP_DOWN_ARROW			28
#define ICON_FOLDER_MAIL			29
#define ICON_FOLDER_CONTACTS		30
#define ICON_FOLDER_FILTERS			31
#define ICON_FOLDER_CALENDAR		32
#define ICON_DISABLED_FOLDER		33
#define ICON_MAILBOX				34
#define ICON_CALENDAR				35
#define ICON_REMOTE_FOLDER_CLOSED	36
#define ICON_REMOTE_FOLDER_OPEN		37
#define ICON_CONTACT_GROUP			38
#define ICON_FLAGS_BOUNCE			39
#define ICON_LAYOUT1				40
#define ICON_LAYOUT2				41
#define ICON_LAYOUT3				42
#define ICON_LAYOUT4				43
#define ICON_LINK					44
#define ICON_HELP					45
#define ICON_CUT					46
#define ICON_COPY					47
#define ICON_PASTE					48
#define ICON_TODO					49
#define ICON_PLUGINS				50
#define ICON_OPTIONS				51
#define ICON_LOCK					52

enum IconImageIdx
{
	IMG_NEW_MAIL,			// 0
	IMG_NEW_CONTACT,
	IMG_SEND,
	IMG_RECEIVE,
	IMG_REPLY,
	IMG_REPLY_ALL,
	IMG_FORWARD,
	IMG_PRINT,

	IMG_PREV_ITEM,			// 8
	IMG_NEXT_ITEM,

	IMG_TRASH,				// 10
	IMG_SAVE,
	IMG_SAVE_AND_CLOSE,
	IMG_ATTACH_FILE,
	IMG_PREVIEW,
	IMG_CALENDAR,
	IMG_DELETE_SPAM,
	IMG_SEARCH,

	IMG_HIGH_PRIORITY,		// 18
	IMG_LOW_PRIORITY,
	IMG_READ_RECEIPT,
	IMG_THREADS,

	IMG_CAL_DAY,			// 22
	IMG_CAL_WEEK,
	IMG_CAL_MONTH,
	IMG_CAL_YEAR,
	IMG_HELP,
	IMG_CAL_PREV,
	IMG_CAL_BACK,
	IMG_CAL_TODAY,
	IMG_CAL_FORWARD,
	IMG_CAL_NEXT,
	IMG_CAL_CONFIG,

	IMG_BOUNCE,				// 33
	IMG_CAL_TODO,			
	IDM_XGATE,
	IMG_CONSOLE_NOMSG,
	IDM_CONSOLE_MSGS,
	// 38 icons
};

// Folders
#define FOLDER_INBOX				0
#define FOLDER_OUTBOX				1
#define FOLDER_SENT					2
#define FOLDER_TRASH				3
#define FOLDER_CONTACTS				4
#define FOLDER_TEMPLATES			5
#define FOLDER_FILTERS				6
#define FOLDER_CALENDAR				7
#define FOLDER_GROUPS				8
#define FOLDER_SPAM					9
#define FOLDER_MAX					10

// Update Flags
#define UPDATE_TREE					1
#define UPDATE_LIST					2

// Mail fields
#include "lgi/common/Store3Defs.h"

//////////////////////////////////////////////////////////////
// Filter op types
#define OP_EQUAL					0
#define OP_NOT_EQUAL				1
#define OP_LESS_THAN				2
#define OP_LESS_THAN_OR_EQUAL		3
#define OP_GREATER_THAN				4
#define OP_GREATER_THAN_OR_EQUAL	5
#define OP_LIKE						6
#define OP_CONTAINS					7
#define OP_STARTS_WITH				8
#define OP_ENDS_WITH				9

// Filter action types
enum FilterActionTypes
{
	ACTION_MOVE_TO_FOLDER,
	ACTION_DELETE,
	ACTION_PRINT,
	ACTION_PLAY_SOUND,
	ACTION_OPEN,
	ACTION_EXECUTE,
	ACTION_MARK,
	ACTION_SET_READ,
	ACTION_LABEL,
	ACTION_EMPTY_FOLDER,
	ACTION_MARK_AS_SPAM,
	ACTION_REPLY,
	ACTION_FORWARD,
	ACTION_BOUNCE,
	ACTION_SAVE_ATTACHMENTS,
	ACTION_DELETE_ATTACHMENTS,
	ACTION_CHANGE_CHARSET,
	ACTION_COPY,
	ACTION_EXPORT,
};

// SSL modes
enum ScribeSslMode
{
	/// No SSL
	SSL_NONE = 0,
	/// SSL, using normal socket connect and then STARTTLS command
	SSL_STARTTLS = 1,
	/// Direct SSL connection from the start.
	SSL_DIRECT = 2
};

// Status update on send / receive
#if defined WIN32
#define STATUS_UPDATE_RES			50
#elif defined BEOS
#define STATUS_UPDATE_RES			1000
#endif

// Enums
enum ScribePerm
{
	PermRequireNone = 0,
	PermRequireUser = 1,
	PermRequireAdmin = 2
};

enum ScribeRemoteContent
{
	RemoteDefault,
	RemoteNeverLoad,
	RemoteAlwaysLoad,
};

enum ScribeAccessType
{
	ScribeReadAccess,
	ScribeWriteAccess
};

enum ScribeBayesianFilterMode
{
	BayesOff = 0,
	BayesTrain = 1,
	BayesFilter = 2
};

enum ScribeMailType
{
	BayesMailUnknown = 0,
	BayesMailHam,
	BayesMailSpam
};

enum ScribeDeleteAction
{
	DeleteActionNext = 0,
	DeleteActionClose,
	DeleteActionPrev
};

enum CalendarViewMode
{
	CAL_VIEW_DAY,
	CAL_VIEW_WEEKDAY,
	CAL_VIEW_WEEK,
	CAL_VIEW_MONTH,
	CAL_VIEW_YEAR
};

enum ScribeAccountletStatusIcon
{
	STATUS_OFFLINE,
	STATUS_ONLINE,
	STATUS_WAIT,
	STATUS_ERROR,

	STATUS_MAX
};

// Colours
#define L_MAIL_PREVIEW				(L_MAXIMUM+1)
#define L_UNREAD_COUNT				(L_MAXIMUM+2)

// File utility macros
#define SizeStrField(Var)			(sizeof(short)+SizeofStr(Var))
#define SizeIntField(Var)			(sizeof(short)+sizeof(ulong)+sizeof(Var))
#define SizeObjField(Var)			(sizeof(short)+sizeof(ulong)+(Var).Sizeof())

#define WriteStrField(Id, Var)		{ f << ((short)Id); WriteStr(f, Var); }
#define WriteIntField(Id, Var)		{ f << ((short)Id); f << ((ulong)sizeof(Var)); f << Var; }
#define WriteObjField(Id, Var)		{ f << ((short)Id); f << ((ulong)(Var).Sizeof()); (Var).Serialize(f, Write); }
#define WriteDateField(Id, Var, Utc) { f << ((short)Id); f << ((ulong)(Var).Sizeof()); \
										if (Utc) Var.ToUtc(); (Var).Serialize(f, Write); \
										if (Utc) Var.ToLocal(); }

#define ReadStrField(Id, Var)		case Id: Var = ReadStr(f PassDebugArgs); break
#define ReadAutoStrField(Id, Var)	case Id: Var.Reset(ReadStr(f PassDebugArgs)); break
#define ReadIntField(Id, Var)		case Id: { int32 FieldSize; f >> FieldSize; f >> Var; } break
#define ReadObjField(Id, Var)		case Id: { int32 FieldSize; f >> FieldSize; (Var).Serialize(f, Write); } break
#define ReadDateField(Id, Var, Utc) case Id: { int32 FieldSize; f >> FieldSize; \
										if (Utc) (Var).ToUtc(); (Var).Serialize(f, Write); \
										if (Utc) (Var).ToLocal(); } break

// Misc
#define	VIEW_PULSE_RATE				100

extern const char *MailAddressDelimiters;

#endif
