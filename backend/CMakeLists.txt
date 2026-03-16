cmake_minimum_required(VERSION 3.14)
project(AutoPlanner VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_AZURE_SQL "Enable Azure SQL backend via ODBC" OFF)

include(FetchContent)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(json)

# Download SQLite amalgamation directly instead of FetchContent
# (avoids URL-in-list bug in older CMake)
set(SQLITE_DIR ${CMAKE_BINARY_DIR}/sqlite3)
if(NOT EXISTS ${SQLITE_DIR}/sqlite3.c)
    file(DOWNLOAD
        "https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip"
        ${CMAKE_BINARY_DIR}/sqlite3.zip
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/sqlite3.zip
        DESTINATION ${CMAKE_BINARY_DIR})
    file(RENAME ${CMAKE_BINARY_DIR}/sqlite-amalgamation-3450000 ${SQLITE_DIR})
endif()

add_library(sqlite3_lib STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${SQLITE_DIR})

find_package(OpenSSL REQUIRED)

add_executable(autoplanner
    src/main.cpp
    src/auth.cpp
    src/database.cpp
    src/routes.cpp
    src/seed.cpp
    src/sqlite_backend.cpp
    src/azuresql_backend.cpp
)

target_compile_definitions(autoplanner PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_include_directories(autoplanner PRIVATE src)
target_link_libraries(autoplanner PRIVATE httplib::httplib nlohmann_json::nlohmann_json sqlite3_lib OpenSSL::SSL OpenSSL::Crypto pthread)

if(USE_AZURE_SQL)
    find_library(ODBC_LIBRARY NAMES odbc32 odbc)
    target_compile_definitions(autoplanner PRIVATE HAS_AZURE_SQL)
    target_link_libraries(autoplanner PRIVATE ${ODBC_LIBRARY})
endif()
