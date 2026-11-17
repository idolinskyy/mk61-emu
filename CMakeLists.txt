cmake_minimum_required(VERSION 3.25)

project(
    mk61emu
    VERSION 1.0.1
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Опції збірки
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_APPS "Build CLI executable" ON)
option(BUILD_GUI "Build the Qt Widgets application" ON)
option(ENABLE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)

add_library(project_options INTERFACE)
add_library(Project::Options ALIAS project_options)

if(ENABLE_SANITIZERS)
    if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang on a Unix-like platform")
    endif()
    target_compile_options(project_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(project_options INTERFACE -fsanitize=address,undefined)
endif()

# Глобальна interface-ціль для суворих прапорців
add_library(project_warnings INTERFACE)
add_library(Project::Warnings ALIAS project_warnings)

if(ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(project_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Werror>)
endif()

target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive->
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic -Wnon-virtual-dtor>
)

# 2. Надвимогливі попередження: тільки для бізнес-логіки
add_library(project_strict_warnings INTERFACE)
add_library(Project::StrictWarnings ALIAS project_strict_warnings)

target_compile_options(project_strict_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:
        /w14242 /w14254 /w14265 /w14287 /w14826>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wconversion -Wsign-conversion -Wshadow>
)

# Підключення піддиректорій
add_subdirectory(libs/core)

if(BUILD_APPS)
    add_subdirectory(apps/cli)
endif()

if(BUILD_GUI)
    add_subdirectory(apps/gui)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy-22 clang-tidy-21 clang-tidy REQUIRED)
    foreach(project_target IN ITEMS core_lib cli_app gui_app unit_tests)
        if(TARGET ${project_target})
            set_target_properties(${project_target} PROPERTIES
                CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE};--warnings-as-errors=*")
        endif()
    endforeach()
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format-22 clang-format-21 clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE PROJECT_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/apps/*.cpp" "${PROJECT_SOURCE_DIR}/apps/*.hpp"
        "${PROJECT_SOURCE_DIR}/libs/*.cpp" "${PROJECT_SOURCE_DIR}/libs/*.hpp"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/*.hpp")
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${PROJECT_FORMAT_SOURCES}
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${PROJECT_FORMAT_SOURCES}
        VERBATIM)
endif()

include(GNUInstallDirs)

if(TARGET cli_app)
    install(TARGETS cli_app
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

set(MK61_BUNDLE_QT OFF)
if(TARGET gui_app AND UNIX AND NOT APPLE AND DEFINED Qt6_DIR)
    get_filename_component(MK61_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    if(NOT MK61_QT_PREFIX MATCHES "^/usr(/|$)")
        set(MK61_BUNDLE_QT ON)
        set(MK61_BUNDLE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/mk61emu")
    endif()
endif()

if(TARGET gui_app AND MK61_BUNDLE_QT)
    set_target_properties(gui_app PROPERTIES INSTALL_RPATH "$ORIGIN")
    install(TARGETS gui_app
        RUNTIME DESTINATION ${MK61_BUNDLE_LIBDIR})
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/mk61emu-wrapper.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mk61emu-wrapper.sh"
        @ONLY
        NEWLINE_STYLE UNIX)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/mk61emu-wrapper.sh"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME mk61emu-gui)
    install(CODE "
        set(MK61_QT_PREFIX \"${MK61_QT_PREFIX}\")
        set(MK61_BUNDLE_LIBDIR \"${MK61_BUNDLE_LIBDIR}\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallBundledQt.cmake\")
    ")
elseif(TARGET gui_app)
    install(TARGETS gui_app
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(TARGET gui_app)
    foreach(_mk61_icon_size 16 24 32 48 64 128 256 512)
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/hicolor/${_mk61_icon_size}x${_mk61_icon_size}/apps/mk61emu.png"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_mk61_icon_size}x${_mk61_icon_size}/apps")
    endforeach()
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/mk61emu.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.uk.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")

set(CPACK_PACKAGE_NAME "mk61emu")
set(CPACK_PACKAGE_VENDOR "Ihor Dolinskyi")
set(CPACK_PACKAGE_CONTACT "Ihor Dolinskyi <idolinskyy@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Emulator of the Elektronika MK-61 programmable calculator")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES TRUE)
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ihor Dolinskyi <idolinskyy@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    if(MK61_BUNDLE_QT)
        # У пакеті свій Qt з префікса збірки, системний libqt6widgets не тягнемо.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libx11-6, libx11-xcb1, libxcb1, libxkbcommon0, libxkbcommon-x11-0, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render0, libxcb-render-util0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxcb-util1, libxcb-glx0, libfontconfig1, libfreetype6, zlib1g, libglib2.0-0 | libglib2.0-0t64, libdbus-1-3, libzstd1")
    else()
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()
endif()
include(CPack)
