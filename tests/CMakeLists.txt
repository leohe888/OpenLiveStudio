find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

qt_add_executable(tst_imageloader
    tst_imageloader.cpp
    ../src/media/imageloader.cpp
    ../src/media/imageloader.h
)
target_include_directories(tst_imageloader PRIVATE "${PROJECT_SOURCE_DIR}/src/media")
target_link_libraries(tst_imageloader PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.imageloader
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_imageloader>"
        -o "-,txt"
)
set_tests_properties(core.imageloader PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_studiostate
    tst_studiostate.cpp
    ../src/scene/studiostate.cpp
    ../src/scene/studiostate.h
    ../src/media/imageloader.cpp
    ../src/media/imageloader.h
)
target_include_directories(tst_studiostate PRIVATE
    "${PROJECT_SOURCE_DIR}/src/media"
    "${PROJECT_SOURCE_DIR}/src/scene"
)
target_link_libraries(tst_studiostate PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.studiostate
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_studiostate>"
        -o "-,txt"
)
set_tests_properties(core.studiostate PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow
    tst_mainwindow.cpp
    ../src/ui/mainwindow.cpp
    ../src/ui/mainwindow.h
    ../src/scene/studiostate.cpp
    ../src/scene/studiostate.h
    ../src/media/imageloader.cpp
    ../src/media/imageloader.h
)

target_include_directories(tst_mainwindow PRIVATE
    "${PROJECT_SOURCE_DIR}/src/ui"
    "${PROJECT_SOURCE_DIR}/src/media"
    "${PROJECT_SOURCE_DIR}/src/scene"
)
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Widgets Qt6::Test Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg
)

# Embed the same source at the application's resource URL.
qt_add_resources(tst_mainwindow "test_ui"
    PREFIX "/OpenLiveStudio/Ui"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/qml/ScenesPanel.qml"
          "${PROJECT_SOURCE_DIR}/qml/SourcesPanel.qml"
          "${PROJECT_SOURCE_DIR}/qml/MixerPanel.qml"
          "${PROJECT_SOURCE_DIR}/qml/ControlsPanel.qml"
          "${PROJECT_SOURCE_DIR}/qml/AboutDialog.qml"
          "${PROJECT_SOURCE_DIR}/qml/SceneNameDialog.qml"
          "${PROJECT_SOURCE_DIR}/qml/SceneRemovalDialog.qml"
          "${PROJECT_SOURCE_DIR}/qml/SourceAddDialog.qml"
          "${PROJECT_SOURCE_DIR}/qml/SourceRemovalDialog.qml"
)

qt_add_resources(tst_mainwindow "test_icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
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
    TIMEOUT 90
)
