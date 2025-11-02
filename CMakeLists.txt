cmake_minimum_required(VERSION 3.23)

project(mempatcher LANGUAGES CXX VERSION 0.2.1.0)

include(cmake/get_cpm.cmake)
include(cmake/GetGitRevisionDescription.cmake)

option(STATIC_MSVC_RUNTIME "Static link MSVC runtime" OFF)

if (STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

CPMAddPackage(
    NAME              spdlog
    GIT_TAG           v1.14.1
    GITHUB_REPOSITORY gabime/spdlog
)

add_library(${PROJECT_NAME} SHARED
    src/main.cc
    src/util.cc
    src/hooks.cc
    src/patch.cc
    src/parser.cc
    res/mempatcher.rc
)

target_link_libraries(${PROJECT_NAME} spdlog)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/macros)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME}64)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME}32)
else ()
    message(FATAL_ERROR "Unsupported architecture")
endif()

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