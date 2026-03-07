cmake_minimum_required(VERSION 3.20)
project(AutoPlanner VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)
FetchContent_MakeAvailable(json)

FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)

add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})

add_executable(autoplanner
    src/main.cpp
    src/database.cpp
    src/routes.cpp
    src/seed.cpp
)

target_include_directories(autoplanner PRIVATE src)
target_link_libraries(autoplanner PRIVATE httplib::httplib nlohmann_json::nlohmann_json sqlite3_lib)
target_link_libraries(autoplanner PRIVATE pthread)
