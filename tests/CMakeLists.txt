find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

qt_add_executable(tst_thememanager
    tst_thememanager.cpp
    ../src/core/thememanager.cpp
    ../src/core/thememanager.h
)
target_include_directories(tst_thememanager PRIVATE "${PROJECT_SOURCE_DIR}/src/core")
target_link_libraries(tst_thememanager PRIVATE Qt6::Gui Qt6::Test)
set_target_properties(tst_thememanager PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_resources(tst_thememanager "test_themes"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_THEME_FILES}
)
add_test(NAME core.thememanager
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_thememanager>" -o "${CMAKE_CURRENT_BINARY_DIR}/thememanager-results.txt,txt"
)
set_tests_properties(core.thememanager PROPERTIES LABELS "core;smoke" TIMEOUT 15)

qt_add_executable(tst_imageprobe
    tst_imageprobe.cpp
    ../src/sources/imageprobe.cpp
    ../src/sources/imageprobe.h
)
target_include_directories(tst_imageprobe PRIVATE "${PROJECT_SOURCE_DIR}/src/sources")
target_link_libraries(tst_imageprobe PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.imageprobe
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_imageprobe>"
        -o "-,txt"
)
set_tests_properties(core.imageprobe PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_scenemanager
    tst_scenemanager.cpp
    ../src/core/scenemanager.cpp
    ../src/core/scenemanager.h
    ../src/sources/imageprobe.cpp
    ../src/sources/imageprobe.h
)
target_include_directories(tst_scenemanager PRIVATE
    "${PROJECT_SOURCE_DIR}/src/sources"
    "${PROJECT_SOURCE_DIR}/src/core"
)
target_link_libraries(tst_scenemanager PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.scenemanager
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_scenemanager>"
        -o "-,txt"
)
set_tests_properties(core.scenemanager PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow
    tst_mainwindow.cpp
    ../src/core/thememanager.cpp
    ../src/core/thememanager.h
    ../src/mainwindow.cpp
    ../src/mainwindow.h
    ../src/core/scenemanager.cpp
    ../src/core/scenemanager.h
    ../src/sources/imageprobe.cpp
    ../src/sources/imageprobe.h
)

target_include_directories(tst_mainwindow PRIVATE
    "${PROJECT_SOURCE_DIR}/src/core"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/sources"
)
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Widgets Qt6::Test Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg
)
set_target_properties(tst_mainwindow PROPERTIES WIN32_EXECUTABLE FALSE)

# Embed the same source at the application's resource URL.
qt_add_resources(tst_mainwindow "test_ui"
    PREFIX "/OpenLiveStudio/Ui"
    BASE "${PROJECT_SOURCE_DIR}/src"
    FILES ${OPENLIVE_STUDIO_QML_FILES}
)

qt_add_resources(tst_mainwindow "test_icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

qt_add_resources(tst_mainwindow "mainwindow_themes"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_THEME_FILES}
)

# This smoke test needs no desktop; future GPU tests need real GL.
add_test(NAME ui.mainwindow
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen"
        "QT_QUICK_BACKEND=software"
        "QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../qml"
        "$<TARGET_FILE:tst_mainwindow>"
        -o "-,txt"
)
# Timeout covers the complete UI suite; allow headroom for QML setup and CI variance.
set_tests_properties(ui.mainwindow PROPERTIES
    LABELS "ui;smoke"
    TIMEOUT 120
)
