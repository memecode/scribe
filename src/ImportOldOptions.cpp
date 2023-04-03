#include "Scribe.h"
#include "../Resources/resdefs.h"

char OldOptionsFileName[] = "scribe.r";
char AltOptionsFileName[] = "scribe.ini";

#define OLD_UserName				"UserName"
#define OLD_EmailAddr				"EmailAddr"
#define OLD_ReplyToEmail			"ReplyToEmail"
#define OLD_SmtpServer				"SmtpServer"
#define OLD_SmtpDomain				"SmtpDomain"
#define OLD_SmtpName				"SmtpName"
#define OLD_SmtpAuth				"SmtpAuth"
#define OLD_SmtpAuthType			"SmtpAuthType"
#define OLD_SmtpSSL					"SmtpSSL"
#define	OLD_Pop3Type				"Pop3Type"
#define	OLD_Pop3Protocol			"Pop3Protocol"
#define OLD_Pop3Server				"Pop3Server"
#define OLD_Pop3Name				"Pop3Name"
#define OLD_Pop3AutoReceive			"Pop3AutoReceive"
#define OLD_Pop3CheckEvery			"Pop3CheckEvery"
#define OLD_Pop3LeaveOnServer		"Pop3LeaveOnServer"
#define OLD_Pop3Folder				"Pop3Folder"
#define OLD_Pop3SSL					"Pop3SSL"
#define OLD_ReceiveSpamIds			"SpamIds"
#define OLD_ReceiveAuthType			"RecAuthType"
#define OLD_QuoteReply				"ReplyQuote"
#define OLD_QuoteReplyStr			"ReplyQuoteStr"
#define OLD_ReplyWithSig			"ReplyWithSig"
#define OLD_LogFile					"LogFile"
#define OLD_LogFormat				"LogFmt"
#define OLD_MinimizeToTray			"MinToTray"
#define OLD_SplitterPos				"SplitterPos"
#define OLD_SubSplitPos				"SubSplitPos"
#define OLD_WordWrap				"WordWrap"
#define OLD_CheckForDialUp			"CheckDialUp"
#define OLD_EditorFont				"EditorFont"
#define OLD_HtmlFont				"HtmlFont"
#define OLD_WrapAtColumn			"WrapAtColumn"
#define OLD_SoftwareKey				"SoftwareKey"
#define OLD_NewMailSoundFile		"NewMailSound"
#define OLD_CheckDefaultEmail		"DefMailer"
#define OLD_UseSocks				"UseSocks5"
#define OLD_Socks5Server			"Socks5Server"
#define OLD_Socks5UserName			"Socks5Name"
#define OLD_Pop3OnStart				"Pop3OnStart"
#define OLD_Pop3DefAction			"Pop3DefAction"
#define OLD_RecipientFromClipboard	"ClipRecip"
#define OLD_GridLines				"GridLines"
#define OLD_PreviewLines			"PreviewLines"
#define OLD_MaxEmailSize			"MaxEmailSize"
#define OLD_ToolbarText				"ToolText"
#define OLD_WindowState				"WndState"
#define OLD_DefCodePage				"DefCodePage"
#define OLD_ConfirmDelete			"ConfirmDel"
#define OLD_AccountName				"AccName"
#define OLD_AccountDisabled			"AccDisable"
#define OLD_AccIdent				"AccId"
#define OLD_DefaultSendAccount		"DefSend"
#define OLD_AccIdentName			"AccIdent"
#define OLD_AccIdentKey				"AccKey"
#define OLD_AccIdentEmail			"AccEmail"
#define OLD_AccIdentReply			"AccReply"
#define OLD_NewMailNotify			"NewMailNotify"
#define OLD_CurrentIdentity			"CurId"
#define OLD_DefaultAlternative		"DefAlt"
#define OLD_BoldUnread				"BdUnread"
#define OLD_DateFormat				"DateFormat"
#define OLD_UiLanguage				"UiLang"
#define OLD_UiFontSize				"UiFontSize"
#define OLD_CreateFoldersIfMissing	"CreateIfMissing"
#define OLD_LocalCalendarColour		"LocCalCol"
#define OLD_HttpProxy				"HttpPr"
#define OLD_PersonalFolders			"Folders"
#define OLD_GlyphSub				"GlyphSub"
#define OLD_WorkOffline				"Offline"
#define OLD_SendCharset1			"PrefCs1"
#define OLD_SendCharset2			"PrefCs2"
#define OLD_Receive8BitCs			"Rec8bCs"
#define OLD_ReceiveAsciiCs			"RecAsciiCs"
#define OLD_DebugTrace				"DbgTrc"
#define OLD_AccPermRead				"AccPermRd"
#define OLD_AccPermWrite			"AccPermWr"
#define OLD_BayesFilterMode			"BayesMode"
#define OLD_BayesMoveTo				"BayesMvTo"
#define OLD_BayesUserWhiteList		"BayesWhiteLst"
#define OLD_BayesHam				"BayesHam"
#define OLD_BayesSpam				"BayesSpam"
#define OLD_BayesFalsePositives		"BayesFalsePos"
#define OLD_BayesFalseNegitives		"BayesFalseNeg"
#define OLD_BayesThreshold			"BayesThres"
#define OLD_BayesIncremental		"BayesInc"
#define OLD_BayesDebug				"BayesDbg"
#define OLD_PrintSettings			"PrintOpts"
#define OLD_AutoDeleteExe			"AutoDelExe"
#define OLD_MarkReadAfterPreview	"PreRead"
#define OLD_MarkReadAfterSeconds	"PreReadSec"
#define OLD_ExtraHeaders			"XtrHeaders"
#define OLD_CalendarViewPos			"CalViewPos"
#define OLD_CalendarViewMode		"CalViewMode"
#define OLD_CalendarViewTodo		"CalViewTodo"
#define OLD_DefaultReplyAllSetting	"DefReplayAll"
#define OLD_OnlySendThroughThis		"OnlySendThis"
#define OLD_HideId					"HideId"
#define OLD_LayoutMode				"Layout"
#define OLD_BlinkNewMail			"Blink"
#define OLD_HtmlLoadImages			"HtmlImgs"
#define OLD_SendHotFolder			"SndHotFolder"
#define OLD_ReceiveHotFolder		"RecHotFolder"
#define OLD_StartInFolder			"StartFdr"
#define OLD_OutlookImportSrc		"OiSrc"
#define OLD_OutlookImportDst		"OiDst"
#define OLD_OutlookImportAll		"OiAll"
#define OLD_OutlookExportSrc		"OutlookExpFld"
#define OLD_OutlookExportDst		"OeDst"
#define OLD_OutlookExportAll		"OeAll"
#define OLD_OutlookExportExclude	"OeExc"
#define OLD_DisableUserFilters		"DisFilt"
#define OLD_ScribeExpSrcPaths		"ExpSrcPaths"
#define OLD_ScribeExpDstPath		"ExpDstPath"
#define OLD_ScribeExpFolders		"ExpFlds"
#define OLD_ScribeExpAll			"ExpAll"
#define OLD_ScribeExpExclude		"ExpExc"
#define OLD_Signature				"Sig"
#define OLD_ReplyFormat				"ReplyFmt"
#define OLD_ForwardFormat			"FwdFmt"
#define OLD_AccIdentSignature		"AccSignature"

struct OptMap
{
	const char *New;
	const char *Old;
};

static OptMap OptsGeneral[] = {
	{OPT_UserName, OLD_UserName},
	{OPT_ReceiveSpamIds, OLD_ReceiveSpamIds},
	{OPT_QuoteReply, OLD_QuoteReply},
	{OPT_QuoteReplyStr, OLD_QuoteReplyStr},
	{OPT_ReplyWithSig, OLD_ReplyWithSig},
	{OPT_LogFile, OLD_LogFile},
	{OPT_LogFormat, OLD_LogFormat},
	{OPT_MinimizeToTray, OLD_MinimizeToTray},
	{OPT_SplitterPos, OLD_SplitterPos},
	{OPT_SubSplitPos, OLD_SubSplitPos},
	{OPT_WordWrap, OLD_WordWrap},
	{OPT_CheckForDialUp, OLD_CheckForDialUp},
	{OPT_EditorFont, OLD_EditorFont},
	{OPT_HtmlFont, OLD_HtmlFont},
	{OPT_WrapAtColumn, OLD_WrapAtColumn},
	{OPT_SoftwareKey, OLD_SoftwareKey},
	{OPT_NewMailSoundFile, OLD_NewMailSoundFile},
	{OPT_CheckDefaultEmail, OLD_CheckDefaultEmail},
	{OPT_UseSocks, OLD_UseSocks},
	{OPT_Socks5Server, OLD_Socks5Server},
	{OPT_Socks5UserName, OLD_Socks5UserName},
	{OPT_Pop3OnStart, OLD_Pop3OnStart},
	{OPT_Pop3DefAction, OLD_Pop3DefAction},
	{OPT_RecipientFromClipboard, OLD_RecipientFromClipboard},
	{OPT_GridLines, OLD_GridLines},
	{OPT_PreviewLines, OLD_PreviewLines},
	{OPT_ToolbarText, OLD_ToolbarText},
	{OPT_DefCodePage, OLD_DefCodePage},
	{OPT_ConfirmDelete, OLD_ConfirmDelete},
	{OPT_DefaultSendAccount, OLD_DefaultSendAccount},
	{OPT_NewMailNotify, OLD_NewMailNotify},
	{OPT_CurrentIdentity, OLD_CurrentIdentity},
	{OPT_DefaultAlternative, OLD_DefaultAlternative},
	{OPT_BoldUnread, OLD_BoldUnread},
	{OPT_DateFormat, OLD_DateFormat},
	{OPT_UiLanguage, OLD_UiLanguage},
	{OPT_UiFontSize, OLD_UiFontSize},
	{OPT_CreateFoldersIfMissing, OLD_CreateFoldersIfMissing},
	{OPT_LocalCalendarColour, OLD_LocalCalendarColour},
	{OPT_HttpProxy, OLD_HttpProxy},
	{OPT_PersonalFolders, OLD_PersonalFolders},
	{OPT_GlyphSub, OLD_GlyphSub},
	{OPT_WorkOffline, OLD_WorkOffline},
	{OPT_DebugTrace, OLD_DebugTrace},
	{OPT_AccPermRead, OLD_AccPermRead},
	{OPT_AccPermWrite, OLD_AccPermWrite},
	{OPT_BayesFilterMode, OLD_BayesFilterMode},
	{OPT_BayesMoveTo, OLD_BayesMoveTo},
	{OPT_BayesUserWhiteList, OLD_BayesUserWhiteList},
	{OPT_BayesHam, OLD_BayesHam},
	{OPT_BayesSpam, OLD_BayesSpam},
	{OPT_BayesFalsePositives, OLD_BayesFalsePositives},
	{OPT_BayesFalseNegitives, OLD_BayesFalseNegitives},
	{OPT_BayesThreshold, OLD_BayesThreshold},
	{OPT_BayesIncremental, OLD_BayesIncremental},
	{OPT_BayesDebug, OLD_BayesDebug},
	// {OPT_PrintSettings, OLD_PrintSettings},
	{OPT_AutoDeleteExe, OLD_AutoDeleteExe},
	{OPT_MarkReadAfterPreview, OLD_MarkReadAfterPreview},
	{OPT_MarkReadAfterSeconds, OLD_MarkReadAfterSeconds},
	{OPT_ExtraHeaders, OLD_ExtraHeaders},
	{OPT_CalendarViewPos, OLD_CalendarViewPos},
	{OPT_CalendarViewMode, OLD_CalendarViewMode},
	{OPT_CalendarViewTodo, OLD_CalendarViewTodo},
	{OPT_DefaultReplyAllSetting, OLD_DefaultReplyAllSetting},
	{OPT_HideId, OLD_HideId},
	{OPT_LayoutMode, OLD_LayoutMode},
	{OPT_BlinkNewMail, OLD_BlinkNewMail},
	{OPT_HtmlLoadImages, OLD_HtmlLoadImages},
	{OPT_SendHotFolder, OLD_SendHotFolder},
	{OPT_ReceiveHotFolder, OLD_ReceiveHotFolder},
	{OPT_StartInFolder, OLD_StartInFolder},
	{OPT_OutlookImportSrc, OLD_OutlookImportSrc},
	{OPT_OutlookImportDst, OLD_OutlookImportDst},
	{OPT_OutlookImportAll, OLD_OutlookImportAll},
	{OPT_OutlookExportSrc, OLD_OutlookExportSrc},
	{OPT_OutlookExportDst, OLD_OutlookExportDst},
	{OPT_OutlookExportAll, OLD_OutlookExportAll},
	{OPT_OutlookExportExclude, OLD_OutlookExportExclude},
	{OPT_DisableUserFilters, OLD_DisableUserFilters},
	{OPT_ScribeExpSrcPaths, OLD_ScribeExpSrcPaths},
	{OPT_ScribeExpDstPath, OLD_ScribeExpDstPath},
	{OPT_ScribeExpFolders, OLD_ScribeExpFolders},
	{OPT_ScribeExpAll, OLD_ScribeExpAll},
	{OPT_ScribeExpExclude, OLD_ScribeExpExclude},
	// {OPT_Signature, OLD_Signature},
	{OPT_TextReplyFormat, OLD_ReplyFormat},
	{OPT_TextForwardFormat, OLD_ForwardFormat},
	{0, 0}
};

static OptMap OptsAccount[] = {
	{OPT_SmtpServer, OLD_SmtpServer},
	{OPT_SmtpDomain, OLD_SmtpDomain},
	{OPT_SmtpName, OLD_SmtpName},
	{OPT_SmtpAuth, OLD_SmtpAuth},
	{OPT_SmtpAuthType, OLD_SmtpAuthType},
	{OPT_SmtpSSL, OLD_SmtpSSL},
	{OPT_SendCharset1, OLD_SendCharset1},
	{OPT_SendCharset2, OLD_SendCharset2},
	{OPT_Receive8BitCs, OLD_Receive8BitCs},
	{OPT_ReceiveAsciiCs, OLD_ReceiveAsciiCs},
	{OPT_ReceiveAuthType, OLD_ReceiveAuthType},
	{OPT_Pop3Type, OLD_Pop3Type},
	{OPT_Pop3Protocol, OLD_Pop3Protocol},
	{OPT_Pop3Server, OLD_Pop3Server},
	{OPT_Pop3Name, OLD_Pop3Name},
	{OPT_Pop3AutoReceive, OLD_Pop3AutoReceive},
	{OPT_Pop3CheckEvery, OLD_Pop3CheckEvery},
	{OPT_Pop3LeaveOnServer, OLD_Pop3LeaveOnServer},
	{OPT_Pop3Folder, OLD_Pop3Folder},
	{OPT_Pop3SSL, OLD_Pop3SSL},
	{OPT_MaxEmailSize, OLD_MaxEmailSize},
	{OPT_OnlySendThroughThis, OLD_OnlySendThroughThis},
	{OPT_AccIdentName, OLD_AccIdentName},
	{OPT_AccIdentEmail, OLD_AccIdentEmail},
	{OPT_AccIdentReply, OLD_AccIdentReply},
	{OPT_AccIdentTextSig, OLD_AccIdentSignature},
	{OPT_AccountUID, OLD_AccIdent},
	{OPT_AccountName, OLD_AccountName},
	{OPT_AccountDisabled, OLD_AccountDisabled},
	{0, 0}
};

OptMap *FindMap(OptMap *Maps, char *Old)
{
	for (OptMap *o = Maps; o->New; o++)
	{
		if (_stricmp(Old, o->Old) == 0)
		{
			return o;
		}
	}

	return 0;
}

LOptionsFile *DoImportSettings(ScribeWnd *App, char *In)
{
	LOptionsFile *Options = 0;
	char *Ext = LGetExtension(In);
	if (Ext)
	{
		bool IsText = _stricmp(Ext, "ini") == 0;
		ObjProperties Props;
		
		LFile f;
		if (f.Open(In, O_READ))
		{
			if (IsText)
			{
				Props.SerializeText(f, false);
			}
			else
			{
				Props.Serialize(f, false);
			}

			Options = App->GetOptions(true);
			if (Options)
			{
				char s[256];
				LVariant v;
				for (bool b=Props.FirstKey(); b; b=Props.NextKey())
				{
					char *NewKey = 0;
					char *Key = Props.KeyName();
					if (Key)
					{
						int Acc = 0;
						if (IsDigit(*Key))
						{
							// Account?
							Acc = atoi(Key);
							Key = strchr(Key, '.');
							if (Key)
							{
								Key++;

								ProcessAccountProp:
								OptMap *m = FindMap(OptsAccount, Key);
								if (m)
								{
									sprintf_s(NewKey = s, sizeof(s), "Accounts.Account-%i.%s", Acc, m->New);
								}
								else if (_stricmp(Key, "AccPlugins") == 0 ||
										 _stricmp(Key, "SendPlugins") == 0 ||
										 _stricmp(Key, "SmtpEPw") == 0 ||
										 _stricmp(Key, "Pop3EPw") == 0)
								{
									// Do nothing....
								}
								else if (_stricmp(Key, "SpamIds") == 0)
								{
								}
								else if (_strnicmp(Key, "Msg.", 4) == 0)
								{
									Prop *p = Props.GetProp();
									if (p && p->Type == OBJ_STRING)
									{
										sprintf_s(s, sizeof(s), "Accounts.Account-%i.Messages", Acc);
										LXmlTag *Msgs = Options->LockTag(s, __FILE__, __LINE__);
										if (!Msgs)
										{
											Options->CreateTag(s);
											Msgs = Options->LockTag(s, __FILE__, __LINE__);
										}
										if (Msgs)
										{
											LXmlTag *Msg = new LXmlTag("Message");
											if (Msg)
											{
												Msgs->InsertTag(Msg);
												Msg->Content = NewStr(p->Value.Cp);
											}
											Options->Unlock();
										}
									}
								}
							}
						}
						else
						{
							OptMap *m = FindMap(OptsAccount, Key);
							if (m)
							{
								goto ProcessAccountProp;
							}
							else
							{
								m = FindMap(OptsGeneral, Key);
								if (m)
								{
									strcpy_s(NewKey = s, sizeof(s), m->New);
								}
								else if (_strnicmp(Key, "Folder-", 7) == 0 ||
										 _stricmp(Key, "Win32-Folders") == 0)
								{
									strcpy_s(NewKey = s, sizeof(s), Key);
								}
							}
						}
					}

					if (NewKey)
					{
						Prop *p = Props.GetProp();
						if (p)
						{
							char *Dot = strrchr(NewKey, '.');
							if (Dot)
							{
								*Dot = 0;
								Options->CreateTag(NewKey);
								*Dot = '.';
							}

							switch (p->Type)
							{
								case OBJ_INT:
									Options->SetValue(NewKey, v = p->Value.Int);
									break;
								case OBJ_FLOAT:
									Options->SetValue(NewKey, v = p->Value.Dbl);
									break;
								case OBJ_STRING:
									Options->SetValue(NewKey, v = p->Value.Cp);
									break;
								case OBJ_BINARY:
								{
									v.SetBinary(p->Size, p->Value.Cp);
									if (!Options->SetValue(NewKey, v))
									{
									}
									break;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			LgiMsg(App, LLoadString(IDS_ERROR_CANT_READ), AppName, MB_OK, In);
		}
	}
	else
	{
		LgiMsg(App, "'%s' has no extension.", AppName);
	}

	return Options;
}

LAutoString FindOldOptionsFile()
{
	LAutoString Old;
	
	Old.Reset(LgiFindFile(OldOptionsFileName));

	if (!FileExists(Old))
		Old.Reset(LgiFindFile(AltOptionsFileName));

	return Old;
}

bool ImportOptions(ScribeWnd *App, char *File)
{
	if (File)
	{
		if (LgiMsg(App, LLoadString(IDS_IMPORT_SETTINGS_Q), AppName, MB_YESNO, File) == IDYES)
		{
			DoImportSettings(App, File);
			LgiMsg(App, LLoadString(IDS_IMPORT_SETTINGS_DONE), AppName);
			return true;
		}
	}

	return false;
}