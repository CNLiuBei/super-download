cmake_minimum_required(VERSION 3.20)
project(super-download LANGUAGES CXX)

# ── C++17 standard ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler options ────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── FetchContent dependencies ──────────────────────────────────
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent GoogleTest from overriding compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# RapidCheck
FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(rapidcheck)

# ── libcurl ─────────────────────────────────────────────────────
# Build libcurl from source via FetchContent (no system install needed)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)   # Windows native TLS
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)            # We only need HTTP/HTTPS
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_1
)
FetchContent_MakeAvailable(curl)

# Create an alias so our code can use CURL::libcurl
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl_static)
endif()

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ── Subdirectories ─────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/gui)

# ── Testing ────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
