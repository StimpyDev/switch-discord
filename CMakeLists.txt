cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Source /etc/profile.d/devkit-env.sh in MSYS2.")
endif()

include($ENV{DEVKITPRO}/cmake/Switch.cmake)

project(switchcord CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS source/*.cpp)
list(APPEND SOURCES vendor/qrcodegen.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE source vendor)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

target_link_libraries(${PROJECT_NAME} PRIVATE
    nx
    PkgConfig::CURL
    PkgConfig::JANSSON
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
)

set(NRO_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
set(NRO_NACP "${CMAKE_CURRENT_BINARY_DIR}/switchcord.nacp")
nx_generate_nacp(OUTPUT "${NRO_NACP}" NAME "Switchcord" AUTHOR "StimpyDev" VERSION "1.0.3")
nx_create_nro(${PROJECT_NAME} ICON "${NRO_ICON}" NACP "${NRO_NACP}")
