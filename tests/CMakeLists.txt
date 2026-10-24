find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# Opt in on a desktop with real OpenGL; never substitute the smoke UI backend.
option(OPENLIVE_STUDIO_GL_TESTS "Build and register native OpenGL composition tests" OFF)
if(OPENLIVE_STUDIO_GL_TESTS)
    qt_add_executable(tst_previewwidget
        tst_previewwidget.cpp
        ../src/ui/preview/previewwidget.cpp
        ../src/ui/preview/previewwidget.h
        ../src/graphics/composition/compositionsession.cpp
        ../src/graphics/composition/compositionsession.h
        ../src/graphics/composition/imagecompositor.cpp
        ../src/graphics/composition/imagecompositor.h
    )
    target_include_directories(tst_previewwidget PRIVATE
        "${PROJECT_SOURCE_DIR}/src/ui/preview"
        "${PROJECT_SOURCE_DIR}/src/graphics/composition"
    )
    target_link_libraries(tst_previewwidget PRIVATE Qt6::OpenGLWidgets Qt6::QuickWidgets Qt6::Test)
    set_target_properties(tst_previewwidget PROPERTIES WIN32_EXECUTABLE FALSE)
    foreach(scale IN ITEMS 1 2)
        add_test(NAME graphics.previewwidget.scale${scale}
            COMMAND "${CMAKE_COMMAND}" -E env
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
                "QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../qml"
                "QT_QPA_PLATFORM=windows"
                "QT_QUICK_BACKEND=rhi"
                "QSG_RHI_BACKEND=opengl"
                "QT_SCALE_FACTOR=${scale}"
                "$<TARGET_FILE:tst_previewwidget>" -o "-,txt"
        )
        set_tests_properties(graphics.previewwidget.scale${scale} PROPERTIES LABELS "graphics" TIMEOUT 45)
    endforeach()

    qt_add_executable(tst_compositionsession
        tst_compositionsession.cpp
        ../src/graphics/composition/compositionsession.cpp
        ../src/graphics/composition/compositionsession.h
        ../src/graphics/composition/imagecompositor.cpp
        ../src/graphics/composition/imagecompositor.h
    )
    target_include_directories(tst_compositionsession PRIVATE "${PROJECT_SOURCE_DIR}/src/graphics/composition")
    target_link_libraries(tst_compositionsession PRIVATE Qt6::OpenGL Qt6::Test)
    set_target_properties(tst_compositionsession PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME graphics.compositionsession
        COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "QT_QPA_PLATFORM=windows"
            "$<TARGET_FILE:tst_compositionsession>" -o "-,txt"
    )
    set_tests_properties(graphics.compositionsession PROPERTIES LABELS "graphics" TIMEOUT 30)

    qt_add_executable(tst_imagecompositor
        tst_imagecompositor.cpp
        ../src/graphics/composition/imagecompositor.cpp
        ../src/graphics/composition/imagecompositor.h
    )
    target_include_directories(tst_imagecompositor PRIVATE "${PROJECT_SOURCE_DIR}/src/graphics/composition")
    target_link_libraries(tst_imagecompositor PRIVATE Qt6::OpenGL Qt6::Test)
    set_target_properties(tst_imagecompositor PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME graphics.imagecompositor
        COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "QT_QPA_PLATFORM=windows"
            "$<TARGET_FILE:tst_imagecompositor>" -o "-,txt"
    )
    set_tests_properties(graphics.imagecompositor PROPERTIES LABELS "graphics" TIMEOUT 30)
endif()

qt_add_executable(tst_imageloader
    tst_imageloader.cpp
    ../src/sources/imageloader.cpp
    ../src/sources/imageloader.h
)
target_include_directories(tst_imageloader PRIVATE "${PROJECT_SOURCE_DIR}/src/sources")
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
    ../src/sources/imageloader.cpp
    ../src/sources/imageloader.h
)
target_include_directories(tst_studiostate PRIVATE
    "${PROJECT_SOURCE_DIR}/src/sources"
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
    ../src/ui/widgets/mainwindow.cpp
    ../src/ui/widgets/mainwindow.h
    ../src/scene/studiostate.cpp
    ../src/scene/studiostate.h
    ../src/sources/imageloader.cpp
    ../src/sources/imageloader.h
)

target_include_directories(tst_mainwindow PRIVATE
    "${PROJECT_SOURCE_DIR}/src/ui/widgets"
    "${PROJECT_SOURCE_DIR}/src/sources"
    "${PROJECT_SOURCE_DIR}/src/scene"
)
target_link_libraries(tst_mainwindow PRIVATE
    Qt6::Widgets Qt6::Test Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg
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
