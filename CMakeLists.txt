cmake_minimum_required(VERSION 3.23)

option(BUILD_TESTING "Build tests" OFF)
option(STATIC_MSVC_RUNTIME "Static link MSVC runtime" OFF)

if (BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(mempatcher LANGUAGES CXX VERSION 0.2.1.0)

include(cmake/GetGitRevisionDescription.cmake)

if (STATIC_MSVC_RUNTIME AND CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(spdlog CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cc
    src/util.cc
    src/hooks.cc
    src/patch.cc
    src/parser.cc
    src/memory.cc
    res/mempatcher.rc
)

target_link_libraries(${PROJECT_NAME} spdlog::spdlog)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/macros)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

get_git_head_revision(GIT_REFSPEC GIT_COMMIT_HASH)

if (NOT GIT_REFSPEC)
    set(GIT_REFSPEC "release")
endif()

string(REPLACE "refs/heads/" "" GIT_BRANCH_NAME ${GIT_REFSPEC})
string(SUBSTRING ${GIT_COMMIT_HASH} 0 8 GIT_COMMIT_HASH_SHORT)

configure_file(src/buildinfo.h.in macros/buildinfo.h @ONLY)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()