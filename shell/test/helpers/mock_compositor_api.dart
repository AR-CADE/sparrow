import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

/// Sets up mock platform channel message handlers for all
/// Pigeon `CompositorHostApi` endpoints to prevent channel-error exceptions
/// during unit and widget tests.
void setUpMockCompositorHostApi() {
  const prefix = 'dev.flutter.pigeon.pigeon_compositor.CompositorHostApi.';
  const methodNames = [
    'surfaceRequestResize',
    'surfaceEndResize',
    'surfaceToplevelSetSize',
    'surfaceToplevelSetMaximized',
    'surfaceToplevelClose',
    'surfaceFocus',
    'surfaceClearFocus',
    'surfaceSetPosition',
    'forceRenderAllViews',
    'setDirectInputMode',
    'setPrimaryOutput',
    'setVsyncOutput',
    'setVsyncRateLimit',
    'setOutputMode',
    'setOutputPosition',
    'setOutputScale',
    'debugSetDamageVisualization',
    'debugGetDamageVisualization',
    'getSocketPaths',
    'compositorReady',
    'isCompositor',
    'surfaceKeyboardKey',
    'surfacePointerEvent',
    'popupPointerEvent',
    'setFullscreenDockRevealed',
    'surfaceToplevelSetFullscreen',
  ];

  for (final method in methodNames) {
    final channel = '$prefix$method';
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMessageHandler(channel, (message) async {
          if (channel.endsWith('surfaceToplevelClose') ||
              channel.endsWith('isCompositor') ||
              channel.endsWith('setPrimaryOutput')) {
            return const StandardMessageCodec().encodeMessage(<Object?>[true]);
          }
          return const StandardMessageCodec().encodeMessage(<Object?>[null]);
        });
  }
}
