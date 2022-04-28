# cmake -G "Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=Debug ..
cmake_minimum_required (VERSION 3.12)

cmake_policy(SET CMP0054 NEW)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(CMAKE_GENERATOR STREQUAL Xcode)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)
endif()

project (Scribe)

if (UNIX AND NOT APPLE)
	set(LINUX TRUE)
endif()
get_filename_component(SCRIBE "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
get_filename_component(SCRIBELIBS "${SCRIBE}/../libs" REALPATH)
get_filename_component(ROOT "${SCRIBE}/../.." REALPATH)
get_filename_component(LGI "${ROOT}/Lgi/trunk" REALPATH)
get_filename_component(CODELIB "${ROOT}/../CodeLib" REALPATH)
get_filename_component(VCPKG "${CODELIB}/vcpkg" ABSOLUTE)
message(STATUS "SCRIBE=${SCRIBE}")
message(STATUS "ROOT=${ROOT}")
message(STATUS "CODELIB=${CODELIB}")

add_subdirectory("${LGI}/src/linux" LgiDir)

if (NOT DEFINED ${CMAKE_BUILD_TYPE})
	set(CMAKE_BUILD_TYPE "DEBUG")
endif()
if (CMAKE_BUILD_TYPE STREQUAL "DEBUG")
	add_definitions("-D_DEBUG")
endif()

if (APPLE)

	set(GTK3_PATH "/Users/Shared/inst")
	if (NOT EXISTS ${GTK3_PATH})
		set(GTK3_PATH "${ROOT}/inst")
		if (NOT EXISTS ${GTK3_PATH})
			message(FATAL_ERROR "Where is GTK3?")
		endif()
	endif()
	message(STATUS "Scribe.GTK3_PATH=${GTK3_PATH}")

	add_definitions("-DSCRIBE_APP -DMAC")
	SET(GUI_TYPE MACOSX_BUNDLE)

	get_filename_component(LIBJPEG_SEARCH "${CODELIB}/libjpeg-9a" ABSOLUTE)

	add_subdirectory(${LIBJPEG_SEARCH} libjpeg_dir)

elseif (LINUX)

	add_definitions("-DSCRIBE_APP -DLINUX -DPOSIX -fPIC -w -fno-inline -fpermissive")
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
	find_package(PNG REQUIRED)
	
elseif (MSVC)

	enable_language(ASM_MASM)

	if ("${CMAKE_GENERATOR}" MATCHES "(Win64|IA64)")
		set(WORDSIZE 64)
		set(OPENSSL_INCLUDE_DIR "c:/Program Files/OpenSSL-Win64/include")
	else()
		set(WORDSIZE 32)
		set(OPENSSL_INCLUDE_DIR "c:/Program Files (x86)/OpenSSL-Win32/include")
	endif()
	
	if (NOT EXISTS ${OPENSSL_INCLUDE_DIR})
		message(FATAL_ERROR "OpenSSL not found @ ${OPENSSL_INCLUDE_DIR}")
	endif()
	
	if (${MSVC_VERSION} GREATER_EQUAL 1900)
		set(VS_VER 14)
	elseif (${MSVC_VERSION} EQUAL 1800)
		set(VS_VER 12)
	else()
		message(FATAL_ERROR "Unknown Visual Studio version: ${MSVC_VERSION}")
	endif()

	add_definitions("/W1 -DSCRIBE_APP -DHAVE_STRING_H -DWINDOWS -DWIN32 -DLGI_RES -D_UNICODE -DUNICODE")
	get_filename_component(LIBPNG_SEARCH "${CODELIB}/libpng" ABSOLUTE)
	get_filename_component(ZLIB_SEARCH "${CODELIB}/zlib" ABSOLUTE)
	get_filename_component(LIBJPEG_SEARCH "${CODELIB}/libjpeg-9a" ABSOLUTE)
	
	add_subdirectory(${LIBPNG_SEARCH} libpng_dir)
	add_subdirectory(${LIBJPEG_SEARCH} libjpeg_dir)
	
endif ()

add_subdirectory(${SCRIBELIBS}/aspell-0.60.6.1 aspell_dir)

set (CommonSource
	
	# Headers
	${SCRIBE}/Code/Scribe.h
	${SCRIBE}/Code/ScribeInc.h
	${SCRIBE}/Code/ScribePrivate.h

	# Accounts
	${SCRIBE}/Code/ScribeAccount.cpp
	${SCRIBE}/Code/ScribeAccountPreview.cpp
	${SCRIBE}/Code/ScribeAccountPreview.h
	${SCRIBE}/Code/ScribeAccountUI.cpp
	${SCRIBE}/Code/ScribeAccountUI.h
	${SCRIBE}/Code/ScribeListAddr.cpp
	${SCRIBE}/Code/ScribeListAddr.h
	${SCRIBE}/Code/ScribeSendReceive.cpp

	# App
	${SCRIBE}/Code/InScribeKey.cpp
	${SCRIBE}/Code/ReplicateDlg.cpp
	${SCRIBE}/Code/ReplicateDlg.h
	${SCRIBE}/Code/ScribeApp.cpp
	${SCRIBE}/Code/ScribeMain.cpp
	${SCRIBE}/Code/ScribeUtils.cpp
	${SCRIBE}/Code/ScribeUtils.h

	# BTree
	${SCRIBELIBS}/Btree/btree.c
	${SCRIBELIBS}/Btree/btree_delete.c
	${SCRIBELIBS}/Btree/btree_header.c
	${SCRIBELIBS}/Btree/btree_insert.c
	${SCRIBELIBS}/Btree/btree_lock.c
	${SCRIBELIBS}/Btree/btree_node.c
	${SCRIBELIBS}/Btree/btree_search.c
	${SCRIBELIBS}/Btree/btree_traverse.c
	${SCRIBELIBS}/Btree/db.c
	${SCRIBELIBS}/Btree/db_blocklist.c
	${SCRIBELIBS}/Btree/db_blocks.c
	${SCRIBELIBS}/Btree/db_cache.c
	${SCRIBELIBS}/Btree/db_header.c
	${SCRIBELIBS}/Btree/db_lock.c
	${SCRIBELIBS}/Btree/db_utils.c
	${SCRIBELIBS}/Btree/gbtree.cpp
	${SCRIBELIBS}/Btree/hashtable.c
	${SCRIBELIBS}/Btree/offsetlist.c

	# BZip
	${SCRIBELIBS}/bzip2-1.0.6/blocksort.c
	${SCRIBELIBS}/bzip2-1.0.6/bzlib.c
	${SCRIBELIBS}/bzip2-1.0.6/compress.c
	${SCRIBELIBS}/bzip2-1.0.6/crctable.c
	${SCRIBELIBS}/bzip2-1.0.6/decompress.c
	${SCRIBELIBS}/bzip2-1.0.6/huffman.c
	${SCRIBELIBS}/bzip2-1.0.6/randtable.c

	# Calendar
	${SCRIBE}/Code/Calendar.cpp
	${SCRIBE}/Code/Calendar.h
	${SCRIBE}/Code/CalendarView.cpp
	${SCRIBE}/Code/CalendarView.h
	${LGI}/src/common/Widgets/GDatePopup.cpp
	${LGI}/src/common/Widgets/GTimePopup.cpp
	${LGI}/src/common/Widgets/MonthView.cpp
	${LGI}/src/common/Widgets/YearView.cpp

	# Components
	${SCRIBE}/Code/Components.cpp
	${SCRIBE}/Code/Components.h

	# Controls
	${LGI}/src/common/Text/Emoji/EmojiMap.cpp
	${LGI}/src/common/Text/Emoji/EmojiTools.cpp
	${LGI}/src/common/Gdc2/Font/LEmojiFont.cpp
	${LGI}/src/common/Lgi/GBrowser.cpp
	${LGI}/src/common/Text/GHtml.cpp
	${LGI}/include/common/GHtml.h
	${LGI}/src/common/Text/GHtmlCommon.cpp
	${LGI}/include/common/GHtmlCommon.h
	${LGI}/src/common/Text/GHtmlParser.cpp
	${LGI}/include/common/GHtmlParser.h
	${LGI}/include/common/GHtmlPriv.h
	${SCRIBE}/Code/HtmlToText.cpp
	${LGI}/src/common/INet/HttpTools.cpp
	${LGI}/src/common/Widgets/Editor/BlockCursor.cpp
	${LGI}/src/common/Widgets/Editor/GRichTextEdit.cpp
	${LGI}/include/common/GRichTextEdit.h
	${LGI}/src/common/Widgets/Editor/GRichTextEditPriv.cpp
	${LGI}/src/common/Widgets/Editor/GRichTextEditPriv.h
	${LGI}/src/common/Widgets/Editor/HorzRuleBlock.cpp
	${LGI}/src/common/Widgets/Editor/ImageBlock.cpp
	${LGI}/src/common/Widgets/Editor/TextBlock.cpp

	${SCRIBE}/Code/DomType.h

	${SCRIBE}/Code/Encryption/GnuPG.cpp
	${SCRIBE}/Code/Encryption/GnuPG.h

	${SCRIBE}/Code/BayesianFilter.cpp
	${SCRIBE}/Code/BayesianFilter.h
	${LGI}/src/common/Widgets/GFilterUi.cpp
	${SCRIBE}/Code/ScribeFilter.cpp
	${SCRIBE}/Code/GBTreeWordStore.cpp

	${LGI}/src/common/Gdc2/Filters/Jpeg.cpp
	${LGI}/src/common/Gdc2/Filters/Lzw.cpp
	${LGI}/src/common/Gdc2/Filters/Gif.cpp
	${LGI}/src/common/Gdc2/Filters/Png.cpp
	${LGI}/src/common/Gdc2/Filters/Tiff.cpp

	${SCRIBE}/Code/Exp_Scribe.cpp
	${LGI}/src/common/Db/GDb-Csv.cpp
	${LGI}/include/common/GTextFile.h
	${SCRIBE}/Code/Imp_CsvContacts.cpp
	${SCRIBE}/Code/Imp_Eml.cpp
	${SCRIBE}/Code/Imp_Eudora.cpp
	${SCRIBE}/Code/Imp_Mozilla.cpp
	${SCRIBE}/Code/Imp_NetscapeContacts.cpp
	${SCRIBE}/Code/Imp_Outlook.h
	${SCRIBE}/Code/Imp_OutlookExpress.cpp
	${SCRIBE}/Code/ImpExp_Mbox.cpp
	# ${SCRIBE}/Code/ImpExp_Outlook.cpp
	${LGI}/src/common/Text/RtfHtml.cpp
	${LGI}/src/common/Text/vCard-vCal.cpp


	${SCRIBE}/Code/MContainer.cpp
	${LGI}/src/common/INet/LOAuth2.cpp
	${LGI}/include/common/LOAuth2.h
	${LGI}/src/common/INet/GMime.cpp
	${LGI}/src/common/INet/IHttp.cpp
	${LGI}/src/common/INet/Mail.cpp
	${LGI}/src/common/INet/MailHttp.cpp
	${LGI}/src/common/INet/MailImap.cpp
	${SCRIBE}/Code/ScribeSockets.cpp

	${SCRIBE}/Code/PrintContext.cpp
	${SCRIBE}/Code/PrintContext.h
	${SCRIBE}/Code/ScribeAttachment.cpp
	${SCRIBE}/Code/ScribeContact.cpp
	${SCRIBE}/Code/ScribeFolder.cpp
	${SCRIBE}/Code/ScribeFolderTree.cpp
	${SCRIBE}/Code/ScribeGroup.cpp
	${SCRIBE}/Code/ScribeItemList.cpp
	${SCRIBE}/Code/ScribeMail.cpp
	${SCRIBE}/Code/ScribeThing.cpp

	${LGI}/src/common/Coding/ExternCall.asm
	${LGI}/src/common/Coding/GLexCpp.cpp
	${LGI}/src/common/Coding/GScriptCompiler.cpp
	${LGI}/include/common/GScripting.h
	${LGI}/src/common/Coding/GScriptingPriv.h
	${LGI}/src/common/Coding/GScriptLibrary.cpp
	${LGI}/src/common/Coding/GScriptVM.cpp
	${LGI}/src/common/Coding/Instructions.h
	${SCRIBE}/Code/Scripting.cpp

	${SCRIBE}/Code/SpellCheckAspell.cpp

	${LGI}/src/common/INet/OpenSSLSocket.cpp
	${LGI}/include/common/OpenSSLSocket.h

	${LGI}/src/common/Db/LDbTable.cpp
	${LGI}/include/common/LDbTable.h
	${SCRIBE}/Code/Store3Imap/ScribeImap.h
	${SCRIBE}/Code/Store3Imap/ScribeImap_Attachment.cpp
	${SCRIBE}/Code/Store3Imap/ScribeImap_Folder.cpp
	${SCRIBE}/Code/Store3Imap/ScribeImap_Mail.cpp
	${SCRIBE}/Code/Store3Imap/ScribeImap_Store.cpp
	${SCRIBE}/Code/Store3Imap/ScribeImap_Thread.cpp
	${SCRIBE}/Code/Sqlite/v3.6.14/sqlite3.c
	${SCRIBE}/Code/Sqlite/v3.6.14/sqlite3.h
	${SCRIBE}/Code/Sqlite/v3.6.14/sqlite3ext.h
	${SCRIBE}/Code/Store3Mail3/Mail3.h
	${SCRIBE}/Code/Store3Mail3/Mail3Attachment.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Calendar.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Contact.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Filter.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Folder.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Group.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Mail.cpp
	${SCRIBE}/Code/Store3Mail3/Mail3Store.cpp
	${LGI}/include/common/Store3.h
	${SCRIBE}/Code/Store3Common.cpp
	${SCRIBE}/Code/Store3Common.h
	${LGI}/include/common/Store3Defs.h
	${LGI}/include/common/Store3MimeTree.h
	${SCRIBE}/Utils/Tables/MailFlags.c
	${SCRIBE}/Utils/Tables/Tables.h

	${SCRIBE}/Code/ScribeAbout.cpp
	${SCRIBE}/Code/ScribeFinder.cpp
	${SCRIBE}/Code/ObjectInspector.cpp
	${SCRIBE}/Code/ObjectInspector.h
	${SCRIBE}/Code/OptionsDlg.cpp
	${SCRIBE}/Code/PreviewPanel.cpp
	${SCRIBE}/Code/PreviewPanel.h
	${SCRIBE}/Code/PrintPreview.cpp
	${SCRIBE}/Code/PrintPreview.h
	${SCRIBE}/Code/ScribeStatusPanel.cpp
	${SCRIBE}/Code/ScribeStatusPanel.h
	${SCRIBE}/Code/TitlePage.cpp
	${SCRIBE}/Code/AddressSelect.h
	${SCRIBE}/Code/AddressSelect.cpp
	${SCRIBE}/Code/BayesDlg.cpp
	${SCRIBE}/Code/GNewMailDlg.cpp
	${SCRIBE}/Code/GNewMailDlg.h
	${SCRIBE}/Code/GSearchView.cpp
	${SCRIBE}/Code/GSearchView.h
	${SCRIBE}/Code/ManageMailStores.cpp
	${SCRIBE}/Code/ManageMailStores.h
	${SCRIBE}/Code/ScribeFolderDlg.cpp
	${SCRIBE}/Code/ScribeFolderDlg.h
	${SCRIBE}/Code/ScribeFolderProp.cpp
	${SCRIBE}/Code/ScribeFolderSelect.cpp
	${SCRIBE}/Code/ScribeFolderSelect.h
	${SCRIBE}/Code/ScribeLangDlg.cpp
	${SCRIBE}/Code/ScribePageSetup.cpp
	${SCRIBE}/Code/ScribePageSetup.h
	${SCRIBE}/Code/ScribePassword.cpp
	${SCRIBE}/Code/ScribeUi.cpp
	${SCRIBE}/Code/SecurityDlg.cpp

	# LGI
	${LGI}/src/common/Lgi/LgiMain.cpp
	${LGI}/src/common/Gdc2/GDrawListSurface.cpp
	${LGI}/include/common/GDrawListSurface.h
	${LGI}/src/common/Widgets/GControlTree.cpp
	${LGI}/src/common/Gdc2/Tools/GdcTools.cpp
	${LGI}/src/common/General/GProperties.cpp
	${LGI}/src/common/General/GSegmentTree.cpp
	${LGI}/src/common/General/GSharedMemory.cpp
	${LGI}/src/common/General/GSoftwareUpdate.cpp
	${LGI}/src/common/General/GTimeZone.cpp
	${LGI}/src/common/General/GTnef.cpp
	${LGI}/src/common/Text/GXmlTreeUi.cpp
	${LGI}/src/common/INet/IFtp.cpp
	${LGI}/src/common/Widgets/GZoomView.cpp
	${LGI}/src/common/Widgets/GColourSelect.cpp
	${LGI}/src/common/Lgi/GProcess.cpp
	)

set(CommonResourceFiles
	${SCRIBE}/Resources/Scribe.lr8
	${SCRIBE}/Resources/resdefs.h
	${SCRIBE}/Resources/EmojiMap.png
	${SCRIBE}/Resources/About64px.png
	${SCRIBE}/Resources/About.html
	${SCRIBE}/Resources/Flags.gif
	${SCRIBE}/Resources/Icons.gif
	${SCRIBE}/Resources/Title.gif
	${SCRIBE}/Resources/Title.html
	${SCRIBE}/Resources/xgate-icons-32.png
	${SCRIBE}/Resources/NoFace80.png
	${SCRIBE}/Resources/NoFace160.png
	${SCRIBE}/Resources/PreviewMail.html
	${SCRIBE}/Resources/PreviewContact.html
	${SCRIBE}/Resources/aspell-languages.csv
	${SCRIBE}/Resources/tray_error.png
	${SCRIBE}/Resources/tray_mail.png
	${SCRIBE}/Resources/tray_small.png
	${SCRIBE}/Help
	${SCRIBE}/Scripts
	)

set(WindowsSource
	${SCRIBE}/Code/Store3Mapi/ScribeMapi.h
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Attachment.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Calendar.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Contact.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Folder.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Mail.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Store.cpp
	${SCRIBE}/Code/Store3Mapi/ScribeMapi_Thing.cpp
)

if (APPLE)
	set(MacResourceFiles
		${SCRIBE}/Resources/Icons/Email.icns
		${SCRIBE}/Resources/scribe-mac-icon.icns
		${SCRIBE}/MacCarbon/English.lproj/InfoPlist.strings
		${SCRIBE}/MacCarbon/English.lproj/main.nib)

	set(MACOSX_BUNDLE_ICON_FILE scribe-mac-icon.icns)
	set_source_files_properties(${CommonResourceFiles} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
	set_source_files_properties(${MacResourceFiles} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

	add_executable(Scribe MACOSX_BUNDLE
				${CommonSource}
				# ${LGI}/src/mac/cocoa/Lgi/SpellCheckMac.mm
				${CommonResourceFiles}
				${MacResourceFiles})

	message(STATUS "${GTK3_PATH}/include")
	target_include_directories(Scribe PUBLIC
							${GTK3_PATH}/include)
	target_link_libraries(Scribe Lgi aspell ${EXTRA_LIBS})
	set_target_properties(Scribe PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_FRAMEWORK_IDENTIFIER com.memecode.Scribe
		RESOURCE "${RESOURCE_FILES}"
		)
	set(RESOURCE_DEST "../Resources")
	
	# set(MACOSX_BUNDLE_ICON_FILE ".icns")
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "mv"
						"$<TARGET_FILE:Lgi>"
						"$<TARGET_FILE_DIR:Scribe>"
					COMMENT "Copying Lgi to output directory")
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "${CMAKE_COMMAND}" -E copy
						"$<TARGET_FILE:libjpeg9a>"
						"$<TARGET_FILE_DIR:Scribe>"
					COMMENT "Copying libjpeg to output directory")
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "${CMAKE_COMMAND}" -E copy
						"/opt/local/lib/libssl.dylib"
						"$<TARGET_FILE_DIR:Scribe>"
					COMMENT "Copying libssl")
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "${CMAKE_COMMAND}" -E copy
						"/opt/local/lib/libcrypto.1.1.dylib"
						"$<TARGET_FILE_DIR:Scribe>"
					COMMENT "Copying libcrypto")

	# Aspell components
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "${CMAKE_COMMAND}" -E copy
						"$<TARGET_FILE:aspell>"
						"$<TARGET_FILE_DIR:Scribe>")
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_directory
						${SCRIBELIBS}/aspell-0.60.6.1/data
						$<TARGET_FILE_DIR:Scribe>/../Resources/Aspell/data)

	# Fixup the paths for all the libraries
	get_filename_component(RPATH_PY "${CMAKE_CURRENT_LIST_DIR}/fix-rpaths.py" REALPATH)
	add_custom_command(TARGET Scribe POST_BUILD
					COMMAND "/opt/local/bin/python" "${RPATH_PY}" "$<TARGET_FILE:Scribe>"
					COMMENT "Fixing relative paths")

	# Copy over header for scripts.
	add_custom_command(	TARGET Scribe
						POST_BUILD
						COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../Resources/resdefs.h $<TARGET_FILE_DIR:Scribe>/${RESOURCE_DEST})

elseif (LINUX)
	add_executable(Scribe ${CommonSource})
	target_include_directories(Scribe PUBLIC
								${GTK3_INCLUDE_DIRS})
	target_link_libraries(Scribe Lgi pthread ${GTK2_LIBRARIES} ${PNG_LIBRARY})
	set(RESOURCE_DEST "Resources")
elseif (MSVC)

	set(WindowsResourceFiles
		)

	add_executable(Scribe WIN32
				${CommonSource}
				${LGI}/src/win32/Lgi/SpellCheckWindows.cpp
				${SCRIBE}/Code/Resource.rc
				${CommonResourceFiles}
				${WindowsResourceFiles}
				${WindowsSource})
	target_include_directories(Scribe PUBLIC
							${VCPKG}/installed/x64-windows/include)
	target_link_libraries(	Scribe
							Lgi
							libpng15
							libjpeg9a)

	set(RESOURCE_DEST "Resources")
	set(AllResources ${CommonResourceFiles})
	list(APPEND AllResources ${WindowsResourceFiles})
	add_custom_command(	TARGET Scribe
						POST_BUILD
						COMMAND ${CMAKE_COMMAND} -E make_directory ${RESOURCE_DEST}
						WORKING_DIRECTORY $<TARGET_FILE_DIR:Scribe>)
	foreach (FILE ${AllResources})
		if (IS_DIRECTORY ${FILE})
			get_filename_component(LEAF ${FILE} NAME)
			add_custom_command(	TARGET Scribe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${FILE} $<TARGET_FILE_DIR:Scribe>/${LEAF})
		else()
			add_custom_command(	TARGET Scribe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${FILE} $<TARGET_FILE_DIR:Scribe>/${RESOURCE_DEST})
		endif()
	endforeach()
	
endif ()

target_include_directories(Scribe PUBLIC
						${SCRIBE}/Code
						${SCRIBE}/Utils/Tables
						${SCRIBELIBS}/aspell-0.60.6.1/interfaces/cc
						${SCRIBELIBS}/mapi
						${PNG_INCLUDE_DIRS}
						${JPEG_INCLUDE_DIRS})

target_include_directories (Scribe PUBLIC
							"${ROOT}/include/common"
							"${ROOT}/include/linux/gtk"
							"../Resources"
							${PNG_INCLUDE_DIR}
							${OPENSSL_INCLUDE_DIR}
							"${CODELIB}/libiconv-1.14/include")
