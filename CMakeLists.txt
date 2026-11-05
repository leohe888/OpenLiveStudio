# 项目基础设置
cmake_minimum_required(VERSION 3.19)
project(OpenLiveStudio LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "OpenLive Studio supports Linux only.")
endif()

# 依赖与 Qt 初始化
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets Svg OpenGL OpenGLWidgets DBus)

find_package(PkgConfig REQUIRED)
pkg_check_modules(avformat REQUIRED IMPORTED_TARGET libavformat>=61.7)
pkg_check_modules(avcodec REQUIRED IMPORTED_TARGET libavcodec>=61.19)
pkg_check_modules(avutil REQUIRED IMPORTED_TARGET libavutil>=59.39)
pkg_check_modules(swresample REQUIRED IMPORTED_TARGET libswresample>=5.3)
pkg_check_modules(swscale REQUIRED IMPORTED_TARGET libswscale>=8.3)
pkg_get_variable(OPENLIVE_FFMPEG_PREFIX libavcodec prefix)
pkg_check_modules(pulse REQUIRED IMPORTED_TARGET libpulse>=13.0)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3>=0.3.48)

qt_standard_project_setup()

# 媒体与来源核心：源码、头文件路径、依赖和着色器
add_library(openlive_core STATIC
    src/audio/audiobuffer.h
    src/audio/audiocaptureworker.cpp
    src/audio/audiocaptureworker.h
    src/audio/audiomixer.cpp
    src/audio/audiomixer.h
    src/audio/audioresampler.cpp
    src/audio/audioresampler.h
    src/audio/audiosource.h
    src/audio/audiosourcemanager.cpp
    src/audio/audiosourcemanager.h

    src/graphics/canvasrenderer.cpp
    src/graphics/canvasrenderer.h
    src/graphics/captureframe.h
    src/graphics/compositor.cpp
    src/graphics/compositor.h
    src/graphics/rendersourcesnapshot.h
    src/graphics/videoformat.h

    src/output/mediaencoder.cpp
    src/output/mediaencoder.h
    src/output/outputformat.h
    src/output/outputsettings.cpp
    src/output/outputsettings.h
    src/output/outputtimeline.cpp
    src/output/outputtimeline.h
    src/output/recordingcontroller.cpp
    src/output/recordingcontroller.h
    src/output/streamingcontroller.cpp
    src/output/streamingcontroller.h

    src/util/mediaclock.h
    src/util/outputbus.h
    src/util/outputtap.h

    src/video/videosource.cpp
    src/video/videosource.h
    src/video/videosourceaggregator.cpp
    src/video/videosourceaggregator.h
    src/video/videosourcefilter.cpp
    src/video/videosourcefilter.h
    src/video/videosourcemanager.cpp
    src/video/videosourcemanager.h
    src/video/videosourcetransform.cpp
    src/video/videosourcetransform.h

    src/video/camera/cameracaptureworker.cpp
    src/video/camera/cameracaptureworker.h
    src/video/camera/camerasourcecontroller.cpp
    src/video/camera/camerasourcecontroller.h
    src/video/camera/v4l2camera.cpp
    src/video/camera/v4l2camera.h

    src/video/display/displaycaptureworker.cpp
    src/video/display/displaycaptureworker.h
    src/video/display/displaysourcecontroller.cpp
    src/video/display/displaysourcecontroller.h
    src/video/display/waylanddisplaycapture.cpp
    src/video/display/waylanddisplaycapture.h

    src/video/image/imagesourcecontroller.cpp
    src/video/image/imagesourcecontroller.h
)

target_include_directories(openlive_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(openlive_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::Network Qt6::DBus
    PkgConfig::avformat PkgConfig::avcodec PkgConfig::avutil
    PkgConfig::swresample PkgConfig::swscale PkgConfig::pulse PkgConfig::pipewire
)

set(OPENLIVE_STUDIO_SHADER_FILES
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture_draw.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/texture_draw.frag"
    "${PROJECT_SOURCE_DIR}/assets/shaders/video_convert.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/video_convert.frag"
)
qt_add_resources(openlive_core "shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES}
)

# 用户界面：源码、依赖、图标和样式
add_library(openlive_ui STATIC
    src/ui/audiosourcepanel.cpp
    src/ui/audiosourcepanel.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/outputwidgets.cpp
    src/ui/outputwidgets.h
    src/ui/previewwidget.cpp
    src/ui/previewwidget.h
    src/ui/sourceadddialog.cpp
    src/ui/sourcedialogs.h
    src/ui/sourcefiltersdialog.cpp
    src/ui/sourcewidgets.h
    src/ui/videosourcepanel.cpp
    src/ui/videosourcepanel.h
)

target_link_libraries(openlive_ui PUBLIC
    openlive_core Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets
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
qt_add_resources(openlive_ui "icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

qt_add_resources(openlive_ui "style"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/assets/styles/style.qss"
)

# 应用入口
qt_add_executable(OpenLiveStudio src/main.cpp)
target_link_libraries(OpenLiveStudio PRIVATE openlive_ui)

# 测试
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# 安装与部署
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
