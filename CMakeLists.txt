cmake_minimum_required(VERSION 3.20)
project(imabikisou-port LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Self-contained Windows tools: no Visual C++ runtime to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

add_executable(imbk src/tools/imbk.cpp)
if(MSVC)
  target_sources(imbk PRIVATE src/tools/imbk.manifest)
endif()
target_link_libraries(imbk PRIVATE imbk_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(imbk_tests tests/tests.cpp)
  target_link_libraries(imbk_tests PRIVATE imbk_core)
  add_test(NAME imbk_tests COMMAND imbk_tests)
endif()

# Windows launcher: first-run setup from the user's disc image, language
# choice, settings and Play. Dear ImGui (MIT) on Direct3D 11.
if(WIN32)
  set(IMGUI_DIR launcher/third_party/imgui)
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(imgui PUBLIC ${IMGUI_DIR})
  if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
  else()
    target_compile_options(imgui PRIVATE -w)
  endif()
  add_executable(ImabikisoLauncher WIN32 launcher/main.cpp launcher/installer.cpp)
  add_dependencies(ImabikisoLauncher imbk)
  target_link_libraries(ImabikisoLauncher PRIVATE imgui d3d11 dxgi d3dcompiler dwmapi bcrypt ole32 shell32)
endif()
