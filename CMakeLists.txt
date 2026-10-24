cmake_minimum_required(VERSION 3.19)
project(OpenLiveStudio LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets QuickWidgets QuickControls2 Svg)

qt_standard_project_setup()

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(OpenLiveStudio
    WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/ui/widgets/mainwindow.cpp
    src/ui/widgets/mainwindow.h
    src/scene/studiostate.cpp
    src/scene/studiostate.h
    src/sources/imageloader.cpp
    src/sources/imageloader.h
)

set(OPENLIVE_STUDIO_QML_FILES
    "${PROJECT_SOURCE_DIR}/src/ui/qml/ScenesPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourcesPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/MixerPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/ControlsPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/AboutDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SceneNameDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SceneRemovalDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourceAddDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourceRemovalDialog.qml"
)

# Preserve the existing qrc:/OpenLiveStudio/Ui/qml/<name>.qml URLs.
foreach(qml_file IN LISTS OPENLIVE_STUDIO_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "qml/${qml_name}"
    )
endforeach()

qt_add_qml_module(OpenLiveStudio
    URI OpenLiveStudio.Ui
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES ${OPENLIVE_STUDIO_QML_FILES}
)

set(OPENLIVE_STUDIO_ICON_FILES
    "${PROJECT_SOURCE_DIR}/assets/icons/plus.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/minus.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/arrow-up.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/arrow-down.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/eye.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/eye-off.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/lock.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/lock-open.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/sliders-horizontal.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/settings.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/volume-2.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/arrow-right.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/circle.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/radio.svg"
)
qt_add_resources(OpenLiveStudio "icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

target_include_directories(OpenLiveStudio PRIVATE
    "${PROJECT_SOURCE_DIR}/src/ui/widgets"
    "${PROJECT_SOURCE_DIR}/src/sources"
    "${PROJECT_SOURCE_DIR}/src/scene"
)

target_link_libraries(OpenLiveStudio
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::QuickWidgets
        Qt::QuickControls2
        Qt::Svg
)

install(TARGETS OpenLiveStudio
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES assets/icons/NOTICE.md
    DESTINATION licenses
    RENAME lucide-icons.md
)

qt_generate_deploy_qml_app_script(
    TARGET OpenLiveStudio
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
