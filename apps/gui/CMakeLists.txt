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
target_compile_definitions(gui_app PRIVATE "MK61EMU_VERSION=\"${PROJECT_VERSION}\"")
