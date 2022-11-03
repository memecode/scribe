/*
 *      FILE:           SpellChecker.cpp
 *      AUTHOR:         Matthew Allen
 *      DATE:           11/12/2009
 *      DESCRIPTION:    Scribe Spell Checker Plugin
 *
 *      Copyright (C) 2009, Matthew Allen
 *              fret@memecode.com


Spell checker overview:
- ScribeWnd::GetSpellThread is called to create a SpellThread
- MailTextView uses that thread to SpellThread::Put a request to spell check
- This is queued in SpellThread::In
- SpellThread::Main picks that up and tokenises it
- Then calls into ScribePlugin_SpellChecker::CheckWord (which it owns)

Dictionary installation overview:
- Aspell_SpellChecker::CheckWord detects missing dictionary (err contains 'No word lists can be found')
- Calls Aspell::InstallDictionary to do install
*/


#include <stdio.h>
#include <ctype.h>

#include "aspell.h"

#include "lgi/common/Lgi.h"
#include "lgi/common/Store3Defs.h"
#include "lgi/common/LibraryUtils.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/List.h"
#include "lgi/common/Variant.h"
#include "lgi/common/Http.h"
#include "lgi/common/Ftp.h"
#include "lgi/common/StringClass.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/TarFile.h"
#include "lgi/common/OptionsFile.h"
#include "resdefs.h"
#include "lgi/common/SpellCheck.h"

#if _MSC_VER
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "ScribeSpellCheck.h"
#include "ScribeDefs.h"
#include "lgi/common/Db.h"

#include "../../libs/bzip2-1.0.6/bzlib.h"

static char AspellDelim[] =
{
	' ', '\t', '\r', '\n', ',', ',', '.', ':', ';',
	'{', '}', '[', ']', '!', '@', '#', '$', '%', '^', '&', '*',
	'(', ')', '_', '-', '+', '=', '|', '\\', '/', '?', '\"',
	0
};

static char AspellUrlDelim[] = " \r\t\n,";

class LBzip2File : public LFile
{
	int BufLen;
	LAutoString Buf;
	bz_stream Bz;
	bool Decomp;
	bool Openned;
	int Result;

public:
	LBzip2File(int buflen = 64 << 10)
	{
		ZeroObj(Bz);
		BufLen = buflen;
		Buf.Reset(new char[BufLen]);
		Decomp = false;
		Result = BZ_PARAM_ERROR;
		Openned = false;
	}

	int GetResult()
	{
		return Result;
	}
	
	int Open(const char *Name, int Attrib)
	{
		if (!LFile::Open(Name, Attrib))
			return false;

		Decomp = Attrib == O_READ;
		if (Decomp)
			Result = BZ2_bzDecompressInit(&Bz, 0/*verbosity*/, 0/*small*/);
		else
			Result = BZ2_bzCompressInit(&Bz, 7/*block size*/, 0/*verbosity*/, 0/*default workfactor*/);
		if (Result < BZ_OK)
			return false;
		
		Openned = true;
		return true;
	}
	
	int Close()
	{
		if (Openned)
		{
			if (Decomp)
				BZ2_bzDecompressEnd(&Bz);
			else
				BZ2_bzCompressEnd(&Bz);
			Openned = false;
		}
		
		return LFile::Close();
	}
	
	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0)
	{
		if (Result == BZ_STREAM_END)
			return 0;

		if (!Buffer || Size <= 0 || !Buf)
		{
			LAssert(!"Bad parameter.");
			return 0;
		}
		if (!Decomp)
		{
			LAssert(!"Can't read from a compression stream.");
			return 0;
		}

		ssize_t Rd = 0, Wr = 0;
		do
		{
			if (Bz.avail_in > 0)
			{
				// Shift data down to the bottom of the buffer.
				memmove(Buf, Bz.next_in, Bz.avail_in);
			}
			
			Rd = LFile::Read(Buf + Bz.avail_in, BufLen - Bz.avail_in);
			// LgiTrace("Src.Read next_in=%i avail_in=%i Rd=%i\n", Bz.next_in?Bz.next_in-Buf:-1, Bz.avail_in, Rd);
			
			Bz.next_in = Buf;
			Bz.avail_in += (unsigned int)Rd;
			Bz.next_out = (char*)Buffer;
			Bz.avail_out = (int)Size;
			
			// LgiTrace("\tBZ2_bzDecompress avail_in=%i, avail_out=%i\n", Bz.avail_in, Bz.avail_out);
			Result = BZ2_bzDecompress(&Bz);
			Wr = Size - Bz.avail_out;
			// LgiTrace("\tResult=%i avail_in=%i, avail_out=%i, Wr=%i\n", Result, Bz.avail_in, Bz.avail_out, Wr);

			if (Result != BZ_OK)
			{
				if (Result < 0)
				{
					LgiTrace("%s:%i - BZ2_bzDecompress failed with %i\n", _FL, Result);
					return -1;
				}
				break;
			}
		}
		while (Rd > 0 && Wr == 0);
		
		return Wr;
	}
	
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		return -1;
	}
};

#define PLUGIN_VER				60

#define IDM_SPELL_CHECK			300
#define IDM_SETUP				301

const char *UrlDelim					= "!~/:%+-?@&$#._=,";

///////////////////////////////////////////////////////////////////////
char *PrepPath(char *p)
{
	static char Buf[MAX_PATH_LEN];

	strcpy_s(Buf, sizeof(Buf), p);
	
	char *s = Buf;
	while ((s = strchr(s, '\\')))
	{
		*s = '/';
	}

	return Buf;
}

///////////////////////////////////////////////////////////////////////
#ifndef WIN32
char *GetAspellPath(char *Path)
{
	char Cmd[256];
	sprintf_s(Cmd, sizeof(Cmd), "dump config %s", Path);

	LStringPipe Out;
	LSubProcess p("aspell", Cmd);
	if (p.Start())
	{
		p.Communicate(&Out);
		return Out.NewStr();
	}
	
	return 0;	
}
#endif

///////////////////////////////////////////////////////////////////////
// Class
bool WarnMissingAspell = true;

class ASpell : public LLibrary, public LTarParser
{
protected:
	struct LanguageEntry
	{
		char *Lang;
		char *EnglishName;
		char *NativeName;
		char *File;
	};
	LAutoPtr<LDb> LanguageDb;
	LArray<LanguageEntry> Languages;

	LAutoPtr<LSpellCheck::Params> SpellParams;
	int InstallAttempt;
	LViewI *Wnd;
	LString Language, Dictionary; // e.g. "en"
	AspellConfig *SpellConfig;
	bool PreSendCheck;
	LString AspellPath, DataDir, DictBasePath, DictLangPath;

	bool CheckCreateFolder(const char *Dir)
	{
		if (LDirExists(Dir))
			return true;
		if (FileDev->CreateFolder(Dir))
			return true;
		LgiTrace("%s:%i - ASpell failed to create '%s'\n", _FL, Dir);
		return false;
	}
	
	LanguageEntry *GetLangEntry(const char *Lang, const char *Name)
	{
		LString Lng = Lang;
		
		for (unsigned i=0; i<Languages.Length(); i++)
		{
			LanguageEntry &le = Languages[i];
			if (Lang &&
				!_stricmp(le.Lang, Lang))
				return &le;
			
			if (Name &&
				le.EnglishName &&
				!_stricmp(le.EnglishName, Name))
				return &le;

			if (Name &&
				le.NativeName &&
				!_stricmp(le.NativeName, Name))
				return &le;
		}
		return NULL;
	}

	bool Process(GTarFile &f, LStreamI *s, bool &SkipOverFile)
	{
		LString::Array p = f.Dir.SplitDelimit("\\/");
		if (p.Length() > 1)
		{
			p.DeleteAt(0, true);
			LString sep(DIR_STR);
			f.Dir = sep.Join(p);
		}	
		else
		{
			f.Dir.Empty();
		}
	
		return LTarParser::Process(f, s, SkipOverFile);
	}

public:
	ASpell() :
        LLibrary
        (
	    	#if defined(LINUX) || defined(MAC)
            "lib"
            #endif
            "aspell-dist-0.60"
            #ifdef _DEBUG
            "d"
            #endif
        )
	{
		Wnd = NULL;
		SpellConfig = NULL;
		PreSendCheck = false;
		InstallAttempt = 0;
	}

	~ASpell()
	{
		if (SpellConfig)
			delete_aspell_config(SpellConfig);
			
		LgiTrace("%s:%i - %p::~ASpell(), SpellParams:%p\n", _FL, this, SpellParams.Get());
	}

	virtual void OnMissingDictionary() {}

	void OnInit()
	{
		if (!SpellParams)
		{
			LAssert(!"No params.");
			return;
		}
		
		// Get the root folder for aspell data files...
		char Dir[MAX_PATH_LEN];
		if (SpellParams->OptionsPath)
			LMakePath(Dir, sizeof(Dir), SpellParams->OptionsPath, "..");
		else
			LGetSystemPath(SpellParams->IsPortable ? LSP_APP_INSTALL : LSP_APP_ROOT, Dir, sizeof(Dir));
		LMakePath(Dir, sizeof(Dir), Dir, "Aspell");
		AspellPath = Dir;
		CheckCreateFolder(AspellPath);		
		// LgiTrace("Aspell.Path='%s'\n", AspellPath.Get());
		
		// Find and load the CSV file with all the languages...
		const char *CsvFile = "aspell-languages.csv";
		LString LanguagesCsv;
		LFile::Path CsvPath(LSP_APP_INSTALL);
		CsvPath += "Aspell";
		CsvPath += CsvFile;
		if (LFileExists(CsvPath))
			LanguagesCsv = CsvPath.GetFull();
		else
			LanguagesCsv = LFindFile(CsvFile);
		if (LanguagesCsv)
		{
			if (LanguageDb.Reset(OpenCsvDatabase(LanguagesCsv, false)))
			{
				LDbRecordset *t = LanguageDb->TableAt(0);
				if (t)
				{
					LDbRecordset &tbl = *t;
					for (bool b = tbl.MoveFirst(); b; b = tbl.MoveNext())
					{
						LanguageEntry &e = Languages.New();
						e.Lang = tbl[0U];
						e.EnglishName = tbl[1];
						e.NativeName = tbl[2];
						e.File = tbl[3];
					}
					
					// LgiTrace("Aspell.LanguagesLoaded=%i\n", Languages.Length());
				}
				else LgiTrace("%s:%i - Error: no table in 'aspell-languages.csv'.\n", _FL);
			}			
			else LgiTrace("%s:%i - Error: failed to load 'aspell-languages.csv'.\n", _FL);
		}
		else LgiTrace("%s:%i - Error: failed to find 'aspell-languages.csv'.\n", _FL);
		
		if (!IsLoaded())
		{
			LFile::Path p(LSP_EXE);
			#if defined(WINDOWS)
			p += "..";
			#endif
			#if defined(_DEBUG)
			p += "..\\..\\libs\\aspell-0.60.6.1";
			#endif
			#if defined(WINDOWS)
			    p += "win32\\dist";
				#ifdef _DEBUG
					#ifdef WIN64
					p += "x64Debug14";
					#else
					p += "Win32Debug";
					#endif
				#else
					#ifdef WIN64
					p += "x64Release14";
					#else
					p += "Win32Release";
					#endif
				#endif
    			p += "aspell-dist-0.60." LGI_LIBRARY_EXT;
			#elif defined(LINUX)
				#ifdef _DEBUG
					p += "linux/Debug";
				#endif
    			p += "libaspell-dist-060"
	    			#ifdef _DEBUG
					"d"
    				#endif
    			    "." LGI_LIBRARY_EXT;
			#endif
			if (!Load(p))
				LgiTrace("%s:%i - Failed to load aspell: %s\n", _FL, p.GetFull().Get());
		}
		
		if (IsLoaded())
			LgiTrace("%s:%i - Aspell: %s\n", _FL, GetFullPath().Get());
		
		if (Language)
			Init(Language, Dictionary);
	}

	DynFunc3(int, aspell_config_replace, struct AspellConfig *, ths, const char *, key, const char *, value);
	DynFunc2(int, aspell_config_remove, struct AspellConfig *, ths, const char *, key);
	DynFunc0(struct AspellConfig *, new_aspell_config);
	DynFunc1(int, aspell_clear_all_lists, struct AspellConfig *, config);
	DynFunc1(int, delete_aspell_config, struct AspellConfig *, cfg);
	DynFunc1(int, delete_aspell_dict_info_enumeration, struct AspellDictInfoEnumeration *, ths);
	DynFunc3(int, aspell_speller_check, struct AspellSpeller *, ths, const char *, word, int, word_size);
	DynFunc1(struct AspellSpeller *, to_aspell_speller, struct AspellCanHaveError *, obj);
	DynFunc1(const char *, aspell_error_message, const struct AspellCanHaveError *, ths);
	DynFunc1(unsigned int, aspell_error_number, const struct AspellCanHaveError *, ths);
	DynFunc1(struct AspellCanHaveError *, new_aspell_speller, struct AspellConfig *, config);
	DynFunc1(const char *, aspell_string_enumeration_next, struct AspellStringEnumeration *, ths);
	DynFunc1(struct AspellStringEnumeration *, aspell_word_list_elements, const struct AspellWordList *, ths);
	DynFunc3(const struct AspellWordList *, aspell_speller_suggest, struct AspellSpeller *, ths, const char *, word, int, word_size);
	DynFunc2(const char *, aspell_config_retrieve, struct AspellConfig *, ths, const char *, key);
	DynFunc1(struct AspellDictInfoList *, get_aspell_dict_info_list, struct AspellConfig *, config);
	DynFunc1(struct AspellDictInfoEnumeration *, aspell_dict_info_list_elements, const struct AspellDictInfoList *, ths);
	DynFunc1(const struct AspellDictInfo *, aspell_dict_info_enumeration_next, struct AspellDictInfoEnumeration *, ths);
	DynFunc3(int, aspell_speller_add_to_personal, struct AspellSpeller *, ths, const char *, word, int, word_size);
	DynFunc1(int, delete_aspell_speller, struct AspellSpeller *, ths);
	DynFunc1(int, aspell_speller_save_all_word_lists, struct AspellSpeller *, ths);
	DynFunc4(int, aspell_create_ro_master, void*, config, const char *, InFile, char *, ErrOut, int, ErrLen);
	DynFunc5(int, aspell_prezip, const char*, InFile, const char *, OutFile, bool, Decomp, char *, ErrOut, int, ErrLen);

	bool IsOk()
	{
		return IsLoaded() && SpellConfig != NULL;
	}
	
	bool SetupPaths()
	{
		LFile::Path AppInstall(LSP_APP_INSTALL);
		LFile::Path Data = AppInstall;
		Data += "Aspell/data";
		DataDir = Data.GetFull();
		bool DataPopulated = false;

		if (!Data.IsFolder())
		{
			Data = AspellPath;
			Data += "data";
			DataDir = Data.GetFull();
		}

		{
			LFile::Path s = DataDir;
			s += "iso-8859-1.cmap";
			DataPopulated = s.Exists();
			if (!DataPopulated)
				LgiTrace("%s:%i - Aspell data dir not populated.\n", _FL);
		}

		bool CopyError = false;
		if (!DataPopulated)
		{
			LString::Array Sources;

			{	// Check runtime install location
				LFile::Path s = AppInstall;
				#ifdef MAC
				s = LGetExeFile();
				s += "Contents/Resources";
				#endif
				s += "Aspell/data";
				if (s.Exists())
					Sources.Add(s.GetFull());
			}
			{	// Check debug time install location
				LFile::Path s = AppInstall;
				s += "../../libs/aspell-0.60.6.1/data";
				if (s.Exists())
					Sources.Add(s.GetFull());
			}
			
			if (Sources.Length() == 0)
				LAssert(!"Failed to find data source folder.");

			for (auto Src: Sources)
			{
				if (!Src.Equals(Data.GetFull()))
				{
					// Try and copy over files from the install folder...
					if (!Data.IsFolder())
						FileDev->CreateFolder(Data);

					LDirectory d;
					for (int b = d.First(Src); b; b = d.Next())
					{
						if (!d.IsDir())
						{
							LFile::Path In = Src, Out = Data;
							In += d.GetName();
							Out += d.GetName();
							if (FileDev->Copy(In.GetFull(), Out.GetFull()))
							{
								LgiTrace("%s:%i - Copied in '%s'\n", _FL, Out.GetFull().Get());
							}
							else
							{
								CopyError = true;
								break;
							}
						}
					}
				}
			}
		}
	
		if (CopyError)
		{
			LgiTrace("%s:%i - Error: Aspell data folder '%s' doesn't exist.\n", _FL, DataDir.Get());
			return false;
		}

		if (!aspell_config_replace(		SpellConfig,
										"data-dir",
										PrepPath(DataDir)
										))
		{
			LgiTrace("%s:%i - Error: Couldn't set Aspell data path '%s'", _FL, DataDir.Get());
			return false;
		}
	    // LgiTrace("%s:%i - Aspell: DataPath='%s'.\n", _FL, DataDir.Get());

		LFile::Path DictDir(AspellPath);
		DictDir += "dict";
		DictBasePath = DictDir.GetFull();
		CheckCreateFolder(DictBasePath);

		if (!Language)
		{
			LgiTrace("%s:%i - Error: No dictionary specified.", _FL);
			return false;
		}

		DictDir += Language;
		DictLangPath = DictDir.GetFull();
		
		if (!LDirExists(DictLangPath))
		{
			OnMissingDictionary();
			return false;
		}
		
		if (!aspell_config_replace(		SpellConfig,
										"dict-dir",
										PrepPath(DictLangPath)
										))
		{
			LgiTrace("%s:%i - Error: Couldn't set Aspell dictionary path '%s'", _FL, DictLangPath.Get());
			return false;
		}
	    // LgiTrace("%s:%i - Aspell: DictPath='%s'.\n", _FL, DictLangPath.Get());

		aspell_config_replace(SpellConfig, "local-data-dir", PrepPath(DictLangPath));

		if (!aspell_config_replace(		SpellConfig,
										"home-dir",
										PrepPath(AspellPath)
										))
		{
			LgiTrace("%s:%i - Error: Couldn't set Aspell home-dir to '%s'", _FL, AspellPath.Get());
			return false;
		}
		
		return true;
	}
	
	bool DumpSettings()
	{
		if (!SpellConfig)
			return false;

		LString sLang = aspell_config_retrieve(SpellConfig, "lang");
		LString sEnc = aspell_config_retrieve(SpellConfig, "encoding");
		LString sMaster = aspell_config_retrieve(SpellConfig, "master");
		LString sDataDir = aspell_config_retrieve(SpellConfig, "data-dir");
		LString sDictDir = aspell_config_retrieve(SpellConfig, "dict-dir");
		LString sHomeDir = aspell_config_retrieve(SpellConfig, "home-dir");
		LString sLocalDataDir = aspell_config_retrieve(SpellConfig, "local-data-dir");
		
		LgiTrace("%s:%i - Dump settings:\n"
				"\tLang=%s\n"
				"\tMaster=%s\n"
				"\tDataDir=%s\n"
				"\tDictDir=%s\n"
				"\tHomeDir=%s\n"
				"\tLocalDataDir=%s\n"
				"\tEncoding=%s\n",
				_FL,
				sLang.Get(),
				sMaster.Get(),
				sDataDir.Get(),
				sDictDir.Get(),
				sHomeDir.Get(),
				sLocalDataDir.Get(),
				sEnc.Get());
		
		return true;
	}

	bool Init(const char *Lang, const char *Dict = NULL)
	{
		if (!IsLoaded())
		{
			LgiTrace("%s:%i - Failed to load Aspell library.\n", _FL);
			return false;
		}

		if (!SpellConfig)
		{
			SpellConfig = new_aspell_config();
			if (!SpellConfig)
			{
				LgiTrace("%s:%i - new_aspell_config failed.\n", _FL);
				return false;
			}
		}

		if (!ValidStr(Lang))
		{
		    LgiTrace("%s:%i - Error: No language specified.\n", _FL);
		    return false;
		}

		LanguageEntry *Le = GetLangEntry(NULL, Lang);
		if (!Le)
		{
		    LgiTrace("%s:%i - Error: Unknown language '%s'.\n", _FL, Lang);
    	    return false;
		}

		const char *AspellDict = Dict ? Dict : Le->Lang;
		if (!aspell_config_replace(SpellConfig, "lang", AspellDict))
		{
		    LgiTrace("%s:%i - Error: Failed to set 'lang' to '%s'.\n", _FL, AspellDict);
		    return false;
		}
		Language = Le->Lang;
		Dictionary = Dict;
	    // LgiTrace("%s:%i - Aspell: Init to '%s' (%s).\n", _FL, Le->Lang, AspellDict);

		if (!SetupPaths())
			return false;

		aspell_config_replace(SpellConfig, "encoding", "utf-8");
		
		// LgiTrace("%s:%i - Aspell initialized for language '%s' (Dictionary: %s)\n", _FL, Lang, Dict);
		
		return true;
	}

	void ListDictionaries(List<char> &d)
	{
		if (!IsLoaded())
		{
			LgiTrace("%s:%i - Aspell lib not loaded.\n", _FL);
			return;
		}
		
		// This lists all the INSTALLED dictionaries...
		if (SpellConfig)
		{
			AspellDictInfoList *Dicts = get_aspell_dict_info_list(SpellConfig);
			if (Dicts)
			{
				AspellDictInfoEnumeration *Dlist = aspell_dict_info_list_elements(Dicts);
				if (Dlist)
				{
					const AspellDictInfo *Info;
					
					while ((Info = aspell_dict_info_enumeration_next(Dlist)))
					{
						d.Insert(NewStr((char*)Info->name));
						// LgiTrace("%s:%i - ListDictionaries: %s\n", _FL, Info->name);
					}

					delete_aspell_dict_info_enumeration(Dlist);
				}
				else LgiTrace("%s:%i aspell_dict_info_list_elements failed.\n", _FL);
			}
			else LgiTrace("%s:%i get_aspell_dict_info_list failed.\n", _FL);
		}
		else LgiTrace("%s:%i No SpellConfig.\n", _FL);
	}

	LString CreateReadOnlyMaster(LString InputFile)
	{
		LString Ret;
		
		if (!SpellConfig)
		{
			LgiTrace("%s:%i - No config.\n", _FL);
			return Ret;
		}

		char DictDir[MAX_PATH_LEN];
		LMakePath(DictDir, sizeof(DictDir), DictBasePath, Dictionary);
		if (!aspell_config_replace(		SpellConfig,
										"dict-dir",
										PrepPath(DictDir)))
		{
			LgiTrace("%s:%i - Aspell: Couldn't set dictionary path '%s'", _FL, DictDir);
			return Ret;
		}
		
		char Err[MAX_PATH_LEN] = "";
		if (aspell_create_ro_master(SpellConfig, InputFile, Err, sizeof(Err)))
		{
			OnSuccess:
			LString::Array a = InputFile.RSplit(".", 1);
			Ret = a[0] + ".rws";
		}
		else
		{
			// Try removing the dictionary...
			LanguageEntry *Le = GetLangEntry(Dictionary, NULL);
			if (Le)
			{
				// Change "lang" to the base language
				aspell_config_replace(SpellConfig, "lang", Le->Lang);

				// Retry...
				if (aspell_create_ro_master(SpellConfig, InputFile, Err, sizeof(Err)))
				{
					goto OnSuccess;
				}
			}	
	
			LgiTrace("%s:%i - aspell_create_ro_master failed: %s\n", _FL, Err);
		}				
		
		return Ret;
	}
	
	char *GetLeaf(char *Path)
	{
		if (!Path) return NULL;
		char *l = strrchr(Path, '/');
		return l ? l + 1 : Path;
	}
	
	bool DownloadFile(const char *OutFile, const char *InUrl)
	{
		LUri Url(InUrl);
		LProxyUri Proxy;
		LAutoPtr<LSocketI> Sock(new LSocket);

        LgiTrace("%s:%i - Downloading '%s' to '%s'\n",
            _FL,
            InUrl,
            OutFile);

		bool ValidProxy = !Proxy.sHost.IsEmpty() && Proxy.Port > 0;
		if
		(
			ValidProxy ||
			(
				!Url.sProtocol.IsEmpty() &&
				!Stricmp(Url.sProtocol.Get(), "http")
			)
		)
		{
			LHttp Http;
			if (Proxy.sHost && Proxy.Port)
				Http.SetProxy(Proxy.sHost, Proxy.Port);
			if (!Http.Open(Sock, Url.sHost, Url.Port))
			{
				LgiTrace("%s:%i - Failed to connect to %s:%i.\n", _FL, Url.sHost.Get(), Url.Port);
				return false;
			}
		
			LFile Out;
			if (!Out.Open(OutFile, O_WRITE))
			{
				LgiTrace("%s:%i - Failed to open '%s' for writing\n", _FL, OutFile);
				return false;
			}
			
			Out.SetSize(0);

			int Status = 0;
			LHttp::ContentEncoding Enc;
			if (!Http.Get(InUrl, NULL, &Status, &Out, &Enc, NULL))
			{
				Out.Close();
				FileDev->Delete(OutFile);
				LgiTrace("%s:%i - HTTP failed '%s' with %i\n", _FL, OutFile, Status);
				return false;
			}
		}
		else if (Url.sProtocol && !_stricmp(Url.sProtocol, "ftp"))
		{
			IFtp Ftp;
			FtpOpenStatus Status = Ftp.Open(Sock.Release(), Url.sHost, Url.Port, NULL, NULL);
			if (Status != FO_Connected)
			{
				LgiTrace("%s:%i - FTP connect to '%s:%i' failed\n", _FL, Url.sHost.Get(), Url.Port);
				return false;
			}
			
			LString::Array Parts = Url.sPath.RSplit("/", 1);
			if (Parts.Length() > 1)
			{
				if (!Ftp.SetDir(Parts[0]))
				{
					LgiTrace("%s:%i - Failed to change dir '%s'\n", _FL, Parts[0].Get());
					return false;
				}
			}
			
			LArray<IFtpEntry*> Ls;
			if (!Ftp.ListDir(Ls))
			{
				LgiTrace("%s:%i - Failed to list dir '%s'\n", _FL, InUrl);
				return false;
			}
			
			IFtpEntry *e = NULL;
			for (unsigned i=0; i<Ls.Length(); i++)
			{
				IFtpEntry *entry = Ls[i];
				if (entry->Name && !_stricmp(entry->Name, Parts.Last()))
				{
					e = entry;
					break;
				}
			}
			
			if (!e)
			{
				LgiTrace("%s:%i - FTP file '%s' not found\n", _FL, Parts.Last().Get());
				return false;
			}

			if (!Ftp.DownloadFile(OutFile, e))
			{
				LgiTrace("%s:%i - Failed FTP download of '%s'\n", _FL, Parts.Last().Get());
				return false;
			}
		}
		else
		{
			LgiTrace("%s:%i - Unhandled protocol '%s'\n", _FL, Url.sProtocol.Get());
			return false;
		}
		
		return true;
	}
	
	bool Decompress(LString &File)
	{
	    LgiTrace("%s:%i - Decompressing '%s'\n", _FL, File.Get());
		char *Ext = LGetExtension(File);
		if (Ext)
		{
			if (!_stricmp(Ext, "bz2"))
			{
				LBzip2File In;
				LFile Out;
				if (!In.Open(File, O_READ))
				{
					LgiTrace("%s:%i - Couldn't open '%s' for reading.\n", _FL, File.Get());
					return false;
				}

				LString OutPath = File(0, File.RFind("."));
				if (!Out.Open(OutPath, O_WRITE))
				{
					LgiTrace("%s:%i - Couldn't open '%s' for writing.\n", _FL, OutPath.Get());
					return false;
				}

				// Use zlib's bzip2 functions to decompress the file
				int BufLen = 64 << 10;
				LAutoString Buf(new char[BufLen]);
				ssize_t Rd;
				while ((Rd = In.Read(Buf, BufLen)) > 0)
				{
					Out.Write(Buf, Rd);
				}
				
				In.Close();
				Out.Close();

				if (Rd >= 0)
				{
					FileDev->Delete(File, false);
					File = OutPath;
					return true;
				}
			}
			else if (!_stricmp(Ext, "tar"))
			{
				LFile In;
				if (In.Open(File, O_READ))
				{
					char OutFolder[MAX_PATH_LEN];
					LMakePath(OutFolder, sizeof(OutFolder), File, "..");
					
					if (!Extract(&In, OutFolder))
					{
						LgiTrace("%s:%i - Untar '%s' failed.\n", _FL, File.Get());
						return false;
					}
					
					In.Close();
					FileDev->Delete(File, false);
				}
			}
			else if (!_stricmp(Ext, "cwl"))
			{
				LString Out = File;
				auto Dot = Out.RFind(".");
				if (Dot < 0)
					return false;
				Out = Out(0, Dot) + ".wl";
				
				char Err[256] = "Unspecified aspell_prezip error.";
				int r = aspell_prezip(File, Out, true, Err, sizeof(Err));
				if (r)
				{
					File = Out;
					return true;
				}
				else
				{
					LgiTrace("%s:%i - %s\n", _FL, Err);
				}
			}
		}
		
		return false;
	}	
	
	/*
	
	The process of installing a dictionary:
	- Lookup the dictionary download.
	- Create the output directory.
	- Download the file to output directory and unpack it.
	- Convert all the compressed word lists (.cwl) to readonly word store (.rws):
		${PREZIP} -d < $< | ${ASPELL} ${ASPELL_FLAGS} --lang=en create master ./$@
	
	*/
	bool InstallDictionary(const char *Lang)
	{
		if (InstallAttempt > 2)
		{
			LgiTrace("%s:%i - Error: InstallAttemp=%i\n", _FL, InstallAttempt);
			return false;
		}
		
		InstallAttempt++;
        LgiTrace("%s:%i - Dictionary for '%s' install attempt %i\n", _FL, Lang, InstallAttempt);

		// Setup the root dictionary path
		LanguageEntry *Le = GetLangEntry(Lang, NULL);
		if (!Le || !Le->File)
		{
			LgiTrace("%s:%i - Language '%s' not found (File='%s').\n", _FL, Lang, Le?Le->File:NULL);
			return false;
		}
		
		// Set the "lang" to the base language here. If it's been left 
		// as a dictionary CreateReadOnlyMaster can fail.
		aspell_config_replace(SpellConfig, "lang", Le->Lang);

		Dictionary = Lang;

		SetupPaths();
		
		if (!DictLangPath)
		{
			LgiTrace("%s:%i - No base dictionary path.\n", _FL);
			return false;
		}
		
		if (!CheckCreateFolder(DictLangPath))
		{
			LgiTrace("%s:%i - Failed to create '%s'.\n", _FL, DictLangPath.Get());
			return false;
		}
		
		// Now download the file
		LUri Url(Le->File);
		char Dir[MAX_PATH_LEN];
		LMakePath(Dir, sizeof(Dir), DictLangPath, GetLeaf(Url.sPath));
		LString Arc = Dir;
		if (!LFileExists(Arc) && !DownloadFile(Arc, Le->File))
		{
			LgiTrace("%s:%i - '%s' is missing and can't be downloaded.\n", _FL, Arc.Get());
			return false;
		}
		
		// Decompress the archive into the folder
		while (Decompress(Arc))
			;
		
		// Convert all the word lists
		LDirectory dir;
		for (int b=dir.First(DictLangPath); b; b=dir.Next())
		{
			if (!dir.IsDir() && MatchStr("*.cwl", dir.GetName()))
			{
				char p[MAX_PATH_LEN];
				if (!dir.Path(p, sizeof(p)))
				{
					LgiTrace("%s:%i - dir.Path failed.\n", _FL);
					return false;
				}
				
				LString Decomp = p;		
				if (!Decompress(Decomp))
				{
					// LgiTrace("%s:%i - PrezipDecompress('%s') failed.\n", _FL, p);
					return false;
				}
				
				LString ReadOnlyWL = CreateReadOnlyMaster(Decomp);
				if (!ReadOnlyWL.Get())
				{
					LgiTrace("%s:%i - CreateReadOnlyMaster('%s') failed.\n", _FL, p);
					return false;
				}
				
				// Delete temp files...
				FileDev->Delete(p, false);
				FileDev->Delete(Decomp, false);
			}
		}

		LgiTrace("%s:%i - Aspell: Dictionary for '%s' installed OK.\n", _FL, Lang);

		if (SpellConfig)
		{
			aspell_clear_all_lists(SpellConfig);
			delete_aspell_config(SpellConfig);
			SpellConfig = NULL;
		}		

		return true;
	}
};

///////////////////////////////////////////////////////////////////////
// Ui
//#define IDC_MESSAGE					1000
//#define IDC_LIST					1001
#define IDC_ADD_DICT				1004
#define IDC_IGNORE					1005
#define IDC_REPLACE					1006

class Dlg : public LDialog
{
	LTextLabel *Ctrl0;
	LList *Ctrl1;
	LButton *Ctrl2;
	LButton *Ctrl3;
	LButton *Ctrl4;
	LButton *Ctrl5;

public:
	char *Str;
	static int DlgX, DlgY;

	Dlg(LViewI *Parent, ASpell *Spell, char *Msg, AspellStringEnumeration *Elements)
	{
		Str = 0;

		SetParent(Parent);
		Name("Spelling Error");
		LRect r(0,
				0,
				255 + LAppInst->GetMetric(LGI_MET_DECOR_X),
				300 + LAppInst->GetMetric(LGI_MET_DECOR_Y));
		SetPos(r);
		if (DlgX < 0 && DlgY < 0)
		{
			MoveToCenter();
		}
		else
		{
			r.Offset(DlgX, DlgY);
			SetPos(r);			
		}

		Children.Insert(Ctrl0 = new LTextLabel(IDC_MESSAGE, 7, 7, 300, 14, Msg));
		Children.Insert(Ctrl1 = new LList(IDC_LIST, 7, 28, 238, 203));
		Ctrl1->AddColumn("Suggestion", 220);

		// Button row 1
		Children.Insert(Ctrl2 = new LButton(IDC_REPLACE, 7, 238, 63, 21, "Replace"));
		Children.Insert(Ctrl5 = new LButton(IDC_IGNORE, 7 + 130 - 60, 238, 60, 21, "Ignore"));
		Children.Insert(Ctrl4 = new LButton(IDC_ADD_DICT, 7, 268, 130, 21, "Add to Dictionary"));

		// Button row 2
		Children.Insert(Ctrl3 = new LButton(IDOK,		182, 238, 63, 21, "Finish"));
		Children.Insert(Ctrl3 = new LButton(IDCANCEL,	182, 268, 63, 21, "Cancel"));

		const char *Word;
		while ( (Word = Spell->aspell_string_enumeration_next(Elements)) != NULL )
		{
			LListItem *i;
			Ctrl1->Insert(i = new LListItem);
			if (i)
			{
				i->SetText((char*)Word, 0);
			}
		}
	}

	~Dlg()
	{
		DlgX = GetPos().x1;
		DlgY = GetPos().y1;
		
		DeleteArray(Str);
	}

	void SetStr()
	{
		LListItem *i = Ctrl1->GetSelected();
		if (i)
		{
			Str = NewStr(i->GetText(0));
		}
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		switch (Ctrl->GetId())
		{
			case IDC_LIST:
			{
				if (n.Type == LNotifyItemDoubleClick)
				{
					SetStr();
					EndModal(IDC_REPLACE);
				}
				break;
			}
			case IDC_REPLACE:
			case IDC_ADD_DICT:
			case IDC_IGNORE:
			{
				SetStr();
				// Fall thru
			}
			case IDOK:
			case IDCANCEL:
			{
				EndModal(Ctrl->GetId());
				break;
			}
		}

		return 0;
	}
};

int Dlg::DlgX = -1, Dlg::DlgY = -1;

///////////////////////////////////////////////////////////////////////
class SpellCheckAspell : public LSpellCheck, public ASpell
{
	AspellSpeller *SpellChecker;
	AspellCanHaveError *PossibleErr;

public:
	SpellCheckAspell() : LSpellCheck("SpellCheckAspell")
	{
		SpellChecker = NULL;
		PossibleErr = NULL;
	}
	
	/*
	bool GetValue(const char *Var, LVariant &s)
	{
		ScribeDomType p = StrToDom(Var);
	
		// visible props
		switch (p)
		{
			case SdDictionary:
			{
				List<char> d;
				ListDictionaries(d);

				if (s.Type == GV_LIST)
				{
					List<LVariant> v;

					for (char *c=d.First(); c; c=d.Next())
					{
						v.Insert(new LVariant(c));
					}

					s.SetList(&v);
				}
				else
				{
					int n = 0;
					for (char *c=d.First(); c; c=d.Next(), n++)
					{
						if (Dictionary.Get() && _stricmp(Dictionary, c) == 0)
						{
							s = n;
						}
					}
				}

				d.DeleteArrays();
				break;
			}
			// global/hidden props
			case SdType:
			{
				s = PLUGIN_TYPE_SPELLCHECKER;
				break;
			}
			case SdDescription:
			{
				s = "Aspell Checker";
				break;
			}
			case SdVersion:
			{
				s = PLUGIN_VER;
				break;
			}
			case SdProps:
			{
				s.SetList();
				for (int i=0; i<CountOf(Props); i++)
				{
					char t[256];
					Props[i].Print(t, sizeof(t));
					s.Value.Lst->Insert(new LVariant(t));
				}
				break;
			}
			default:
			{
				LAssert(0);
				return false;
			}
		}

		return true;
	}

	bool SetValue(const char *Var, LVariant &s)
	{
		ScribeDomType p = StrToDom(Var);
		switch (p)
		{
			case SdLanguage:
			{
				LanguageEntry *Le = GetLangEntry(NULL, s.Str());
				if (Le)
				{
					if (!SpellConfig)
					{
						if (!Init(Le->EnglishName))
							return false;
					}
					else if (SpellConfig)
					{
						aspell_config_replace(SpellConfig, "lang", Dictionary = Le->Lang);
					}
					else return false;
				}
				break;
			}
			default:
			{
				LAssert(0);
				return false;
			}
		}

		return true;
	}

	bool CallMethod(const char *Var, LVariant *Ret, LArray<LVariant*> &Args)
	{
		ScribeDomType p = StrToDom(Var);
		switch (p)
		{
			case SdCheckWord:
			{
				List<char> Options;
				bool Status = CheckWord(Args[0]->Str(), &Options, Args[1]->CastInt32() != 0, Args[2]->CastView());
				if (!Status)
				{
					Ret->SetList();
					for (char *o=Options.First(); o; o=Options.Next())
					{
						LVariant *v = new LVariant;
						v->OwnStr(o);
						Ret->Value.Lst->Insert(v);
					}
				}
				else *Ret = true;
				break;
			}
			case SdEnumLanguages:
			{
				if (!Ret || !Ret->SetList())
					return false;

				LVariant *v;
				char Buf[256];
				for (unsigned i=0; i<Languages.Length(); i++)
				{
					LanguageEntry &e = Languages[i];
					sprintf_s(Buf, sizeof(Buf), "%s,%s,%s", e.Lang, e.EnglishName, e.NativeName);
					if (v = new LVariant(Buf))
					{
						Ret->Value.Lst->Insert(v);
					}
				}
				break;
			}
			case SdEnumDictionaries:
			{
				Ret->SetList();
				
				if (!SpellConfig && Dictionary)
					Init(Dictionary);
				
				List<char> Langs;
				ListDictionaries(Langs);
				
				for (char *l = Langs.First(); l; l = Langs.Next())
				{
					LVariant *v = new LVariant;
					v->OwnStr(l);
					Ret->Value.Lst->Insert(v);
				}
				break;
			}
			case SdAddToDict:
			{
				char *Utf8 = Args[0]->Str();
				if (IsOk() && Utf8)
					*Ret = aspell_speller_add_to_personal(SpellChecker, Utf8, strlen(Utf8)) != 0;
				else
					*Ret = false;
				break;
			}
			case SdInstall:
			{
				bool Status = InstallDictionary(Dictionary);
				if (Status)
				{				
					// Find the language name to init
					LanguageEntry *Le = GetLangEntry(Dictionary, NULL);
					if (Le)
						Init(Le->EnglishName);
					else
						LgiTrace("%s:%i - No entry for '%s'\n", _FL, Dictionary.Get());
				}
				else
				{
					#ifndef _DEBUG
					// Clean up bad install..
					if (LDirExists(DictLangPath))
						FileDev->RemoveFolder(DictLangPath, true);
					#endif
				}
				break;
			}			
			default:
			{
				LAssert(0);
				return false;
			}		
		}
		
		return true;
	}
	
	bool CheckWord(	char *Word,
					List<char> *Options,
					bool Ask = false,
					LViewI *AskDlgParent = 0)
	{
		bool Status = false;
		LViewI *Parent = AskDlgParent ? AskDlgParent : Wnd;

		if (IsOk() && ValidStr(Word))
		{
			if (!SpellChecker)
			{
				PossibleErr = new_aspell_speller(SpellConfig);
				if (aspell_error_number(PossibleErr) != 0)
				{
					if (Ask)
					{
						LgiMsg(Parent, (char*)aspell_error_message(PossibleErr), Wnd?Wnd->Name():(char*)"Aspell");
					}
					else
					{
						const char *Err = aspell_error_message(PossibleErr);
						LgiTrace("Aspell Checker: %s\n", Err);
						if (Err &&
							stristr(Err, "No word lists can be found") &&
							Dictionary)
						{
							#if 0 // Immediate install
							InstallDictionary(Dictionary);
							
							// Find the language name to init
                            LanguageEntry *Le = GetLangEntry(Dictionary, NULL);
                            if (Le)
                            {
							    Init(Le->EnglishName);
							}
							else
							{
							    LgiTrace("%s:%i - No entry for '%s'\n", _FL, Dictionary.Get());
							}
							#else
							if (CapTarget)
							{
								CapTarget->NeedsCapability("SpellingDictionary", Dictionary);
							}
							#endif
						}
					}
				}
				else
				{
					SpellChecker = to_aspell_speller(PossibleErr);
				}
			}

			if (!SpellChecker)
			{
				// No spell check available, so default to spelling ok.
				// Everything spelt ok is better than everything incorrect.
				return true;
			}			
			
			if (SpellChecker)
			{
				int Len = strlen(Word);
				Status = aspell_speller_check(SpellChecker, Word, Len) != 0;

				if (!Status)
				{
					if (Ask)
					{
						// mis-spelt word
						const AspellWordList *Suggestions = aspell_speller_suggest(SpellChecker, Word, Len);
						AspellStringEnumeration *Elements = aspell_word_list_elements(Suggestions);

						char Msg[256];
						sprintf_s(Msg, sizeof(Msg), "No dictionary entry for '%.*s'", Len, Word);

						Dlg d(Parent, this, Msg, Elements);
						if (d.DoModal())
						{
							if (d.Str)
							{
								// FIXME: strcpy_s(Word, d.Str);
								LAssert(0);
							}
						}
					}
					else if (Options)
					{
						const AspellWordList *Suggestions = aspell_speller_suggest(SpellChecker, Word, Len);
						if (Suggestions)
						{
							AspellStringEnumeration *Elements = aspell_word_list_elements(Suggestions);
							if (Elements)
							{
								const char *Word;
								while ( (Word = aspell_string_enumeration_next(Elements)) != NULL )
								{
									Options->Insert(NewStr((char*)Word));
								}
							}
						}
					}
				}
			}
		}

		return Status;
	}
	*/
	
	void OnMissingDictionary()
	{
		if (!Language)
			; // do nothing
		else if (SpellParams && SpellParams->CapTarget)
			SpellParams->CapTarget->NeedsCapability("SpellingDictionary", Language);
		else
			LgiTrace("%s:%i - Error: no capability target to install dictionary.\n", _FL);
	}
	
	LMessage::Result OnEvent(LMessage *m)
	{
		switch (m->Msg())
		{
			case M_SET_PARAMS:
			{
				SpellParams.Reset((LSpellCheck::Params*)m->A());
				printf("%s:%i - SpellParams=%p\n", _FL, SpellParams.Get());
				OnInit();
				printf("%s:%i - SpellParams=%p\n", _FL, SpellParams.Get());
				if (SpellParams && SpellParams->Lang)
				{
					if (Init(SpellParams->Lang, SpellParams->Dict))
					{
						PossibleErr = new_aspell_speller(SpellConfig);
						if (aspell_error_number(PossibleErr) != 0)
						{
							const char *Err = aspell_error_message(PossibleErr);
							if (Err &&
								stristr(Err, "No word lists can be found") &&
								Dictionary)
							{
								OnMissingDictionary();
							}
						}
						else
						{
							SpellChecker = to_aspell_speller(PossibleErr);
						}
					}
				}
				break;
			}
		 	case M_SET_DICTIONARY:
			{
				int ResponseHnd = (int)m->A();
				LAutoPtr<DictionaryId> Dict((DictionaryId*)m->B());
				bool Success = false;
				if (Dict)
				{
					LanguageEntry *Le = GetLangEntry(Dict->Lang, NULL);
					Success = Init(Le ? Le->EnglishName :  Dict->Lang.Get(), Dict->Dict);
				}

				PostThreadEvent(ResponseHnd, M_SET_DICTIONARY, (LMessage::Param)Success);
				break;
			}
			case M_ENUMERATE_LANGUAGES:
			{
				int ResponseHnd = (int)m->A();
				if (ResponseHnd < 0)
					break;

				if (Languages.Length() == 0)
					OnInit();
					
				LAutoPtr< LArray<LanguageId> > a(new LArray<LanguageId>);
				if (!a)
					break;
					
				for (unsigned i=0; i<Languages.Length(); i++)
				{
					LanguageEntry &e = Languages[i];
					LanguageId &id = a->New();
					
					id.LangCode = e.Lang;
					id.EnglishName = e.EnglishName;
					if (ValidStr(e.NativeName))
						id.NativeName = e.NativeName;
				}

				PostObject(ResponseHnd, m->Msg(), a);
				break;
			}
			case M_ENUMERATE_DICTIONARIES:
			{
				int ResponseHnd = (int)m->A();
				LAutoPtr<LString> Lang((LString*) m->B());
				if (ResponseHnd < 0 || !Lang.Get())
					break;

				LAutoPtr< LArray<DictionaryId> > a(new LArray<DictionaryId>);
				if (!a)
					break;

				if (!SpellConfig)
					Init(*Lang);
				
				List<char> Dictionaries;
				ListDictionaries(Dictionaries);
				
				for (auto l: Dictionaries)
				{
					DictionaryId &di = a->New();
					di.Lang = *Lang;
					di.Dict = l;
				}
				
				Dictionaries.DeleteArrays();
				PostObject(ResponseHnd, m->Msg(), a);
				break;
			}
			case M_CHECK_TEXT:
			{
				int ResponseHnd = (int)m->A();
				LAutoPtr<CheckText> c((CheckText*) m->B());
				if (!c)
					break;
				
				// Check the spelling...
				if (!SpellChecker && IsLoaded())
				{
					PossibleErr = new_aspell_speller(SpellConfig);
					if (aspell_error_number(PossibleErr) != 0)
					{
						const char *Err = aspell_error_message(PossibleErr);
						LgiTrace("Aspell Checker: %s\n", Err);
						if (Err &&
							stristr(Err, "No word lists can be found") &&
							Dictionary)
						{
							OnMissingDictionary();
						}
					}
					else
					{
						SpellChecker = to_aspell_speller(PossibleErr);
					}
				}

				if (SpellChecker)
				{
					LUtf8Ptr t(c->Text);
					
					// Break the text into words...
					int SPos = 0;
					for (LUtf8Ptr s(c->Text); s; )
					{
						while (s && strchr(AspellDelim, s))
						{
							s++;
							SPos++;
						}
						
						LUtf8Ptr e = s;
						int EPos = SPos;
						bool IsUrl = false;
						while (e)
						{
							if (e.GetPtr()[0] == ':' &&
								e.GetPtr()[1] == '/' &&
								e.GetPtr()[2] == '/')
							{
								// Url handling...
								IsUrl = true;

								while (e && !strchr(AspellUrlDelim, e))
								{
									e++;
									EPos++;
								}
							}
							else if (strchr(AspellDelim, e))
								break;
							
							e++;
							EPos++;
						}
						if (e - s == 0)
							break;
						
						if (IsDigit(s) ||
							s == '$' ||
							IsUrl)
						{
							// Don't spell check
						}
						else
						{
							// And check each word....
							int Status = aspell_speller_check(SpellChecker, (const char*) s.GetPtr(), (int)(e-s)) != 0;
							if (!Status)
							{
								SpellingError &Se = c->Errors.New();
								Se.Start = SPos;
								Se.Len = EPos - SPos;
							
								// Get some suggestions for the mis-spelt word...
								const AspellWordList *Suggestions = aspell_speller_suggest(SpellChecker, (const char*) s.GetPtr(), (int)(e-s));
								if (Suggestions)
								{
									AspellStringEnumeration *Elements = aspell_word_list_elements(Suggestions);
									if (Elements)
									{
										const char *Word;
										while ( (Word = aspell_string_enumeration_next(Elements)) != NULL )
										{
											Se.Suggestions.New() = Word;
										}
									}
								}
							}
						}
						
						if (!e)
							break;

						s = ++e;
						SPos = ++EPos;
					}
				}

				// Return the object to the caller...				
				PostObject(ResponseHnd, m->Msg(), c);
				break;
			}
			case M_ADD_WORD:
			{
				LAutoPtr<LString> Word((LString*)m->A());
				if (!Word)
					break;
				
				if (IsOk() && SpellChecker)
					aspell_speller_add_to_personal(SpellChecker, *Word, (int)Word->Length());
				break;
			}
			case M_INSTALL_DICTIONARY:
			{
				bool Status = ASpell::InstallDictionary(Language);
				if (Status)
				{				
					// Find the language name to init
					LanguageEntry *Le = GetLangEntry(Language, NULL);
					if (Le)
						Init(Le->EnglishName, Dictionary);
					else
						LgiTrace("%s:%i - No entry for '%s'\n", _FL, Language.Get());
				}
				else
				{
					#ifndef _DEBUG
					// Clean up bad install..
					if (LDirExists(DictLangPath))
						FileDev->RemoveFolder(DictLangPath, true);
					#endif
				}
				break;
			}
			default:
			{
				LAssert(!"Impl me.");
				break;
			}
		}
		
		return 0;
	}
};

///////////////////////////////////////////////////////////////////////
LAutoPtr<LSpellCheck> CreateAspellObject()
{
	LAutoPtr<LSpellCheck> a(new SpellCheckAspell);
	return a;
}
