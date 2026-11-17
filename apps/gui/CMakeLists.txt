find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(gui_app
    src/main.cpp
    resources/resources.qrc
)

set_target_properties(gui_app PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_link_libraries(gui_app PRIVATE
    mk61emu::Core
    Qt6::Widgets
    Project::Options
    Project::Warnings
    Project::StrictWarnings
)
set_target_properties(gui_app PROPERTIES OUTPUT_NAME "mk61emu-gui")

set(MK61_BUILD_INFO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/mk61_build_info.hpp")
add_custom_target(mk61_build_info
    COMMAND ${CMAKE_COMMAND}
        -DMK61_VERSION=${PROJECT_VERSION}
        -DMK61_TEMPLATE=${CMAKE_SOURCE_DIR}/cmake/mk61_build_info.hpp.in
        -DMK61_OUTPUT=${MK61_BUILD_INFO_HEADER}
        -P ${CMAKE_SOURCE_DIR}/cmake/write_build_info.cmake
    BYPRODUCTS ${MK61_BUILD_INFO_HEADER}
    COMMENT "Запис версії і часу збірки МК-61"
    VERBATIM)
add_dependencies(gui_app mk61_build_info)
target_include_directories(gui_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_source_files_properties(src/main.cpp PROPERTIES OBJECT_DEPENDS "${MK61_BUILD_INFO_HEADER}")
