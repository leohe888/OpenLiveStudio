cmake_minimum_required(VERSION 3.19)
project(OpenLiveStudio LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
find_package(FFmpeg REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets QuickWidgets QuickControls2 Svg OpenGL OpenGLWidgets)

qt_standard_project_setup()

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(OpenLiveStudio
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/previewwidget.cpp
    src/ui/previewwidget.h
    src/preview/previewsource.h
    src/ui/uicolors.h
    src/core/source.cpp
    src/core/source.h
    src/core/sourcefilter.h
    src/core/sourcetransform.h
    src/core/videosourcemodel.cpp
    src/core/videosourcemodel.h
    src/core/imagesourcecontroller.cpp
    src/core/imagesourcecontroller.h
    src/capture/cameracaptureworker.h
    src/capture/cameracaptureworker.cpp
    src/capture/cameracapturecontroller.h
    src/capture/cameracapturecontroller.cpp
    src/capture/captureframe.h
    src/capture/d3dtexture.h
    src/capture/displaytargets.h
    src/capture/displaytargets.cpp
    src/capture/displaycaptureworker.h
    src/capture/displaycaptureworker.cpp
    src/capture/gpuinterop.h
    src/capture/gpuinterop.cpp
    src/capture/displaycapturecontroller.h
    src/capture/displaycapturecontroller.cpp
    src/preview/canvasrenderer.h
    src/preview/canvasrenderer.cpp
    src/preview/compositor.h
    src/preview/compositor.cpp
    src/audio/audiobuffer.h
    src/audio/audioresampler.h
    src/audio/audioresampler.cpp
    src/audio/audiocaptureworker.h
    src/audio/audiocaptureworker.cpp
    src/audio/audiomixer.h
    src/audio/audiomixer.cpp
    src/audio/audiosourcemodel.h
    src/audio/audiosourcemodel.cpp
    src/output/outputtimeline.h
    src/output/outputtimeline.cpp
    src/output/streamingcontroller.h
    src/output/streamingcontroller.cpp
    src/output/mediaencoder.h
    src/output/mediaencoder.cpp
    src/output/recordingcontroller.h
    src/output/recordingcontroller.cpp
    src/output/outputsettings.h
    src/output/outputsettings.cpp
    src/core/mediaclock.h
)

set(OPENLIVE_STUDIO_QML_FILES
    "${PROJECT_SOURCE_DIR}/src/ui/qml/OutputSettingsDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/AudioSourceDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourcesPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/MixerPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/ControlsPanel.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourceAddDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourceRemovalDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/SourceFiltersDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/DisplayCaptureSettings.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/DisplayPropertiesDialog.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/CameraCaptureSettings.qml"
    "${PROJECT_SOURCE_DIR}/src/ui/qml/CameraPropertiesDialog.qml"
)

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
    "${PROJECT_SOURCE_DIR}/assets/icons/volume-x.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/arrow-right.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/circle.svg"
    "${PROJECT_SOURCE_DIR}/assets/icons/radio.svg"
)
qt_add_resources(OpenLiveStudio "icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

set(OPENLIVE_STUDIO_SHADER_FILES
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture.frag"
)
qt_add_resources(OpenLiveStudio "shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES}
)

target_include_directories(OpenLiveStudio PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(OpenLiveStudio
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::QuickWidgets
        Qt::QuickControls2
        Qt::Svg
        Qt::OpenGL
        Qt::OpenGLWidgets
        Qt::Network
        FFmpeg::avdevice
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swresample
        FFmpeg::swscale
)

if(WIN32)
    target_compile_definitions(OpenLiveStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(OpenLiveStudio PRIVATE
        d3d11 dxgi windowsapp opengl32 mfplat mfreadwrite mfuuid mf ole32 uuid)
endif()

# Include the shared build's transitive DLL dependencies as well.
if(WIN32 AND FFmpeg_RUNTIME_DLLS)
    add_custom_command(TARGET OpenLiveStudio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${FFmpeg_RUNTIME_DLLS} "$<TARGET_FILE_DIR:OpenLiveStudio>"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

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
