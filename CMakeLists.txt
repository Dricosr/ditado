cmake_minimum_required(VERSION 3.28)
project(ditado VERSION 0.3.0 LANGUAGES CXX RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/gen/version.h @ONLY)

add_executable(ditado WIN32
  src/main.cpp
  src/log.cpp
  src/config.cpp
  src/tray.cpp
  src/hotkey.cpp
  src/settings.cpp
  src/audio.cpp
  src/gate.cpp
  src/inject.cpp
  src/history.cpp
  src/som.cpp
  src/app.rc
)
target_include_directories(ditado PRIVATE src ${CMAKE_BINARY_DIR}/gen third_party)
target_link_libraries(ditado PRIVATE
  comdlg32 ole32 shell32 user32 gdi32 wininet advapi32 winmm)
target_compile_features(ditado PRIVATE cxx_std_20)
target_compile_options(ditado PRIVATE /utf-8 /W4 /EHsc /permissive-)
