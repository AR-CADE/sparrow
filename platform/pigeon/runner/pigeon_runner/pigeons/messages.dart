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
    dartPackageName: 'pigeon_runner',
  ),
)
// #enddocregion config
class RunnerPongData {
  const RunnerPongData({required this.pong, required this.timestampMs});
  final bool pong;
  final String timestampMs;
}

class RunnerSystemInfoData {
  RunnerSystemInfoData({
    required this.runner,
    required this.version,
    required this.waylandDisplay,
    required this.width,
    required this.height,
    required this.pixelRatio,
    required this.fullscreen,
    required this.maximized,
    required this.title,
    required this.appId,
  });
  final String runner;
  final String version;
  final String waylandDisplay;
  final int width;
  final int height;
  final double pixelRatio;
  final bool fullscreen;
  final bool maximized;
  final String title;
  final String appId;
}

// #docregion host-definitions
@HostApi()
abstract class RunnerHostApi {
  // Surface / Window controls
  @async
  RunnerSystemInfoData? getSystemInfo();

  @async
  bool setWindowTitle(String title);

  @async
  bool setFullscreen({bool enabled = false});

  @async
  bool setMaximized({bool enabled = false});

  @async
  bool minimize();

  @async
  RunnerPongData ping();
}
// #enddocregion host-definitions

// #docregion flutter-definitions
// @FlutterApi()
// abstract class RunnerFlutterApi {}
// #enddocregion flutter-definitions

// #docregion constants
// #enddocregion constants

// #enddocregion config
class CompositorPongData {
  const CompositorPongData({
    required this.pong,
    required this.timestampUs,
    required this.peerPid,
  });
  final bool pong;
  final int timestampUs;
  final int peerPid;
}

class CompositorSystemInfoData {
  CompositorSystemInfoData({
    required this.compositor,
    required this.version,
    required this.ipcChannel,
    required this.surfacesCount,
    required this.clientFps,
    required this.peerPid,
    required this.appId,
  });
  final String compositor;
  final String version;
  final String ipcChannel;
  final int surfacesCount;
  final double clientFps;
  final int peerPid;
  final String appId;
}

class CompositorSurfaceData {
  CompositorSurfaceData({
    required this.title,
    required this.handle,
    required this.appId,
    required this.width,
    required this.height,
    required this.fullscreen,
    required this.maximized,
    required this.activated,
  });
  final String title;
  final int handle;
  final String appId;
  final int width;
  final int height;
  final bool fullscreen;
  final bool maximized;
  final bool activated;
}

// #docregion host-definitions
@HostApi()
abstract class RunnerHostIpcApi {
  // Surface / Window controls
  @async
  CompositorSystemInfoData? getCompositorInfo();

  @async
  CompositorPongData? pingCompositor();

  @async
  List<CompositorSurfaceData>? listSurfaces();

  @async
  bool closeSurface(int surfaceHandle);
}
// #enddocregion host-definitions

// #docregion flutter-definitions
@FlutterApi()
abstract class RunnerFlutterIpcApi {
  void onNotification(String method, String? paramsJson);
}
// #enddocregion flutter-definitions

// #docregion constants
// #enddocregion constants
