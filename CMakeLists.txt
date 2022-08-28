cmake_minimum_required(VERSION 3.0.0)
project(sseClock VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
enable_testing()

include(FetchContent)
#find_package(Threads REQUIRED)

set(FMT_SYSTEM_HEADERS ON CACHE INTERNAL "Expose headers with marking them as system.")
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 8.1.1
    UPDATE_DISCONNECTED ON
    )
FetchContent_MakeAvailable(fmt)
target_compile_definitions(fmt INTERFACE FMT_STRING)

set(JSON_Diagnostics ON CACHE INTERNAL "")
set(JSON_SystemInclude ON CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.10.5
    UPDATE_DISCONNECTED ON
    )
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib options
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE INTERNAL "")
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.11.1
    UPDATE_DISCONNECTED ON
    )
FetchContent_MakeAvailable(httplib)

add_executable(sseClock main.cpp)
target_link_libraries(sseClock -static)
target_link_libraries(sseClock nlohmann_json::nlohmann_json fmt::fmt httplib)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
