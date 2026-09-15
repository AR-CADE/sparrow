// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:pigeon/pigeon.dart';

// #docregion config
@ConfigurePigeon(
  PigeonOptions(
    dartOut: 'lib/src/messages.g.dart',
    dartOptions: DartOptions(),
    cppOptions: CppOptions(namespace: 'sparrow'),
    cppHeaderOut: '../cpp/messages.g.h',
    cppSourceOut: '../cpp/messages.g.cpp',
    gobjectHeaderOut: '../gobject/messages.g.h',
    gobjectSourceOut: '../gobject/messages.g.cc',
    gobjectOptions: GObjectOptions(),
    copyrightHeader: 'pigeons/copyright.txt',
    dartPackageName: 'pigeon_compositor',
  ),
)
// #enddocregion config
class CompositorSocketsData {
  const CompositorSocketsData({required this.wayland, required this.x});
  final String wayland;
  final String x;
}

// #docregion host-definitions
@HostApi()
abstract class CompositorHostApi {
  // Surface / Window controls
  @async
  void surfaceRequestResize(
    int surfaceHandle,
    int width,
    int height,
    int requestId,
  );

  @async
  void surfaceEndResize(int surfaceHandle);

  @async
  void surfaceToplevelSetSize(int surfaceHandle, int width, int height);

  @async
  void surfaceToplevelSetMaximized(int surfaceHandle, {bool maximized = true});

  @async
  bool surfaceToplevelClose(int surfaceHandle);

  @async
  void surfaceFocus(int surfaceHandle);

  @async
  void surfaceClearFocus();

  @async
  void surfaceSetPosition(int surfaceHandle, int x, int y);

  // Render & Input modes
  @async
  void forceRenderAllViews({required bool force});

  @async
  void setDirectInputMode(int surfaceHandle, {bool enabled = false});

  @async
  void setPrimaryOutput(int outputId);

  // Vsync & Outputs
  @async
  bool setVsyncOutput(int outputId);

  @async
  bool setVsyncRateLimit(int maxHz);

  @async
  bool setOutputMode(int outputId, int width, int height, int refresh);

  @async
  bool setOutputPosition(int outputId, int x, int y);

  @async
  bool setOutputScale(int outputId, double scale);

  // Debug & System
  @async
  void debugSetDamageVisualization({bool enabled = false});

  @async
  bool debugGetDamageVisualization();

  @async
  CompositorSocketsData getSocketPaths();

  @async
  void compositorReady();

  @async
  bool isCompositor();

  @async
  void surfaceKeyboardKey(
    int surfaceHandle,
    int keycode,
    int status,
    int timestampMicros,
  );

  @async
  void surfacePointerEvent(List<Object?> data);

  @async
  void popupPointerEvent(List<Object?> data);
}
// #enddocregion host-definitions

// #docregion flutter-definitions
@FlutterApi()
abstract class CompositorFlutterApi {
  void surfaceMap(Map<String, Object?> surface);
  void surfaceUnmap(int handle);
  void surfaceTitle(int handle, String title, String appId);
  void surfaceGeometry(
    int handle,
    int width,
    int height,
    int bufferWidth,
    int bufferHeight,
    int geoX,
    int geoY,
  );

  /// Pigeon requires positional arguments for FlutterApi methods.
  // ignore: avoid_positional_boolean_parameters
  void surfaceDecoration(int handle, bool usesSsd, bool usesCsd);
  void surfaceMinimize(int handle);
  void surfaceRequestActivate(int handle, String token, String appId);
  void resizeReady(int handle, int requestId);
  void subsurfaceMap(Map<String, Object?> subsurface);
  void subsurfaceUnmap(int handle, int parentHandle);
  void subsurfacePosition(
    int handle,
    int parentHandle,
    int x,
    int y,
    int width,
    int height,
    int bufferWidth,
    int bufferHeight,
  );
  void popupMap(Map<String, Object?> popup);
  void popupUnmap(int handle);
  void outputAdded(Map<String, Object?> output);
  void outputRemoved(int outputId);
  void outputChanged(Map<String, Object?> output);
  void gestureSwipeBegin(int fingers, int timeMsec);
  void gestureSwipeUpdate(double dx, double dy, int timeMsec);

  /// Pigeon requires positional arguments for FlutterApi methods.
  // ignore: avoid_positional_boolean_parameters
  void gestureSwipeEnd(bool cancelled, int timeMsec);
  void zoomScroll(double delta, double x, double y);
  void zoomKey(int action, double x, double y);
}
// #enddocregion flutter-definitions

// #docregion constants
// #enddocregion constants
