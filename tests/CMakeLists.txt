find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

qt_add_executable(tst_mainwindow
    tst_mainwindow.cpp
    ../mainwindow.cpp
    ../mainwindow.h
)

target_include_directories(tst_mainwindow PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(tst_mainwindow PRIVATE Qt6::Widgets Qt6::Test)

# This smoke test needs no desktop; future GPU tests need real GL.
add_test(NAME ui.mainwindow
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen"
        "$<TARGET_FILE:tst_mainwindow>"
)
set_tests_properties(ui.mainwindow PROPERTIES
    LABELS "ui;smoke"
    TIMEOUT 15
)
