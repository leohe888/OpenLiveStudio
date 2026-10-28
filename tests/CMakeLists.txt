find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

find_program(OPENLIVE_FFPROBE NAMES ffprobe HINTS "${FFmpeg_RUNTIME_DIR}" REQUIRED)
qt_add_executable(tst_recording tst_recording.cpp)
target_link_libraries(tst_recording PRIVATE openlive_output Qt6::Test)
target_compile_definitions(tst_recording PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
qt_add_resources(tst_recording "recording_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME output.recording COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_recording>" -o "${CMAKE_CURRENT_BINARY_DIR}/recording-results.txt,txt")
set_tests_properties(output.recording PROPERTIES LABELS "output;gpu" TIMEOUT 60)

qt_add_executable(tst_audio tst_audio.cpp)
target_link_libraries(tst_audio PRIVATE openlive_audio Qt6::Test)
add_test(NAME audio.sources COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${FFmpeg_RUNTIME_DIR};$ENV{PATH}"
    "$<TARGET_FILE:tst_audio>" -o "${CMAKE_CURRENT_BINARY_DIR}/audio-results.txt,txt")
set_tests_properties(audio.sources PROPERTIES LABELS "audio" TIMEOUT 30)

qt_add_executable(tst_canvasrenderer tst_canvasrenderer.cpp)
target_link_libraries(tst_canvasrenderer PRIVATE openlive_compositor Qt6::Test)
qt_add_resources(tst_canvasrenderer "compositor_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME gpu.compositor COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_canvasrenderer>" -o "${CMAKE_CURRENT_BINARY_DIR}/compositor-results.txt,txt")
set_tests_properties(gpu.compositor PROPERTIES LABELS "gpu" TIMEOUT 30)

qt_add_executable(tst_captureinterop tst_captureinterop.cpp
    ../src/preview/previewwidget.cpp ../src/preview/previewwidget.h
    ../src/core/source.cpp ../src/core/sourcelistmodel.cpp ../src/core/sourcelistmodel.h)
target_link_libraries(tst_captureinterop PRIVATE openlive_compositor Qt6::OpenGLWidgets Qt6::Widgets Qt6::Test)
if(WIN32)
    target_link_libraries(tst_captureinterop PRIVATE psapi)
endif()
qt_add_resources(tst_captureinterop "capture_shaders" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES})
add_test(NAME capture.interop COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    "$<TARGET_FILE:tst_captureinterop>" -o "${CMAKE_CURRENT_BINARY_DIR}/captureinterop-results.txt,txt")
set_tests_properties(capture.interop PROPERTIES LABELS "capture;gpu" TIMEOUT 30)

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

qt_add_executable(tst_sourcelistmodel
    tst_sourcelistmodel.cpp
    ../src/core/source.cpp
    ../src/core/source.h
    ../src/core/sourcetransform.h
    ../src/core/sourcelistmodel.cpp
    ../src/core/sourcelistmodel.h
)
target_include_directories(tst_sourcelistmodel PRIVATE
    "${PROJECT_SOURCE_DIR}/src/core"
)
target_link_libraries(tst_sourcelistmodel PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME core.sourcelistmodel
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:tst_sourcelistmodel>"
        -o "-,txt"
)
set_tests_properties(core.sourcelistmodel PROPERTIES LABELS "core;smoke" TIMEOUT 10)

qt_add_executable(tst_mainwindow
    tst_mainwindow.cpp
    ../src/core/thememanager.cpp
    ../src/core/thememanager.h
    ../src/mainwindow.cpp
    ../src/mainwindow.h
    ../src/preview/previewwidget.cpp
    ../src/preview/previewwidget.h
    ../src/preview/previewsource.h
    ../src/core/source.cpp
    ../src/core/source.h
    ../src/core/sourcetransform.h
    ../src/core/sourcelistmodel.cpp
    ../src/core/sourcelistmodel.h
    ../src/core/imagesourcecontroller.cpp
    ../src/core/imagesourcecontroller.h
)

target_include_directories(tst_mainwindow PRIVATE
    "${PROJECT_SOURCE_DIR}/src/core"
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Widgets Qt6::Test Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg
    Qt6::OpenGL Qt6::OpenGLWidgets
    openlive_compositor
    openlive_audio
    openlive_output
)
target_compile_definitions(tst_mainwindow PRIVATE OPENLIVE_FFPROBE="${OPENLIVE_FFPROBE}")
if(WIN32)
    target_link_libraries(tst_mainwindow PRIVATE psapi)
endif()
set_target_properties(tst_mainwindow PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_resources(tst_mainwindow "mainwindow_shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES ${OPENLIVE_STUDIO_SHADER_FILES}
)

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
    tst_previewwidget.cpp
    ../src/preview/previewwidget.cpp
    ../src/preview/previewwidget.h
    ../src/preview/previewsource.h
    ../src/core/sourcetransform.h
)
target_include_directories(tst_previewwidget PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tst_previewwidget PRIVATE Qt6::OpenGLWidgets Qt6::OpenGL Qt6::Test)
target_link_libraries(tst_previewwidget PRIVATE openlive_compositor)
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
    tst_imagesourcecontroller.cpp
    ../src/core/source.cpp
    ../src/core/sourcelistmodel.cpp
    ../src/core/imagesourcecontroller.cpp
    ../src/core/imagesourcecontroller.h
    ../src/preview/previewsource.h
    ../src/core/sourcetransform.h
)
target_include_directories(tst_imagesourcecontroller PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/core")
target_link_libraries(tst_imagesourcecontroller PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME core.imagesourcecontroller
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_imagesourcecontroller>" -o "-,txt")
set_tests_properties(core.imagesourcecontroller PROPERTIES LABELS "core;smoke" TIMEOUT 30)

qt_add_executable(tst_cameracapture tst_cameracapture.cpp
    ../src/core/source.cpp ../src/core/sourcelistmodel.cpp ../src/core/sourcelistmodel.h)
target_link_libraries(tst_cameracapture PRIVATE openlive_capture Qt6::Test)
if(WIN32)
    target_link_libraries(tst_cameracapture PRIVATE psapi)
endif()
add_test(NAME capture.camera COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    "$<TARGET_FILE:tst_cameracapture>" -o "-,txt")
set_tests_properties(capture.camera PROPERTIES LABELS "capture;camera" TIMEOUT 60)
