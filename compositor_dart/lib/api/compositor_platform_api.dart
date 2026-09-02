import 'dart:collection' show HashMap;
import 'dart:io' show stderr, stdout;

import 'package:compositor_dart/core/constants.dart' show KeyStatus;
import 'package:compositor_dart/data/models/compositor_sockets.dart'
    show CompositorSockets;
import 'package:compositor_dart/data/models/display_mode.dart' show DisplayMode;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:flutter/foundation.dart' show FlutterError;
import 'package:flutter/services.dart'
    show MethodCall, MethodChannel, MissingPluginException, PlatformException;
import 'package:logging/logging.dart' show Logger;
import 'package:material_ui/material_ui.dart' show debugPrint;

class CompositorPlatformApi {
  CompositorPlatformApi() {
    channel.setMethodCallHandler((call) async {
      final handler = handlers[call.method];
      if (handler == null) {
        debugPrint('unhandled call: ${call.method}');
      } else {
        debugPrint('handled call ${call.method}');
        return handler(call);
      }
    });
  }
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

  final MethodChannel channel = const MethodChannel('wlroots');

  final HashMap<String, Future<dynamic> Function(MethodCall)> handlers =
      HashMap();

  void addHandler(String method, Future<dynamic> Function(MethodCall) handler) {
    if (handlers.containsKey(method)) {
      throw Exception('attemped to add duplicate handler for $method');
    }
    handlers[method] = handler;
  }

  Future<void> surfaceToplevelSetSize(
    Surface surface,
    int width,
    int height,
  ) async {
    final r = await channel.invokeListMethod('surface_toplevel_set_size', [
      surface.handle,
      width,
      height,
    ]);
    // print(r);
  }

  Future<void> surfaceToplevelSetMaximized(
    Surface surface, {
    bool maximized = true,
  }) async {
    final r = await channel.invokeListMethod('surface_toplevel_set_maximized', [
      surface.handle,
      if (maximized) 1 else 0,
    ]);
    // print(r);
  }

  Future<bool> surfaceToplevelClose(Surface surface) async {
    try {
      await channel.invokeListMethod('surface_toplevel_close', [
        surface.handle,
      ]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to close toplevel: $e');
      return false;
    }
  }

  Future<void> surfaceFocus(Surface surface) async {
    await channel.invokeListMethod('surface_focus', [surface.handle]);
  }

  Future<void> clearFocus(Surface surface) async {
    await channel.invokeMethod('surface_clear_focus', [surface.handle]);
  }

  Future<void> forceRenderAllViews(bool force) async {
    await channel.invokeListMethod('force_render_all_views', [
      if (force) 1 else 0,
    ]);
  }

  Future<void> setPrimaryOutput(int outputId) async {
    await channel.invokeMethod('set_primary_output', [outputId]);
  }

  // NOTE: surfaceBeginMove and surfaceBeginResize have been removed.
  // Move/resize is now fully Dart-controlled via WindowManager in avio_wm.
  // Position/size updates are sent via surfaceSetPosition
  // and surfaceToplevelSetSize.

  Future<void> surfaceSetPosition(Surface surface, int x, int y) async {
    await channel.invokeListMethod('surface_set_position', [
      surface.handle,
      x,
      y,
    ]);
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
    await channel.invokeListMethod('surface_request_resize', [
      handle,
      width,
      height,
      requestId,
    ]);
  }

  /// Signal end of interactive resize operation.
  Future<void> surfaceEndResize(int handle) async {
    await channel.invokeListMethod('surface_end_resize', [handle]);
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
    await channel.invokeListMethod('set_direct_input_mode', [
      enabled,
      surface?.handle ?? 0,
    ]);
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
    await channel.invokeListMethod('surface_keyboard_key', [
      surface.handle,
      keycode,
      status.index,
      timestamp.inMicroseconds,
    ]);
  }

  Future<CompositorSockets> getSocketPaths() async {
    final response =
        await channel.invokeMethod('get_socket_paths') as Map<dynamic, dynamic>;
    return CompositorSockets(
      wayland: response['wayland'] as String,
      x: response['x'] as String,
    );
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
      await channel.invokeMethod('is_compositor');
      _isCompositor = true;
    } on MissingPluginException {
      _isCompositor = false;
    }

    return _isCompositor!;
  }

  /// Set which output drives Flutter's vsync (0 = auto/highest refresh).
  /// Returns true on success, false on failure.
  Future<bool> setVsyncOutput(int outputId) async {
    try {
      await channel.invokeMethod('set_vsync_output', [outputId]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set vsync output: $e');
      return false;
    }
  }

  /// Set vsync rate limit for power saving (0 = unlimited, >0 = max Hz).
  /// Returns true on success, false on failure.
  Future<bool> setVsyncRateLimit(int maxHz) async {
    try {
      await channel.invokeMethod('set_vsync_rate_limit', [maxHz]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set vsync rate limit: $e');
      return false;
    }
  }

  /// Set output mode (resolution and refresh rate).
  /// Returns true on success, false on failure.
  Future<bool> setOutputMode(int outputId, DisplayMode mode) async {
    try {
      await channel.invokeMethod('set_output_mode', [
        outputId,
        mode.width,
        mode.height,
        mode.refresh,
      ]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set output mode: $e');
      return false;
    }
  }

  /// Set output position in the layout.
  /// Returns true on success, false on failure.
  Future<bool> setOutputPosition(int outputId, int x, int y) async {
    try {
      await channel.invokeMethod('set_output_position', [outputId, x, y]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set output position: $e');
      return false;
    }
  }

  /// Set output scale factor.
  /// Returns true on success, false on failure.
  Future<bool> setOutputScale(int outputId, double scale) async {
    try {
      await channel.invokeMethod('set_output_scale', [outputId, scale]);
      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set output scale: $e');
      return false;
    }
  }

  /// Toggle or set damage region visualization overlay
  Future<void> debugSetDamageVisualization(bool enabled) async {
    try {
      await channel.invokeMethod('debug_set_damage_visualization', enabled);
    } on PlatformException catch (e) {
      debugPrint('Failed to set damage visualization: $e');
    }
  }

  /// Get whether damage region visualization overlay is enabled
  Future<bool> debugGetDamageVisualization() async {
    try {
      final res =
          await channel.invokeMethod<bool>('debug_get_damage_visualization');
      return res ?? false;
    } on PlatformException catch (e) {
      debugPrint('Failed to get damage visualization: $e');
      return false;
    }
  }

  void close() {}
}
