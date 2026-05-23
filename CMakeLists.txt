cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)

project(ModPlayerNeow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# include_directories(${PROJECT_SOURCE_DIR}/include)

add_library(ThinMix
    src/ThinMix/ThinMix.cpp
    src/ThinMix/ThinMixPort_PortAudio.cpp
)

target_include_directories(ThinMix PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ThinMix
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(ThinMix PUBLIC
    ${PORTAUDIO_LIBRARIES}
)

add_executable(ModPlayerNeow
    src/binio_helper.c
    src/LFO.cpp
    src/mod_file.cpp
    src/main.cpp
)

target_link_libraries(ModPlayerNeow ThinMix)