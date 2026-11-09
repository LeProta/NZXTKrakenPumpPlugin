# Tests sans matériel de PumpWorker (Qt seul : ni hidapi ni lhwm-wrapper requis).
#   cmake -S tests -B build-tests -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
#   cmake --build build-tests --config Release && ctest --test-dir build-tests -C Release
cmake_minimum_required(VERSION 3.16)
project(NZXTKrakenPumpTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Test)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_executable(tst_pumpworker tst_pumpworker.cpp fakes.cpp fakes.h
               ${SRC}/ui/PumpWorker.cpp ${SRC}/ui/PumpWorker.h)
target_include_directories(tst_pumpworker PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tst_pumpworker PRIVATE /utf-8)
target_compile_definitions(tst_pumpworker PRIVATE NOMINMAX)
target_link_libraries(tst_pumpworker PRIVATE Qt5::Core Qt5::Test)

enable_testing()
add_test(NAME tst_pumpworker COMMAND tst_pumpworker)   # Qt bin/ doit être dans le PATH
