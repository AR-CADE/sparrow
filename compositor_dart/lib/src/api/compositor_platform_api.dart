import 'dart:io' show stderr, stdout;

import 'package:compositor_dart/src/core/constants.dart' show KeyStatus;
import 'package:compositor_dart/src/data/models/compositor_sockets.dart'
    show CompositorSockets;
import 'package:compositor_dart/src/data/models/display_mode.dart'
    show DisplayMode;
import 'package:compositor_dart/src/data/models/surface.dart' show Surface;
import 'package:flutter/foundation.dart' show FlutterError;
import 'package:flutter/services.dart' show PlatformException;
import 'package:logging/logging.dart' show Logger;
import 'package:material_ui/material_ui.dart' show debugPrint;
import 'package:pigeon_compositor/pigeon_compositor.dart'
    show CompositorHostApi;

/// Platform interface for communicating with the native Wayland compositor.
class CompositorPlatformApi {
  /// Creates a [CompositorPlatformApi] with an optional [hostApi].
  new({CompositorHostApi? hostApi}) : _hostApi = hostApi ?? CompositorHostApi();

  /// Whether user interaction (keyboard/pointer inputs) is forwarded.
  bool interactive = true;

  /// Initializes console loggers and error handlers for compositor debugging.
  static void initLogger() {
    FlutterError.onError = (details) {
      FlutterError.presentError(details);
      stderr.writeln(details.toString());
    };
    Logger.root.onRecord.listen((record) {
      stdout.writeln('${record.level.name}: ${record.time}: ${record.message}');
    });
  }

  final CompositorHostApi _hostApi;

  // Cached states for distinct deduplication
  bool? _lastImmersiveSticky;
  bool? _lastScreenZoomActive;
  bool? _lastForceRenderAllViews;
  bool? _lastDebugDamageVisualization;
  int? _lastVsyncRateLimit;
  int? _lastPrimaryOutput;
  int? _lastVsyncOutput;

  final Map<int, bool> _surfaceMaximized = <int, bool>{};
  final Map<int, bool> _surfaceFullscreen = <int, bool>{};
  final Map<int, ({int x, int y})> _surfacePositions =
      <int, ({int x, int y})>{};
  final Map<int, ({int width, int height})> _surfaceSizes =
      <int, ({int width, int height})>{};
  final Map<
    int,
    ({int x, int y, int width, int height, double cornerRadius, bool visible})
  >
  _surfaceSceneBounds =
      <
        int,
        ({
          int x,
          int y,
          int width,
          int height,
          double cornerRadius,
          bool visible,
        })
      >{};

  /// Clears all cached surface states for [surfaceHandle].
  void clearSurfaceState(int surfaceHandle) {
    _surfaceMaximized.remove(surfaceHandle);
    _surfaceFullscreen.remove(surfaceHandle);
    _surfacePositions.remove(surfaceHandle);
    _surfaceSizes.remove(surfaceHandle);
    _surfaceSceneBounds.remove(surfaceHandle);
  }

  /// Synchronizes cached fullscreen state for [surfaceHandle].
  void syncSurfaceFullscreen(int surfaceHandle, {bool fullscreen = false}) {
    _surfaceFullscreen[surfaceHandle] = fullscreen;
    if (fullscreen) {
      _surfaceMaximized[surfaceHandle] = false;
    }
  }

  /// Synchronizes cached maximized state for [surfaceHandle].
  void syncSurfaceMaximized(int surfaceHandle, {bool maximized = false}) {
    _surfaceMaximized[surfaceHandle] = maximized;
    if (maximized) {
      _surfaceFullscreen[surfaceHandle] = false;
    }
  }

  /// The underlying Pigeon host API client.
  CompositorHostApi get hostApi => _hostApi;

  /// Sets the size of a top-level [surface] in logical pixels.
  Future<void> surfaceToplevelSetSize(
    Surface surface,
    int width,
    int height,
  ) async {
    final last = _surfaceSizes[surface.handle];
    if (last != null && last.width == width && last.height == height) {
      return;
    }
    _surfaceSizes[surface.handle] = (width: width, height: height);
    await _hostApi.surfaceToplevelSetSize(surface.handle, width, height);
  }

  /// Configures whether a top-level [surface] is maximized.
  Future<void> surfaceToplevelSetMaximized(
    Surface surface, {
    bool maximized = true,
  }) async {
    if (_surfaceMaximized[surface.handle] == maximized) {
      return;
    }
    _surfaceMaximized[surface.handle] = maximized;
    if (maximized) {
      _surfaceFullscreen[surface.handle] = false;
    }
    await _hostApi.surfaceToplevelSetMaximized(
      surface.handle,
      maximized: maximized,
    );
  }

  /// Configures whether a top-level [surface] is fullscreen.
  Future<void> surfaceToplevelSetFullscreen(
    Surface surface, {
    bool fullscreen = true,
  }) async {
    if (_surfaceFullscreen[surface.handle] == fullscreen) {
      return;
    }
    _surfaceFullscreen[surface.handle] = fullscreen;
    if (fullscreen) {
      _surfaceMaximized[surface.handle] = false;
    }
    await _hostApi.surfaceToplevelSetFullscreen(
      surface.handle,
      fullscreen: fullscreen,
    );
  }

  /// Requests closing a top-level [surface].
  ///
  /// Returns `true` if the close request succeeded, or `false` otherwise.
  Future<bool> surfaceToplevelClose(Surface surface) async {
    try {
      return await _hostApi.surfaceToplevelClose(surface.handle);
    } on PlatformException catch (e) {
      debugPrint('Failed to close toplevel: $e');
      return false;
    }
  }

  /// Sets keyboard focus and activation to the given [surface].
  Future<void> surfaceFocus(Surface surface) async {
    await _hostApi.surfaceFocus(surface.handle);
  }

  /// Clears keyboard focus from all surfaces.
  Future<void> clearFocus(Surface surface) async {
    await _hostApi.surfaceClearFocus();
  }

  /// Requests all platform views to re-render immediately.
  Future<void> forceRenderAllViews({required bool force}) async {
    if (_lastForceRenderAllViews == force) {
      return;
    }
    _lastForceRenderAllViews = force;
    await _hostApi.forceRenderAllViews(force: force);
  }

  /// Sets whether the compositor operates in immersive mode.
  ///
  /// When [sticky] is true (default), the dock is hidden and fullscreen
  /// surfaces can utilize direct scanout (`immersiveSticky`).
  /// When [sticky] is false, the dock is shown/revealed (`immersive`),
  /// temporarily yielding direct scanout so Flutter can composite overlays.
  Future<void> setImmersive({bool sticky = true}) async {
    if (_lastImmersiveSticky == sticky) {
      return;
    }
    _lastImmersiveSticky = sticky;
    await _hostApi.setImmersive(sticky);
  }

  /// Sets whether screen zoom is active, yielding direct scanout so Flutter
  /// can composite the magnified view.
  Future<void> setScreenZoomActive({bool active = false}) async {
    if (_lastScreenZoomActive == active) {
      return;
    }
    _lastScreenZoomActive = active;
    await _hostApi.setScreenZoomActive(active: active);
  }

  /// Sets the primary display output to [outputId].
  Future<void> setPrimaryOutput(int outputId) async {
    if (_lastPrimaryOutput == outputId) {
      return;
    }
    _lastPrimaryOutput = outputId;
    await _hostApi.setPrimaryOutput(outputId);
  }

  // NOTE: surfaceBeginMove and surfaceBeginResize have been removed.
  // Move/resize is now fully Dart-controlled via WindowManager in avio_wm.
  // Position/size updates are sent via surfaceSetPosition
  // and surfaceToplevelSetSize.

  /// Sets the layout position of [surface] to ([x], [y]).
  Future<void> surfaceSetPosition(Surface surface, int x, int y) async {
    final last = _surfacePositions[surface.handle];
    if (last != null && last.x == x && last.y == y) {
      return;
    }
    _surfacePositions[surface.handle] = (x: x, y: y);
    await _hostApi.surfaceSetPosition(surface.handle, x, y);
  }

  /// Sets the global screen-space bounds, [cornerRadius], and [visible] state
  /// of [surface] for accurate compositor capture censoring and scene queries.
  Future<void> surfaceSetSceneBounds(
    Surface surface,
    int x,
    int y,
    int width,
    int height,
    double cornerRadius, {
    bool visible = true,
  }) async {
    final last = _surfaceSceneBounds[surface.handle];
    if (last != null &&
        last.x == x &&
        last.y == y &&
        last.width == width &&
        last.height == height &&
        (last.cornerRadius - cornerRadius).abs() < 0.1 &&
        last.visible == visible) {
      return;
    }
    _surfaceSceneBounds[surface.handle] = (
      x: x,
      y: y,
      width: width,
      height: height,
      cornerRadius: cornerRadius,
      visible: visible,
    );
    await _hostApi.surfaceSetSceneBounds(
      surface.handle,
      x,
      y,
      width,
      height,
      cornerRadius,
      visible: visible,
    );
  }

  /// Updates the list of forced sensitive application IDs / title patterns
  /// configured in `sparrow.ini` (`[Secure-Content]`).
  Future<void> setSensitiveAppIds(List<String> appIds) async {
    await _hostApi.setSensitiveAppIds(appIds);
  }

  /// Updates the list of forced sensitive output names
  /// configured in `sparrow.ini` (`[Secure-Content]`).
  Future<void> setSensitiveOutputNames(List<String> outputNames) async {
    await _hostApi.setSensitiveOutputNames(outputNames);
  }

  /// Adds or updates a managed service in the process supervisor.
  Future<void> addOrUpdateService(
    String id,
    String command, {
    bool restartable = false,
  }) async {
    await _hostApi.addOrUpdateService(id, command, restartable);
  }

  /// Starts all configured services in the process supervisor.
  Future<void> startAllServices() async {
    await _hostApi.startAllServices();
  }

  /// Request a synchronized resize -
  /// waits for client to commit matching buffer.
  /// Returns immediately, resize_ready event is sent when client complies.
  Future<void> surfaceRequestResize(
    int handle,
    int width,
    int height,
    int requestId,
  ) async {
    await _hostApi.surfaceRequestResize(handle, width, height, requestId);
  }

  /// Signal end of interactive resize operation.
  Future<void> surfaceEndResize(int handle) async {
    await _hostApi.surfaceEndResize(handle);
  }

  /// Enable direct input mode for low-latency gaming.
  /// When enabled, input events bypass Flutter and go directly to the surface.
  /// Use for fullscreen games or other latency-sensitive applications.
  Future<void> setDirectInputMode(
    Surface? surface, {
    required bool enabled,
  }) async {
    if (!interactive) {
      return;
    }
    await _hostApi.setDirectInputMode(surface?.handle ?? 0, enabled: enabled);
  }

  /// Sends a keyboard key event to the focused [surface].
  Future<void> surfaceSendKey(
    Surface surface,
    int keycode,
    KeyStatus status,
    Duration timestamp,
  ) async {
    if (!interactive) {
      return;
    }
    await _hostApi.surfaceKeyboardKey(
      surface.handle,
      keycode,
      status.index,
      timestamp.inMicroseconds,
    );
  }

  /// Dispatches raw pointer event data to the target surface.
  Future<void> surfacePointerEvent(List<dynamic> data) async {
    if (!interactive) {
      return;
    }
    await _hostApi.surfacePointerEvent(data);
  }

  /// Dispatches raw pointer event data to a popup surface.
  Future<void> popupPointerEvent(List<dynamic> data) async {
    if (!interactive) {
      return;
    }
    await _hostApi.popupPointerEvent(data);
  }

  /// Retrieves the Wayland and X11 display socket paths.
  Future<CompositorSockets> getSocketPaths() async {
    final response = await _hostApi.getSocketPaths();
    return CompositorSockets(wayland: response.wayland, x: response.x);
  }

  bool? _isCompositor;

  /// Returns whether the application is running inside the compositor.
  ///
  /// If `true`, all functionality in this library is available. Returns
  /// `false` in all other cases.
  Future<bool> isCompositor() async {
    if (_isCompositor != null) return _isCompositor!;

    try {
      _isCompositor = await _hostApi.isCompositor();
    } on PlatformException {
      _isCompositor = false;
    }

    return _isCompositor!;
  }

  /// Set which output drives Flutter's vsync (0 = auto/highest refresh).
  /// Returns true on success, false on failure.
  Future<bool> setVsyncOutput(int outputId) async {
    if (_lastVsyncOutput == outputId) {
      return true;
    }
    try {
      final success = await _hostApi.setVsyncOutput(outputId);
      if (success) {
        _lastVsyncOutput = outputId;
      }
      return success;
    } on PlatformException catch (e) {
      debugPrint('Failed to set vsync output: $e');
      return false;
    }
  }

  /// Set vsync rate limit for power saving (0 = unlimited, >0 = max Hz).
  /// Returns true on success, false on failure.
  Future<bool> setVsyncRateLimit(int maxHz) async {
    if (_lastVsyncRateLimit == maxHz) {
      return true;
    }
    try {
      final success = await _hostApi.setVsyncRateLimit(maxHz);
      if (success) {
        _lastVsyncRateLimit = maxHz;
      }
      return success;
    } on PlatformException catch (e) {
      debugPrint('Failed to set vsync rate limit: $e');
      return false;
    }
  }

  /// Set output mode (resolution and refresh rate).
  /// Returns true on success, false on failure.
  Future<bool> setOutputMode(int outputId, DisplayMode mode) async {
    try {
      return await _hostApi.setOutputMode(
        outputId,
        mode.width,
        mode.height,
        mode.refresh,
      );
    } on PlatformException catch (e) {
      debugPrint('Failed to set output mode: $e');
      return false;
    }
  }

  /// Set output position in the layout.
  /// Returns true on success, false on failure.
  Future<bool> setOutputPosition(int outputId, int x, int y) async {
    try {
      return await _hostApi.setOutputPosition(outputId, x, y);
    } on PlatformException catch (e) {
      debugPrint('Failed to set output position: $e');
      return false;
    }
  }

  /// Set output scale factor.
  /// Returns true on success, false on failure.
  Future<bool> setOutputScale(int outputId, double scale) async {
    try {
      return await _hostApi.setOutputScale(outputId, scale);
    } on PlatformException catch (e) {
      debugPrint('Failed to set output scale: $e');
      return false;
    }
  }

  /// Configures whether the damage region visualization overlay is enabled.
  Future<void> debugSetDamageVisualization({bool enabled = false}) async {
    if (_lastDebugDamageVisualization == enabled) {
      return;
    }
    _lastDebugDamageVisualization = enabled;
    try {
      await _hostApi.debugSetDamageVisualization(enabled: enabled);
    } on PlatformException catch (e) {
      debugPrint('Failed to set damage visualization: $e');
    }
  }

  /// Returns whether the damage region visualization overlay is enabled.
  Future<bool> debugGetDamageVisualization() async {
    try {
      return await _hostApi.debugGetDamageVisualization();
    } on PlatformException catch (e) {
      debugPrint('Failed to get damage visualization: $e');
      return false;
    }
  }

  /// Signals to the native compositor that Dart is ready to receive messages.
  Future<void> compositorReady() async {
    try {
      await _hostApi.compositorReady();
    } on PlatformException catch (e) {
      debugPrint('Failed to signal compositor ready: $e');
    }
  }

  /// Closes and releases resources used by this API instance.
  void close() {
    _surfaceMaximized.clear();
    _surfaceFullscreen.clear();
    _surfacePositions.clear();
    _surfaceSizes.clear();
    _lastImmersiveSticky = null;
    _lastForceRenderAllViews = null;
    _lastDebugDamageVisualization = null;
    _lastVsyncRateLimit = null;
    _lastPrimaryOutput = null;
    _lastVsyncOutput = null;
  }
}
