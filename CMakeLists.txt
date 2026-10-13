cmake_minimum_required(VERSION 3.20)
project(imabikisou-port LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
# Self-contained executables: no GCC runtime DLLs needed next to them.
if(MINGW)
  add_link_options(-static)
endif()

add_library(imbk_core
  src/core/file.cpp
  src/core/iso9660.cpp
  src/core/game.cpp
  src/core/sfo.cpp
  src/core/afs.cpp
  src/core/scr.cpp
  src/core/sn4.cpp
  src/core/sns.cpp
  src/core/snspatch.cpp
  src/core/tsv.cpp
  src/core/sjis.cpp
  src/core/crc32.cpp
)
target_include_directories(imbk_core PUBLIC src)
if(NOT WIN32)
  find_package(Iconv REQUIRED)
  target_link_libraries(imbk_core PUBLIC Iconv::Iconv)
endif()

add_library(imbk_engine
  src/engine/image.cpp
  src/engine/player.cpp
)
target_link_libraries(imbk_engine PUBLIC imbk_core)

add_executable(imbk src/tools/imbk.cpp)
target_link_libraries(imbk PRIVATE imbk_core)

# Prototype player (Windows/GDI for now; the engine itself is platform independent).
if(WIN32)
  add_executable(imbk_player src/player/main_win32.cpp)
  target_link_libraries(imbk_player PRIVATE imbk_engine gdi32 user32)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(imbk_tests tests/tests.cpp)
  target_link_libraries(imbk_tests PRIVATE imbk_engine)
  add_test(NAME imbk_tests COMMAND imbk_tests)
endif()
