cmake_minimum_required(VERSION 3.20)

# bridge the MAPI world to the real world via IMAP.
project(mapiConnector)

set(LGI_LIB_ONLY TRUE) # don't need utils
add_subdirectory(../../../../lgi/trunk lgi)

add_executable(mapiConnector
    src/main.cpp)

target_link_libraries(mapiConnector
    PRIVATE
        lgi)