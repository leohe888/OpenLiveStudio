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

## GitHub Actions

The `Windows CI` workflow in `.github/workflows/windows-ci.yml` builds the
application and tests on `windows-2022` with Visual Studio 2022, x64, Debug and
Qt 6.8.3. The standard desktop Qt installation includes Qt Test, Qt Quick,
Quick Controls and SVG; no Qt account or custom repository secrets are required.
The Qt installation is cached. Actions are pinned to commit hashes, with their
release versions recorded beside each reference.

The workflow runs on pushes to `main`, pull requests, and manual dispatch.
A local commit does not trigger CI until pushed to GitHub. Once the workflow
is on the default branch, open **Actions > Windows CI > Run workflow** to
start it manually. If Actions are disabled by repository or organization
policy, enable this workflow and allow the referenced actions first.

The build uses the Visual Studio multi-configuration generator:

```powershell
cmake -S . -B build/ci -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/ci --config Debug --parallel
ctest --test-dir build/ci -C Debug --output-on-failure --no-tests=error --output-junit test-results.xml
```

Install Qt and make its CMake packages discoverable before running these
commands locally, for example by setting `CMAKE_PREFIX_PATH` to the Qt kit.
The Qt installation action supplies this environment in CI; local absolute
paths are not embedded in the workflow.

In a workflow run, inspect the Configure, Build and Test steps. CTest reports
failures in the Test step and treats an empty test set as an error. The existing
CTest configuration sets the Qt DLL, plugin and QML paths and uses `offscreen`
with the software Qt Quick backend. The job has a 30-minute timeout; each test
keeps its CTest timeout. A newer run on the same branch cancels the older run.

The `windows-debug-test-logs` artifact contains available CTest logs, JUnit
results and the CMake configure log, including after a failed step. Logs are
retained for seven days; a failure before configuration may leave no files to
upload. Build errors remain visible in the Build step's console log.

Accept the workflow after its first GitHub-hosted run has passed configuration,
compilation and testing. Local checks do not establish that the hosted runner
or Qt download succeeds. This workflow does not package or publish the app,
and it does not verify real GPU rendering, capture devices or media output.

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

Dialog tests cover initial properties before QML completion, closing and
resource cleanup, reopening, parent-window destruction, and load failures
(missing files, invalid QML, missing required properties and nonvisual roots).
About-dialog checks cover the help action, product and runtime Qt information,
the application version or development fallback, resizing, reuse while open,
the Close button, Esc, and reopening. The view menu no longer contains the
cancelled fullscreen-preview entry.

After the about-dialog implementation, Qt Test reported 51 passes and CTest reported
one passing test. Local validation used Qt 6.12.0 with MSVC 2022 x64, Debug.
The Visual Studio multi-configuration build and CTest commands used by CI also
passed locally and produced the JUnit report. CI's Qt 6.8.3 installation and
the GitHub-hosted workflow have not yet been verified.
These checks use Qt's `offscreen` platform plugin and the
software Qt Quick backend. The CTest command supplies Qt DLL, plugin and QML
import paths and has a 30-second timeout. Default and minimum window size
screenshots and the about dialog were also inspected with software rendering. Real GPU rendering,
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
