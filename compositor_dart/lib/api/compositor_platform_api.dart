import 'dart:io' show stderr, stdout;

import 'package:compositor_dart/core/constants.dart' show KeyStatus;
import 'package:compositor_dart/data/models/compositor_sockets.dart'
    show CompositorSockets;
import 'package:compositor_dart/data/models/display_mode.dart' show DisplayMode;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:flutter/foundation.dart' show FlutterError;
import 'package:flutter/services.dart' show PlatformException;
import 'package:logging/logging.dart' show Logger;
import 'package:material_ui/material_ui.dart' show debugPrint;
import 'package:pigeon_compositor/pigeon_compositor.dart'
    show CompositorHostApi;

class CompositorPlatformApi {
  new({CompositorHostApi? hostApi}) : _hostApi = hostApi ?? CompositorHostApi();

  bool interactive = true;
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
  CompositorHostApi get hostApi => _hostApi;

  Future<void> surfaceToplevelSetSize(
    Surface surface,
    int width,
    int height,
  ) async {
    await _hostApi.surfaceToplevelSetSize(surface.handle, width, height);
  }

  Future<void> surfaceToplevelSetMaximized(
    Surface surface, {
    bool maximized = true,
  }) async {
    await _hostApi.surfaceToplevelSetMaximized(
      surface.handle,
      maximized: maximized,
    );
  }

  Future<bool> surfaceToplevelClose(Surface surface) async {
    try {
      return await _hostApi.surfaceToplevelClose(surface.handle);
    } on PlatformException catch (e) {
      debugPrint('Failed to close toplevel: $e');
      return false;
    }
  }

  Future<void> surfaceFocus(Surface surface) async {
    await _hostApi.surfaceFocus(surface.handle);
  }

  Future<void> clearFocus(Surface surface) async {
    await _hostApi.surfaceClearFocus();
  }

  Future<void> forceRenderAllViews({required bool force}) async {
    await _hostApi.forceRenderAllViews(force: force);
  }

  Future<void> setPrimaryOutput(int outputId) async {
    await _hostApi.setPrimaryOutput(outputId);
  }

  // NOTE: surfaceBeginMove and surfaceBeginResize have been removed.
  // Move/resize is now fully Dart-controlled via WindowManager in avio_wm.
  // Position/size updates are sent via surfaceSetPosition
  // and surfaceToplevelSetSize.

  Future<void> surfaceSetPosition(Surface surface, int x, int y) async {
    await _hostApi.surfaceSetPosition(surface.handle, x, y);
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

  Future<void> surfacePointerEvent(List<dynamic> data) async {
    if (!interactive) {
      return;
    }
    await _hostApi.surfacePointerEvent(data);
  }

  Future<void> popupPointerEvent(List<dynamic> data) async {
    if (!interactive) {
      return;
    }
    await _hostApi.popupPointerEvent(data);
  }

  Future<CompositorSockets> getSocketPaths() async {
    final response = await _hostApi.getSocketPaths();
    return CompositorSockets(wayland: response.wayland, x: response.x);
  }

  /// Returns `true` if we are currently running in the compositor embedder.
  /// If so, all functionality in this library is available.
  ///
  /// Returns `false` in all other cases. If so, no funcitonality in this
  /// library should be used.
  bool? _isCompositor;
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
    try {
      return await _hostApi.setVsyncOutput(outputId);
    } on PlatformException catch (e) {
      debugPrint('Failed to set vsync output: $e');
      return false;
    }
  }

  /// Set vsync rate limit for power saving (0 = unlimited, >0 = max Hz).
  /// Returns true on success, false on failure.
  Future<bool> setVsyncRateLimit(int maxHz) async {
    try {
      return await _hostApi.setVsyncRateLimit(maxHz);
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

  /// Toggle or set damage region visualization overlay
  Future<void> debugSetDamageVisualization({bool enabled = false}) async {
    try {
      await _hostApi.debugSetDamageVisualization(enabled: enabled);
    } on PlatformException catch (e) {
      debugPrint('Failed to set damage visualization: $e');
    }
  }

  /// Get whether damage region visualization overlay is enabled
  Future<bool> debugGetDamageVisualization() async {
    try {
      return await _hostApi.debugGetDamageVisualization();
    } on PlatformException catch (e) {
      debugPrint('Failed to get damage visualization: $e');
      return false;
    }
  }

  /// Signal to C that Dart is ready to receive messages
  Future<void> compositorReady() async {
    try {
      await _hostApi.compositorReady();
    } on PlatformException catch (e) {
      debugPrint('Failed to signal compositor ready: $e');
    }
  }

  void close() {}
}
