cmake_minimum_required(VERSION 3.16)
project(MacFans VERSION 2.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

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
target_link_libraries(macfans-helper PRIVATE "-framework IOKit" "-framework CoreFoundation")

# --- main app bundle ---
set(APP_ICON "${CMAKE_SOURCE_DIR}/resources/AppIcon.icns")
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_source_files_properties(src/macnative.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

qt_add_executable(MacFans
    src/main.cpp
    src/alerts.cpp          src/alerts.h
    src/appsettings.cpp     src/appsettings.h
    src/charts.cpp          src/charts.h
    src/curveeditor.cpp     src/curveeditor.h
    src/fancard.cpp         src/fancard.h
    src/fanmodel.cpp        src/fanmodel.h
    src/gauge.cpp           src/gauge.h
    src/hidsensors.cpp      src/hidsensors.h
    src/history.h
    src/macnative.mm        src/macnative.h
    src/mainwindow.cpp      src/mainwindow.h
    src/pages.cpp           src/pages.h
    src/powermon.cpp        src/powermon.h
    src/procmon.cpp         src/procmon.h
    src/procview.cpp        src/procview.h
    src/prowidgets.cpp      src/prowidgets.h
    src/sensortree.cpp      src/sensortree.h
    src/smc.cpp             src/smc.h
    src/sysmon.cpp          src/sysmon.h
    src/theme.h
    src/traypopup.cpp       src/traypopup.h
    resources/resources.qrc
    ${APP_ICON}
)

target_link_libraries(MacFans PRIVATE Qt6::Widgets
    "-framework IOKit"
    "-framework CoreFoundation"
    "-framework AppKit"
    "-framework UserNotifications"
    IOReport
)

# Menu-bar-first app: LSUIElement keeps it out of the Dock until a window is
# opened (the app switches its activation policy at runtime).
set_target_properties(MacFans PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "MacFans"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.macfans.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
)

# Place the helper inside the bundle (Contents/MacOS) so the app can install it.
add_dependencies(MacFans macfans-helper)
add_custom_command(TARGET MacFans POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:macfans-helper>
        $<TARGET_BUNDLE_CONTENT_DIR:MacFans>/MacOS/macfans-helper
    COMMENT "Bundling macfans-helper")
