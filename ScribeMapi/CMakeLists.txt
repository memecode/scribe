

add_library(scribe-mapi SHARED
	${LGI_DIR}/src/common/Gdc2/Colour.cpp
    ${LGI_DIR}/src/common/Hash/md5/md5.c
    ${LGI_DIR}/src/common/Net/Base64.cpp
    ${LGI_DIR}/src/common/Net/Mime.cpp
    ${LGI_DIR}/src/common/Net/Uri.cpp
    ${LGI_DIR}/src/common/Net/MDStringToDigest.cpp
    ${LGI_DIR}/src/common/Lgi/MemStream.cpp
    ${LGI_DIR}/src/common/Lgi/Object.cpp
    ${LGI_DIR}/src/common/Lgi/OptionsFile.cpp
    ${LGI_DIR}/src/common/Text/XmlTree.cpp
    ${LGI_DIR}/src/common/Text/String.cpp
    Code/ScribeMapi.cpp
	)

target_link_libraries(scribe-mapi
    PRIVATE
        lgi-stub)

target_include_directories(scribe-mapi
    PRIVATE
        )

target_compile_definitions(scribe-mapi
    PRIVATE
        LGI_STATIC
        WINDOWS
        _USRDLL
        SCRIBEMAPI_EXPORTS)