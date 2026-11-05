cmake_minimum_required(VERSION 3.19)
project(OpenLiveStudio LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "OpenLive Studio supports Linux only.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(avformat REQUIRED IMPORTED_TARGET libavformat>=61.7)
pkg_check_modules(avcodec REQUIRED IMPORTED_TARGET libavcodec>=61.19)
pkg_check_modules(avutil REQUIRED IMPORTED_TARGET libavutil>=59.39)
pkg_check_modules(swresample REQUIRED IMPORTED_TARGET libswresample>=5.3)
pkg_check_modules(swscale REQUIRED IMPORTED_TARGET libswscale>=8.3)
pkg_get_variable(OPENLIVE_FFMPEG_PREFIX libavcodec prefix)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets Svg OpenGL OpenGLWidgets DBus)
pkg_check_modules(pulse REQUIRED IMPORTED_TARGET libpulse>=13.0)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3>=0.3.48)

qt_standard_project_setup()

qt_add_executable(OpenLiveStudio
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/outputwidgets.cpp
    src/ui/videosourcepanel.cpp
    src/ui/audiosourcepanel.cpp
    src/ui/audiosourcepanel.h
    src/ui/videosourcepanel.h
    src/ui/outputwidgets.h
    src/ui/mainwindow.h
    src/ui/previewwidget.cpp
    src/ui/previewwidget.h
    src/graphics/rendersourcesnapshot.h
    src/video/videosource.cpp
    src/video/videosource.h
    src/video/videosourcetransform.cpp
    src/video/videosourcefilter.cpp
    src/video/videosourcefilter.h
    src/video/videosourcetransform.h
    src/video/videosourcemanager.cpp
    src/video/videosourcemanager.h
    src/video/videosourceaggregator.cpp
    src/video/videosourceaggregator.h
    src/video/image/imagesourcecontroller.cpp
    src/video/image/imagesourcecontroller.h
    src/video/camera/cameracaptureworker.h
    src/video/camera/cameracaptureworker.cpp
    src/video/camera/v4l2camera.cpp
    src/video/camera/v4l2camera.h
    src/video/camera/camerasourcecontroller.h
    src/video/camera/camerasourcecontroller.cpp
    src/graphics/captureframe.h
    src/video/display/displaycaptureworker.h
    src/video/display/displaycaptureworker.cpp
    src/video/display/displaysourcecontroller.h
    src/video/display/displaysourcecontroller.cpp
    src/graphics/canvasrenderer.h
    src/graphics/canvasrenderer.cpp
    src/graphics/compositor.h
    src/graphics/compositor.cpp
    src/audio/audiobuffer.h
    src/audio/audioresampler.h
    src/audio/audioresampler.cpp
    src/audio/audiocaptureworker.h
    src/audio/audiocaptureworker.cpp
    src/audio/audiomixer.h
    src/audio/audiomixer.cpp
    src/audio/audiosource.h
    src/audio/audiosourcemanager.h
    src/audio/audiosourcemanager.cpp
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
    src/util/mediaclock.h
    src/util/outputtap.h
    src/util/outputbus.h
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
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture_draw.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture_draw.frag"
    "${PROJECT_SOURCE_DIR}/assets/shaders/video_convert.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/video_convert.frag"
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
        Qt::Svg
        Qt::OpenGL
        Qt::OpenGLWidgets
        Qt::Network
        PkgConfig::avformat
        PkgConfig::avcodec
        PkgConfig::avutil
        PkgConfig::swresample
        PkgConfig::pulse
        PkgConfig::swscale
)

target_sources(OpenLiveStudio PRIVATE src/video/display/waylanddisplaycapture.cpp
    src/video/display/waylanddisplaycapture.h)
target_link_libraries(OpenLiveStudio PRIVATE Qt6::DBus PkgConfig::pipewire)


install(TARGETS OpenLiveStudio
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES assets/icons/NOTICE.md
    DESTINATION licenses
    RENAME lucide-icons.md
)

qt_generate_deploy_app_script(
    TARGET OpenLiveStudio
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

qt_add_resources(OpenLiveStudio "style"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/assets/styles/style.qss"
)
