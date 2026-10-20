cmake_minimum_required(VERSION 3.16)
project(MacFans LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Help CMake find the Homebrew Qt6 on Apple Silicon.
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt6")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# --- privileged helper (no Qt; installed setuid-root on first use) ---
add_executable(macfans-helper
    src/helper_main.cpp
    src/smc.cpp
    src/smc.h
)
target_link_libraries(macfans-helper PRIVATE
    "-framework IOKit"
    "-framework CoreFoundation"
)

# --- main app bundle ---
set(APP_ICON "${CMAKE_SOURCE_DIR}/resources/AppIcon.icns")
set_source_files_properties(${APP_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")

qt_add_executable(MacFans
    src/main.cpp
    src/smc.cpp
    src/smc.h
    src/fanmodel.cpp
    src/fanmodel.h
    src/hidsensors.cpp
    src/hidsensors.h
    src/gauge.cpp
    src/gauge.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/traypopup.cpp
    src/traypopup.h
    resources/resources.qrc
    ${APP_ICON}
)

target_link_libraries(MacFans PRIVATE Qt6::Widgets
    "-framework IOKit"
    "-framework CoreFoundation"
)

# Build a proper .app bundle (menu-bar app, no Dock icon via LSUIElement).
set_target_properties(MacFans PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "MacFans"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.macfans.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
)

# Place the helper inside the bundle (Contents/MacOS) so the app can find and
# install it from a stable location next to its own binary.
add_dependencies(MacFans macfans-helper)
add_custom_command(TARGET MacFans POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:macfans-helper>
        $<TARGET_BUNDLE_CONTENT_DIR:MacFans>/MacOS/macfans-helper
    COMMENT "Bundling macfans-helper")
