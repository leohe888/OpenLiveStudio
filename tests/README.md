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
the left column extending to the bottom, with mixer, transitions and controls
arranged left to right beneath the preview. All five docks are checked
for floating, redocking, closing, load failure and recovery, including isolation
from the other panels. Icon checks cover decoding all twelve SVG resources and
rendering the panel buttons in enabled and disabled states. Source row checks
use test-only model data. Mixer checks also cover disabled sliders ignoring
clicks, compact panel height, scrollable content at small sizes, and fixed-width
channels staying grouped on the left as the panel grows. The application still has no
scene, source, audio or transition backend; mixer meters remain empty and
unconnected controls disabled. Rendering checks isolate the transition,
recording and streaming icons from button text and borders.

Controls panel checks cover exactly the recording, streaming and settings
entries, all disabled and arranged vertically with fixed heights at the top.
A bottom spacer takes up remaining height; short panels can scroll to the last
button. The exit entry, exit signal and panel duration labels have been removed.
REC/LIVE duration placeholders remain in the main window status bar. Transition
checks also verify that the transition button stays at the bottom of its panel.
Recording, streaming, timers and the settings dialog remain unimplemented.

View-menu checks cover hiding and restoring every panel, check-state sync for
docked and floating panels, and restoring all panels after they have been hidden.
Layout reset checks cover moved, tabbed, floating, off-screen and hidden panels,
plus repeated resets at default and minimum window sizes. Reset restores the
current default layout without changing window size or recreating QML roots.
Layout persistence across application restarts remains future work.

After the latest layout and alignment updates, Qt Test reported 45 passes and CTest reported
one passing test. These checks use Qt's `offscreen` platform plugin and the
software Qt Quick backend. The CTest command supplies Qt DLL, plugin and QML
import paths and has a 30-second timeout. Default and minimum window size
screenshots were also inspected with software rendering. Real GPU rendering,
media processing and device behavior remain unverified.

The controls panel's width and height binding loops have been fixed in project
source. The regression check was verified to fail with the original bindings;
each test now fails on binding-loop warnings. Current test and software-rendering
logs contain no such warnings. Expected warnings from missing-resource recovery
tests and limitations of the offscreen platform are not treated as binding loops.

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
