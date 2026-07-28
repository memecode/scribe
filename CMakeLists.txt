cmake_minimum_required (VERSION 3.18)
project(scribe)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (APPLE)

	set(CMAKE_OSX_ARCHITECTURES "x86_64;ARM64")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
elseif (UNIX)
    set(LINUX TRUE)
elseif (WIN32)
    # Turn on symbols for release build, before we include other targets
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
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

get_filename_component(SCRIBE_LIBS ${CMAKE_CURRENT_LIST_DIR}/../libs ABSOLUTE)
set(ASPELL_DIR "${SCRIBE_LIBS}/aspell-0.60.6.1")
if (NOT EXISTS ${ASPELL_DIR})
    message(FATAL_ERROR "Missing ASPELL_DIR=${ASPELL_DIR}")
endif()

if (WIN32)
    add_subdirectory(ScribeMapi)
endif()

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
	set(APP_ICON resources/About64px.png)
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
    src/AddressSelect.cpp
    src/BayesDlg.cpp
    src/BayesianFilter.cpp
    src/Calendar.cpp
    src/CalendarView.cpp
    src/Components.cpp
    src/Encryption/GnuPG.cpp
    src/Exp_Scribe.cpp
    src/FolderCalendarSource.cpp
    src/GBTreeWordStore.cpp
    src/NewMailDlg.cpp
    src/RemoteCalendarSource.cpp
    src/SearchView.cpp
    src/HtmlToText.cpp
    src/ImpExp_Mbox.cpp
    src/Imp_CsvContacts.cpp
    src/Imp_Eml.cpp
    src/Imp_Eudora.cpp
    src/Imp_Mozilla.cpp
    src/Imp_NetscapeContacts.cpp
    src/Imp_OutlookExpress.cpp
    src/ManageMailStores.cpp
    src/MContainer.cpp
    src/ObjectInspector.cpp
    src/OptionsDlg.cpp
    src/PreviewPanel.cpp
    src/PrintContext.cpp
    src/PrintPreview.cpp
    src/ReplicateDlg.cpp
    src/ScribeAbout.cpp
    src/ScribeAccount.cpp
    src/ScribeAccountPreview.cpp
    src/ScribeAccountUI.cpp
    src/ScribeApp.cpp
    src/ScribeAttachment.cpp
    src/ScribeContact.cpp
    src/ScribeFilter.cpp
    src/ScribeFinder.cpp
    src/ScribeFolder.cpp
    src/ScribeFolderDlg.cpp
    src/ScribeFolderProp.cpp
    src/ScribeFolderSelect.cpp
    src/ScribeFolderTree.cpp
    src/ScribeGroup.cpp
    src/ScribeItemList.cpp
    src/ScribeLangDlg.cpp
    src/ScribeListAddr.cpp
    src/ScribeMail.cpp
    src/ScribeMain.cpp
    src/ScribePageSetup.cpp
    src/ScribePassword.cpp
    src/ScribeSendReceive.cpp
    src/ScribeSockets.cpp
    src/ScribeStatusPanel.cpp
    src/ScribeThing.cpp
    src/ScribeUi.cpp
    src/ScribeUtils.cpp
    src/Scripting.cpp
    src/SecurityDlg.cpp
    src/StrToDom.cpp
    src/ScribeIpc.cpp
    src/Sqlite/v3.50.1/sqlite3.c
    src/Store3Common.cpp
    src/Store3Imap/ScribeImap_Attachment.cpp
    src/Store3Imap/ScribeImap_Folder.cpp
    src/Store3Imap/ScribeImap_Mail.cpp
    src/Store3Imap/ScribeImap_Store.cpp
    src/Store3Imap/ScribeImap_Thread.cpp
    src/Store3Mail3/Mail3Attachment.cpp
    src/Store3Mail3/Mail3Calendar.cpp
    src/Store3Mail3/Mail3Contact.cpp
    src/Store3Mail3/Mail3Filter.cpp
    src/Store3Mail3/Mail3Folder.cpp
    src/Store3Mail3/Mail3Group.cpp
    src/Store3Mail3/Mail3Mail.cpp
    src/Store3Mail3/Mail3Store.cpp
    src/Store3Webdav/WebdavCalendar.cpp
    src/Store3Webdav/WebdavContact.cpp
    src/Store3Webdav/WebdavFolder.cpp
    src/Store3Webdav/WebdavStore.cpp
    src/Store3Webdav/WebdavThread.cpp
    src/TitlePage.cpp
    utils/Tables/MailFlags.c
    )

if (WIN32)

    target_sources(scribe PRIVATE
        src/Store3Mapi/ScribeMapi_Attachment.cpp
        src/Store3Mapi/ScribeMapi_Calendar.cpp
        src/Store3Mapi/ScribeMapi_Contact.cpp
        src/Store3Mapi/ScribeMapi_Folder.cpp
        src/Store3Mapi/ScribeMapi_Mail.cpp
        src/Store3Mapi/ScribeMapi_Store.cpp
        src/Store3Mapi/ScribeMapi_Thing.cpp
        src/ImpExp_Outlook.cpp
        src/Resource.rc
        ${LGI_DIR}/src/common/Coding/ExternCall.asm
        )

    target_compile_options(scribe PRIVATE
        $<IF:$<CONFIG:Release>,/Zi,>
        )

    target_link_options(scribe PRIVATE
        $<IF:$<CONFIG:Release>,/DEBUG /OPT:REF /OPT:ICF,>)

endif()

target_include_directories(scribe
    PRIVATE
        src
        src/Sqlite
        resources
        utils/Tables
        ${LGI_DIR}/private/common
        ${ASPELL_DIR}/interfaces/cc
        ${OPENSSL_INCLUDE_DIR})

target_link_libraries(scribe
    PRIVATE
        lgi
        chardet
        bzip2
        btree
        ntlm)

target_compile_definitions(scribe
    PRIVATE
        -DSCRIBE_APP)

if (APPLE)

    macBundlePostBuild(scribe)

elseif (LINUX)
    
    target_include_directories(scribe
        PRIVATE
            $<$<CONFIG:Debug>:${LGI_DEPS_DEBUG}/include>
            $<$<NOT:$<CONFIG:Debug>>:${LGI_DEPS_RELEASE}/include>
            $<$<CONFIG:Debug>:${SCRIBE_LIBS}/build-x64-debug/include>
            $<$<NOT:$<CONFIG:Debug>>:${SCRIBE_LIBS}/build-x64-release/include>
            )

    target_link_libraries(scribe
        PRIVATE
            dl)

    target_link_directories(scribe
        PRIVATE
            $<$<CONFIG:Debug>:${LGI_DEPS_DEBUG}/lib>
            $<$<NOT:$<CONFIG:Debug>>:${LGI_DEPS_RELEASE}/lib>)

    add_dependencies(scribe crash-handler)

    add_custom_command(TARGET scribe POST_BUILD
        COMMENT "Copy crash handler."
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:crash-handler>" "${CMAKE_CURRENT_BINARY_DIR}"
        )

    get_filename_component(DEPLOY "~/apps/appimagetool-x86_64.AppImage" ABSOLUTE)
    if (NOT EXISTS ${DEPLOY})
        message(WARNING "No appimage tool at '${DEPLOY}', skipping 'dist' target.")
    else()

        execute_process(COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/utils/getversion.py
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

        # we're building libpng here, because for linux putting it in the Lgi deps breaks
        # things for linux (if you put that lib folder in the path)
        include(FetchContent)
        FetchContent_Declare(zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG        v1.3.2)
        FetchContent_Declare(libpng
            GIT_REPOSITORY https://github.com/pnggroup/libpng.git
            GIT_TAG        v1.6.58)

        # Make zlib available first, then force libpng to find this zlib.
        FetchContent_MakeAvailable(zlib)

        # Use FindZLIB module mode (not ZLIBConfig.cmake), because the build-tree
        # config package can reference export files that are not always generated.
        unset(ZLIB_DIR CACHE)
        set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
        set(ZLIB_ROOT "${zlib_BINARY_DIR}" CACHE PATH "Path to fetched zlib build" FORCE)
        set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "Fetched zlib include dir" FORCE)
        set(ZLIB_LIBRARY "${zlib_BINARY_DIR}/libz.so" CACHE FILEPATH "Fetched zlib library" FORCE)
        set(ZLIB_LIBRARY_RELEASE "${zlib_BINARY_DIR}/libz.so" CACHE FILEPATH "Fetched zlib release library" FORCE)
        set(ZLIB_LIBRARY_DEBUG "${zlib_BINARY_DIR}/libz.so" CACHE FILEPATH "Fetched zlib debug library" FORCE)

        FetchContent_MakeAvailable(libpng)

        add_custom_target(dist-linux
            COMMAND rm -rf ${APPDIR}
            COMMAND echo "Building AppDir..."
            COMMAND mkdir -p ${BINDIR}
            COMMAND mkdir -p ${LIBDIR}
            COMMAND mkdir -p ${RESDIR}
            COMMAND echo "Copying binaries..."
            # executables:
            COMMAND cp $<TARGET_FILE:scribe>                    ${BINDIR}
            COMMAND cp $<TARGET_FILE:crash-handler>             ${BINDIR}
            # libraries:
            COMMAND cp $<TARGET_FILE:lgi>                       ${LIBDIR}
            COMMAND cp $<TARGET_FILE:aspell>                    ${LIBDIR}
            COMMAND cp $<TARGET_FILE:chardet>                   ${LIBDIR}
            COMMAND cp $<TARGET_FILE:png_shared>                ${LIBDIR}
            COMMAND ln -s ./$<TARGET_FILE_NAME:png_shared>      ${LIBDIR}/libpng.so 
            COMMAND cp $<TARGET_FILE:zlib>                      ${LIBDIR}
            COMMAND cp ${LGI_DEPS_RELEASE}/lib/libjpeg.so       ${LIBDIR}
            COMMAND cp ${LGI_DEPS_RELEASE}/lib/libplutovg.so.1  ${LIBDIR}
            # Resources:
            COMMAND echo "Copy resources..."
            COMMAND chmod 700 ${CMAKE_CURRENT_LIST_DIR}/resources/AppRun
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/AppRun               ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Linux/scribe.desktop ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/About64px.png        ${APPDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Scribe.lr8           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Flags.png            ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Icons-16.png         ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/xgate-icons-32.png   ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/About.html           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/About64px.png        ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Title.html           ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Title.png            ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/NoFace*.png          ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/EmojiMap.png         ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/Preview*.html        ${RESDIR}
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/tray_*.png           ${RESDIR}
            # Help and scripting:
            COMMAND echo "Creating DOM documentation..."
            COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/src/Py/DomScan.py > ${RESDIR}/Dom.txt
            COMMAND mkdir -p ${RESDIR}/help/scripting
            COMMAND echo "Help files..."
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/help/*.html                    ${RESDIR}/help
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/help/*.css                     ${RESDIR}/help
            COMMAND cp ${LGI_DIR}/docs/scripting/*.html                         ${RESDIR}/help/scripting
            COMMAND cp ${LGI_DIR}/docs/scripting/*.css                          ${RESDIR}/help/scripting
            COMMAND mkdir -p ${RESDIR}/scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/scripts/*.script               ${RESDIR}/scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/scripts/ScribeScripts.h        ${RESDIR}/scripts
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/resdefs.h            ${RESDIR}/scripts
            # Spell check:
            COMMAND mkdir -p ${RESDIR}/aspell/data
            COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/resources/aspell-languages.csv ${RESDIR}/aspell
            COMMAND cp ${ASPELL_DIR}/data/*.cmap                                ${RESDIR}/aspell/data
            COMMAND cp ${ASPELL_DIR}/data/*.cset                                ${RESDIR}/aspell/data
            COMMAND cp ${ASPELL_DIR}/data/*.kbd                                 ${RESDIR}/aspell/data
            # Output appimage:
            COMMAND echo "Building AppImage..."
            COMMAND "${DEPLOY}" "${APPDIR}"
        )

    endif()

elseif (WIN32)

    target_include_directories(scribe
        PRIVATE
            ../libs/mapi)

    if (FALSE)
        get_cmake_property(_variableNames VARIABLES)
        list (SORT _variableNames)
        foreach (_variableName ${_variableNames})
            message(STATUS "${_variableName}=${${_variableName}}")
        endforeach()
    endif()

    find_program(NSIS
        makensis.exe
        PATHS "C:/Program Files (x86)/NSIS/Bin")

    if (NOT EXISTS ${NSIS})

        message(WARNING "No NSIS installed, skipping dist target.")

    else()

        # Copy in the files that the installer will reference:
        file(COPY ${CMAKE_CURRENT_LIST_DIR}/utils/Store DESTINATION ${CMAKE_BINARY_DIR}/Utils)
        file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/Py DESTINATION ${CMAKE_BINARY_DIR}/Code)
        file(COPY ${CMAKE_CURRENT_LIST_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
        file(COPY ${CMAKE_CURRENT_LIST_DIR}/scripts DESTINATION ${CMAKE_BINARY_DIR})
        file(COPY ${CMAKE_CURRENT_LIST_DIR}/help DESTINATION ${CMAKE_BINARY_DIR})
        
        # Generate the installer:
        set(NSI_IN "${CMAKE_CURRENT_LIST_DIR}/win/Installer-64bit.nsi.in")
        if (NOT EXISTS ${NSI_IN})
            message(FATAL_ERROR "Expecting to see ${NSI_IN}")
        endif()
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/installer-$<CONFIG>.nsi" INPUT ${NSI_IN})

        # Add a target for it:
        add_custom_target(dist-win
            DEPENDS scribe ScribeMapi aspell Updater
            COMMAND "${NSIS}" "${CMAKE_BINARY_DIR}/installer-$<CONFIG>.nsi")
    
    endif()


endif()

if (NOT APPLE)

    add_custom_command(TARGET scribe POST_BUILD
        COMMENT "Copy resource files."
        COMMAND cp -R "${CMAKE_CURRENT_LIST_DIR}/resources" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND cp -R "${CMAKE_CURRENT_LIST_DIR}/scripts" "${CMAKE_CURRENT_BINARY_DIR}"
        )

endif()