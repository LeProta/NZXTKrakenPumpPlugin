cmake_minimum_required(VERSION 3.16)
# Release forcé (avant project : CMake initialise sinon Debug sous MSVC -> Qt5*d.dll
# + /MD, refusé par OpenRGB). lhwm-cpp-wrapper.lib est compilé en Release /MD.
set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
project(NZXTKrakenPumpPlugin LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "NZXTKrakenPumpPlugin targets Windows x64 only.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt : chemin par défaut si non fourni (comme les CMakeLists d'origine)
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt5_DIR AND NOT DEFINED ENV{CMAKE_PREFIX_PATH}
   AND EXISTS "C:/Qt/5.15.2/msvc2019_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/5.15.2/msvc2019_64")
endif()
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

# Disposition locale d'origine (third_party/, non versionné) utilisée si présente
foreach(_dep hidapi)
    string(TOUPPER ${_dep} _DEP)
    if(NOT ${_DEP}_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}")
        set(${_DEP}_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}")
    endif()
endforeach()

# hidapi : vcpkg (CMAKE_TOOLCHAIN_FILE) ou -DHIDAPI_ROOT=<dossier avec include/hidapi/hidapi.h + lib>
find_path(HIDAPI_INCLUDE_DIR hidapi/hidapi.h HINTS ${HIDAPI_ROOT} PATH_SUFFIXES include)
find_library(HIDAPI_LIBRARY NAMES hidapi HINTS ${HIDAPI_ROOT} PATH_SUFFIXES lib)
if(NOT HIDAPI_INCLUDE_DIR OR NOT HIDAPI_LIBRARY)
    message(FATAL_ERROR "hidapi not found: pass -DHIDAPI_ROOT=... or the vcpkg toolchain.")
endif()

# lhwm-cpp-wrapper : third_party/ local s'il existe, sinon clone de
# https://github.com/LeProta/lhwm-wrapper à côté de ce dépôt
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lhwm-cpp-wrapper/lhwm-cpp-wrapper.h")
    set(_LHWM_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lhwm-cpp-wrapper")
else()
    set(_LHWM_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../lhwm-wrapper/lhwm-cpp-wrapper/lhwm-cpp-wrapper")
endif()
set(LHWM_WRAPPER_DIR "${_LHWM_DEFAULT}"
    CACHE PATH "Folder containing lhwm-cpp-wrapper.h and x64/Release/lhwm-cpp-wrapper.lib")
if(NOT EXISTS "${LHWM_WRAPPER_DIR}/lhwm-cpp-wrapper.h"
   OR NOT EXISTS "${LHWM_WRAPPER_DIR}/x64/Release/lhwm-cpp-wrapper.lib")
    message(FATAL_ERROR "lhwm-cpp-wrapper not found in LHWM_WRAPPER_DIR=${LHWM_WRAPPER_DIR}")
endif()
get_filename_component(LHWM_WRAPPER_DIR "${LHWM_WRAPPER_DIR}" ABSOLUTE)   # retire les '..' (NMake + chemins longs)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
# ADLX SDK (AMD, GPUOpen) : en-têtes + helper de chargement d'amdadlx64.dll.
# Vérifier la licence du SDK ("ADLX SDK License Agreement.pdf" du dépôt AMD) avant
# de redistribuer third_party/adlx.
set(ADLX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/adlx")
if(NOT EXISTS "${ADLX_ROOT}/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp")
    message(FATAL_ERROR "ADLX SDK not found in ${ADLX_ROOT} (see https://github.com/GPUOpen-LibrariesAndSDKs/ADLX)")
endif()
list(APPEND PLUGIN_SOURCES
    "${ADLX_ROOT}/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp"
    "${ADLX_ROOT}/SDK/Platform/Windows/WinAPIs.cpp")

add_library(NZXTKrakenPumpPlugin SHARED ${PLUGIN_SOURCES})
target_include_directories(NZXTKrakenPumpPlugin PRIVATE src ${HIDAPI_INCLUDE_DIR} ${LHWM_WRAPPER_DIR} ${ADLX_ROOT})
# /EHa : les exceptions .NET de lhwm-cpp-wrapper (C++/CLI) arrivent en SEH ; sans
# /EHa les catch(...) qui entourent les appels LHWM:: ne les interceptent pas.
# /utf-8 : sources UTF-8 sans BOM (libellés UI accentués, °, —). /bigobj : lib C++/CLI.
target_compile_options(NZXTKrakenPumpPlugin PRIVATE /EHa /utf-8 /bigobj /W3)
target_compile_definitions(NZXTKrakenPumpPlugin PRIVATE NOMINMAX _USE_MATH_DEFINES
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)   # qlist.h Qt 5.15 + STL VS 2022
target_link_libraries(NZXTKrakenPumpPlugin PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets
    ${HIDAPI_LIBRARY}
    "${LHWM_WRAPPER_DIR}/x64/Release/lhwm-cpp-wrapper.lib"
    hid setupapi winhttp)
