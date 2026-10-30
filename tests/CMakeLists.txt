find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

find_program(OPENLIVE_FFPROBE NAMES ffprobe HINTS "${FFmpeg_RUNTIME_DIR}" REQUIRED)
qt_add_executable(tst_streaming output/tst_streaming.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/previewsource.h
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemodel.h
    ../src/audio/audiosourcemodel.cpp
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
    ../src/core/mediaclock.h
)
target_include_directories(tst_streaming PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_streaming PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Test
    FFmpeg::swresample FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swscale)
if(WIN32)
    target_compile_definitions(tst_streaming PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_streaming PRIVATE d3d11 dxgi opengl32 ole32 uuid)
endif()

target_compile_definitions(tst_streaming PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
qt_add_resources(tst_streaming "streaming_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME output.streaming COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_streaming>" -o "${CMAKE_CURRENT_BINARY_DIR}/streaming-results.txt,txt")
set_tests_properties(output.streaming PROPERTIES LABELS "output;gpu;network" TIMEOUT 60)

qt_add_executable(tst_recording output/tst_recording.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/previewsource.h
    ../src/audio/audiobuffer.h
    ../src/audio/audioresampler.h
    ../src/audio/audioresampler.cpp
    ../src/audio/audiocaptureworker.h
    ../src/audio/audiocaptureworker.cpp
    ../src/audio/audiomixer.h
    ../src/audio/audiomixer.cpp
    ../src/audio/audiosourcemodel.h
    ../src/audio/audiosourcemodel.cpp
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
    ../src/core/mediaclock.h
)
target_include_directories(tst_recording PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_recording PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Test
    FFmpeg::swresample FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swscale)
if(WIN32)
    target_compile_definitions(tst_recording PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_recording PRIVATE d3d11 dxgi opengl32 ole32 uuid)
endif()

target_compile_definitions(tst_recording PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
qt_add_resources(tst_recording "recording_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME output.recording COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
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
    ../src/audio/audiosourcemodel.h
    ../src/audio/audiosourcemodel.cpp
)
target_include_directories(tst_audio PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_audio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    FFmpeg::swresample FFmpeg::avutil)
if(WIN32)
    target_compile_definitions(tst_audio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_audio PRIVATE ole32 uuid)
endif()

add_test(NAME audio.sources COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
    "$<TARGET_FILE:tst_audio>" -o "${CMAKE_CURRENT_BINARY_DIR}/audio-results.txt,txt")
set_tests_properties(audio.sources PROPERTIES LABELS "audio" TIMEOUT 30)

qt_add_executable(tst_canvasrenderer graphics/tst_canvasrenderer.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
    ../src/graphics/gpuinterop.h
    ../src/graphics/gpuinterop.cpp
    ../src/graphics/canvasrenderer.h
    ../src/graphics/canvasrenderer.cpp
    ../src/graphics/compositor.h
    ../src/graphics/compositor.cpp
    ../src/graphics/previewsource.h
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
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_canvasrenderer>" -o "${CMAKE_CURRENT_BINARY_DIR}/compositor-results.txt,txt")
set_tests_properties(gpu.compositor PROPERTIES LABELS "gpu" TIMEOUT 30)

qt_add_executable(tst_captureinterop graphics/tst_captureinterop.cpp
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/video/source.cpp
    ../src/video/videosourcemodel.cpp
    ../src/video/videosourcemodel.h
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
    ../src/video/display/displaytargets.h
    ../src/video/display/displaytargets.cpp
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
    ../src/graphics/previewsource.h
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
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_captureinterop>" -o "${CMAKE_CURRENT_BINARY_DIR}/captureinterop-results.txt,txt")
set_tests_properties(capture.interop PROPERTIES LABELS "capture;gpu" TIMEOUT 30)

qt_add_executable(tst_videosourcemodel
    video/tst_videosourcemodel.cpp
    ../src/video/source.cpp
    ../src/video/source.h
    ../src/video/sourcetransform.h
    ../src/video/videosourcemodel.cpp
    ../src/video/videosourcemodel.h
)
target_include_directories(tst_videosourcemodel PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tst_videosourcemodel PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.videosourcemodel
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_videosourcemodel>"
        -o "-,txt"
)
set_tests_properties(core.videosourcemodel PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow
    ui/tst_mainwindow.cpp
    ../src/ui/uicolors.h
    ../src/ui/mainwindow.cpp
    ../src/ui/mainwindow.h
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/graphics/previewsource.h
    ../src/video/source.cpp
    ../src/video/source.h
    ../src/video/sourcetransform.h
    ../src/video/videosourcemodel.cpp
    ../src/video/videosourcemodel.h
    ../src/video/image/imagesourcecontroller.cpp
    ../src/video/image/imagesourcecontroller.h
    ../src/video/camera/cameracaptureworker.h
    ../src/video/camera/cameracaptureworker.cpp
    ../src/video/camera/camerasourcecontroller.h
    ../src/video/camera/camerasourcecontroller.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
    ../src/video/display/displaytargets.h
    ../src/video/display/displaytargets.cpp
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
    ../src/audio/audiosourcemodel.h
    ../src/audio/audiosourcemodel.cpp
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
    ../src/core/mediaclock.h
)
target_include_directories(tst_mainwindow PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network Qt6::Widgets Qt6::Test Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg Qt6::OpenGLWidgets
    FFmpeg::swresample FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swscale)
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

# Embed the same source at the application's resource URL.
qt_add_resources(tst_mainwindow "test_ui"
    PREFIX "/OpenLiveStudio/Ui"
    BASE "${PROJECT_SOURCE_DIR}/src/ui"
    FILES ${OPENLIVE_STUDIO_QML_FILES}
)

qt_add_resources(tst_mainwindow "test_icons"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_ICON_FILES}
)

# This smoke test needs no desktop; future GPU tests need real GL.
add_test(NAME ui.mainwindow
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen"
        "QT_QUICK_BACKEND=software"
        "QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../qml"
        "$<TARGET_FILE:tst_mainwindow>"
        -o "${CMAKE_CURRENT_BINARY_DIR}/mainwindow-results.txt,txt"
)
# Timeout covers the complete UI suite; allow headroom for QML setup and CI variance.
set_tests_properties(ui.mainwindow PROPERTIES
    LABELS "ui;smoke"
    TIMEOUT 120
)

qt_add_executable(tst_previewwidget
    ui/tst_previewwidget.cpp
    ../src/ui/previewwidget.cpp
    ../src/ui/previewwidget.h
    ../src/graphics/previewsource.h
    ../src/video/sourcetransform.h
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
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

# Uses a desktop OpenGL context; explicitly skips when the platform cannot create one.
add_test(NAME ui.previewwidget
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "$<TARGET_FILE:tst_previewwidget>" -o "${CMAKE_CURRENT_BINARY_DIR}/previewwidget-results.txt,txt"
)
set_tests_properties(ui.previewwidget PROPERTIES
    LABELS "ui;gpu"
    TIMEOUT 30
)

qt_add_executable(tst_imagesourcecontroller
    video/tst_imagesourcecontroller.cpp
    ../src/video/source.cpp
    ../src/video/videosourcemodel.cpp
    ../src/video/image/imagesourcecontroller.cpp
    ../src/video/image/imagesourcecontroller.h
    ../src/graphics/previewsource.h
    ../src/video/sourcetransform.h
)
target_include_directories(tst_imagesourcecontroller PRIVATE
    "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_imagesourcecontroller PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME core.imagesourcecontroller
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_imagesourcecontroller>" -o "-,txt")
set_tests_properties(core.imagesourcecontroller PROPERTIES LABELS "core;smoke" TIMEOUT 30)

qt_add_executable(tst_cameracapture video/tst_cameracapture.cpp
    ../src/video/source.cpp
    ../src/video/videosourcemodel.cpp
    ../src/video/videosourcemodel.h
    ../src/video/camera/cameracaptureworker.h
    ../src/video/camera/cameracaptureworker.cpp
    ../src/video/camera/camerasourcecontroller.h
    ../src/video/camera/camerasourcecontroller.cpp
    ../src/graphics/captureframe.h
    ../src/graphics/d3dtexture.h
)
target_include_directories(tst_cameracapture PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_cameracapture PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Test)
if(WIN32)
    target_compile_definitions(tst_cameracapture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(tst_cameracapture PRIVATE d3d11 dxgi windowsapp opengl32 mfplat mfreadwrite mfuuid mf ole32 psapi)
endif()

add_test(NAME capture.camera COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "$<TARGET_FILE:tst_cameracapture>" -o "-,txt")
set_tests_properties(capture.camera PROPERTIES LABELS "capture;camera" TIMEOUT 60)
