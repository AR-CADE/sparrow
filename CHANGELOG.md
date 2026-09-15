# Changelog

All notable changes to the Sparrow compositor project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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

### Fixed
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

## [Unreleased] - Post-0.2
