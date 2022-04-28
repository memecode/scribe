; Scribe install/uninstall support
!include LogicLib.nsh
!include WinVer.nsh

!system "python Utils\Store\check-build.py 32" = 0

!system "mkdir scribe-setup"
!system "del /Q scribe-setup\*.*"

!system "copy .\Win32ReleaseNoOptimize14\Scribe.exe scribe-setup" = 0
!system "copy .\Win32Release14\ScribeMapi.dll scribe-setup" = 0
!system "copy ..\libs\aspell-0.60.6.1\win32\dist\Win32Release14\aspell-dist-0.60.dll scribe-setup" = 0
!system "copy ..\..\Lgi\trunk\lib\Lgi14x32nop.dll scribe-setup" = 0
!system "copy ..\..\Lgi\trunk\lib\libntlm14x32nop.dll scribe-setup" = 0
!system "copy ..\..\Lgi\trunk\Updater\Win32Release14\Updater.exe scribe-setup" = 0
!system "copy ..\..\..\CodeLib\libjpeg-9a\build32\Release\libjpeg.dll scribe-setup\libjpeg14x32.dll" = 0
!system "copy ..\..\..\CodeLib\libpng\build32\Release\libpng15.dll scribe-setup\libpng14x32.dll" = 0

;system '"c:\Program Files\Upx\upx.exe" -9 .\scribe-setup\*.exe'
;system '"c:\Program Files\Upx\upx.exe" -9 .\scribe-setup\*.dll'

; Generate the DOM documentation
!system "python Code\Py\DomScan.py > scribe-setup\Dom.txt"

!system "python Utils\Store\store.py .\Win32ReleaseNoOptimize14\*.pdb Scribe ${__DATE__} ${__TIME__}" = 0
!system "python Utils\Store\store.py .\Win32Release14\ScribeMapi.pdb Mapi ${__DATE__} ${__TIME__}" = 0
!system "python Utils\Store\store.py ..\..\Lgi\trunk\lib\Lgi14x32nop.pdb Lgi ${__DATE__} ${__TIME__}" = 0

;--------------------------------
SetCompressor lzma

; The name of the installer
Name "Memecode Scribe"

; The file to write
!define OUTFILE "scribe-win32-v###.exe"
OutFile ${OUTFILE}

; The default installation directory
InstallDir $PROGRAMFILES\Memecode\Scribe
InstallDirRegKey HKCU "Software\Memecode\Scribe" Install

;--------------------------------

; Pages

Page directory
Page components
Page instfiles

; The stuff to install
Section ""

	; Set output path to the installation directory.
	SetOutPath $INSTDIR

	; Program files
	Delete $INSTDIR\Lgi*.dll
	File .\scribe-setup\Scribe.exe
	File .\scribe-setup\Updater.exe
	File .\scribe-setup\ScribeMapi.dll
	File .\scribe-setup\aspell-dist-0.60.dll
	File .\scribe-setup\Lgi14x32nop.dll
	File .\scribe-setup\libntlm14x32nop.dll
	File .\scribe-setup\libjpeg14x32.dll
	File .\scribe-setup\libpng14x32.dll
	
	; Resources
	CreateDirectory $INSTDIR\Resources
	SetOutPath $INSTDIR\Resources
	File .\Resources\Scribe.lr8
	File .\Resources\Flags.gif
	File .\Resources\Icons.gif
	File .\Resources\xgate-icons-32.png
	File .\Resources\About64px.png
	File .\Resources\About.html
	File .\Resources\Title.html
	File .\Resources\Title.gif
	File .\Resources\NoFace*.png
	File .\Resources\EmojiMap.png
	File .\Resources\Preview*.html
	
	; Aspell support
	CreateDirectory $INSTDIR\Aspell
	SetOutPath $INSTDIR\Aspell
	File .\Resources\aspell-languages.csv
	CreateDirectory $INSTDIR\Aspell\data
	SetOutPath $INSTDIR\Aspell\data
	File ..\libs\aspell-0.60.6.1\data\*.cmap
	File ..\libs\aspell-0.60.6.1\data\*.cset
	File ..\libs\aspell-0.60.6.1\data\*.kbd
 
	; Scripts
	CreateDirectory $INSTDIR\Scripts
	SetOutPath $INSTDIR\Scripts
	File .\scribe-setup\Dom.txt
	File .\Scripts\Api.html
	File .\Resources\resdefs.h

	File ".\Scripts\ScribeScripts.h"
	File ".\Scripts\Add Senders To Contacts.script"
	File ".\Scripts\Delete Attachments.script"
	File ".\Scripts\Delete Duplicate Messages.script"
	File ".\Scripts\Mail Filters Menu.script"

	; Help files
	CreateDirectory $INSTDIR\Help
	SetOutPath $INSTDIR\Help
	File .\Help\*.html
	File .\Help\*.css
	CreateDirectory $INSTDIR\Help\scripting
	SetOutPath $INSTDIR\Help\scripting
	File ..\..\Lgi\trunk\docs\scripting\*.html
	File ..\..\Lgi\trunk\docs\scripting\*.css

	; Do CRT check
	ExecWait '"$INSTDIR\Scribe.exe" -crtcheck' $0
	${If} $0 != 0
		inetc::get /caption "Visual Studio 2015 Redistributable" /popup "" "http://memecode.com/scribe/data/vcredist_vc14x32.exe" "$INSTDIR\vcredist_vc14x32.exe" /end
		Pop $0 # return value = exit code, "OK" means OK
		DetailPrint "Download: $0"
		${If} $0 == "OK"
			ExecWait "$INSTDIR\vcredist_vc14x32.exe"
			Delete "$INSTDIR\vcredist_vc14x32.exe"
		${EndIf}
	${EndIf}
	
SectionEnd ; end the section

Section "Desktop Install (Non-portable)"

	; Install location into registry
	WriteRegStr HKCU "Software\Memecode\Scribe" Install $INSTDIR

	; Uninstaller
	WriteUninstaller $INSTDIR\uninstall.exe

	GetDllVersionLocal ".\scribe-setup\Scribe.exe" $R0 $R1
	IntOp $R2 $R0 / 0x00010000
	IntOp $R3 $R0 & 0x0000FFFF
	IntOp $R4 $R1 / 0x00010000
	IntOp $R5 $R1 & 0x0000FFFF
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "DisplayVersion" "$R2.$R3.$R4.$R5"

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "DisplayName" "Scribe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "DisplayIcon" "$INSTDIR\Scribe.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "URLInfoAbout" "http://www.memecode.com/"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "URLUpdateInfo" "http://www.memecode.com/scribe.php"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "Publisher" "Memecode"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""

	; Start menu items
	CreateDirectory "$SMPROGRAMS\Memecode Scribe"
	CreateShortCut "$SMPROGRAMS\Memecode Scribe\Scribe.lnk" "$INSTDIR\Scribe.exe" "" "$INSTDIR\Scribe.exe" 0
	CreateShortCut "$SMPROGRAMS\Memecode Scribe\Help.lnk" "$INSTDIR\Help\index.html" "" "$WINDIR\WINHLP32.EXE" 1
	CreateShortCut "$SMPROGRAMS\Memecode Scribe\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
	
SectionEnd

Section "Run Scribe"

	ExecShell "open" "$INSTDIR\Scribe.exe"
	SetAutoClose true
	
SectionEnd

UninstPage components
UninstPage instfiles

Section "un.Program and Start Menu Items"

	Delete $INSTDIR\Uninst.exe ; delete self

	RMDir /r $INSTDIR\Help
	RMDir /r $INSTDIR\Resources
	RMDir /r $INSTDIR\Scripts
	RMDir /r $INSTDIR\ImapCache
	RMDir /r $INSTDIR\Aspell
	RMDir /r $INSTDIR\Resources

	Delete $INSTDIR\*.exe
	Delete $INSTDIR\*.dll
	Delete $INSTDIR\*.gif
	Delete $INSTDIR\*.lr8
	Delete $INSTDIR\*.html
	Delete $INSTDIR\*.txt
	Delete $INSTDIR\*.asm
	
	RMDir $INSTDIR ; recursive is dangerous if they install to "c:\program files\" for instance.

	RMDir /r "$SMPROGRAMS\Memecode Scribe"

	DeleteRegKey HKCU SOFTWARE\Memecode\Scribe
	DeleteRegKey HKCU SOFTWARE\Clients\Mail\Scribe
	DeleteRegKey HKLM SOFTWARE\Clients\Mail\Scribe
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.2"

	SetAutoClose true

SectionEnd

Section /o "un.Remove Email and Settings"

	RMDir /r $APPDATA\Scribe

SectionEnd

; !system "del /Q .\scribe-setup"
; !system "rmdir .\scribe-setup"
!finalize "python Utils\Store\store.py ${OUTFILE} . ${__DATE__} ${__TIME__}"
