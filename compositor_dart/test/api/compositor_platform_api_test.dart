import 'package:compositor_dart/src/api/compositor_platform_api.dart';
import 'package:compositor_dart/src/core/constants.dart';
import 'package:compositor_dart/src/data/models/display_mode.dart';
import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:pigeon_compositor/pigeon_compositor.dart';

class MockCompositorHostApi extends Mock implements CompositorHostApi;

void main() {
  group('CompositorPlatformApi', () {
    late CompositorHostApi mockHostApi;
    late CompositorPlatformApi platformApi;

    const surface = Surface(
      handle: 1,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'App',
    );

    setUp(() {
      mockHostApi = MockCompositorHostApi();
      platformApi = CompositorPlatformApi(hostApi: mockHostApi);
    });

    test('default constructor instantiates hostApi', () {
      final api = CompositorPlatformApi();
      expect(api.hostApi, isNotNull);
    });

    test('initLogger runs without throwing', () {
      expect(CompositorPlatformApi.initLogger, returnsNormally);
    });

    test('surfaceToplevelSetSize calls hostApi', () async {
      when(() => mockHostApi.surfaceToplevelSetSize(1, 800, 600))
          .thenAnswer((_) async {});

      await platformApi.surfaceToplevelSetSize(surface, 800, 600);
      verify(() => mockHostApi.surfaceToplevelSetSize(1, 800, 600)).called(1);
    });

    test('surfaceToplevelSetMaximized calls hostApi', () async {
      when(() => mockHostApi.surfaceToplevelSetMaximized(1))
          .thenAnswer((_) async {});

      await platformApi.surfaceToplevelSetMaximized(surface);
      verify(() => mockHostApi.surfaceToplevelSetMaximized(1)).called(1);
    });

    test('surfaceToplevelSetFullscreen calls hostApi', () async {
      when(() => mockHostApi.surfaceToplevelSetFullscreen(1))
          .thenAnswer((_) async {});

      await platformApi.surfaceToplevelSetFullscreen(surface);
      verify(() => mockHostApi.surfaceToplevelSetFullscreen(1)).called(1);
    });

    test(
      'surfaceToplevelClose calls hostApi and returns true on success',
      () async {
        when(() => mockHostApi.surfaceToplevelClose(1))
            .thenAnswer((_) async => true);

        final result = await platformApi.surfaceToplevelClose(surface);
        expect(result, isTrue);
        verify(() => mockHostApi.surfaceToplevelClose(1)).called(1);
      },
    );

    test('surfaceToplevelClose returns false on PlatformException', () async {
      when(() => mockHostApi.surfaceToplevelClose(1))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.surfaceToplevelClose(surface);
      expect(result, isFalse);
    });

    test('surfaceFocus calls hostApi', () async {
      when(() => mockHostApi.surfaceFocus(1)).thenAnswer((_) async {});

      await platformApi.surfaceFocus(surface);
      verify(() => mockHostApi.surfaceFocus(1)).called(1);
    });

    test('clearFocus calls hostApi', () async {
      when(() => mockHostApi.surfaceClearFocus()).thenAnswer((_) async {});

      await platformApi.clearFocus(surface);
      verify(() => mockHostApi.surfaceClearFocus()).called(1);
    });

    test('forceRenderAllViews calls hostApi', () async {
      when(() => mockHostApi.forceRenderAllViews(force: true))
          .thenAnswer((_) async {});

      await platformApi.forceRenderAllViews(force: true);
      verify(() => mockHostApi.forceRenderAllViews(force: true)).called(1);
    });

    test('setImmersive calls hostApi', () async {
      when(() => mockHostApi.setImmersive(false)).thenAnswer((_) async {});

      await platformApi.setImmersive(sticky: false);
      verify(() => mockHostApi.setImmersive(false)).called(1);
    });

    test('setScreenZoomActive calls hostApi', () async {
      when(() => mockHostApi.setScreenZoomActive(active: true))
          .thenAnswer((_) async {});

      await platformApi.setScreenZoomActive(active: true);
      verify(() => mockHostApi.setScreenZoomActive(active: true)).called(1);
    });

    test('setPrimaryOutput calls hostApi', () async {
      when(() => mockHostApi.setPrimaryOutput(1)).thenAnswer((_) async {});

      await platformApi.setPrimaryOutput(1);
      verify(() => mockHostApi.setPrimaryOutput(1)).called(1);
    });

    test('surfaceSetPosition calls hostApi', () async {
      when(() => mockHostApi.surfaceSetPosition(1, 10, 20))
          .thenAnswer((_) async {});

      await platformApi.surfaceSetPosition(surface, 10, 20);
      verify(() => mockHostApi.surfaceSetPosition(1, 10, 20)).called(1);
    });

    test('surfaceSetSceneBounds calls hostApi and deduplicates', () async {
      when(() => mockHostApi.surfaceSetSceneBounds(1, 10, 20, 800, 600, 12))
          .thenAnswer((_) async {});

      await platformApi.surfaceSetSceneBounds(surface, 10, 20, 800, 600, 12);
      await platformApi.surfaceSetSceneBounds(surface, 10, 20, 800, 600, 12);
      verify(() => mockHostApi.surfaceSetSceneBounds(1, 10, 20, 800, 600, 12))
          .called(1);
    });

    test('surfaceRequestResize calls hostApi', () async {
      when(() => mockHostApi.surfaceRequestResize(1, 800, 600, 42))
          .thenAnswer((_) async {});

      await platformApi.surfaceRequestResize(1, 800, 600, 42);
      verify(() => mockHostApi.surfaceRequestResize(1, 800, 600, 42)).called(1);
    });

    test('surfaceEndResize calls hostApi', () async {
      when(() => mockHostApi.surfaceEndResize(1)).thenAnswer((_) async {});

      await platformApi.surfaceEndResize(1);
      verify(() => mockHostApi.surfaceEndResize(1)).called(1);
    });

    test('setDirectInputMode calls hostApi when interactive', () async {
      when(() => mockHostApi.setDirectInputMode(1, enabled: true))
          .thenAnswer((_) async {});

      await platformApi.setDirectInputMode(surface, enabled: true);
      verify(() => mockHostApi.setDirectInputMode(1, enabled: true)).called(1);
    });

    test('setDirectInputMode handles null surface', () async {
      when(() => mockHostApi.setDirectInputMode(0)).thenAnswer((_) async {});

      await platformApi.setDirectInputMode(null, enabled: false);
      verify(() => mockHostApi.setDirectInputMode(0)).called(1);
    });

    test('setDirectInputMode does nothing when not interactive', () async {
      platformApi.interactive = false;
      await platformApi.setDirectInputMode(surface, enabled: true);
      verifyNever(
        () => mockHostApi.setDirectInputMode(
          any(),
          enabled: any(named: 'enabled'),
        ),
      );
    });

    test('surfaceSendKey calls hostApi when interactive', () async {
      when(
        () => mockHostApi.surfaceKeyboardKey(
          1,
          30,
          KeyStatus.pressed.index,
          1000,
        ),
      ).thenAnswer((_) async {});

      await platformApi.surfaceSendKey(
        surface,
        30,
        KeyStatus.pressed,
        const Duration(milliseconds: 1),
      );
      verify(
        () => mockHostApi.surfaceKeyboardKey(
          1,
          30,
          KeyStatus.pressed.index,
          1000,
        ),
      ).called(1);
    });

    test('setSensitiveAppIds calls hostApi', () async {
      when(() => mockHostApi.setSensitiveAppIds(['rpcs3', 'keepassxc']))
          .thenAnswer((_) async {});

      await platformApi.setSensitiveAppIds(['rpcs3', 'keepassxc']);
      verify(() => mockHostApi.setSensitiveAppIds(['rpcs3', 'keepassxc']))
          .called(1);
    });

    test('setSensitiveOutputNames calls hostApi', () async {
      when(() => mockHostApi.setSensitiveOutputNames(['DP-1', 'HDMI-A-1']))
          .thenAnswer((_) async {});

      await platformApi.setSensitiveOutputNames(['DP-1', 'HDMI-A-1']);
      verify(() => mockHostApi.setSensitiveOutputNames(['DP-1', 'HDMI-A-1']))
          .called(1);
    });

    test('addOrUpdateService calls hostApi', () async {
      when(() => mockHostApi.addOrUpdateService('alacritty', 'alacritty', true))
          .thenAnswer((_) async {});

      await platformApi.addOrUpdateService(
        'alacritty',
        'alacritty',
        restartable: true,
      );
      verify(
        () => mockHostApi.addOrUpdateService('alacritty', 'alacritty', true),
      ).called(1);
    });

    test('startAllServices calls hostApi', () async {
      when(() => mockHostApi.startAllServices()).thenAnswer((_) async {});

      await platformApi.startAllServices();
      verify(() => mockHostApi.startAllServices()).called(1);
    });

    test('surfaceSendKey does nothing when not interactive', () async {
      platformApi.interactive = false;
      await platformApi.surfaceSendKey(
        surface,
        30,
        KeyStatus.pressed,
        const Duration(milliseconds: 1),
      );
      verifyNever(
        () => mockHostApi.surfaceKeyboardKey(any(), any(), any(), any()),
      );
    });

    test('surfacePointerEvent calls hostApi when interactive', () async {
      when(() => mockHostApi.surfacePointerEvent([1, 2, 3]))
          .thenAnswer((_) async {});

      await platformApi.surfacePointerEvent([1, 2, 3]);
      verify(() => mockHostApi.surfacePointerEvent([1, 2, 3])).called(1);
    });

    test('surfacePointerEvent does nothing when not interactive', () async {
      platformApi.interactive = false;
      await platformApi.surfacePointerEvent([1, 2, 3]);
      verifyNever(() => mockHostApi.surfacePointerEvent(any()));
    });

    test('popupPointerEvent calls hostApi when interactive', () async {
      when(() => mockHostApi.popupPointerEvent([4, 5, 6]))
          .thenAnswer((_) async {});

      await platformApi.popupPointerEvent([4, 5, 6]);
      verify(() => mockHostApi.popupPointerEvent([4, 5, 6])).called(1);
    });

    test('popupPointerEvent does nothing when not interactive', () async {
      platformApi.interactive = false;
      await platformApi.popupPointerEvent([4, 5, 6]);
      verifyNever(() => mockHostApi.popupPointerEvent(any()));
    });

    test('getSocketPaths returns sockets from hostApi', () async {
      when(() => mockHostApi.getSocketPaths()).thenAnswer(
        (_) async => CompositorSocketsData(wayland: 'wayland-0', x: ':0'),
      );

      final sockets = await platformApi.getSocketPaths();
      expect(sockets.wayland, equals('wayland-0'));
      expect(sockets.x, equals(':0'));
    });

    test('isCompositor returns value and caches result', () async {
      when(() => mockHostApi.isCompositor()).thenAnswer((_) async => true);

      final res1 = await platformApi.isCompositor();
      final res2 = await platformApi.isCompositor();
      expect(res1, isTrue);
      expect(res2, isTrue);
      verify(() => mockHostApi.isCompositor()).called(1);
    });

    test('isCompositor returns false on PlatformException', () async {
      when(() => mockHostApi.isCompositor())
          .thenThrow(PlatformException(code: 'ERR'));

      final res = await platformApi.isCompositor();
      expect(res, isFalse);
    });

    test('setVsyncOutput calls hostApi', () async {
      when(() => mockHostApi.setVsyncOutput(1)).thenAnswer((_) async => true);

      final result = await platformApi.setVsyncOutput(1);
      expect(result, isTrue);
      verify(() => mockHostApi.setVsyncOutput(1)).called(1);
    });

    test('setVsyncOutput returns false on PlatformException', () async {
      when(() => mockHostApi.setVsyncOutput(1))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.setVsyncOutput(1);
      expect(result, isFalse);
    });

    test('setVsyncRateLimit calls hostApi', () async {
      when(() => mockHostApi.setVsyncRateLimit(60))
          .thenAnswer((_) async => true);

      final result = await platformApi.setVsyncRateLimit(60);
      expect(result, isTrue);
      verify(() => mockHostApi.setVsyncRateLimit(60)).called(1);
    });

    test('setVsyncRateLimit returns false on PlatformException', () async {
      when(() => mockHostApi.setVsyncRateLimit(60))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.setVsyncRateLimit(60);
      expect(result, isFalse);
    });

    test('setOutputMode calls hostApi', () async {
      const mode = DisplayMode(width: 1920, height: 1080, refresh: 60000);
      when(() => mockHostApi.setOutputMode(1, 1920, 1080, 60000))
          .thenAnswer((_) async => true);

      final result = await platformApi.setOutputMode(1, mode);
      expect(result, isTrue);
      verify(() => mockHostApi.setOutputMode(1, 1920, 1080, 60000)).called(1);
    });

    test('setOutputMode returns false on PlatformException', () async {
      const mode = DisplayMode(width: 1920, height: 1080, refresh: 60000);
      when(() => mockHostApi.setOutputMode(1, 1920, 1080, 60000))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.setOutputMode(1, mode);
      expect(result, isFalse);
    });

    test('setOutputPosition calls hostApi', () async {
      when(() => mockHostApi.setOutputPosition(1, 100, 200))
          .thenAnswer((_) async => true);

      final result = await platformApi.setOutputPosition(1, 100, 200);
      expect(result, isTrue);
      verify(() => mockHostApi.setOutputPosition(1, 100, 200)).called(1);
    });

    test('setOutputPosition returns false on PlatformException', () async {
      when(() => mockHostApi.setOutputPosition(1, 100, 200))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.setOutputPosition(1, 100, 200);
      expect(result, isFalse);
    });

    test('setOutputScale calls hostApi', () async {
      when(() => mockHostApi.setOutputScale(1, 1.5))
          .thenAnswer((_) async => true);

      final result = await platformApi.setOutputScale(1, 1.5);
      expect(result, isTrue);
      verify(() => mockHostApi.setOutputScale(1, 1.5)).called(1);
    });

    test('setOutputScale returns false on PlatformException', () async {
      when(() => mockHostApi.setOutputScale(1, 1.5))
          .thenThrow(PlatformException(code: 'ERR'));

      final result = await platformApi.setOutputScale(1, 1.5);
      expect(result, isFalse);
    });

    test('debugSetDamageVisualization calls hostApi', () async {
      when(() => mockHostApi.debugSetDamageVisualization(enabled: true))
          .thenAnswer((_) async {});

      await platformApi.debugSetDamageVisualization(enabled: true);
      verify(() => mockHostApi.debugSetDamageVisualization(enabled: true))
          .called(1);
    });

    test('debugSetDamageVisualization catches PlatformException', () async {
      when(() => mockHostApi.debugSetDamageVisualization(enabled: true))
          .thenThrow(PlatformException(code: 'ERR'));

      await expectLater(
        platformApi.debugSetDamageVisualization(enabled: true),
        completes,
      );
    });

    test('debugGetDamageVisualization calls hostApi', () async {
      when(() => mockHostApi.debugGetDamageVisualization())
          .thenAnswer((_) async => true);

      final result = await platformApi.debugGetDamageVisualization();
      expect(result, isTrue);
      verify(() => mockHostApi.debugGetDamageVisualization()).called(1);
    });

    test(
      'debugGetDamageVisualization returns false on PlatformException',
      () async {
        when(() => mockHostApi.debugGetDamageVisualization())
            .thenThrow(PlatformException(code: 'ERR'));

        final result = await platformApi.debugGetDamageVisualization();
        expect(result, isFalse);
      },
    );

    test('compositorReady calls hostApi', () async {
      when(() => mockHostApi.compositorReady()).thenAnswer((_) async {});

      await platformApi.compositorReady();
      verify(() => mockHostApi.compositorReady()).called(1);
    });

    test('compositorReady catches PlatformException', () async {
      when(() => mockHostApi.compositorReady())
          .thenThrow(PlatformException(code: 'ERR'));

      await expectLater(platformApi.compositorReady(), completes);
    });

    test('close disposes platform resources', () {
      expect(platformApi.close, returnsNormally);
    });

    group('distinct deduplication', () {
      test('setImmersive deduplicates identical consecutive calls', () async {
        when(() => mockHostApi.setImmersive(true)).thenAnswer((_) async {});

        await platformApi.setImmersive();
        await platformApi.setImmersive();
        await platformApi.setImmersive();

        verify(() => mockHostApi.setImmersive(true)).called(1);
      });

      test(
        'setScreenZoomActive deduplicates identical consecutive calls',
        () async {
          when(() => mockHostApi.setScreenZoomActive())
              .thenAnswer((_) async {});

          await platformApi.setScreenZoomActive();
          await platformApi.setScreenZoomActive();
          await platformApi.setScreenZoomActive();

          verify(() => mockHostApi.setScreenZoomActive()).called(1);
        },
      );

      test(
        'forceRenderAllViews deduplicates identical consecutive calls',
        () async {
          when(() => mockHostApi.forceRenderAllViews(force: true))
              .thenAnswer((_) async {});

          await platformApi.forceRenderAllViews(force: true);
          await platformApi.forceRenderAllViews(force: true);

          verify(() => mockHostApi.forceRenderAllViews(force: true)).called(1);
        },
      );

      test(
        'surfaceSetPosition deduplicates identical consecutive positions',
        () async {
          when(() => mockHostApi.surfaceSetPosition(1, 10, 20))
              .thenAnswer((_) async {});

          await platformApi.surfaceSetPosition(surface, 10, 20);
          await platformApi.surfaceSetPosition(surface, 10, 20);

          verify(() => mockHostApi.surfaceSetPosition(1, 10, 20)).called(1);
        },
      );

      test(
        'surfaceToplevelSetMaximized deduplicates identical states',
        () async {
          when(() => mockHostApi.surfaceToplevelSetMaximized(1))
              .thenAnswer((_) async {});

          await platformApi.surfaceToplevelSetMaximized(surface);
          await platformApi.surfaceToplevelSetMaximized(surface);

          verify(() => mockHostApi.surfaceToplevelSetMaximized(1)).called(1);
        },
      );

      test(
        'surfaceToplevelSetFullscreen deduplicates identical states',
        () async {
          when(() => mockHostApi.surfaceToplevelSetFullscreen(1))
              .thenAnswer((_) async {});

          await platformApi.surfaceToplevelSetFullscreen(surface);
          await platformApi.surfaceToplevelSetFullscreen(surface);

          verify(() => mockHostApi.surfaceToplevelSetFullscreen(1)).called(1);
        },
      );

      test(
        'clearSurfaceState allows re-sending previously cached state',
        () async {
          when(() => mockHostApi.surfaceSetPosition(1, 50, 60))
              .thenAnswer((_) async {});

          await platformApi.surfaceSetPosition(surface, 50, 60);
          platformApi.clearSurfaceState(surface.handle);
          await platformApi.surfaceSetPosition(surface, 50, 60);

          verify(() => mockHostApi.surfaceSetPosition(1, 50, 60)).called(2);
        },
      );
    });
  });
}
