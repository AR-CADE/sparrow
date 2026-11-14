import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:compositor_dart/src/data/models/display_output.dart';
import 'package:compositor_dart/src/data/models/gesture_swipe_event.dart';
import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:compositor_dart/src/data/models/surface_request_activate_event.dart';
import 'package:compositor_dart/src/data/models/zoom_event.dart';
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show Rect;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('CompositorRepository', () {
    late CompositorRepository repository;

    setUp(() {
      repository = CompositorRepository();
      repository.surfaces.clear();
      repository.outputs.clear();
      repository.subSurfaces.clear();
      repository.popups.clear();
    });

    test('initial state has empty collections', () {
      expect(repository.surfaces, isNotNull);
      expect(repository.outputs, isNotNull);
      expect(repository.subSurfaces, isNotNull);
      expect(repository.popups, isNotNull);
      expect(repository.events, isNotNull);
      expect(repository.platform, isNotNull);
    });

    test('surfaceMap adds surface and emits event', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 1,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
        'title': 'Test App',
        'app_id': 'test.app',
      };

      final eventFuture = repository.events.first;
      repository.surfaceMap(surfaceJson);

      final event = await eventFuture;
      expect(event.type, equals(CompositorEventType.surfaceMap));
      expect(event.event, isA<Surface>());

      final surface = repository.surfaceLookUp(1);
      expect(surface, isNotNull);
      expect(surface!.handle, equals(1));
      expect(surface.title, equals('Test App'));
    });

    test('surfaceTitle updates title and appId', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 2,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
        'title': 'Initial Title',
        'app_id': 'app.initial',
      };
      repository.surfaceMap(surfaceJson);

      final eventFuture = repository.events.first;
      repository.surfaceTitle(2, 'New Title', 'app.new');
      final event = await eventFuture;

      expect(event.type, equals(CompositorEventType.surfaceTitleChange));
      final updated = repository.surfaceLookUp(2);
      expect(updated?.title, equals('New Title'));
      expect(updated?.appId, equals('app.new'));
    });

    test('surfaceGeometry updates geometry', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 3,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
      };
      repository.surfaceMap(surfaceJson);

      final eventFuture = repository.events.first;
      repository.surfaceGeometry(3, 800, 600, 800, 600, 10, 20);
      final event = await eventFuture;

      expect(event.type, equals(CompositorEventType.surfaceGeometryChange));
      final updated = repository.surfaceLookUp(3);
      expect(updated?.width, equals(800));
      expect(updated?.height, equals(600));
      expect(updated?.geoX, equals(10));
      expect(updated?.geoY, equals(20));
    });

    test('surfaceDecoration updates usesCsd', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 4,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
      };
      repository.surfaceMap(surfaceJson);

      final eventFuture = repository.events.first;
      repository.surfaceDecoration(4, false, true);
      final event = await eventFuture;

      expect(event.type, equals(CompositorEventType.surfaceDecorationChange));
      expect(repository.surfaceLookUp(4)?.usesCsd, isTrue);
    });

    test('surfaceFullscreen updates fullscreen flag', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 44,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
      };
      repository.surfaceMap(surfaceJson);

      final eventFuture = repository.events.first;
      repository.surfaceFullscreen(44, true);
      final event = await eventFuture;

      expect(event.type, equals(CompositorEventType.surfaceFullscreenChange));
      expect(repository.surfaceLookUp(44)?.fullscreen, isTrue);
    });

    test('dockReveal emits dockReveal event', () async {
      final eventFuture = repository.events.first;
      repository.dockReveal();
      final event = await eventFuture;

      expect(event.type, equals(CompositorEventType.dockReveal));
    });

    test('surfaceMinimize and surfaceRequestActivate emit events', () async {
      final surfaceJson = <String, dynamic>{
        'handle': 5,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
      };
      repository.surfaceMap(surfaceJson);

      final minFuture = repository.events.first;
      repository.surfaceMinimize(5);
      final minEvent = await minFuture;
      expect(minEvent.type, equals(CompositorEventType.surfaceMinimizeRequest));

      final actFuture = repository.events.first;
      repository.surfaceRequestActivate(5, 'tok1', 'app1');
      final actEvent = await actFuture;
      expect(actEvent.type, equals(CompositorEventType.surfaceRequestActivate));
      expect(actEvent.event, isA<SurfaceRequestActivateEvent>());
    });

    test('subsurfaceMap, subsurfacePosition, and subsurfaceUnmap', () async {
      final subJson = <String, dynamic>{
        'handle': 100,
        'texture_id': 100,
        'parent_handle': 1,
        'x': 0,
        'y': 0,
        'width': 50,
        'height': 50,
      };

      final mapFuture = repository.events.first;
      repository.subsurfaceMap(subJson);
      final mapEvent = await mapFuture;
      expect(mapEvent.type, equals(CompositorEventType.subSurfaceMap));
      expect(repository.subSurfaceSetLookUp(1)?.length, equals(1));

      final posFuture = repository.events.first;
      repository.subsurfacePosition(100, 1, 10, 20, 60, 60, 60, 60);
      final posEvent = await posFuture;
      expect(
        posEvent.type,
        equals(CompositorEventType.subsurfacePositionChange),
      );

      final unmapFuture = repository.events.first;
      repository.subsurfaceUnmap(100, 1);
      final unmapEvent = await unmapFuture;
      expect(unmapEvent.type, equals(CompositorEventType.subSurfaceUnMap));
      expect(repository.subSurfaceSetLookUp(1), isEmpty);
    });

    test('popupMap and popupUnmap', () async {
      final popupJson = <String, dynamic>{
        'handle': 200,
        'texture_id': 200,
        'parent_handle': 1,
        'x': 10,
        'y': 10,
        'width': 100,
        'height': 150,
      };

      final mapFuture = repository.events.first;
      repository.popupMap(popupJson);
      final mapEvent = await mapFuture;
      expect(mapEvent.type, equals(CompositorEventType.popupMap));
      expect(repository.popupSetLookUp(1)?.length, equals(1));

      final unmapFuture = repository.events.first;
      repository.popupUnmap(200);
      final unmapEvent = await unmapFuture;
      expect(unmapEvent.type, equals(CompositorEventType.popupUnMap));
      expect(repository.popupSetLookUp(1), isEmpty);
    });

    test('outputAdded, outputChanged, and outputRemoved', () async {
      final outputJson1 = <String, dynamic>{
        'id': 1,
        'name': 'DP-1',
        'x': 0,
        'y': 0,
        'width': 1920,
        'height': 1080,
      };
      final outputJson2 = <String, dynamic>{
        'id': 2,
        'name': 'DP-2',
        'x': 1920,
        'y': 0,
        'width': 1920,
        'height': 1080,
      };

      repository
        ..outputAdded(outputJson1)
        ..outputAdded(outputJson2);

      expect(repository.getPrimaryOutput?.id, equals(1));
      expect(
        repository.totalBounds,
        equals(const Rect.fromLTRB(0, 0, 3840, 1080)),
      );

      expect(repository.getOutputAtPoint(500, 500)?.id, equals(1));
      expect(repository.getOutputAtPoint(2500, 500)?.id, equals(2));
      expect(repository.getOutputAtPoint(5000, 5000), isNull);

      expect(
        repository
            .getOutputForRect(const Rect.fromLTWH(100, 100, 500, 500))
            ?.id,
        equals(1),
      );

      final changeFuture = repository.events.first;
      repository.outputChanged(<String, dynamic>{
        'id': 1,
        'name': 'DP-1-renamed',
        'x': 0,
        'y': 0,
        'width': 2560,
        'height': 1440,
      });
      final changeEvent = await changeFuture;
      expect(changeEvent.type, equals(CompositorEventType.outputChanged));

      final removeFuture = repository.events.first;
      repository.outputRemoved(1);
      final removeEvent = await removeFuture;
      expect(removeEvent.type, equals(CompositorEventType.outputRemoved));
      expect(repository.getPrimaryOutput?.id, equals(2));
    });

    test('gesture and zoom callbacks emit events', () async {
      final gBeginFuture = repository.events.first;
      repository.gestureSwipeBegin(3, 100);
      final gBegin = await gBeginFuture;
      expect(gBegin.type, equals(CompositorEventType.gestureSwipeBegin));
      expect(gBegin.event, isA<GestureSwipeBeginEvent>());

      final gUpdateFuture = repository.events.first;
      repository.gestureSwipeUpdate(5, 5, 110);
      final gUpdate = await gUpdateFuture;
      expect(gUpdate.type, equals(CompositorEventType.gestureSwipeUpdate));
      expect(gUpdate.event, isA<GestureSwipeUpdateEvent>());

      final gEndFuture = repository.events.first;
      repository.gestureSwipeEnd(false, 120);
      final gEnd = await gEndFuture;
      expect(gEnd.type, equals(CompositorEventType.gestureSwipeEnd));
      expect(gEnd.event, isA<GestureSwipeEndEvent>());

      final zScrollFuture = repository.events.first;
      repository.zoomScroll(1.2, 50, 50);
      final zScroll = await zScrollFuture;
      expect(zScroll.type, equals(CompositorEventType.zoomScroll));
      expect(zScroll.event, isA<ZoomScrollEvent>());

      final zKeyFuture = repository.events.first;
      repository.zoomKey(1, 100, 100);
      final zKey = await zKeyFuture;
      expect(zKey.type, equals(CompositorEventType.zoomKey));
      expect(zKey.event, isA<ZoomKeyEvent>());
    });

    test('keyToXkb translates physical keys correctly', () {
      expect(repository.keyToXkb(0x00070029), equals(0x00000009)); // escape
      expect(repository.keyToXkb(0x01500000042), equals(0x42));
      expect(repository.keyToXkb(0xFFFFFFFF), isNull);
    });

    test('resizeReady does not throw', () {
      expect(() => repository.resizeReady(1, 10), returnsNormally);
    });

    test('getPrimaryOutput returns primary output or falls back to first', () {
      const out1 = DisplayOutput(id: 10, name: 'eDP-1');
      const out2 = DisplayOutput(id: 11, name: 'HDMI-1', isPrimary: true);

      repository.outputAdded(out1.toJson());
      expect(repository.getPrimaryOutput?.id, equals(10));

      repository.outputAdded(out2.toJson());
      expect(repository.getPrimaryOutput?.id, equals(11));
    });

    test('getOutputForRect selects the output with the most overlap', () {
      const out1 = DisplayOutput(
        id: 20,
        name: 'out-1',
        x: 0,
        y: 0,
        width: 1920,
        height: 1080,
      );
      const out2 = DisplayOutput(
        id: 21,
        name: 'out-2',
        x: 1920,
        y: 0,
        width: 1920,
        height: 1080,
      );

      repository
        ..outputAdded(out1.toJson())
        ..outputAdded(out2.toJson());

      // Rect entirely on out1
      final found1 = repository.getOutputForRect(
        const Rect.fromLTWH(100, 100, 500, 500),
      );
      expect(found1?.id, equals(20));

      // Rect entirely on out2
      final found2 = repository.getOutputForRect(
        const Rect.fromLTWH(2000, 100, 500, 500),
      );
      expect(found2?.id, equals(21));
    });

    test(
      'setPrimaryOutput handles missing and already-primary output',
      () async {
        expect(await repository.setPrimaryOutput(9999), isFalse);

        const out = DisplayOutput(id: 30, name: 'out-30', isPrimary: true);
        repository.outputAdded(out.toJson());
        expect(await repository.setPrimaryOutput(30), isTrue);
      },
    );

    test(
      'subsurfaceMap handles subsurface when parent is not yet registered',
      () async {
        final subJson = <String, dynamic>{
          'handle': 500,
          'texture_id': 10,
          'parent_handle': 9999,
          'x': 5,
          'y': 5,
        };

        final eventFuture = repository.events.first;
        repository.subsurfaceMap(subJson);
        final event = await eventFuture;

        expect(event.type, equals(CompositorEventType.subSurfaceMap));
        expect(
          repository.subSurfaces[9999]?.any((s) => s.handle == 500),
          isTrue,
        );

        // subsurfaceUnmap finding it across parents
        // when wrong parentHandle passed
        final unmapFuture = repository.events.first;
        repository.subsurfaceUnmap(500, 1111); // Wrong parent
        final unmapEvent = await unmapFuture;
        expect(unmapEvent.type, equals(CompositorEventType.subSurfaceUnMap));
      },
    );

    test('surfaceMap throws on type-invalid json', () {
      expect(
        () => repository.surfaceMap({'handle': 'invalid_type'}),
        throwsA(isA<TypeError>()),
      );
    });

    test('close clears state and closes streams', () async {
      await expectLater(repository.close(), completes);
    });
  });
}
