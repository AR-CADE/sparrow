# Changelog

All notable changes to the Sparrow compositor project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.2.4] - 2026-10-22

### Added

- **CSIR PGO + LTO + BOLT 4-Stage Optimization Pipeline (`./build.sh csir-bolt`)**:
  - Automated 4-stage optimization pipeline combining Context-Sensitive IR PGO, ThinLTO, and LLVM BOLT post-link layout optimization (`tools/bot/csir_bolt_optimize.sh`).
  - **Stage 1 (IR-PGO)**: Instruments LLVM IR (`-fprofile-generate=out/csir_profiles/ir`) and trains on multi-window PageView/Overview and automated bot workloads, generating `sparrow_ir.profdata`.
  - **Stage 2 (CSIR-PGO)**: Context-Sensitive IR PGO instrumentation (`-fcs-profile-generate=out/csir_profiles/csir`) capturing context-sensitive call graphs, merged into `sparrow_csir.profdata`.
  - **Stage 3 (CSIR + ThinLTO)**: Production release compilation with ThinLTO and preserved relocations (`-Wl,--emit-relocs`).
  - **Stage 4 (LLVM BOLT Layout Optimization)**: Hardware LBR branch profiling via Linux `perf record`, basic-block layout reordering (`-reorder-blocks=ext-tsp`), function reordering (`-reorder-functions=cdsort`), and cold block splitting (`-split-functions`, `-split-all-cold`).
  - Dedicated Flutter Engine BOLT optimization (`./build.sh bolt-engine` / `tools/bot/bolt_engine.sh`) for post-link binary optimization of `libflutter_engine.so`.
- **Integrated `wlrctl` Meson Wrap Subproject**:
  - Added `wlrctl` as a Meson subproject wrap (`subprojects/wlrctl.wrap`), automatically building `wlrctl v0.2.2` with Clang.
  - Automatically installs the compiled binary to `out/wlrctl` via `./build.sh`.
  - Updated all test suites and bots (`sparrow_bot.sh`, `runner_test.sh`, `pointer_calibration_test.sh`, `jank_frame_test.sh`, `pageview_overview_test.sh`, `fixed_size_test.sh`, `pgo_workload_runner.py`) to prioritize `out/wlrctl` over the system `$PATH`.
- **Shell Configuration: Texture Filter Quality (`filterQuality`)**:
  - Added `filterQuality` option in `[Quirks]` section of `sparrow.ini` (`none`, `low`, `medium`, `high`) to control texture sampling quality for Wayland client surfaces in Flutter.
  - Dynamically propagates `filterQuality` from `ConfigurationCubit` to `SurfaceView` via `BlocSelector` in `SurfacePage`.
- **Runner Session Lock Protocol (`ext-session-lock-v1`)**:
  - Implemented `ext-session-lock-v1` protocol support in `sparrow-app-runner`, enabling Flutter clients to act as secure Wayland session lock screens with dedicated keyboard event routing, lock surface reconfiguration, and event loop wakeups.
- **OpenSSF Security Hardening**:
  - Added modern compiler and linker security flags: `-fstack-protector-strong`, `-fstack-clash-protection`, `-fno-delete-null-pointer-checks`, `-D_GLIBCXX_ASSERTIONS`, `-Wformat-security`, `-ftrivial-auto-var-init=zero`, `-D_FORTIFY_SOURCE=3`, `-fcf-protection=full`, and full RELRO `-Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code`.

### Fixed

- **Dialog Window Sizing & Centering (Mullvad Browser / Firefox / xdg-shell)**:
  - Fixed an issue where transient and child dialog surfaces (e.g. the Mullvad Browser / Firefox download prompt) were incorrectly forced to maximize to full screen dimensions (1920x1200) instead of being configured with `(0, 0)` so the client can size itself to its natural content dimensions.
  - Added `wlr_xdg_toplevel` `set_parent` signal listener and parent view tracking in `SparrowView` (`src/surface/surface.cpp`, `src/surface/view.hpp`).
  - Allowed dialogs to unmaximize freely in `xdg_toplevel_request_maximize` instead of enforcing fullscreen workspace maximization.
  - Transmitted `parent_handle` in `SurfaceMap` to Dart via Pigeon IPC.
  - Updated `SurfaceView` in `compositor_dart` to automatically detect dialogs (`isDialog`), unmaximized surfaces, and subsurface bounding boxes, centering them with proper letterboxing (`Center(ClipRect(...))`) and accurate pointer coordinate translation via `updatePosition`.

- **IPC Client Connection Lifecycle & RAII Cleanup**:
  - Encapsulated `ClientConnection` Wayland event source and socketpair file descriptor management into RAII destructor in `src/ipc/ipc_server.cpp`, eliminating file descriptor and event source leaks on client disconnect or compositor shutdown.
- **Idle Timeout Activity Tracking**:
  - Reset idle timeout on keyboard key events and low-framerate content redraw updates in `src/input/seat.cpp`.
- **Super Key Shortcut Disambiguation**:
  - Prevented application launcher from opening upon `Super` key release when `Super` was used in a modifier combination (e.g. `Super+F`) or during touch/scroll gestures.
- **Fullscreen & RFP (Resist Fingerprinting) Letterboxing**:
  - Synchronized fullscreen state toggle on `Super+F` and removed key conflict with FPS OSD (`F11`).
  - Preserved fullscreen status across letterboxing transitions for RFP-enabled browsers like Firefox and Tor Browser.
  - Deferred `unset_fullscreen` calls via idle callback to eliminate race conditions on surface unmap.
  - Ensured correct geometry and subsurface alignment for client-side decorations (CSD) in fullscreen and letterboxed views.
- **Platform API Deduplication**:
  - Added deduplication guards for platform API state updates in both Dart (`CompositorPlatformApi`) and C++ (`SurfaceCallback`), reducing redundant RPC traffic during window geometry and overview state updates.
- **Overview Scrolling Redundant RPC**:
  - Prevented redundant fullscreen dock reveal calls during PageView overview scrolling.
- **Static Analysis & Code Quality**:
  - Applied clang-tidy and clang-analyzer fixes across C++ codebase.
  - Added session lock integration tests to the comprehensive test suite (`tools/bot/test_all.sh`).

## [0.2.3] - 2026-10-16

### Added

- **Direct Scanout for Fullscreen Clients (`wl_shm` & Hardware DRM Blit)**:
  - Direct Scanout hardware blit pipeline for fullscreen applications and software-rendered clients (`wl_shm`).
  - Active damage pacing and immediate frame presentation avoiding stale buffer display across multi-frame commits.
  - Dedicated hardware cursor plane and software cursor rendering overlay during Direct Scanout without forcing fallback to full compositing.
  - Seamless yield to Flutter compositing (DS: OFF) on dock gestures or overview transitions, with automatic and jitter-free resumption (DS: ON).
  - Integrated automated test harness `tools/bot/fullscreen_scanout_test.sh` into `tools/bot/test_all.sh` verifying fullscreen transitions, dynamic commits, and screenshot captures.
- **Wayland Gesture Models Serialization**:
  - Implemented `@JsonSerializable()` and `Equatable` for `GestureSwipeEvent` (`GestureSwipeBeginEvent`, `GestureSwipeUpdateEvent`, `GestureSwipeEndEvent`) and `ZoomEvent` (`ZoomBeginEvent`, `ZoomUpdateEvent`, `ZoomEndEvent`).
  - Added robust fallback constructors and custom JSON converters (`_readCancelled`) to handle nullable fields and boolean/integer types from C/Wayland IPC.
  - Added unit test suites in `compositor_dart/test/data/models/` validating JSON serialization, deserialization, and value equality.
- **CI Test Automation**:
  - Added automated test steps for `compositor_dart` and `shell` in `.github/workflows/ci.yml` before building release artifacts.

### Fixed

- **Dock Reveal & Surface Focus on Window Map from Overview**:
  - Resolved dock hover detection bug when opening a new application from Overview mode.
  - Properly resets `forceRenderAllViews(force: false)` on the compositor platform interface, allowing edge hover detection in C++ to re-engage immediately.
  - Restores surface focus and dock hide timer state when returning from Overview.
- **Input & Pointer Seat Capabilities**:
  - Preserved `WL_SEAT_CAPABILITY_POINTER` on seat whenever the compositor cursor is active (`instance->cursor != nullptr`).
  - Eliminates unwanted `wl_pointer.leave` events and client pointer destruction during virtual pointer attach/detach cycles in headless testing and multi-device setups.

### Changed

- **Dock Auto-Hide Delay**:
  - Reduced default dock reveal auto-hide delay from 4 seconds to 2 seconds (`Duration(seconds: 2)`) for faster and more responsive desktop shell interactions.

## [0.2.2] - 2026-10-03

### Added

- **Display Rotation & Output Transform Architecture**:
  - Display transform support (90°, 180°, 270°, and flipped orientations) across the compositor render pipeline, damage tracking, and Flutter presentation.
  - Integrated dedicated automated test `tools/bot/popup_rotation_test.sh` validating multi-generation nested popup positioning and bounds checking across rotations.
- **Flutter Application Lifecycle Support**:
  - Wired Flutter lifecycle events (`AppLifecycleState.resumed`, `inactive`, `paused`, `detached`) in both the compositor shell and the standalone app runner (`sparrow-app-runner`).
- **Comprehensive Automated Integration & Regression Test Suite (`tools/bot/test_all.sh`)**:
  - **Dart VM Service Client (`tools/bot/vm_service_client.py`)**:
    - Direct JSON-RPC 2.0 client connecting to the Dart VM Service (`http://127.0.0.1:8181/`).
    - Inspects isolate health, uncaught Dart exceptions, heap/external memory usage, and registered Flutter views during automated tests.
  - **PageView & Overview Integration Test (`tools/bot/pageview_overview_test.sh`)**:
    - End-to-end multi-window test mapping clients across multiple pages.
    - Exercises PageView horizontal swipe navigation, Overview zoom mode (`Alt_L`), card navigation (arrow keys), and window selection (`Return`).
    - Validates live toggling of Rainbow damage borders (`F12`) and FPS OSD monitor (`F11`), auditing memory and isolate health via the Dart VM Service.
  - **Pointer Calibration & Precision Tool (`tools/bot/repro_clients/pointer_calibrator.c` & `tools/bot/pointer_calibration_test.sh`)**:
    - Dedicated Wayland client measuring subpixel pointer motions and touch coordinates received from the compositor.
    - Accurately tracks `wl_seat`, `wl_pointer`, `wl_touch` events with microsecond timestamps to detect pointer mapping/acceleration drift.
  - **Jank / Junk Frame & Pacing Regression Monitor (`tools/bot/repro_clients/jank_monitor.c` & `tools/bot/jank_frame_test.sh`)**:
    - High-frequency Wayland frame presentation analysis recording real `wl_surface.frame` callback intervals.
    - Computes P50, P95, P99 frame time latencies, effective FPS, and flags budget-exceeding (>20ms) and severe (>33.3ms) dropped frames under synthetic UI load.
  - **Edge-Case Reproduction Clients (`tools/bot/repro_clients/`)**:
    - `repro_tooltip_argb.c`: Reproduces Chromium Aura ARGB8888 translucent tooltips with premultiplied alpha.
    - `repro_subsurface_popup.c`: Reproduces Firefox and CPU-X popup subsurfaces (`wl_subsurface` attached to `xdg_popup`).
  - **Declarative PGO Application Workload (`tools/bot/pgo_apps.json` & `tools/bot/pgo_workload_runner.py`)**:
    - Declarative JSON workload suite specifying applications to open, duration in seconds, and synthetic interaction patterns (mouse gestures, clicks, typing, hotkeys).
    - Integrated with `tools/bot/sparrow_bot.sh` (`--pgo`, `--apps-config=PATH`) and `tools/bot/pgo_optimize.sh` for profile-guided optimization.
- **Session Lock Protocol (`ext-session-lock-v1`)**:
  - Full implementation of `ext-session-lock-v1` in `src/surface/session_lock.cpp` and `src/surface/session_lock.hpp`.
  - Secure screen locking with complete confidentiality: suppresses normal client surfaces and Flutter shell rendering while locked.
  - Renders exclusively the authenticated lock screen surface, falling back to a solid black blanking rect if unmapped.
  - Complete input inhibition: keyboard, mouse pointer, clicks, trackpad gestures (swipe, pinch, hold), and multi-touch events are strictly isolated and routed exclusively to the focused lock surface, ensuring no keystrokes or cursor actions leak to Flutter or background windows.
  - Dynamic display reconfiguration: sends `configure` events to session lock clients upon output transform, mode, or scale changes, properly supporting dynamic screen rotation (e.g., with `swaylock -i`).
  - Aspect-ratio preservation and letterboxing during rotation transition to eliminate anamorphic image distortion before client re-render.
- **Idle Inhibition Protocol (`wlr_idle_inhibit_v1` & `ext-idle-notify-v1`)**:
  - Integrated `wlr_idle_inhibit_v1` with `wlr_idle_notifier_v1` to allow media players, browsers, and presentation apps to inhibit idle blanking and DPMS sleep during active playback.
  - Automatically suspends idle inhibition while the session is locked so power-saving / DPMS can properly turn off displays during screen lock.
  - Implemented automatic idle activity notification for active window redraws (`xdg_toplevel`, `subsurface`, and `popup` commits). Visible animating windows (such as RPCS3 game previews, videos, and game viewports) keep the compositor awake without requiring continuous hardware mouse/keyboard input.
  - Applied minimum damage area filtering (`MIN_IDLE_DAMAGE_AREA = 2048 px²`) and rate limiting (1 Hz) to prevent small background updates (e.g. 10x20 terminal cursor blinks or 1Hz clocks) from resetting the idle timer indefinitely.
- **Hardware DRM Render Node Auto-Detection**:
  - Implemented automatic scanning and opening of hardware DRM render nodes (`/dev/dri/renderD128..136`) in `src/core.cpp` when running non-DRM backends (such as headless testing for PGO/Bot, nested Wayland under Weston/Niri/KDE, or X11).
  - Eliminates crashes caused by Mesa `llvmpipe` software rasterizer falling back in environments that do not export `WLR_RENDER_DRM_DEVICE`.
  - Added automated DRM node export and `mesa_glthread=false` safeguards in `tools/bot/sparrow_bot.sh`.
- **Scudo Standalone Build Pipeline**:
  - Added `build_scudo.sh` script to build `libclang_rt.scudo_standalone-<arch>.so` with full GWP-ASan hooks and signal handling enabled.
- **Session Lock & Idle Automated Test Suite (`tools/bot/session_lock_test.sh`)**:
  - 4-test harness covering: protocol negotiation & lock surface lifecycle, exclusive keyboard focus immunity under lock, redraw-based idle prevention (active window redraws keep compositor awake), and cursor noise filtering (blinking text cursor does not inhibit sleep).
  - Includes sanitizer log inspection (ASan, UBSan, TSan, LSan) and clean shutdown verification.
  - Self-contained Makefile (`tools/bot/session_lock/Makefile`) for automatic wayland-scanner protocol header/code generation and test binary compilation.

### Changed

- **POSIX Threading Modernization to C++26 RAII**:
  - Refactored legacy POSIX threading primitives (`pthread_mutex_t`, `pthread_cond_t`) across compositor core modules to modern C++ `std::mutex`, `std::recursive_mutex`, and RAII wrappers (`std::lock_guard`, `std::unique_lock`), eliminating manual unlock hazards and potential deadlock vectors.
- **Replaced Legacy RapidJSON with Modern simdjson**:
  - Replaced RapidJSON across the entire compositor, runner, and client wrappers (`src/ipc/ipc_server.cpp`, `src/runner/ipc_client.cpp`, `src/runner/flutter_runner.cpp`, `src/flutter/platform/text_input.cpp`, `platform/client_wrapper/json_message_codec.cc`, `platform/client_wrapper/json_method_codec.cc`, `examples/simple_app/linux/runner/my_application.cc`).
  - Implemented high-performance zero-copy `FastJsonSerializer` for `flutter::EncodableValue` (`platform/client_wrapper/fast_json_serializer.h`).
  - Completely deleted the unmaintained 2016 `platform/rapidjson` directory (>1,900 files removed).
  - Microbenchmarks on real Sparrow payloads achieved a **2.3x to 5.08x parsing speedup** (up to 16.45M msg/s) and sub-microsecond serialization.
- **Shell architecture restructured**: Flat `shell/lib/*.dart` files reorganized into `shell/lib/src/{core,data,domain,presentation}/` following Clean Architecture conventions.
- **PGO Profile Generation Multi-Suite Training**:
  - Integrated `pageview_overview_test.sh` into the PGO generation pipeline (`tools/bot/pgo_optimize.sh`) alongside the declarative app workload bot.
  - Generates and merges multi-process raw profile datasets (`out/pgo_profiles/sparrow-%p.profraw`) capturing realistic multi-window PageView swipes, Overview zoom transitions, and individual client applications.
- **Updated Dependencies**:
  - Upgraded Flutter SDK to version 3.47.6.

### Fixed

- Popup repositioning and constraint re-unconstraining (`wlr_xdg_popup_unconstrain_from_box`) on display rotation and parent view movement.
- **Heap-Use-After-Free on Window Teardown with Popups**:
  - Fixed a critical heap-use-after-free crash in `sparrow_popup_damage_whole` and `popup_handle_unmap` when closing a toplevel view that owned active child popups.
  - Added an explicit detachment loop (`pop->parent_view = nullptr`) for all child popups of the closing view in `src/surface/surface.cpp`.
- **Display Rotation Surface Deformation & Aspect Ratio**:
  - Prevented non-uniform surface deformation and anamorphic stretching of toplevel windows during display orientation transitions.
  - Centered fixed-size windows with black letterboxing and maintained proportional scaling in overview mode.
  - Resolved buffer stride collisions with `wleird` and fixed FPS OSD and rainbow damage border clipping on rotated displays.
- **Chromium Translucent Tooltips & Buffer Release Deadlock**:
  - Fixed alpha blending for Chromium Aura ARGB8888 translucent tooltips and eliminated buffer release deadlocks when tooltips are dismissed.
- **Premature Repro Client Exit on `popup_done` During PGO & Workloads**:
  - Fixed an issue where `repro_tooltip_argb` and `repro_subsurface_popup` prematurely exited (causing `[WARN] Process exited prematurely with code 0`) when an outside click or Overview toggle dismissed the popup.
  - Conformed to xdg-shell specification: the client now destroys only the popup surface upon receiving `xdg_popup.popup_done` while keeping the main toplevel window active and responsive for its full test duration.
  - Added robust command-line argument parsing supporting both `--duration=N` and `--duration N` across test clients.
- **mpv & Vulkan Overview Animation Flickering and Tearing**:
  - Disabled premature advertisement of `wp_linux_drm_syncobj_manager_v1` in `src/core.cpp`. Because Sparrow renders surfaces via Flutter external textures without syncobj timeline fence synchronization, advertising this protocol caused Mesa Vulkan WSI (e.g. mpv with `gpu-next`) to disable implicit kernel sync and emit acquire/release timeline points that Sparrow never waited on or signaled, causing severe data races, frame tearing, and flickering during overview transitions. Disabling this global forces Mesa to use Linux DRM kernel implicit dma-buf sync (`dma_fence`), guaranteeing hardware-synchronized reader and writer access.
  - Eliminated nearest-neighbor (`GL_NEAREST` / `FilterQuality.none`) downsampling artifacts in `compositor_dart/lib/src/presentation/surface.dart` and `popup.dart`, preventing moiré and scanline flickering during continuous fractional window scaling in overview transitions.
- **Session Lock Focus Immunity Against Background View Interactions**:
  - Resolved a bug where background applications (e.g. RPCS3 animating game thumbnails with audio/popups) could steal keyboard focus away from the lock screen, preventing password input.
  - Active views are now deactivated upon lock establishment, all view focus requests and xdg-activations are strictly guarded during session lock, and lock surfaces maintain continuous keyboard focus via map/commit synchronization and input event auto-refocusing.
- **Session Lock Subsurfaces Rendering (e.g. Swaylock Typing Indicator)**:
  - Added full recursive subsurface composition and damage tracking (`wlr_surface_for_each_surface`) in the session lock render pass.
  - Fixes missing graphical password indicator rings/dots in `swaylock` by accurately compositing child subsurfaces with position offsets, scale, transform, and premultiplied alpha blending over the background lock surface.
- **Headless & Nested Startup Crash under Non-wlroots Compositors**:
  - Resolved SIGSEGV on startup during PGO bot training when running inside KDE Plasma, Niri, or Weston, ensuring seamless 3-stage PGO pipeline execution across all host environments.
- **Window Close Animation Missing After BLoC Migration**:
  - Restored `previousPage` animation in `surfaceUnMap` handler. The original `setState` + `previousPage(duration: 230ms)` pattern was lost during migration to BLoC. Now tracks surface handles via `ShellControllers.surfaceHandles` to detect which surface was unmapped relative to `currentPageIndex`.
- **Rapid Terminal Opening Skipping Animations**:
  - Fixed `surfaceMap` handler by wrapping `animateToPage` in `WidgetsBinding.instance.addPostFrameCallback`. The BLoC migration removed the implicit `setState` rebuild that updated `PageView.itemCount` before `animateToPage` was called, causing `maxScrollExtent` to be stale and clamping animation distance to 0.
- **Overview Detection During Animation Transitions**:
  - Uses `effectiveControllers.isOverview` (based on `overviewController.value > 0.5`) alongside `shellBloc.state.isOverview` for reliable overview state detection during in-flight animation transitions.
- **Subsurface & Popup Overview Animation Flickering**:
  - Initialized `SubSurfaceBloc` and `PopupBloc` states synchronously from `CompositorRepository.subSurfaceSetLookUp` and `popupSetLookUp` rather than defaulting to empty lists during asynchronous subscription.
  - Eliminated 1-frame empty widget collapse (`SizedBox.shrink()`) causing subsurfaces (e.g. video playback in Firefox) and popups to blink during overview entrance and exit transitions.
- **Touchpad 3-Finger Overview Swipe Threshold**:
  - Restored gesture baseline tracking (`swipeStartVal` and `isSwipingOverview` in `ShellControllers`) and asymmetric threshold evaluation in `ShellView.gestureSwipeEnd`: `startVal == 0 ? val >= 0.14 : val > 0.75`.
  - Fixes touchpad overview close gesture snapping back open when swiping down from active overview mode.
- **Chromium Subsurface Dialogue / Popup Click Handling**:
  - Fixed an issue where dialog bubbles implemented via `wl_subsurface` (such as Chromium's "Restore pages?" prompt or bubble dialogs) could not receive mouse clicks.
  - In `sparrow_view_focus`, guarded `wlr_seat_pointer_notify_enter` to avoid stealing pointer focus away when pointer focus already belongs to the view or one of its child subsurfaces.
  - In `sparrow_handle_surface_pointer_event` and `sparrow_handle_popup_pointer_event`, sequenced view focus *prior* to pointer enter, motion, and frame notification to ensure proper dispatch to subsurface child surfaces.

## [0.2.1] - 2026-09-15

### Added

- **Global Shortcuts & Window Management**: Added desktop shortcut handling (`Super+Enter` for terminal, `Super+Q` to close surface, `Ctrl+Alt+Del` to logout, `Super` for launcher), input event deduplication, and taskbar focus exclusion.
- **Add wleird tests**: Add wleird tests to the build process and automated test runner (`tools/bot/wleird_test.sh`).
- **Add compositor killer tests**: Add compositor killer tests to the build process (`tools/bot/killer_test.sh`).
- **Add clang-tidy tests**: Add clang-tidy tests to the build process.
- **Add pigeon gobject support**: Enable IPC connection between flutter engine and compositor via gobject.
- **Multi-Page FPS & Background Surface Throttling Test Suite**: Automated regression test (`tools/bot/multi_page_fps_test.sh`) verifying 0 FPS throttling of background pages (`vkcube`) when switching to an idle client page (`sparrow-app-runner` with `simple_app`).
- **XDG Icon Theme Resolution & Fast Cache**: Embedded XDG icon theme resolver in `shell/lib/icon_theme.dart` with `.desktop` metadata inspection, theme inheritance traversal, and persistent disk caching (`~/.cache/icon_theme_cache.json`).
- **Pigeon Strongly-Typed Architecture**:
  - Replaced legacy untyped `MethodChannel('wlroots')` with strongly typed, code-generated C++ and Dart message protocols using Flutter's **Pigeon** tool (`compositor_dart/pigeons/messages.dart`).
  - **Host API (`CompositorHostApi` - Client -> Server)**: Strongly typed remote procedure calls for window management, surface resizing, maximizing, closing, focusing, primary output configuration, and direct input mode.
  - **Flutter API (`CompositorFlutterApi` - Server -> Client)**: Zero-overhead, type-safe asynchronous event dispatcher routing 21 compositor events covering surfaces, subsurfaces, popups, outputs, touchscreen swipe gestures, and desktop zoom.
  - **Native C++ Adaptations**: Upgraded `libplatform` (`binary_messenger.hpp`, `binary_messenger.cpp`) to fully conform with official Flutter embedder C++ client wrapper standards (`namespace flutter`, virtual `BinaryMessenger` interface, official message header hierarchy in `platform/include/flutter/`).
  - **Automated Generation Target**: Added `./build.sh pigeon` build option to cleanly re-generate C++ (`messages.g.h`, `messages.g.cpp`) and Dart (`messages.g.dart`) bindings on demand.
- **Smooth Desktop Zoom**:
  - Fullscreen hardware-accelerated screen magnifier powered by Flutter's `RawMagnifier`.
  - Frame-rate-independent continuous exponential decay animation (`ZoomController` with Flutter `Ticker`) ensuring jitter-free transitions at 60/120/144+ Hz without velocity resets or animation restarts.
  - Dynamic optical cursor tracking: exact pixel under the cursor remains anchored and fully interactive (clicks, hover, selection).
  - Isolated overlay rendering via `ScreenZoomOverlay` and `ListenableBuilder`, eliminating whole-shell rebuilds and frame drops during zoom.
  - Interactive shortcuts: `Super` + Mouse Wheel Scroll, `Super` + `+` (zoom in), `Super` + `-` (zoom out), and `Super` + `0` (reset zoom).
  - Low-level wlroots input integration in `pointer.cpp` and `keyboard.cpp` forwarding compositor zoom messages while suppressing client event leakage.
- **Native Vulkan Wayland Renderer for App Runner (`sparrow-app-runner`)**:
  - Implemented a complete native Vulkan client backend (`src/runner/vulkan_window.hpp`, `src/runner/vulkan_window.cpp`) running Flutter with the next-generation **Impeller** engine over Wayland.
  - Native surface creation via `VK_KHR_surface` and `VK_KHR_wayland_surface` directly bound to the client `wl_surface`.
  - Automatic GPU discovery prioritizing discrete/integrated GPUs (AMD Radeon RADV) with Wayland presentation support.
  - Prioritized standard `VK_PRESENT_MODE_FIFO_KHR` Swapchain Present Mode by default (guaranteed vsync compliance on Wayland), with support for configurable modes (`--vk-present-mode=<fifo|mailbox|immediate>`).
  - Integrated Khronos Validation Layers support via `--vk-validation` flag for real-time Vulkan API validation.
  - Robust swapchain management with per-image present semaphores and fences, zero validation errors (`VUID-vkQueueSubmit-pSignalSemaphores-00067`), and graceful dynamic resizing.
  - Fully integrated into `tools/bot/runner_test.sh` with 100% clean test passes under ThreadSanitizer (TSan) with zero data races or memory leaks.
- **App Runner Sub-Process (`sparrow-app-runner`)**: Isolated Flutter client application runtime executing independently from the compositor shell.
- **Zero-Trust Compositor IPC**: Secure anonymous Unix `socketpair` channel (`sparrow-ipc-v1`) eliminating disk-based sockets and client spoofing.
- **In-Process Perfetto Tracing**: Native Protobuf timeline trace generation (`.pftrace`) compatible with [ui.perfetto.dev](https://ui.perfetto.dev) without external daemon requirements.
- **RenderDoc GPU Capture API**: Programmatic frame capture triggers via `renderdoc_app.h` integration.
- **Multi-Process Sanitizer Suite**: Automated test harness (`runner_test.sh`) verifying zero memory leaks or data races under ASan, TSan, and UBSan.
- **Compositor Killer ("I Will Kill You") Resilience Suite**:
  - Integrated Scott Anderson's `compositor-killer` malicious test suite to stress-test compositor resilience against abusive GPU Mandelbrot shaders and implicit synchronization stalls.
  - Created automated test harness (`tools/bot/killer_test.sh`) supporting extended torture runs (>= 1 minute) across difficulty levels.
  - Implemented graceful logout verification (`SIGTERM`) while under heavy GPU load, ensuring clean client teardown and preventing fence/driver deadlocks.
  - Fully validated 100% survival and sub-second clean shutdown across **Release**, **ASan**, **UBSan**, and **TSan** builds.
- **Bundle Optimization Tool**: Asset pruning and binary stripping utility (`tools/optimize_bundle.sh`).

### Changed

- **Update to flutter 3.47.4**: Update to flutter 3.47.4 (engine 06a2e2a110089dff50fe635cffd2a61e1b24fbcd).
- **Default PGO Build Pipeline**: Profile-Guided Optimization enabled by default with automatic profile staleness detection.
- **Headless PGO Bot Workload**: PGO training workload defaults to headless mode for reliable container and CI/CD execution.
- **Compilation Target Isolation**: Separated compositor and app-runner compilation units to prevent profile data contamination.
- **External Sanitizer Suppressions**: Replaced hardcoded compiler flags with dedicated suppression files (`*san_suppressions.txt`).
- **Update LICENSE**.

- **Wleird Torture Suite & Output Rotation Rendering**:
  - Fixed display rotation artifacts with `wleird` test clients (`wleird-damage-paint circle` and `vstack`): decoupled output damage visibility culling from platform view scissor clipping in `render_scene_layer_platform`, ensuring full client frame updates properly overwrite previous orientation contents.
  - Adjusted portrait maximized height by 1px to prevent stride and byte size collisions (`W * H == H * W`) on orientation swap, allowing client-side buffer pools to properly detect geometry transitions and reallocate matching buffer strides.
  - Invalidated surface buffer cache and added full view damage across output rotation and geometry updates.
- **Chromium / Aura ARGB Tooltip Rendering & Buffer Release**:
  - Resolved deadlock on single-buffered SHM popup tooltips where compositor buffer locking prevented `wl_buffer.release` events from reaching the client, blocking Chromium from painting.
  - Switched popup external texture retrieval to wlroots-managed GL textures (`sparrow_surface_get_texture`) with `GL_TEXTURE_SWIZZLE_A` opacity handling, properly rendering dark tooltip cards, text, and borders while honoring client `opaque_region` configurations.
- **Output rotation & Cursor transformation**: Fixed output rotation, cursor coordinate projection on rotated displays, and surface refocusing upon display output change.
- **Swapchain and Damage History**: Corrected multi-buffering damage history indexing and swapchain re-creation errors under load.
- **App Runner Wayland Clipboard & Text Editing**:
  - Implemented the Wayland Data Device protocol (`wl_data_device_manager`, `wl_data_device`, `wl_data_source`, `wl_data_offer`) in `sparrow-app-runner` for system-wide copy and paste interoperability.
  - Wired Flutter platform channel clipboard handlers (`Clipboard.setData`, `Clipboard.getData`, `Clipboard.hasStrings`) with asynchronous non-blocking pipe reads (`poll()` timeout) and local cache optimization.
  - Added continuous Wayland input serial propagation across pointer, keyboard, and touch events to validate clipboard ownership.
  - Implemented desktop text editing shortcuts in the runner (`Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+A`, and `Shift` + arrow/home/end text selection).

---

## [0.2.0] - Foundation Release

### Added

- **Hybrid Wayland Compositor Core**: C++26 wlroots display server coupled with Flutter Engine desktop embedder using modern `libplatform`.
- **Zero-Copy DMA-BUF Import**: Direct hardware client surface texturing via `linux-dmabuf-unstable-v1` with zero CPU overhead.
- **Accelerated `wl_shm` via `udmabuf`**: Automatic conversion of software shared memory buffers to DMA-BUFs via Linux `/dev/udmabuf`.
- **Impeller Graphics Backend**: Full support for Flutter Impeller and OpenGL ES graphics rendering pipelines.
- **Dynamic Triple Buffering**: Runtime switching between low-latency double buffering and tear-free triple buffering under load.
- **Damage History Ring**: 4-frame damage ring buffer tracking and re-projecting dirty rectangles to minimize redrawn regions.
- **Direct Scanout**: Fullscreen compositor bypass for gaming and high-performance video playback.
- **Soft-Realtime Scheduling**: Round-robin priority (`SCHED_RR`) for compositor UI loop with automatic child process priority reset.
- **Flutter Isolates Dispatcher**: Thread-safe Wayland event loop dispatcher (`eventfd`) for background Dart isolates.
- **Modern Input & Gesture Stack**: Multi-touch 3/4-finger gestures, high-resolution mouse wheels, and virtual input protocols (`wlr_virtual_keyboard_v1`, `wlr_virtual_pointer_v1`).
- **Extended Wayland Protocols**: Window management (`xdg-shell`, `wlr-foreign-toplevel`, `xdg-activation`), pointer constraints, and clipboard control (`ext-data-control-v1`).
- **Diagnostic & Profiling Tools**: Real-time FPS on-screen display (`F11`), surface tree inspector (`F10`), live damage rainbow visualizer (`F12`), and monitor power management (`DPMS` via `F8`).
- **Automated Bot & Testing Harness**: Automated UI testing bot (`sparrow_bot.sh`) and headless execution mode for containerized CI/CD.

### Fixed

- **Compositor Lifecycle & Shutdown**: Resolved listener dangling pointers on shutdown, subsurface lifetime issues, and popup clipping.
- **Memory & Texture Management**: Fixed texture allocation leaks in renderer and stabilized backing store caching lifecycle.
- **Input Responsiveness**: Eliminated motion lag during trackpad panning and fixed pointer axis crash conditions.
- **Window Decorations**: Corrected popup alignments, GTK client-side decoration glitches, and browser search bar rendering.
