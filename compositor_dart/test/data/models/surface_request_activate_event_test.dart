import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:compositor_dart/src/data/models/surface_request_activate_event.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('SurfaceRequestActivateEvent', () {
    const surface = Surface(handle: 42, pid: 100, gid: 100, uid: 100);

    const event = SurfaceRequestActivateEvent(
      handle: 42,
      token: 'token-xyz',
      appId: 'org.gnome.Calculator',
      surface: surface,
    );

    test('props are correct', () {
      expect(
        event.props,
        equals([42, 'token-xyz', 'org.gnome.Calculator', surface]),
      );
    });

    test('equality works', () {
      expect(
        event,
        equals(
          const SurfaceRequestActivateEvent(
            handle: 42,
            token: 'token-xyz',
            appId: 'org.gnome.Calculator',
            surface: surface,
          ),
        ),
      );
      expect(event, isNot(equals(event.copyWith(handle: 43))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = event.toJson();
      expect(json['handle'], equals(42));
      expect(json['token'], equals('token-xyz'));
      expect(json['app_id'], equals('org.gnome.Calculator'));

      final deserialized = SurfaceRequestActivateEvent.fromJson(json);
      expect(deserialized.handle, equals(42));
      expect(deserialized.token, equals('token-xyz'));
      expect(deserialized.appId, equals('org.gnome.Calculator'));
    });

    test('copyWith updates properties', () {
      final updated = event.copyWith(
        handle: 99,
        token: 'new-token',
        appId: 'org.example.App',
      );

      expect(updated.handle, equals(99));
      expect(updated.token, equals('new-token'));
      expect(updated.appId, equals('org.example.App'));
    });
  });
}
