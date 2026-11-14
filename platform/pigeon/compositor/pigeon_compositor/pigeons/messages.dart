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

class SurfacePointerEventMessage {
  const SurfacePointerEventMessage({
    required this.surfaceHandle,
    required this.device,
    required this.buttons,
    required this.embedderId,
    required this.deviceKind,
    required this.kind,
    required this.viewId,
    required this.localPosX,
    required this.localPosY,
    required this.globalPosX,
    required this.globalPosY,
    required this.platformId,
    required this.pointer,
    required this.widgetSizeX,
    required this.widgetSizeY,
    required this.scrollDeltaX,
    required this.scrollDeltaY,
    required this.panX,
    required this.panY,
    this.scale = 1.0,
    this.rotation = 0.0,
    required this.eventType,
    required this.timestamp,
  });

  final int surfaceHandle;
  final int device;
  final int buttons;
  final int embedderId;
  final int deviceKind;
  final int kind;
  final int viewId;
  final double localPosX;
  final double localPosY;
  final double globalPosX;
  final double globalPosY;
  final int platformId;
  final int pointer;
  final double widgetSizeX;
  final double widgetSizeY;
  final double scrollDeltaX;
  final double scrollDeltaY;
  final double panX;
  final double panY;
  final double scale;
  final double rotation;
  final int eventType;
  final int timestamp;
}

class SurfaceKeyboardKeyMessage {
  const SurfaceKeyboardKeyMessage({
    required this.surfaceHandle,
    required this.keycode,
    required this.status,
    required this.timestampMicros,
  });

  final int surfaceHandle;
  final int keycode;
  final int status;
  final int timestampMicros;
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
  void surfaceToplevelSetFullscreen(
    int surfaceHandle, {
    bool fullscreen = true,
  });

  @async
  bool surfaceToplevelClose(int surfaceHandle);

  @async
  void surfaceFocus(int surfaceHandle);

  @async
  void surfaceClearFocus();

  @async
  void surfaceSetPosition(int surfaceHandle, int x, int y);

  @async
  void surfaceSetSceneBounds(
    int surfaceHandle,
    int x,
    int y,
    int width,
    int height,
    double cornerRadius, {
    bool visible = true,
  });

  @async
  void setSensitiveAppIds(List<String> appIds);

  @async
  void setSensitiveOutputNames(List<String> outputNames);

  @async
  void addOrUpdateService(String id, String command, bool restartable);

  @async
  void startAllServices();

  // Render & Input modes
  @async
  void forceRenderAllViews({required bool force});

  @async
  void setImmersive(bool sticky);

  @async
  void setScreenZoomActive({bool active = false});

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
  void surfaceKeyboardKey(SurfaceKeyboardKeyMessage message);

  @async
  void surfacePointerEvent(SurfacePointerEventMessage message);

  @async
  void popupPointerEvent(SurfacePointerEventMessage message);
  @async
  void layerSurfacePointerEvent(SurfacePointerEventMessage message);
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

  /// Pigeon requires positional arguments for FlutterApi methods.
  // ignore: avoid_positional_boolean_parameters
  void surfaceFullscreen(int handle, bool fullscreen);

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
  void layerSurfaceMap(Map<String, Object?> layerSurface);
  void layerSurfaceUnmap(int handle);
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
  void dockReveal();
}
// #enddocregion flutter-definitions

// #docregion constants
// #enddocregion constants
