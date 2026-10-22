# Tests

The project uses Qt Test for assertions and CTest to discover and run tests.
The Qt kit must include the Qt Test module. Tests are separate executables and
are not installed with the application.

## Build and run

In Qt Creator, enable `BUILD_TESTING` in the CMake configuration (default: ON),
reconfigure, and build the project. Use the kit's developer terminal to run:

```powershell
$buildDir = 'build/Desktop_Qt_6_12_0_MSVC2022_64bit_Debug'
ctest --test-dir $buildDir -N
ctest --test-dir $buildDir --output-on-failure
```

Replace the build directory when using another kit. CTest does not build the
test executables; build after changing code before running tests.

For an already configured build directory, the command-line equivalent is:

```powershell
cmake -S . -B $buildDir -DBUILD_TESTING=ON
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug --output-on-failure
```

Use a terminal with the selected MSVC compiler and build tool available. For a
multi-configuration generator, `-C Debug` must match the configuration built.
To omit test targets, reconfigure with `-DBUILD_TESTING=OFF`.

## Current coverage

`ui.mainwindow` covers window lifecycle and structure, output status visibility
on resize, scene and source QML loading and empty states, source panel resizing,
and mixer QML loading with two unavailable channel slots. Transition panel
checks cover loading, unselected effects, the prototype duration default,
disabled controls ignoring mouse and keyboard input, and resizing. Layout
checks cover the vertically arranged scene and source docks on the left and
the mixer and transition docks on the right, and the controls dock spanning the
bottom above the status bar. All five docks are checked
for floating, redocking, closing, load failure and recovery, including isolation
from the other panels. Icon checks cover decoding all twelve SVG resources and
rendering the panel buttons in enabled and disabled states. Source row checks
use test-only model data. Mixer checks also cover disabled sliders ignoring
clicks and scrollable content at small sizes. The application still has no
scene, source, audio or transition backend; mixer meters remain empty and
unconnected controls disabled. Rendering checks isolate the transition,
recording and streaming icons from button text and borders.

Controls panel checks cover disabled recording, streaming and settings entries,
separate duration placeholders, exit requests, and reaching the exit button by
scrolling a narrow panel. Integration checks verify that exit uses the normal
window close event, honors rejection, and remains connected after a QML reload.
Recording, streaming, timers and the settings dialog remain unimplemented.

After controls dock integration, Qt Test reported 43 passes and CTest reported
one passing test. These checks use Qt's `offscreen` platform plugin and the
software Qt Quick backend. The CTest command supplies Qt DLL, plugin and QML
import paths and has a 15-second timeout. Default and minimum window size
screenshots were also inspected with software rendering. Real GPU rendering,
media processing and device behavior remain unverified.

Known issue: the current controls QML emits a `contentHeight` binding-loop
warning during loading or resizing. The suite passes despite this warning;
these results do not establish a warning-free UI. A candidate correction was
checked only in an ignored build-directory copy and has not been applied to
the project source.

```powershell
ctest --test-dir $buildDir -L smoke --output-on-failure
```

## Adding tests with features

- Add tests for meaningful behavior as each feature is implemented, rather than
  asserting placeholder controls or exact pixel positions.
- Prefer fixed images, generated audio and controllable inputs for repeatability.
- Register each executable with `add_test`, give it a clear name and label,
  and set a timeout. Additional executables can follow `tst_mainwindow`.
- Future logic tests should not require a GUI. UI interaction tests can use
  Qt Test; asynchronous assertions should wait for a condition with a timeout.
- GPU and real-device tests need their own environment and explicit opt-in;
  do not force them to use the smoke test's offscreen plugin or make them part
  of the default device-free run.
- Record unverified device behavior separately. A passing smoke test does not
  establish camera compatibility, audio synchronization or recording quality.
