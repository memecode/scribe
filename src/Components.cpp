/*

Missing capabilities and their installation:

1) An instance of a LCapabilityClient will receive a call to it's NeedsCapability
function. e.g. The parent class of ScribeAccount (LCapabilityClient) calls into the
ScribeWnd::NeedsCapability (a virtual function of the parent LCapabilityTarget class).

2) ScribeWnd::NeedsCapability puts up a MissingCapsBar to show the user what is 
happening.

3) The user clicks one of the action buttons and the MissingCapsBar calls
CapabilityInstaller::StartAction which is implemented in ScribeWnd::StartAction.

4) If the action is to install some component the CapabilityInstaller::StartAction
method is called to begin the process of querying the memecode site for suitable
downloads and then downloading the files.

5) That function creates a CapabilityInstallerPriv::InstallJob instance with the
capability to install. This is run in a worker thread.

6) CapabilityInstallerPriv::Main loops through all the jobs, queries the memecode
site for downloads, and then initiates the HTTP downloads to a temporary folder.

7) If the output folder is writable it just moves the files straight to the 
destination folder. Otherwise it will call the Updater.exe to move the files, which
will request admin permissions as part of it's manifest.

8) Finally an M_UPDATE message is posted back to the MissingCapsBar::OnEvent method
which sets the final message and then creates a timer to hide itself.

9) After the timer expires the LCapabilityTarget::OnCloseInstaller (implemented in 
ScribeWnd::OnCloseInstaller) is called to delete the MissingCapsBar.

*/
#include "lgi/common/Lgi.h"
#include "lgi/common/Button.h"
#include "lgi/common/Http.h"
#include "lgi/common/DisplayString.h"

#include "Scribe.h"
#include "Components.h"
#include "resdefs.h"

#define MISSING_CAPS_BAR_COUNTDOWN		5 // seconds
#define MISSING_ACTION_BASE				100

struct MissingCapsBarPriv
{
    LColour Back;
    int CountDown;
    
    LString::Array Msg;
    LArray<LDisplayString*> Strs;
    
    MissingCapsBarPriv() : Back(0xd2, 0x40, 0x40)
    {
	    CountDown = -1;
    }

	~MissingCapsBarPriv()
	{
		Strs.DeleteObjects();
	}
    
    void SetMsg(const char *m)
    {
		Msg.Empty();
		Msg = LString(m).SplitDelimit("\n");
		Strs.DeleteObjects();
    }
};

MissingCapsBar::MissingCapsBar(	LCapabilityTarget *owner,
								LCapabilityTarget::CapsHash *a,
								const char *msg,
								CapabilityInstaller *inst,
								LArray<const char*> &actions,
								LColour *background)
{
	d = new MissingCapsBarPriv();
	Owner = owner;
    Installer = inst;
    Caps = a;
    if (background)
		d->Back = *background;
    
    d->SetMsg(msg);
    
    int ContentY = (int)d->Msg.Length() * LSysFont->GetHeight();
    
    for (unsigned i=0; i<actions.Length(); i++)
    {
		Actions.Add(actions[i]);
	    
	    LButton *b = new LButton(MISSING_ACTION_BASE + i, 0, 0, -1, -1, actions[i]);
	    if (b)
	    {
			LCss::ColorDef Bk(LCss::ColorRgb, d->Back.c32());
			b->GetCss(true)->NoPaintColor(Bk);
			ContentY = MAX(ContentY, b->Y());
			
			if (i == actions.Length() - 1)
				b->Default(true);

			Btns.Add(b);
			AddView(b);
		}
    }    

	LRect r(0, 0, 200, ContentY + 8);
	SetPos(r);
}

MissingCapsBar::~MissingCapsBar()
{
	if (Progress)
	{
		if (Progress->Lock(_FL))
		{
			Progress->Ui = NULL;
			Progress->Unlock();
			Progress->DecRef();
		}
		else LAssert(0);

		Progress = NULL;
	}

	DeleteObj(d);
}

void MissingCapsBar::Empty()
{
    Actions.Length(0);
	Btns.DeleteObjects();
}

void MissingCapsBar::SetMsg(const char *m)
{
	d->SetMsg(m);
	Invalidate();
}

void MissingCapsBar::OnCreate()
{
    AttachChildren();
    SetPulse(1000);
}

void MissingCapsBar::OnPosChange()
{
    LRect b = GetClient();
	int x = b.x2 - 4;
    for (ssize_t i=Btns.Length()-1; i>=0; i--)
    {
		LButton *Btn = Btns[i];
        LRect r = Btn->GetPos();
        r.x2 = x;
        r.x1 = x - Btn->X() + 1;
        x = r.x1 - 4;
        
        int Px = (b.Y() - Btn->Y()) >> 1;
        r.y1 = Px;
        r.y2 = Px + Btn->Y() - 1;
        
        Btn->SetPos(r);
    }
    
    if (ProgCtrl)
    {
		LRect r = ProgCtrl->GetPos();
		r.Offset((x - r.X()) - r.x1, ((b.Y() - r.Y()) / 2) - r.y1);
		ProgCtrl->SetPos(r);
    }
}

void MissingCapsBar::OnPaint(LSurface *pDC)
{
    LRect c = GetClient();

	pDC->Colour(d->Back);
	pDC->Rectangle();

    if (d->Msg.Length())
    {
		if (!d->Strs.Length())
		{
			for (unsigned i=0; i<d->Msg.Length(); i++)
			{
				d->Strs.Add(new LDisplayString(LSysFont, d->Msg[i]));
			}
		}
		
		LSysFont->Transparent(true);
		LSysFont->Colour(LColour(255, 255, 255), d->Back);

		int Px = Y() - (LSysFont->GetHeight() * (int)d->Msg.Length());
		Px >>= 1;
		
		for (unsigned i=0; i<d->Strs.Length(); i++)
		{
			d->Strs[i]->Draw(pDC, 10, Px + (i * LSysFont->GetHeight()), &c);
		}
	}
}

bool MissingCapsBar::Pour(LRegion &r)
{
    LRect *p = FindLargest(r);
    if (!p) return false;

    LRect rc = *p;
    rc.y2 = rc.y1 + Y() - 1;
    SetPos(rc);

    return true;
}

int MissingCapsBar::OnNotify(LViewI *c, const LNotification &n)
{
	if (c->GetId() == IDOK)
	{
		Detach();
		Owner->OnCloseInstaller();
		delete this;
	}
    else if (c->GetId() >= MISSING_ACTION_BASE)
    {
		int Idx = c->GetId() - MISSING_ACTION_BASE;
		#if DEBUG_CAPABILITIES
		LgiTrace("%s:%i - Idx=%i/%i\n", _FL, Idx, Btns.Length());
		#endif
		if (Idx < (int)Btns.Length())
		{
			c = Btns[Idx];
			
			const char *Action = c->Name();
			if ((Progress = Installer->StartAction(this, Caps, Action)))
			{
				#if DEBUG_CAPABILITIES
				LgiTrace("%s:%i - StartAction=%p\n", _FL, Idx, Progress);
				#endif
				c->Enabled(false);
			}
			else
			{
				#if DEBUG_CAPABILITIES
				LgiTrace("%s:%i - StartAction failed\n", _FL, Idx, Progress);
				#endif
				Detach();
				Owner->OnCloseInstaller();
				delete this;
			}
		}
    }
    return 0;
}

void MissingCapsBar::OnPulse()
{
	if (Progress)
	{
		bool HasProg = Progress->TotalSize > 0 && !Progress->Finished;
		bool HasCtrl = ProgCtrl != NULL;
		if (HasProg ^ HasCtrl)
		{
			if (HasProg)
			{
				// Create ctrl
				ProgCtrl = new LProgressView(-1, 0, 0, 150, 12, NULL);
				ProgCtrl->SetRange(Progress->TotalSize);
				ProgCtrl->Attach(this);
			}
			else
			{
				DeleteObj(ProgCtrl);
			}
			OnPosChange();
		}
		if (ProgCtrl)
		{
			ProgCtrl->Value(Progress->CurrentPos);
		}
	}

	if (d->CountDown > 0)
	{
		d->CountDown--;
		if (d->CountDown <= 0)
		{
			// This detach means the LWindow::Pour won't alloc space for the MissingCapsBar
			Detach();
			
			// This will remove the ownering windows ptr to us and repour the window.
			Owner->OnCloseInstaller();
			
			// We can now RIP
			delete this;
		}
	}
}

LMessage::Param MissingCapsBar::OnEvent(LMessage *m)
{
	switch (m->Msg())
	{
		case M_UPDATE:
		{
			if (Progress && Progress->Lock(_FL))
			{
				bool Finished = !IsFinished && Progress->Finished;
				bool HasError = Progress->HasError;
				if (Progress->Msg)
					d->SetMsg(Progress->Msg);
				Progress->Msg.Empty();
				Progress->Unlock();
				
				Invalidate();

				if (Finished)
				{
					IsFinished = true;
					if (HasError)
					{
						Btns.DeleteObjects();
						
						auto Ok = new LButton(IDOK, 0, 0, -1, -1, LLoadString(IDS_OK));
						Btns.Add(Ok);
						Ok->Attach(this);
						OnPosChange();
					}
					else
					{
						// Is the case that the caller doesn't delete us then setup
						// a count down to automatically remove the install bar.
						d->CountDown = MISSING_CAPS_BAR_COUNTDOWN;
					}
					
					if (ProgCtrl)
						ProgCtrl->Value(Progress->TotalSize);

					if (Progress->Lock(_FL))
					{
						Progress->Ui = NULL;
						Progress->Unlock();
					}
					Progress->DecRef();
					Progress = NULL;
					
					// Tell the owner
					Owner->OnInstall(Caps, !HasError);
					
					// We _may_ get deleted here... so we can't call our parents "OnEvent"
					return 0;
				}
			}
			break;
		}
	}
	
	return LLayout::OnEvent(m);
}

class ProgressSocket : public LSocket
{
	int64 *Prog;

public:
	ProgressSocket(int64 *p)
	{
		Prog = p;
	}
	
	ssize_t Read( void *Data, ssize_t Len, int Flags)
	{
		ssize_t r = LSocket::Read(Data, Len, Flags);
		if (Prog && r > 0)
			*Prog += r;
		return r;
	}
};

///////////////////////////////////////////////////////////////////////////////////
class CapabilityInstallerPriv : public LMutex, public LThread
{
public:
    struct InstallJob
    {
        InstallProgress *Prog;
        LAutoString Component;
    };

private:    
    LAutoString TempFolder;
    LArray<InstallJob*> Jobs;
    bool Loop;
    
public:
    LString Uri, Proxy, App, Version;

    CapabilityInstallerPriv(const char *TmpFolder) :
		LMutex("ComponentInstaller.Mutex"),
		LThread("CompInst.Th")
    {
		TempFolder.Reset(NewStr(TmpFolder));
        Loop = true;
    }
    
    ~CapabilityInstallerPriv()
    {
        Loop = false;
        while (!IsExited())
            LSleep(1);
        Jobs.DeleteObjects();
    }
    
    void Msg(InstallProgress *Prog, const char *Fmt, ...)
    {
		char buffer[512];
		va_list arg;
		va_start(arg, Fmt);
		vsprintf_s(buffer, sizeof(buffer), Fmt, arg);
		va_end(arg);
		
		LgiTrace("CapabilityInstaller: %s\n", buffer);
		
		if (Prog->Lock(_FL))
		{
			Prog->Msg = buffer;
			if (Prog->Ui)
				Prog->Ui->PostEvent(M_UPDATE);
			
			Prog->Unlock();
		}
    }
    
    void AddJob(InstallJob *j)
    {
		if (Lock(_FL))
		{
			Jobs.Add(j);
			Unlock();
		}
    }
    
    int Main()
    {
        while (Loop)
        {
            InstallJob *j = NULL;
            if (Lock(_FL))
            {
                if (Jobs.Length())
                {
                    j = Jobs[0];
                    Jobs.DeleteAt(0, true);
                }
                Unlock();
            }
            
            if (j)
            {
				bool Success = false;
				
                char OutputPath[MAX_PATH_LEN];
                #ifdef MAC
                LMakePath(OutputPath, sizeof(OutputPath), LGetExeFile(), "Contents/Frameworks");
                #else
                strcpy_s(OutputPath, sizeof(OutputPath), LGetExePath());
                #endif
                
				Msg(j->Prog, "Getting download site...");

                LHttp Http;
                if (Proxy)
                {
                    LUri u(Proxy);
                    Http.SetProxy(u.sHost, u.Port?u.Port:HTTP_PORT);
                }
                
                char Url[512];
                const char *Os = LGetOsName();
				#ifdef _MSC_VER
                int ch =
                #endif
                sprintf_s(Url, sizeof(Url),
								"%s?os=%s&wordsize=%i&app=%s&version=%s&component=%s",
								Uri.Get(),
								Os,
								(int)(sizeof(NativeInt)<<3),
								App.Get(),
								Version.Get(),
								j->Component.Get());
				#ifdef _MSC_VER
					#if _MSC_VER == _MSC_VER_VS2008
						ch += sprintf_s(Url+ch, sizeof(Url)-ch, "&tags=vc9");
					#elif _MSC_VER == _MSC_VER_VS2013
						ch += sprintf_s(Url+ch, sizeof(Url)-ch, "&tags=vc12");
					#elif _MSC_VER == _MSC_VER_VS2015
						ch += sprintf_s(Url+ch, sizeof(Url)-ch, "&tags=vc14");
					#elif _MSC_VER == _MSC_VER_VS2017
						ch += sprintf_s(Url+ch, sizeof(Url)-ch, "&tags=vc15");
					#elif _MSC_VER >= _MSC_VER_VS2019
						ch += sprintf_s(Url+ch, sizeof(Url)-ch, "&tags=vc16");
					#else
						#error "Impl me."
					#endif
				#endif
				#if DEBUG_CAPABILITIES
				LgiTrace("%s:%i - ComponentURL: %s\n", _FL, Url);
				#endif
                
                LStringPipe Xml;
                int Status = 0;
                
                LUri u(Url);
                LAutoPtr<LSocketI> Sock(new LSocket);
				Sock->SetTimeout(15 * 1000);
                if (!Http.Open(Sock, u.sHost, u.Port?u.Port:HTTP_PORT))
					Msg(j->Prog, "Error: connection to %s failed.", u.sHost.Get());
				else
                {
                    if (!Http.Get(Url, NULL, &Status, &Xml, NULL))
						Msg(j->Prog, "Error: HTTP get failed.");
					else
                    {
                        if (Status != 200)
							Msg(j->Prog, "Error: HTTP status %i", Status);
						else
                        {
                            LXmlTree t;
                            LXmlTag r;
                            if (!t.Read(&r, &Xml))
								Msg(j->Prog, "Error: XML parsing error.");
							else
                            {
                                if (!r.IsTag("components"))
									Msg(j->Prog, "Error: Unexpected XML.");
								else
                                {
                                    // Collect list of files to download
                                    LArray<char*> InFiles, TmpFiles;
                                    int64 TotalBytes = 0;
                                    for (auto c: r.Children)
                                    {
                                        if (c->IsTag("file") && c->GetContent())
                                        {
                                            InFiles.Add(c->GetContent());
                                            
                                            int Size = c->GetAsInt("size");
                                            if (Size > 0)
												TotalBytes += Size;
                                        }
                                    }

									if (InFiles.Length() == 0)
									{
										Msg(j->Prog, "Error: No downloads available (%s)", Url);
									}
									else
									{
										j->Prog->CurrentPos = 0;
										j->Prog->TotalSize = TotalBytes;
										Msg(j->Prog, "Downloading...");

										for (unsigned i=0; i<InFiles.Length(); i++)
										{
											LFile Out;
											char *File = strrchr(InFiles[i], '/');
											if (!File)
											{
												Msg(j->Prog, "Error: No filename in download URL.");
												break;
											}
											
											File++;
	                                        
											char OutPath[MAX_PATH_LEN];
											LMakePath(OutPath, sizeof(OutPath), TempFolder, File);
											if (!Out.Open(OutPath, O_WRITE))
											{
												Msg(j->Prog, "Error: Can't open '%s' for writing.", OutPath);
												break;
											}
	                                        
											Sock.Reset(new ProgressSocket(&j->Prog->CurrentPos));
											if (!Http.Open(Sock, u.sHost, u.Port?u.Port:HTTP_PORT))
											{
												Msg(j->Prog, "Error: Can't connect to '%s'.", u.sHost.Get());
												break;
											}
	                                        
											if (!Http.Get(InFiles[i], NULL, &Status, &Out, NULL))
											{
												Msg(j->Prog, "Error: HTTP get failed.");
												break;
											}

											if (Status == 200)
											{
												TmpFiles.Add(NewStr(OutPath));
											}
											else
											{
												Msg(j->Prog, "Error: Download component '%s' failed with HTTP %i.", InFiles[i], Status);
												Out.Close();
												FileDev->Delete(OutPath, NULL, false);
											}
										}

										if (TmpFiles.Length() == InFiles.Length())
										{
											// Check to see if the install folder is writable...
											char p[MAX_PATH_LEN];
											LMakePath(p, sizeof(p), OutputPath, "write_test.txt");
											LFile f;
											bool Writable = f.Open(p, O_WRITE) != 0;
											if (Writable)
											{
												f.Close();
												FileDev->Delete(p, NULL, false);
											}
											
											if (Writable)
											{
												// We can move them into place ourself...
												int Copied = 0;
												for (unsigned i=0; i<TmpFiles.Length(); i++)
												{
													char *t = TmpFiles[i];
													char *l = strrchr(t, DIR_CHAR);
													if (l)
													{
														l++;
														LMakePath(p, sizeof(p), OutputPath, l);
														LError CopyStatus;
														if (FileDev->Copy(t, p, &CopyStatus))
														{
															Copied++;
															FileDev->Delete(t, NULL, false);
														}
														else
														{
															Msg(j->Prog, "Error: File copy failed.");
															LgiTrace("%s:%i - Copy '%s' failed during install component: %i (%s)\n",
																	_FL, l, CopyStatus.GetCode(), CopyStatus.GetMsg().Get());
														}																									
													}
												}
												
												if (Copied == TmpFiles.Length())
												{
													Msg(j->Prog, "Component download completed.");
													Success = true;
												}
											}
											else
											{
												// Use privledge escalation to install the files...									
												#ifdef WIN32
												char Updater[MAX_PATH_LEN];
												LMakePath(Updater, sizeof(Updater), OutputPath, "Updater.exe");
												if (LFileExists(Updater))
												{
													LStringPipe p;
													for (unsigned i=0; i<TmpFiles.Length(); i++)
													{
														p.Print(" \"%s\"", TmpFiles[i]);
													}
													LAutoString a(p.NewStr());
													LAutoWString w(Utf8ToWide(a));
													LAutoWString exe(Utf8ToWide(Updater));
													
													SHELLEXECUTEINFOW ShExecInfo;
													ZeroObj(ShExecInfo);
													ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
													ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
													ShExecInfo.lpFile = exe;		
													ShExecInfo.lpParameters = w;	
													ShExecInfo.nShow = SW_SHOW;
													
													if (ShellExecuteExW(&ShExecInfo))
													{
														DWORD ret = 1;

														WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
														GetExitCodeProcess(ShExecInfo.hProcess, &ret);
														if (ret == 0)
														{
															Msg(j->Prog, "Component download completed.");
															Success = true;
														}
														else
															Msg(j->Prog, "Error: Updater component returned an error.");
													}
													else
													{
														Msg(j->Prog, "Error: Failed to run the updater component.");
													}
												}
												else
												{
													Msg(j->Prog, "Error: Updater '%s' missing.", Updater);
												}
												#else
												LAssert(!"Impl privledge escalation for installing software.");
												#endif
											}

											if (Success && LFontSystem::Inst())
												LFontSystem::Inst()->ResetLibCheck();
										}
									}
								}
                            }
                        }
                    }
                }
                
				#if defined(_DEBUG) && !DEBUG_CAPABILITIES
				if (!Success)
					LgiTrace("%s:%i - ComponentURL: %s\n", _FL, Url);
				#endif

                if (j->Prog->Lock(_FL))
                {
					j->Prog->Finished = true;
					j->Prog->HasError = !Success;
					if (j->Prog->Ui)
						j->Prog->Ui->PostEvent(M_UPDATE);
					j->Prog->Unlock();
				}
                j->Prog->DecRef();
                DeleteObj(j);
            }
            else LSleep(200);
        }
        return 0;
    }
};
    
CapabilityInstaller::CapabilityInstaller(const char *App,
										const char *Version,
										const char *Uri,
										const char *TmpPath,
										const char *Proxy)
{
    d = new CapabilityInstallerPriv(TmpPath);
    d->App = App;
    d->Version = Version;
    d->Proxy = Proxy;
    d->Uri = Uri;
    d->Run();
}

CapabilityInstaller::~CapabilityInstaller()
{
    DeleteObj(d);
}

InstallProgress *CapabilityInstaller::StartAction(MissingCapsBar *Bar, LCapabilityTarget::CapsHash *Components, const char *Action)
{
	if (!Components || !Action)
	{
		LgiTrace("%s:%i - CapabilityInstaller::StartAction invalid params\n", _FL);
		return NULL;
	}

	InstallProgress *p = NULL;

	if (d->Lock(_FL))
	{
		if (!d->Proxy)
			d->Proxy = GetHttpProxy();

		p = new InstallProgress;
		if (p)
		{
			p->IncRef(); // for the UI view...
			p->Ui = Bar;

			for (auto k : *Components)
			{
				CapabilityInstallerPriv::InstallJob *j = new CapabilityInstallerPriv::InstallJob;
				j->Prog = p;
				p->IncRef(); // The job owns a reference...
				j->Component.Reset(NewStr(k.key));

				#if DEBUG_CAPABILITIES
				LgiTrace("%s:%i - AddJob(%s)\n", _FL, k);
				#endif

				d->AddJob(j);
			}
		}
		else
		{
			LgiTrace("%s:%i - Alloc failed.\n", _FL);
		}
		
		d->Unlock();
	}
	else
	{
		LgiTrace("%s:%i - Couldn't lock.\n", _FL);
	}
	
    return p;
}
