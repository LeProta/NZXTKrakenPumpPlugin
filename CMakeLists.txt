cmake_minimum_required(VERSION 3.16)
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

if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{CMAKE_PREFIX_PATH}
   AND EXISTS "C:/Qt/6.8.3/msvc2022_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

foreach(_dep hidapi)
    string(TOUPPER ${_dep} _DEP)
    if(NOT ${_DEP}_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}")
        set(${_DEP}_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}")
    endif()
endforeach()

find_path(HIDAPI_INCLUDE_DIR hidapi/hidapi.h HINTS ${HIDAPI_ROOT} PATH_SUFFIXES include)
find_library(HIDAPI_LIBRARY NAMES hidapi HINTS ${HIDAPI_ROOT} PATH_SUFFIXES lib)
if(NOT HIDAPI_INCLUDE_DIR OR NOT HIDAPI_LIBRARY)
    message(FATAL_ERROR "hidapi not found: pass -DHIDAPI_ROOT=... or the vcpkg toolchain.")
endif()

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
get_filename_component(LHWM_WRAPPER_DIR "${LHWM_WRAPPER_DIR}" ABSOLUTE)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
set(ADLX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/adlx")
if(NOT EXISTS "${ADLX_ROOT}/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp")
    message(FATAL_ERROR "ADLX SDK not found in ${ADLX_ROOT} (see https://github.com/GPUOpen-LibrariesAndSDKs/ADLX)")
endif()
list(APPEND PLUGIN_SOURCES
    "${ADLX_ROOT}/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp"
    "${ADLX_ROOT}/SDK/Platform/Windows/WinAPIs.cpp")

add_library(NZXTKrakenPumpPlugin SHARED ${PLUGIN_SOURCES})
target_include_directories(NZXTKrakenPumpPlugin PRIVATE src third_party/openrgb ${HIDAPI_INCLUDE_DIR} ${LHWM_WRAPPER_DIR} ${ADLX_ROOT})
target_compile_options(NZXTKrakenPumpPlugin PRIVATE /EHa /utf-8 /bigobj /W3)
target_compile_definitions(NZXTKrakenPumpPlugin PRIVATE NOMINMAX _USE_MATH_DEFINES
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
target_link_libraries(NZXTKrakenPumpPlugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${HIDAPI_LIBRARY}
    "${LHWM_WRAPPER_DIR}/x64/Release/lhwm-cpp-wrapper.lib"
    hid setupapi winhttp)
