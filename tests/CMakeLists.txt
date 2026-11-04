find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# Linux 继承当前环境；不要用 Windows 的分号拼接 PATH，也不要覆盖系统 Qt 插件路径。
set(OPENLIVE_TEST_ENV)
if(WIN32)
    list(APPEND OPENLIVE_TEST_ENV
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${OPENLIVE_FFMPEG_PREFIX}/bin\;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
endif()

find_program(OPENLIVE_FFPROBE NAMES ffprobe HINTS "${OPENLIVE_FFMPEG_PREFIX}/bin" REQUIRED)
qt_add_executable(tst_streaming output/tst_streaming.cpp
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/rendersourcesnapshot.h
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemanager.h
    ../src/audio/audiosourcemanager.cpp
    ../src/output/outputtimeline.h
    ../src/output/outputtimeline.cpp
    ../src/output/streamingcontroller.h
    ../src/output/streamingcontroller.cpp
    ../src/output/mediaencoder.h
    ../src/output/mediaencoder.cpp
    ../src/output/recordingcontroller.h
    ../src/output/recordingcontroller.cpp
    ../src/output/outputsettings.h
    ../src/output/outputsettings.cpp
    ../src/util/mediaclock.h
)
target_include_directories(tst_streaming PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_streaming PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Test
    PkgConfig::swresample PkgConfig::avutil PkgConfig::avcodec PkgConfig::avformat PkgConfig::swscale)
if(WIN32)
    target_compile_definitions(tst_streaming PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_streaming PRIVATE d3d11 dxgi opengl32 ole32 uuid)
endif()

target_compile_definitions(tst_streaming PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
qt_add_resources(tst_streaming "streaming_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME output.streaming COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_streaming>" -o "${CMAKE_CURRENT_BINARY_DIR}/streaming-results.txt,txt")
set_tests_properties(output.streaming PROPERTIES LABELS "output;gpu;network" TIMEOUT 60)

qt_add_executable(tst_recording output/tst_recording.cpp
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/rendersourcesnapshot.h
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemanager.h
    ../src/audio/audiosourcemanager.cpp
    ../src/output/outputtimeline.h
    ../src/output/outputtimeline.cpp
    ../src/output/streamingcontroller.h
    ../src/output/streamingcontroller.cpp
    ../src/output/mediaencoder.h
    ../src/output/mediaencoder.cpp
    ../src/output/recordingcontroller.h
    ../src/output/recordingcontroller.cpp
    ../src/output/outputsettings.h
    ../src/output/outputsettings.cpp
    ../src/util/mediaclock.h
)
target_include_directories(tst_recording PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_recording PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Test
    PkgConfig::swresample PkgConfig::avutil PkgConfig::avcodec PkgConfig::avformat PkgConfig::swscale)
if(WIN32)
    target_compile_definitions(tst_recording PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_recording PRIVATE d3d11 dxgi opengl32 ole32 uuid)
endif()

target_compile_definitions(tst_recording PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
qt_add_resources(tst_recording "recording_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME output.recording COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_recording>" -o "${CMAKE_CURRENT_BINARY_DIR}/recording-results.txt,txt")
set_tests_properties(output.recording PROPERTIES LABELS "output;gpu" TIMEOUT 60)

qt_add_executable(tst_audio audio/tst_audio.cpp
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemanager.h
    ../src/audio/audiosourcemanager.cpp
)
target_include_directories(tst_audio PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_audio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    PkgConfig::swresample PkgConfig::avutil)
if(WIN32)
    target_compile_definitions(tst_audio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_audio PRIVATE ole32 uuid)
endif()

add_test(NAME audio.sources COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_audio>" -o "${CMAKE_CURRENT_BINARY_DIR}/audio-results.txt,txt")
set_tests_properties(audio.sources PROPERTIES LABELS "audio" TIMEOUT 30)

qt_add_executable(tst_canvasrenderer graphics/tst_canvasrenderer.cpp
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/rendersourcesnapshot.h
)
target_include_directories(tst_canvasrenderer PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_canvasrenderer PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Test)
if(WIN32)
    target_compile_definitions(tst_canvasrenderer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_canvasrenderer PRIVATE d3d11 dxgi opengl32)
endif()

qt_add_resources(tst_canvasrenderer "compositor_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME gpu.compositor COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_canvasrenderer>" -o "${CMAKE_CURRENT_BINARY_DIR}/compositor-results.txt,txt")
set_tests_properties(gpu.compositor PROPERTIES LABELS "gpu" TIMEOUT 30)

qt_add_executable(tst_captureinterop graphics/tst_captureinterop.cpp
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/video/videosource.cpp
    ../src/video/videosourcemanager.cpp
    ../src/video/videosourcemanager.h
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/video/display/displaydevices.h
    ../src/video/display/displaydevices.cpp
    ../src/video/display/displaycaptureworker.h
    ../src/video/display/displaycaptureworker.cpp
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/video/display/displaysourcecontroller.h
    ../src/video/display/displaysourcecontroller.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/rendersourcesnapshot.h
)
target_include_directories(tst_captureinterop PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_captureinterop PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Widgets Qt6::Test)
if(WIN32)
    target_compile_definitions(tst_captureinterop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_captureinterop PRIVATE d3d11 dxgi windowsapp opengl32 mfplat mfreadwrite mfuuid mf ole32 psapi)
endif()

qt_add_resources(tst_captureinterop "capture_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME capture.interop COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_captureinterop>" -o "${CMAKE_CURRENT_BINARY_DIR}/captureinterop-results.txt,txt")
set_tests_properties(capture.interop PROPERTIES LABELS "capture;gpu" TIMEOUT 30)

qt_add_executable(tst_videosource
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    video/tst_videosource.cpp
    ../src/video/videosource.cpp
    ../src/video/videosource.h
)
target_include_directories(tst_videosource PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_videosource PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.videosource COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_videosource>" -o "-,txt")
set_tests_properties(core.videosource PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourcefilter
    video/tst_videosourcefilter.cpp
    ../src/video/videosourcefilter.cpp
    ../src/video/videosourcefilter.h
)

target_include_directories(tst_videosourcefilter PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_videosourcefilter PRIVATE Qt6::Core Qt6::Test)
add_test(NAME core.videosourcefilter COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_videosourcefilter>" -o "-,txt")
set_tests_properties(core.videosourcefilter PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourcemanager
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    video/tst_videosourcemanager.cpp
    ../src/video/videosource.cpp
    ../src/video/videosource.h
    ../src/video/videosourcetransform.h
    ../src/video/videosourcemanager.cpp
    ../src/video/videosourcemanager.h
)
target_include_directories(tst_videosourcemanager PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tst_videosourcemanager PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.videosourcemanager
    COMMAND "${CMAKE_COMMAND}" -E env
        ${OPENLIVE_TEST_ENV}
        "$<TARGET_FILE:tst_videosourcemanager>"
        -o "-,txt"
)
set_tests_properties(core.videosourcemanager PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourceaggregator
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    video/tst_videosourceaggregator.cpp
    ../src/video/videosource.cpp
    ../src/video/videosourcemanager.cpp
    ../src/video/videosourcemanager.h
    ../src/video/videosourceaggregator.cpp
    ../src/video/videosourceaggregator.h
)
target_include_directories(tst_videosourceaggregator PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_videosourceaggregator PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME video.sourceaggregator COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_videosourceaggregator>" -o "-,txt")
set_tests_properties(video.sourceaggregator PROPERTIES LABELS "video;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ui/tst_mainwindow.cpp
    support/recordingacceptance.h
    ../src/ui/mainwindow.cpp
    ../src/ui/outputwidgets.cpp
    ../src/ui/videosourcepanel.cpp
    ../src/ui/audiosourcepanel.cpp
    ../src/ui/audiosourcepanel.h
    ../src/ui/videosourcepanel.h
    ../src/ui/outputwidgets.h
    ../src/ui/mainwindow.h
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/graphics/rendersourcesnapshot.h
    ../src/video/videosource.cpp
    ../src/video/videosource.h
    ../src/video/videosourcetransform.h
    ../src/video/videosourcemanager.cpp
    ../src/video/videosourcemanager.h
    ../src/video/image/imagesourcecontroller.cpp
    ../src/video/image/imagesourcecontroller.h
    ../src/video/videosourceaggregator.cpp
    ../src/video/videosourceaggregator.h
    ../src/video/camera/cameracaptureworker.h
    ../src/video/camera/cameracaptureworker.cpp
    ../src/video/camera/camerasourcecontroller.h
    ../src/video/camera/camerasourcecontroller.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/video/display/displaydevices.h
    ../src/video/display/displaydevices.cpp
    ../src/video/display/displaycaptureworker.h
    ../src/video/display/displaycaptureworker.cpp
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/video/display/displaysourcecontroller.h
    ../src/video/display/displaysourcecontroller.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemanager.h
    ../src/audio/audiosourcemanager.cpp
    ../src/output/outputtimeline.h
    ../src/output/outputtimeline.cpp
    ../src/output/streamingcontroller.h
    ../src/output/streamingcontroller.cpp
    ../src/output/mediaencoder.h
    ../src/output/mediaencoder.cpp
    ../src/output/recordingcontroller.h
    ../src/output/recordingcontroller.cpp
    ../src/output/outputsettings.h
    ../src/output/outputsettings.cpp
    ../src/util/mediaclock.h
)
target_include_directories(tst_mainwindow PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Widgets Qt6::Test Qt6::Svg Qt6::OpenGLWidgets
    PkgConfig::swresample PkgConfig::avutil PkgConfig::avcodec PkgConfig::avformat PkgConfig::swscale)
if(WIN32)
    target_compile_definitions(tst_mainwindow PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_mainwindow PRIVATE d3d11 dxgi windowsapp opengl32 mfplat mfreadwrite mfuuid mf ole32 uuid psapi)
endif()

target_compile_definitions(tst_mainwindow PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
set_target_properties(tst_mainwindow PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_resources(tst_mainwindow "mainwindow_shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES}
)

qt_add_resources(tst_mainwindow "test_icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

# 此测试以离屏平台验证界面结构；需要原生 GL 的子项会按条件跳过。
add_test(NAME ui.mainwindow
    COMMAND "${CMAKE_COMMAND}" -E env
        ${OPENLIVE_TEST_ENV}
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_mainwindow>"
        -o "${CMAKE_CURRENT_BINARY_DIR}/mainwindow-results.txt,txt"
)
# 超时覆盖整个界面测试套件，为设备查询和 CI 调度波动预留余量。
set_tests_properties(ui.mainwindow PROPERTIES
    LABELS "ui;smoke"
    TIMEOUT 120
)

qt_add_executable(tst_previewwidget
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ui/tst_previewwidget.cpp
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/graphics/rendersourcesnapshot.h
    ../src/video/videosourcetransform.h
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
)
target_include_directories(tst_previewwidget PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_previewwidget PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Test)
if(WIN32)
    target_compile_definitions(tst_previewwidget PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_previewwidget PRIVATE d3d11 dxgi opengl32)
endif()

set_target_properties(tst_previewwidget PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_resources(tst_previewwidget "preview_shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES}
)

# 依赖桌面 OpenGL 上下文；平台无法创建时明确跳过，避免误报逻辑失败。
add_test(NAME ui.previewwidget
    COMMAND "${CMAKE_COMMAND}" -E env
        ${OPENLIVE_TEST_ENV}
        "$<TARGET_FILE:tst_previewwidget>" -o "${CMAKE_CURRENT_BINARY_DIR}/previewwidget-results.txt,txt"
)
set_tests_properties(ui.previewwidget PROPERTIES
    LABELS "ui;gpu"
    TIMEOUT 30
)

qt_add_executable(tst_imagesourcecontroller
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    video/tst_imagesourcecontroller.cpp
    ../src/video/videosource.cpp
    ../src/video/videosourcemanager.cpp
    ../src/video/image/imagesourcecontroller.cpp
    ../src/video/image/imagesourcecontroller.h
    ../src/graphics/rendersourcesnapshot.h
    ../src/video/videosourcetransform.h
)
target_include_directories(tst_imagesourcecontroller PRIVATE
    "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_imagesourcecontroller PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME core.imagesourcecontroller
    COMMAND "${CMAKE_COMMAND}" -E env
        ${OPENLIVE_TEST_ENV}
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_imagesourcecontroller>" -o "-,txt")
set_tests_properties(core.imagesourcecontroller PROPERTIES LABELS "core;smoke" TIMEOUT 30)

qt_add_executable(tst_cameracapture video/tst_cameracapture.cpp
    ../src/video/videosourcetransform.cpp
    ../src/video/videosourcefilter.cpp
    ../src/video/videosource.cpp
    ../src/video/videosourcemanager.cpp
    ../src/video/videosourcemanager.h
    ../src/video/camera/cameracaptureworker.h
    ../src/video/camera/cameracaptureworker.cpp
    ../src/video/camera/camerasourcecontroller.h
    ../src/video/camera/camerasourcecontroller.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/gpuframeresource.h
)
target_include_directories(tst_cameracapture PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_cameracapture PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Test)
if(WIN32)
    target_compile_definitions(tst_cameracapture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_cameracapture PRIVATE d3d11 dxgi windowsapp opengl32 mfplat mfreadwrite mfuuid mf ole32 psapi)
endif()

add_test(NAME capture.camera COMMAND "${CMAKE_COMMAND}" -E env
    ${OPENLIVE_TEST_ENV}
    "$<TARGET_FILE:tst_cameracapture>" -o "-,txt")
set_tests_properties(capture.camera PROPERTIES LABELS "capture;camera" TIMEOUT 60)

qt_add_resources(tst_mainwindow "style"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/assets/styles/style.qss"
)

foreach(camera_target tst_cameracapture tst_mainwindow)
    target_link_libraries(${camera_target} PRIVATE PkgConfig::avcodec PkgConfig::avutil)
endforeach()
