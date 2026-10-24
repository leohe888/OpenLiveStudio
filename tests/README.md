# Tests

The project uses Qt Test for assertions and CTest to discover and run tests.
The Qt kit must include the Qt Test module. Tests are separate executables and
are not installed with the application.

## Source layout

Application C++ files live under `src/`: `app/main.cpp` is the entry point,
`ui/widgets/` contains MainWindow, `scene/` contains StudioState, and `sources/` contains
ImageLoader. Headers stay beside their implementations. Test cases remain in
this directory; `tests/CMakeLists.txt` lists the production sources and adds
only the module include directories needed by each target.

QML panels and dialogs live in `src/ui/qml`. The application and UI test share
`OPENLIVE_STUDIO_QML_FILES`; application resource aliases and the test resource
base preserve `qrc:/OpenLiveStudio/Ui/qml/<name>.qml`. The QML module remains
`OpenLiveStudio.Ui`.

Run CMake from the repository root. The application target and CTest names,
QML module URI, QML/icon resource URLs and CI configure commands are unchanged.
When checking a path migration, also configure a new build directory so stale
objects or generated resources cannot mask missing source references.

Directory reorganization validation on 2026-10-24 used Qt 6.12.0, MSVC 2022
x64 and Ninja Debug. Each of the three module moves passed a build and complete
CTest (3/3). After moving the entry point, a fresh configure and build in
`build/source-layout-Debug` also passed; complete CTest passed 3/3 in
37.46 seconds (ui.mainwindow: 37.14 seconds). All seven moved C++ files retain
their original contents. UI verification used offscreen software rendering;
this path-only change has not been verified on a native desktop or real GPU.

### Target directory migration (2026-10-24)

The entry point was moved to `src/app/main.cpp` without content changes. A fresh
Qt 6.12.0 / MSVC 2022 x64 Ninja Debug configure and build in
`build/directory-layout-Debug` passed. Complete CTest passed 3/3 in 36.01 seconds
(ui.mainwindow: 34.42 seconds). UI checks use offscreen software rendering;
native desktop and real GPU behavior were not verified by this path migration.

MainWindow was then moved to `src/ui/widgets` with both files unchanged. The
application and test source/include paths were updated. The same Debug build
passed, followed by complete CTest 3/3 in 30.88 seconds
(ui.mainwindow: 30.59 seconds).

ImageLoader was moved to `src/sources` with both files unchanged and application
and test references updated. The Debug build passed; complete CTest passed 3/3
in 31.41 seconds (ui.mainwindow: 30.97 seconds).

Finally, all nine QML files were moved to `src/ui/qml` without content changes.
A fresh configure and build in `build/directory-layout-final-Debug` passed;
complete CTest passed 3/3 in 30.13 seconds (ui.mainwindow: 29.66 seconds).
Generated application/test resource manifests contain the same nine original
QML URLs, and the generated qmldir retains `OpenLiveStudio.Ui` and its original
relative QML paths. No new empty media-module directories or build libraries
were introduced. Native desktop interaction and real GPU rendering remain
unverified; these tests use offscreen software rendering.

Manual acceptance for the completed migration: run the rebuilt application,
check all four panels and the About dialog, add/rename/remove scenes and image
entries, and exercise panel close/restore and reset. The central preview remains
a placeholder. These actions should behave as before the path migration.

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

`core.studiostate` runs with QCoreApplication and requires no GUI platform.
Its behavior cases cover the default selected empty scene, source-list
isolation and selection clearing on scene changes, invalid and repeated
selections, explicit source deselection, and an empty initial state. IDs,
rather than display names, determine selection. Creation and rename checks
cover trimmed names, empty and case-insensitive duplicate rejection, unused
name suggestions, new empty-scene selection, preservation of existing content,
and stable IDs and selection on rename. Model checks verify row insertion and
name updates without a full scene-model reset. Removal checks cover first, middle
and last selection fallback, source-selection clearing, preservation when
removing another scene, invalid IDs and protecting the final scene without
clearing its content. Movement checks cover selected and unselected scenes,
invalid offsets and boundaries, stable IDs, content and selection, persistent
model indexes and button availability. Image-source checks cover name validation,
scene-local unused-name suggestions and gap reuse, insertion at the top, selection,
metadata and scene isolation. Removal checks cover selection clearing, row
notifications without resetting the list, persistent indexes, invalid targets,
entries with the same ID in different scenes, removing the final source and
preserving its file. Source ordering checks cover invalid targets and offsets,
first/last boundaries, persistent indexes, stable identity/selection and active
versus inactive scene updates without resetting the list. Visibility/lock checks
cover visible/unlocked defaults, role-specific notifications, idempotent setters,
invalid targets, scene-local flags (including equal IDs in different scenes),
state restoration after switching, locked-entry removal and movement availability
after insertion, removal, selection and scene changes. The test has a 10-second
timeout. It links Qt Gui for image
operations but still runs with QCoreApplication, without a GUI platform.

`core.imageloader` uses temporary PNG/BMP images to check decoded dimensions,
content-based detection, relative paths and Unicode filenames. Empty, missing,
directory, corrupt and truncated inputs must return errors without success
metadata. It also uses QCoreApplication and has a 10-second timeout.

Production starts with one selected empty scene and no sources. MainWindow
provides the same StudioState instance to both QML panels; business selection
rules remain in C++. UI checks cover default scene display, clicking scene
and source rows, highlights driven by C++, clearing source selection when
switching scenes, empty-state updates, and restoring highlights after reload.
Tests use controlled scenes and locally generated images. Shared-instance,
load-failure recovery and destruction-order checks verify that panel QML
content is released before the state. The add button and row double-click
open the shared QML name dialog, including after panel reload. Dialog checks
cover prefilling and selection, validation errors, Enter to save, button
confirmation, Tab navigation, and cancellation by button, Esc or window close.
Removal-dialog checks cover confirmation, cancellation, Esc, window close,
stale targets and final-scene protection. The dialog captures a scene ID so
changes to selection or order do not change its target; cancellation has initial
focus. Scene-panel checks cover deletion and movement in an overflowing list,
selection fallback and disabled boundary actions, both initially and after
panel reload. Source creation/removal, ordering, visibility and lock-state
management are implemented. Source-panel tests also cover clicking a nonselected
entry without changing selection, four state icons, model-driven updates, moving
hidden/locked entries, disabled boundaries and restoration after load failure or
QML reload. Reuse, composition, transform restrictions and persistence remain
future work.

B6 acceptance uses a generated PNG added separately to two real scenes. The core
scenario checks distinct entry IDs for the same path, independent additions,
removals, ordering, visibility and lock flags, and exact state restoration over
repeated switches through an empty scene. It also checks current/invalid scene
selection, source-selection clearing, removal of an inactive scene and preservation
of the underlying file. Existing invalid-target and selection cases remain part
of the complete state suite.

The MainWindow scenario runs with both initial panels and recovery after load
failure. It clicks actual QML scene rows and source controls, verifies immediate
C++ scene updates without an animation timer, preserves selection when clicking
the current scene, and checks repeated round trips, independent flags/order,
empty-state action availability and source-panel reload. Scene delegates must be
ready before test input is sent. These are offscreen software-backend checks;
composition, placement editing, real GPU rendering and source reuse are outside
B6. The change has passed user review; it adds tests without changing business
code or introducing a reuse framework.

B6 validation on 2026-10-23 used Qt 6.12.0, MSVC 2022 x64 and Ninja Debug.
The build passed. The new core scenario and both UI data rows passed in targeted
runs. Complete CTest passed 3/3, with ui.mainwindow taking 56.49 seconds and the
full run 56.75 seconds. The first targeted UI run exposed test input arriving
before scene delegates were ready; readiness waits and a null-item assertion
were added to the test, after which targeted and complete runs passed.

The source add form is QML; browsing uses a C++ QFileDialog parented to the form
window. Non-native file-picker tests verify cancellation preserves the previous
input, spaces/Unicode/#/% in paths, picking without creating, confirmation and
cleanup. The second opening receives the next unused suggested name. Removal
checks cover fixed scene/source IDs after switching scenes, last-source removal,
stale scenes/sources, plain-text names, default Cancel focus and file preservation.
Both panel actions are tested initially and after load failure/recovery. The
shared dialog entry no longer calls winId() early or manually positions dialogs;
the temporary centering regression tests were removed with that positioning code.

Both list panels have overflow-only vertical scrollbars. Data-driven tests
use 24 scenes and sources to verify wheel input, thumb dragging to the end,
last-row visibility, reserved scrollbar width, unchanged selection, and
visibility changes on resize or replacement with a short model. Software
rendering also checks scrollbar and row-button visibility.

The HTML prototype now mirrors the name dialog, add and double-click rename
entries, and overflow scrollbar appearance. Its script passed Node's syntax
check. Browser checks verified empty/duplicate rejection, trimmed creation,
Enter confirmation, rename, Esc and button cancellation, and scrolling an
overflowing scene list. Prototype scenes and media remain demonstration data;
these browser checks do not establish application backend or device support.
The prototype has not yet been updated for the implemented scene removal and
ordering interactions, the image-source add/remove behavior completed in B4, or
the source ordering/visibility/lock behavior completed in B5.

`ui.mainwindow` covers window lifecycle and structure, output status visibility
on resize, scene and source QML loading and empty states, source panel resizing,
and mixer QML loading with two unavailable channel slots. The removed transition
dock, transition QQuickWidget and view-menu action are checked for absence. Its standalone
QML loading and icon-rendering cases were removed with the QML resource. Layout
checks cover the vertically arranged scene and source docks on the left and
the left column extending to the bottom, with mixer and controls
arranged left to right beneath the preview. All four docks are checked
for floating, redocking, closing, load failure and recovery, including isolation
from the other panels. Icon checks cover decoding all fourteen SVG resources and
rendering the panel buttons in enabled and disabled states. Source row checks
use test-only model data. Mixer checks also cover disabled sliders ignoring
clicks, compact panel height, scrollable content at small sizes, and fixed-width
channels staying grouped on the left as the panel grows. The application still has no
media capture, audio or transition backend; mixer meters remain empty and
unconnected controls disabled. Rendering checks isolate source,
recording and streaming icons from button text and backgrounds. Source rendering
covers eye/eye-off and lock/lock-open in enabled and disabled states. State icons
use neutral colors and transparent button backgrounds; selection highlights
belong to the source row. Tooltips and accessible names describe current state
(visible/hidden and locked/unlocked), rather than the next click action. Native
hover-tooltip appearance has not been manually verified.

Controls panel checks cover exactly the recording, streaming and settings
entries, all disabled and arranged vertically with fixed heights at the top.
A bottom spacer takes up remaining height; short panels can scroll to the last
button. The exit entry, exit signal and panel duration labels have been removed.
REC/LIVE duration placeholders remain in the main window status bar.
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

After B5 and its icon/tooltip adjustments, the Ninja Debug build passed with
Qt 6.12.0 and MSVC 2022 x64. The final complete CTest run on 2026-10-23 passed
3/3: ui.mainwindow took 31.49 seconds and the full run took 31.86 seconds.
The state-icon decoding, enabled/disabled rendering, interaction and reload
checks passed. Software-rendered source-panel screenshots were inspected after
removing the blue checked-state styling. These results validate state management
and panel behavior, not composition, hidden-source output or transform locking.
Real GPU rendering and native hover-tooltip appearance remain unverified.

At the B4 checkpoint, after image-source add/remove integration, the image-loader
executable reported
12 passes and the state executable 40 passes, including initialization and cleanup.
The final source-action regression run reported 14 passes. The separate complete
window suite reported 88 passes in 32.38 seconds. That Debug build passed.
The original 30-second CTest limit applied to the entire UI executable, including
all data rows. A diagnostic run passed all 88 cases in 30.07 seconds, with the
slowest case taking about 0.78 seconds; no individual case was stuck. Repeated
window/QML setup, rendering and asynchronous event handling accumulate across
the suite. The UI executable now has a 90-second limit to allow setup and CI
variance. Both core executables retain their 10-second limits; individual test
assertion rules are unchanged. The complete CTest run at that checkpoint passed
3/3, with
ui.mainwindow taking 31.04 seconds and the full run 31.29 seconds. The suite has
not been split into smaller executables.
Local validation used Qt 6.12.0 with MSVC 2022 x64, Debug. The Visual Studio
multi-configuration commands used by CI previously passed locally and produced a
JUnit report. CI's Qt 6.8.3 installation and the GitHub-hosted workflow remain
unverified.
UI checks use Qt's `offscreen` platform plugin and the
software Qt Quick backend. The CTest command supplies Qt DLL, plugin and QML
import paths and gives the entire UI executable a 90-second timeout. Software-rendered screenshots of
the default scene, source empty state, selected test source, name-dialog
normal/error states, overflowing lists with scrollbars, removal-dialog
confirmation/error states and movement button states before and after ordering
were checked. Image-source add and removal dialogs were also inspected in
normal/error states. Windows checks confirmed native file-picker opening, owner
window and closing; deployment dry-run scanning identified the dialog dependencies
at that stage. The final implementation uses QFileDialog instead of Qt Quick
FileDialog. Temporary native-window checks also exercised repeated dialogs and
parent-window movement before manual centering was removed; those positioning
checks do not validate the current default placement or prove absence of visual
flicker.
Default and minimum window size screenshots and the about dialog were also
inspected in earlier rounds. Real GPU rendering,
media processing and device behavior remain unverified.

The controls panel's width and height binding loops have been fixed in project
source. The regression check was verified to fail with the original bindings;
each test now fails on binding-loop warnings. Current test and software-rendering
logs contain no such warnings. Expected warnings from missing-resource recovery
tests and limitations of the offscreen platform are not treated as binding loops.

```powershell
ctest --test-dir $buildDir -L smoke --output-on-failure
```

### Transition panel removal validation (2026-10-24)

The application and HTML prototype now have four docks: scenes, sources, mixer
and controls. The bottom row contains mixer followed by controls. Scene changes
use the existing immediate switch behavior; the prototype no longer offers a
transition effect, duration or fade animation. Transition features are deferred.

Both removal steps passed a Qt 6.12.0, MSVC 2022 x64 Ninja Debug build in
`build/remove-transitions-Debug`. The initial attempt in `build/qt-ninja-Debug`
could not replace the running application executable (LNK1168), so validation
used the separate build directory without closing the running application.
Complete CTest passed 3/3 after each step: the first run took 31.93 seconds
(ui.mainwindow: 31.55 seconds), and the final run after QML/resource cleanup
took 30.81 seconds (ui.mainwindow: 30.44 seconds). These UI checks use the
offscreen platform and software Qt Quick backend.

Node checks passed script syntax, direct switching between scenes, clearing
source selection on a different scene, preserving it for the current scene or
an invalid target, four dock/menu entries, resetting four panels, and removal
of transition-script references. These checks exercise extracted functions
with controlled substitutes; they do not establish browser rendering or full
DOM interaction. The browser tool rejected the local `file://` URL under its
security policy. Actual browser display and interaction, native desktop layout
and GPU rendering after this change remain unverified.

Manual acceptance:

1. Run the rebuilt application; confirm the bottom row is mixer then controls,
   and the View menu has only the four remaining dock entries.
2. Float, close and restore panels, then reset the layout at default and minimum
   window sizes; verify all four panels return without overlap.
3. Add two scenes with distinct image entries. Switching scenes must update the
   source list immediately and clear source selection; clicking the current
   scene must preserve it. The central preview is still a placeholder.
4. Open `docs/openlive-studio-prototype.html` in a browser. Confirm no transition
   panel or menu entry remains, scene changes have no fade, panel closing and
   restoration work, and layout reset restores the four panels. Check for
   script errors. Prototype media remain simulated.

## Static image composition and shared frame handoff

C1.1 added real offscreen OpenGL image composition. C1.2a adds optional context
sharing and a fixed pool of three result buffers. This is a graphics-module
capability: the application preview is still a placeholder. A production worker,
scene-to-pixel snapshot assembly, QOpenGLWidget integration and unified Qt Quick
OpenGL startup remain subsequent work.

`ImageCompositor` takes immutable decoded image layers, topmost first. Images fit
the canvas without cropping, preserve transparency and compose over black.
Hidden layers are omitted. Initialization, rendering, diagnostic texture access
and readback, release and destruction belong to the producer thread. The GUI
thread creates/destroys the QOffscreenSurface. Keep the surface and sharing
context alive until the compositor is released.

With sharing enabled, `acquireFrame()` requires a compatible sharing context
current on the consumer thread. It takes the latest unconsumed result and queues
a GPU wait for the producer's flushed completion fence. Bind the shared texture
after acquisition; FBO objects themselves are not shared. A frame has one owner;
copying its descriptor does not extend ownership. After submitting all reads,
call `returnFrame()` with the same consumer context current. It inserts and
flushes a consumer completion fence. Do not use the descriptor again.

The producer polls returned fences with zero timeout, never overwrites acquired
buffers and replaces obsolete unconsumed results. Rendering returns false with
an empty error when all three buffers remain occupied; this is normal
backpressure, not an allocation failure. Acquisition returns a zero token with
an empty error when there is no new result. Invalid operations return errors.
CPU metadata is protected by a mutex, not an unbounded frame queue; drawing runs
outside that mutex. These APIs do not wait for GPU completion on the CPU.

Stop consumer activity and return every acquired frame before shutdown.
`release()` refuses outstanding acquisitions so the caller can return them and
retry. Once returned, it orders cleanup after consumer GPU work, releases GL
resources on the producer thread and permits restart. Destroying a compositor
before release can succeed is a fatal lifecycle error. Stale, foreign, modified
or double-returned tokens and returns from a different consumer context are
rejected.

### Build and manual acceptance

In a Qt/MSVC developer terminal, configure with
`-DBUILD_TESTING=ON -DOPENLIVE_STUDIO_GL_TESTS=ON`, then build. The graphics test
option defaults to OFF; when enabled, `graphics.imagecompositor` uses the native
Windows platform, not the UI smoke test's offscreen software backend.

```powershell
cmake --build build/c1-image-compositor-Debug --parallel
ctest --test-dir build/c1-image-compositor-Debug -R graphics.imagecompositor --output-on-failure
ctest --test-dir build/c1-image-compositor-Debug --output-on-failure
```

For a detailed native test log, use the Qt kit's runtime environment and run
`tst_imagecompositor.exe -o graphics-results.txt,txt` from the build directory.
The log includes GL vendor, renderer and version. A missing OpenGL context is
a test failure, not a silently skipped graphics check.

Review the buffer ownership and fence ordering in `imagecompositor.cpp`, then
run the tests above. Controlled pixels verify orientation, aspect ratio, alpha,
layering and hidden layers. Shared-context checks read textures through local
consumer FBOs; ownership checks hold all three frames while production attempts
are skipped, then return one and resize its replacement without changing the
other two. Tests also cover latest-result selection, pending GPU reads before
return, cross-thread sharing, invalid contexts/tokens, shutdown refusal,
deletion of output texture names and restart. Readback is a diagnostic test
operation, not the future production preview path.

On 2026-10-24, C1.2a passed a Qt 6.12.0 / MSVC 2022 x64 Ninja Debug build.
The native graphics executable passed 13 cases (including initialization and
cleanup), with zero failures or skips, on NVIDIA GeForce RTX 4060 Ti,
OpenGL 4.6.0, driver 566.14. Complete CTest passed 4/4 in 28.98 seconds
(ui.mainwindow: 26.77 seconds).
Detailed local logs are `build/c1-image-compositor-Debug/c12a-graphics.txt`
and `build/c1-image-compositor-Debug/c12a-results.xml`.
This verifies the module on that graphics backend. Other drivers,
unsupported-sync hardware and forced context loss remain
unverified. Window display/resize/context recreation, production scheduling,
minimized continuous media output and device/media output are not established
by these tests.

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
