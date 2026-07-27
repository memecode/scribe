cmake_minimum_required(VERSION 3.20)

# bridge the MAPI world to the real world via IMAP.
project(mapiConnector)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LGI_LIB_ONLY TRUE) # don't need utils
add_subdirectory(../../../../lgi/trunk lgi)

set(LGI_FILES
    ${LGI_DIR}/include/lgi/common/Com.h
    ${LGI_DIR}/include/lgi/common/Mail.h
    ${LGI_DIR}/include/lgi/common/Store3.h
    ${LGI_DIR}/src/common/Text/TextConvert.cpp
    ${LGI_DIR}/src/common/Net/Mail.cpp
    ${LGI_DIR}/src/common/Net/Mime.cpp
    ${LGI_DIR}/src/common/Net/OAuth2.cpp
    ${LGI_DIR}/src/common/Net/OpenSSLSocket.cpp
    ${LGI_DIR}/src/common/Net/Http.cpp)

set(STORE_MAPI_FILES
    ../../src/Store3Mapi/ScribeMapi.h
    ../../src/Store3Mapi/ScribeMapi_Store.cpp
    ../../src/Store3Mapi/ScribeMapi_Mail.cpp
    ../../src/Store3Mapi/ScribeMapi_Calendar.cpp
    ../../src/Store3Mapi/ScribeMapi_Contact.cpp
    ../../src/Store3Mapi/ScribeMapi_Folder.cpp
    ../../src/Store3Mapi/ScribeMapi_Thing.cpp
    ../../src/Store3Mapi/ScribeMapi_Attachment.cpp
    
    ../../src/Store3Common.h
    ../../src/Store3Common.cpp
    ../../src/StrToDom.cpp
    
    ../../../libs/mapi/MAPIX.h)

add_executable(mapiConnector
    src/main.cpp
    src/Context.cpp
    src/FolderMeta.cpp
    src/smtpServer.cpp
    src/smtpServer.h
    src/context.h

    ${STORE_MAPI_FILES}
    ${LGI_FILES})

source_group("Source Files\\Lgi" FILES ${LGI_FILES})
source_group("Source Files\\Mapi" FILES ${STORE_MAPI_FILES})

target_link_libraries(mapiConnector
    PRIVATE
        lgi)

target_include_directories(mapiConnector
    PRIVATE
        ../../src
        ../../src/Store3Mapi
        ../../resources
        ../../../libs/mapi
        ../../../libs/build-x64/include
        ${LGI_DIR}/../deps/build-x64/include)

if(WIN32)
    target_include_directories(mapiConnector
        PRIVATE
            "C:\\Program Files\\OpenSSL-Win64\\include")
endif()

target_compile_definitions(mapiConnector
    PRIVATE
        MAPI_CONNECTOR)

target_compile_definitions(mapiConnector
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>)
