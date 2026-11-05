find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
find_program(OPENLIVE_DBUS_RUN_SESSION dbus-run-session REQUIRED)

find_program(OPENLIVE_FFPROBE NAMES ffprobe HINTS "${OPENLIVE_FFMPEG_PREFIX}/bin" REQUIRED)
qt_add_executable(tst_streaming output/tst_streaming.cpp)

target_compile_definitions(tst_streaming PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
add_test(NAME output.streaming COMMAND
    "$<TARGET_FILE:tst_streaming>" -o "${CMAKE_CURRENT_BINARY_DIR}/streaming-results.txt,txt")
set_tests_properties(output.streaming PROPERTIES LABELS "output;gpu;network" TIMEOUT 60)

qt_add_executable(tst_recording output/tst_recording.cpp)

target_compile_definitions(tst_recording PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
add_test(NAME output.recording COMMAND
    "$<TARGET_FILE:tst_recording>" -o "${CMAKE_CURRENT_BINARY_DIR}/recording-results.txt,txt")
set_tests_properties(output.recording PROPERTIES LABELS "output;gpu" TIMEOUT 60)

qt_add_executable(tst_audio audio/tst_audio.cpp)

add_test(NAME audio.sources COMMAND
    "$<TARGET_FILE:tst_audio>" -o "${CMAKE_CURRENT_BINARY_DIR}/audio-results.txt,txt")
set_tests_properties(audio.sources PROPERTIES LABELS "audio" TIMEOUT 30)

qt_add_executable(tst_pulseaudio audio/tst_pulseaudio.cpp)
target_compile_definitions(tst_pulseaudio PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
add_test(NAME audio.pulseaudio COMMAND "${OPENLIVE_DBUS_RUN_SESSION}" -- "$<TARGET_FILE:tst_pulseaudio>")
set_tests_properties(audio.pulseaudio PROPERTIES LABELS "audio;integration" TIMEOUT 60)

qt_add_executable(tst_canvasrenderer graphics/tst_canvasrenderer.cpp)

add_test(NAME gpu.compositor COMMAND
    "$<TARGET_FILE:tst_canvasrenderer>" -o "${CMAKE_CURRENT_BINARY_DIR}/compositor-results.txt,txt")
set_tests_properties(gpu.compositor PROPERTIES LABELS "gpu" TIMEOUT 30)

qt_add_executable(tst_captureframe graphics/tst_captureframe.cpp)
add_test(NAME capture.frames COMMAND "$<TARGET_FILE:tst_captureframe>")
set_tests_properties(capture.frames PROPERTIES LABELS "capture;smoke" TIMEOUT 10)

qt_add_executable(tst_videosource video/tst_videosource.cpp)
add_test(NAME core.videosource COMMAND
    "$<TARGET_FILE:tst_videosource>" -o "-,txt")
set_tests_properties(core.videosource PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourcefilter video/tst_videosourcefilter.cpp)

add_test(NAME core.videosourcefilter COMMAND
    "$<TARGET_FILE:tst_videosourcefilter>" -o "-,txt")
set_tests_properties(core.videosourcefilter PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourcemanager video/tst_videosourcemanager.cpp)
add_test(NAME core.videosourcemanager
    COMMAND
                "$<TARGET_FILE:tst_videosourcemanager>"
        -o "-,txt"
)
set_tests_properties(core.videosourcemanager PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_videosourceaggregator video/tst_videosourceaggregator.cpp)
add_test(NAME video.sourceaggregator COMMAND
    "$<TARGET_FILE:tst_videosourceaggregator>" -o "-,txt")
set_tests_properties(video.sourceaggregator PROPERTIES LABELS "video;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow ui/tst_mainwindow.cpp)
qt_add_executable(tst_mainwindow_gpu ui/tst_mainwindow_gpu.cpp)
qt_add_executable(tst_mainwindow_hardware ui/tst_mainwindow_hardware.cpp)
foreach(test_target IN ITEMS tst_mainwindow tst_mainwindow_gpu tst_mainwindow_hardware)
    target_include_directories(${test_target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(${test_target} PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
endforeach()
add_test(NAME ui.mainwindow.gpu COMMAND "$<TARGET_FILE:tst_mainwindow_gpu>")
set_tests_properties(ui.mainwindow.gpu PROPERTIES LABELS "ui;gpu;output" TIMEOUT 120)
add_test(NAME ui.mainwindow.hardware COMMAND "$<TARGET_FILE:tst_mainwindow_hardware>")
# Hardware and soak tests retain their explicit environment opt-ins.
set_tests_properties(ui.mainwindow.hardware PROPERTIES LABELS "ui;gpu;hardware;acceptance" TIMEOUT 4200)

# 界面结构与配置逻辑在离屏平台验证；GPU 和硬件验收使用独立套件。
add_test(NAME ui.mainwindow
    COMMAND "${CMAKE_COMMAND}" -E env
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_mainwindow>"
        -o "${CMAKE_CURRENT_BINARY_DIR}/mainwindow-results.txt,txt"
)
# 超时覆盖整个界面测试套件，为设备查询和 CI 调度波动预留余量。
set_tests_properties(ui.mainwindow PROPERTIES
    LABELS "ui;smoke"
    TIMEOUT 120
)

qt_add_executable(tst_previewwidget ui/tst_previewwidget.cpp)

# 依赖桌面 OpenGL 上下文；平台无法创建时明确跳过，避免误报逻辑失败。
add_test(NAME ui.previewwidget
    COMMAND
                "$<TARGET_FILE:tst_previewwidget>" -o "${CMAKE_CURRENT_BINARY_DIR}/previewwidget-results.txt,txt"
)
set_tests_properties(ui.previewwidget PROPERTIES
    LABELS "ui;gpu"
    TIMEOUT 30
)

qt_add_executable(tst_imagesourcecontroller video/tst_imagesourcecontroller.cpp)
add_test(NAME core.imagesourcecontroller
    COMMAND "${CMAKE_COMMAND}" -E env
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_imagesourcecontroller>" -o "-,txt")
set_tests_properties(core.imagesourcecontroller PROPERTIES LABELS "core;smoke" TIMEOUT 30)

qt_add_executable(tst_cameracapture video/tst_cameracapture.cpp)

add_test(NAME capture.camera COMMAND
    "$<TARGET_FILE:tst_cameracapture>" -o "-,txt")
set_tests_properties(capture.camera PROPERTIES LABELS "capture;camera" TIMEOUT 60)

qt_add_executable(tst_portaldisplay video/tst_portaldisplay.cpp)
add_test(NAME capture.portal COMMAND "${OPENLIVE_DBUS_RUN_SESSION}" -- "$<TARGET_FILE:tst_portaldisplay>")
set_tests_properties(capture.portal PROPERTIES LABELS "capture;smoke" TIMEOUT 20)

# Link the same production implementation used by the application.
foreach(test_target IN ITEMS tst_streaming tst_recording tst_audio tst_pulseaudio
        tst_canvasrenderer tst_captureframe tst_videosource tst_videosourcefilter
        tst_videosourcemanager tst_videosourceaggregator tst_imagesourcecontroller
        tst_cameracapture tst_portaldisplay)
    target_link_libraries(${test_target} PRIVATE openlive_core Qt6::Test)
endforeach()
foreach(test_target IN ITEMS tst_mainwindow tst_mainwindow_gpu tst_mainwindow_hardware tst_previewwidget)
    target_link_libraries(${test_target} PRIVATE openlive_ui Qt6::Test)
endforeach()
