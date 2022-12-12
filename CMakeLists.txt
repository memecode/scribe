cmake_minimum_required (VERSION 3.18)
project(scribe)

if (APPLE)
	set(CMAKE_OSX_ARCHITECTURES "x86_64;ARM64")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
elseif (UNIX)
    set(LINUX TRUE)
endif()
set(CMAKE_CXX_STANDARD 14)

# Get all the libs we need:
add_subdirectory(../../lgi/trunk lgi)
add_subdirectory(../libs libs)
find_package(OpenSSL)
if (NOT DEFINED OPENSSL_INCLUDE_DIR)
    message(FATAL_ERROR "No OPENSSL_INCLUDE_DIR")
endif()
if (NOT DEFINED LGI_DIR)
    message(FATAL_ERROR "Lgi should be defining LGI_DIR")
endif()

get_filename_component(ASPELL_DIR ${CMAKE_CURRENT_LIST_DIR}/../libs/aspell-0.60.6.1 ABSOLUTE)
if (NOT EXISTS ${ASPELL_DIR})
    message(FATAL_ERROR "Missing ASPELL_DIR=${ASPELL_DIR}")
endif()

add_subdirectory(ScribeMapi)

# Platform settings
if (WIN32)
	set(APP_TYPE WIN32)
	set(APP_ICON Scribe.rc)
	set(APP_MANIFEST ${LGI_DIR}/src/win/General/Lgi.manifest)
    set(SPELL_CHECK ${LGI_DIR}/src/win/Lgi/SpellCheckWindows.cpp)
elseif (APPLE)
	set(APP_TYPE MACOSX_BUNDLE)
    set(SPELL_CHECK ${LGI_DIR}/src/mac/cocoa/SpellCheckMac.mm)
elseif (LINUX)
	set(APP_ICON Resources/About64px.png)
    set(SPELL_CHECK)
endif()

# Scribe executable target
add_executable(scribe ${APP_TYPE}
    ${LGI_DIR}/src/common/Coding/LexCpp.cpp
    ${LGI_DIR}/src/common/Coding/ScriptCompiler.cpp
    ${LGI_DIR}/src/common/Coding/ScriptLibrary.cpp
    ${LGI_DIR}/src/common/Coding/ScriptVM.cpp
    ${LGI_DIR}/src/common/Db/Db-Csv.cpp
    ${LGI_DIR}/src/common/Db/DbTable.cpp
    ${LGI_DIR}/src/common/Gdc2/Filters/Gif.cpp
    ${LGI_DIR}/src/common/Gdc2/Filters/Jpeg.cpp
    ${LGI_DIR}/src/common/Gdc2/Filters/Lzw.cpp
    ${LGI_DIR}/src/common/Gdc2/Filters/Png.cpp
    ${LGI_DIR}/src/common/Gdc2/Filters/Tiff.cpp
    ${LGI_DIR}/src/common/Gdc2/Font/EmojiFont.cpp
    ${LGI_DIR}/src/common/Gdc2/DrawListSurface.cpp
    ${LGI_DIR}/src/common/Gdc2/Tools/GdcTools.cpp
    ${LGI_DIR}/src/common/General/SegmentTree.cpp
    ${LGI_DIR}/src/common/General/SharedMemory.cpp
    ${LGI_DIR}/src/common/General/SoftwareUpdate.cpp
    ${LGI_DIR}/src/common/General/TimeZone.cpp
    ${LGI_DIR}/src/common/General/Tnef.cpp
    ${LGI_DIR}/src/common/Net/Mime.cpp
    ${LGI_DIR}/src/common/Net/HttpTools.cpp
    ${LGI_DIR}/src/common/Net/Ftp.cpp
    ${LGI_DIR}/src/common/Net/Http.cpp
    ${LGI_DIR}/src/common/Net/OAuth2.cpp
    ${LGI_DIR}/src/common/Net/WebDav.cpp
    ${LGI_DIR}/src/common/Net/Mail.cpp
    ${LGI_DIR}/src/common/Net/MailHttp.cpp
    ${LGI_DIR}/src/common/Net/MailImap.cpp
    ${LGI_DIR}/src/common/Net/OpenSSLSocket.cpp
    ${LGI_DIR}/src/common/Lgi/Browser.cpp
    ${LGI_DIR}/src/common/Lgi/LgiMain.cpp
    ${LGI_DIR}/src/common/Storage/3/Store3.cpp
    ${LGI_DIR}/src/common/Text/Emoji/EmojiMap.cpp
    ${LGI_DIR}/src/common/Text/Emoji/EmojiTools.cpp
    ${LGI_DIR}/src/common/Text/Homoglyphs/Homoglyphs.cpp
    ${LGI_DIR}/src/common/Text/Homoglyphs/HomoglyphsTable.cpp
    ${LGI_DIR}/src/common/Text/Html.cpp
    ${LGI_DIR}/src/common/Text/HtmlCommon.cpp
    ${LGI_DIR}/src/common/Text/HtmlParser.cpp
    ${LGI_DIR}/src/common/Text/XmlTreeUi.cpp
    ${LGI_DIR}/src/common/Text/RtfHtml.cpp
    ${LGI_DIR}/src/common/Text/TextConvert.cpp
    ${LGI_DIR}/src/common/Text/vCard-vCal.cpp
    ${LGI_DIR}/src/common/Text/SpellCheckAspell.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/BlockCursor.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/RichTextEdit.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/RichTextEditPriv.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/HorzRuleBlock.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/ImageBlock.cpp
    ${LGI_DIR}/src/common/Widgets/Editor/TextBlock.cpp
    ${LGI_DIR}/src/common/Widgets/ColourSelect.cpp
    ${LGI_DIR}/src/common/Widgets/ControlTree.cpp
    ${LGI_DIR}/src/common/Widgets/DatePopup.cpp
    ${LGI_DIR}/src/common/Widgets/FilterUi.cpp
    ${LGI_DIR}/src/common/Widgets/Graph.cpp
    ${LGI_DIR}/src/common/Widgets/TimePopup.cpp
    ${LGI_DIR}/src/common/Widgets/ZoomView.cpp
    ${LGI_DIR}/src/common/Widgets/MonthView.cpp
    ${LGI_DIR}/src/common/Widgets/YearView.cpp
    ${SPELL_CHECK}
    Code/AddressSelect.cpp
    Code/BayesDlg.cpp
    Code/BayesianFilter.cpp
    Code/Calendar.cpp
    Code/CalendarView.cpp
    Code/Components.cpp
    Code/Encryption/GnuPG.cpp
    Code/Exp_Scribe.cpp
    Code/FolderCalendarSource.cpp
    Code/GBTreeWordStore.cpp
    Code/GNewMailDlg.cpp
    Code/RemoteCalendarSource.cpp
    Code/SearchView.cpp
    Code/HtmlToText.cpp
    Code/ImpExp_Mbox.cpp
    Code/Imp_CsvContacts.cpp
    Code/Imp_Eml.cpp
    Code/Imp_Eudora.cpp
    Code/Imp_Mozilla.cpp
    Code/Imp_NetscapeContacts.cpp
    Code/Imp_OutlookExpress.cpp
    Code/ManageMailStores.cpp
    Code/MContainer.cpp
    Code/ObjectInspector.cpp
    Code/OptionsDlg.cpp
    Code/PreviewPanel.cpp
    Code/PrintContext.cpp
    Code/PrintPreview.cpp
    Code/ReplicateDlg.cpp
    Code/ScribeAbout.cpp
    Code/ScribeAccount.cpp
    Code/ScribeAccountPreview.cpp
    Code/ScribeAccountUI.cpp
    Code/ScribeApp.cpp
    Code/ScribeAttachment.cpp
    Code/ScribeContact.cpp
    Code/ScribeFilter.cpp
    Code/ScribeFinder.cpp
    Code/ScribeFolder.cpp
    Code/ScribeFolderDlg.cpp
    Code/ScribeFolderProp.cpp
    Code/ScribeFolderSelect.cpp
    Code/ScribeFolderTree.cpp
    Code/ScribeGroup.cpp
    Code/ScribeItemList.cpp
    Code/ScribeLangDlg.cpp
    Code/ScribeListAddr.cpp
    Code/ScribeMail.cpp
    Code/ScribeMain.cpp
    Code/ScribePageSetup.cpp
    Code/ScribePassword.cpp
    Code/ScribeSendReceive.cpp
    Code/ScribeSockets.cpp
    Code/ScribeStatusPanel.cpp
    Code/ScribeThing.cpp
    Code/ScribeUi.cpp
    Code/ScribeUtils.cpp
    Code/Scripting.cpp
    Code/SecurityDlg.cpp
    Code/Sqlite/v3.6.14/sqlite3.c
    Code/Store3Common.cpp
    Code/Store3Imap/ScribeImap_Attachment.cpp
    Code/Store3Imap/ScribeImap_Folder.cpp
    Code/Store3Imap/ScribeImap_Mail.cpp
    Code/Store3Imap/ScribeImap_Store.cpp
    Code/Store3Imap/ScribeImap_Thread.cpp
    Code/Store3Mail3/Mail3Attachment.cpp
    Code/Store3Mail3/Mail3Calendar.cpp
    Code/Store3Mail3/Mail3Contact.cpp
    Code/Store3Mail3/Mail3Filter.cpp
    Code/Store3Mail3/Mail3Folder.cpp
    Code/Store3Mail3/Mail3Group.cpp
    Code/Store3Mail3/Mail3Mail.cpp
    Code/Store3Mail3/Mail3Store.cpp
    Code/Store3Webdav/WebdavCalendar.cpp
    Code/Store3Webdav/WebdavContact.cpp
    Code/Store3Webdav/WebdavFolder.cpp
    Code/Store3Webdav/WebdavStore.cpp
    Code/Store3Webdav/WebdavThread.cpp
    Code/TitlePage.cpp
    Utils/Tables/MailFlags.c
    )

if (WIN32)

    target_sources(scribe PRIVATE
        Code/Store3Mapi/ScribeMapi_Attachment.cpp
        Code/Store3Mapi/ScribeMapi_Calendar.cpp
        Code/Store3Mapi/ScribeMapi_Contact.cpp
        Code/Store3Mapi/ScribeMapi_Folder.cpp
        Code/Store3Mapi/ScribeMapi_Mail.cpp
        Code/Store3Mapi/ScribeMapi_Store.cpp
        Code/Store3Mapi/ScribeMapi_Thing.cpp
        Code/ImpExp_Outlook.cpp
        ${LGI_DIR}/src/common/Coding/ExternCall.asm
        )

endif()

target_include_directories(scribe
    PRIVATE
        Code
        Code/Sqlite
        Resources
        Utils/Tables
        $<TARGET_PROPERTY:libpng16,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:libjpeg9a,INCLUDE_DIRECTORIES>
        ${LGI_DIR}/private/common
        ${ASPELL_DIR}/interfaces/cc
        ${OPENSSL_INCLUDE_DIR})

target_link_libraries(scribe
    PRIVATE
        lgi
        chardet
        libjpeg9a
        libpng16
        bzip2
        btree
        ntlm)

target_compile_definitions(scribe
    PRIVATE
        -DSCRIBE_APP)

if (APPLE)

    macBundlePostBuild(scribe)

elseif (LINUX)
    
    target_link_libraries(scribe
        PRIVATE
            dl)

    add_dependencies(scribe crash-handler)

    add_custom_command(TARGET scribe POST_BUILD
        COMMENT "Copy crash handler."
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:crash-handler>" "${CMAKE_CURRENT_BINARY_DIR}"
        )

    set(DEPLOY ${CODELIB}/appimagetool-x86_64.AppImage)
    if (NOT EXISTS ${DEPLOY})
        message(WARNING "No appimage tool at '${DEPLOY}', skipping 'dist' target.")
    else()

        execute_process(COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/Utils/getversion.py
            OUTPUT_VARIABLE VERSION
            RESULT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (result)
            message(FATAL_ERROR "Error getting scribe version. result=${result}")
        endif()

        message("VERSION=${VERSION}")

        set(APPIMG ${VERSION}.AppImage)
        set(APPDIR ${VERSION}.AppDir)
        set(BINDIR ${APPDIR}/usr/bin)
        set(LIBDIR ${APPDIR}/usr/lib)
        set(RESDIR ${APPDIR}/usr/share/applications)

        add_custom_target(dist-linux
            COMMAND rm -rf ${APPDIR}
            COMMAND echo "Building AppDir..."
            COMMAND mkdir -p ${BINDIR}
            COMMAND mkdir -p ${LIBDIR}
            COMMAND mkdir -p ${RESDIR}
            COMMAND echo "Copying binaries..."
            # executables:
            COMMAND cp $<TARGET_FILE:scribe>        ${BINDIR}
            COMMAND cp $<TARGET_FILE:crash-handler> ${BINDIR}
            # libraries:
            COMMAND cp $<TARGET_FILE:lgi>           ${LIBDIR}
            COMMAND cp $<TARGET_FILE:aspell>        ${LIBDIR}
            COMMAND cp $<TARGET_FILE:chardet>       ${LIBDIR}
            COMMAND cp $<TARGET_FILE:libpng16>      ${LIBDIR}
            COMMAND ln -s ./$<TARGET_FILE_NAME:libpng16> ${LIBDIR}/libpng16.so 
            COMMAND cp $<TARGET_FILE:zlib>          ${LIBDIR}
            COMMAND cp $<TARGET_FILE:libjpeg9a>     ${LIBDIR}
            # Resources:
            COMMAND echo "Copy resources..."
            COMMAND chmod 700 ${CMAKE_CURRENT_LIST_DIR}/Resources/AppRun
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/AppRun               ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Linux/scribe.desktop ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/About64px.png        ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Scribe.lr8           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Flags.png            ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Icons-16.png         ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/xgate-icons-32.png   ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/About.html           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/About64px.png        ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Title.html           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Title.png            ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/NoFace*.png          ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/EmojiMap.png         ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/Preview*.html        ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/tray_*.png           ${RESDIR}
            # Help and scripting:
            COMMAND echo "Creating DOM documentation..."
            COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/Code/Py/DomScan.py > ${RESDIR}/Dom.txt
            COMMAND mkdir -p ${RESDIR}/Help/scripting
            COMMAND echo "Help files..."
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Help/*.html                    ${RESDIR}/Help
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Help/*.css                     ${RESDIR}/Help
            COMMAND cp ${LGI_DIR}/docs/scripting/*.html                         ${RESDIR}/Help/scripting
            COMMAND cp ${LGI_DIR}/docs/scripting/*.css                          ${RESDIR}/Help/scripting
            COMMAND mkdir -p ${RESDIR}/Scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Scripts/*.script               ${RESDIR}/Scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Scripts/ScribeScripts.h        ${RESDIR}/Scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/resdefs.h            ${RESDIR}/Scripts
            # Spell check:
            COMMAND mkdir -p ${RESDIR}/Aspell/data
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/Resources/aspell-languages.csv ${RESDIR}/Aspell
            COMMAND cp ${ASPELL_DIR}/data/*.cmap ${RESDIR}/Aspell/data
            COMMAND cp ${ASPELL_DIR}/data/*.cset ${RESDIR}/Aspell/data
            COMMAND cp ${ASPELL_DIR}/data/*.kbd ${RESDIR}/Aspell/data
            # Output appimage:
            COMMAND @echo "Building AppImage..."
            COMMAND ${DEPLOY} ${APPDIR}
        )


    endif()

elseif (WIN32)

    target_include_directories(scribe
        PRIVATE
            ../libs/mapi)

    if (CMAKE_BUILD_TYPE MATCHES Release)
    
        # get_cmake_property(_variableNames VARIABLES)
        # list (SORT _variableNames)
        # foreach (_variableName ${_variableNames})
        #     message(STATUS "${_variableName}=${${_variableName}}")
        # endforeach()

        find_program(NSIS
            makensis.exe
            PATHS "C:/Program Files (x86)/NSIS/Bin")
        if (NOT EXISTS ${NSIS})
            message(WARNING "No NSIS installed, skipping dist target.")
        else()
            set(NSI_IN "${CMAKE_CURRENT_LIST_DIR}/Windows/Installer-64bit.nsi.in")
            if (NOT EXISTS ${NSI_IN})
                message(FATAL_ERROR "Expecting to see ${NSI_IN}")
            endif()
            set(NSI_OUT "${CMAKE_BINARY_DIR}/installer-${CMAKE_INSTALL_CONFIG_NAME}.nsi")

            file(READ ${NSI_IN} NSI_CONTENT)
            file(GENERATE
                OUTPUT ${NSI_OUT}
                CONTENT "${NSI_CONTENT}"
            )

            # file(TO_NATIVE_PATH ${NSI} NSI)

            add_custom_target(dist-win
                COMMAND echo "cmd: ${NSIS} ${NSI_OUT}"
                COMMAND ${NSIS} ${NSI_OUT})
        endif()

    endif()

endif()

if (NOT APPLE)

    add_custom_command(TARGET scribe POST_BUILD
        COMMENT "Copy resource files."
        COMMAND cp -R "${CMAKE_CURRENT_LIST_DIR}/Resources" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND cp -R "${CMAKE_CURRENT_LIST_DIR}/Scripts" "${CMAKE_CURRENT_BINARY_DIR}"
        )

endif()