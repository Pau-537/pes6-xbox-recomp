cmake_minimum_required(VERSION 3.15)
project(GameInstaller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # static runtime: the exe needs no Visual C++ redistributable
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /W3 /EHsc)
elseif(MINGW)
  add_link_options(-static)
endif()

add_library(installer_core STATIC
  core/util.cpp core/sha256.cpp core/xiso.cpp core/manifest.cpp core/installer.cpp)
target_include_directories(installer_core PUBLIC core)
if(WIN32)
  target_link_libraries(installer_core PUBLIC ole32 shell32 uuid)
endif()

add_executable(installer-cli cli/main.cpp)
target_link_libraries(installer-cli PRIVATE installer_core)

if(WIN32)
  add_executable(installer-gui WIN32 gui/main_win.cpp)
  target_link_libraries(installer-gui PRIVATE installer_core comctl32 shell32 ole32 comdlg32 user32 gdi32)
endif()
