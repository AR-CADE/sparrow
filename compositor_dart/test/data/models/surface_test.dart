import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('Surface', () {
    const surface = Surface(
      handle: 100,
      pid: 1234,
      gid: 1000,
      uid: 1000,
      title: 'Terminal',
      appId: 'alacritty',
      width: 800,
      height: 600,
      bufferWidth: 800,
      bufferHeight: 600,
      maximized: 0,
      activated: 1,
      geoX: 0,
      geoY: 0,
      usesCsd: 0,
      outputId: 1,
      outputScale: 1,
      minWidth: 400,
      maxWidth: 1600,
      minHeight: 300,
      maxHeight: 1200,
    );

    test('props are correct', () {
      expect(surface.props, contains(100));
      expect(surface.props, contains('alacritty'));
      expect(surface.props, contains('Terminal'));
    });

    test('equality works', () {
      expect(
        surface,
        equals(
          const Surface(
            handle: 100,
            pid: 1234,
            gid: 1000,
            uid: 1000,
            title: 'Terminal',
            appId: 'alacritty',
            width: 800,
            height: 600,
            bufferWidth: 800,
            bufferHeight: 600,
            maximized: 0,
            activated: 1,
            geoX: 0,
            geoY: 0,
            usesCsd: 0,
            outputId: 1,
            outputScale: 1,
            minWidth: 400,
            maxWidth: 1600,
            minHeight: 300,
            maxHeight: 1200,
          ),
        ),
      );
      expect(surface, isNot(equals(surface.copyWith(handle: 101))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = surface.toJson();
      expect(json['handle'], equals(100));
      expect(json['client_pid'], equals(1234));
      expect(json['app_id'], equals('alacritty'));
      expect(json['title'], equals('Terminal'));

      final cJson = <String, dynamic>{
        'handle': 100,
        'client_pid': 1234,
        'client_gid': 1000,
        'client_uid': 1000,
        'title': 'Terminal',
        'app_id': 'alacritty',
        'width': 800,
        'height': 600,
        'buffer_width': 800,
        'buffer_height': 600,
        'maximized': 0,
        'fullscreen': 0,
        'activated': 1,
        'geo_x': 0,
        'geo_y': 0,
        'uses_csd': 0,
        'output_id': 1,
        'output_scale': 1.0,
        'min_width': 400,
        'max_width': 1600,
        'min_height': 300,
        'max_height': 1200,
      };
      final deserialized = Surface.fromJson(cJson);
      expect(deserialized, equals(surface));
      expect(deserialized.fullscreen, isFalse);
    });

    test('isFixedSize handles fixed and dynamic dimensions', () {
      expect(surface.isFixedSize, isFalse);

      const fixedWidthSurface = Surface(
        handle: 101,
        pid: 1234,
        gid: 1000,
        uid: 1000,
        minWidth: 500,
        maxWidth: 500,
      );
      expect(fixedWidthSurface.isFixedSize, isTrue);

      const fixedHeightSurface = Surface(
        handle: 102,
        pid: 1234,
        gid: 1000,
        uid: 1000,
        minHeight: 400,
        maxHeight: 400,
      );
      expect(fixedHeightSurface.isFixedSize, isTrue);

      const zeroSizeSurface = Surface(
        handle: 103,
        pid: 1234,
        gid: 1000,
        uid: 1000,
        minWidth: 0,
        maxWidth: 0,
      );
      expect(zeroSizeSurface.isFixedSize, isFalse);
    });

    test('copyWith updates properties', () {
      final updated = surface.copyWith(
        handle: 200,
        title: 'Editor',
        appId: 'code',
        width: 1024,
        height: 768,
        maximized: true,
        fullscreen: true,
        activated: false,
        usesCsd: true,
      );

      expect(updated.handle, equals(200));
      expect(updated.title, equals('Editor'));
      expect(updated.appId, equals('code'));
      expect(updated.width, equals(1024));
      expect(updated.height, equals(768));
      expect(updated.maximized, isTrue);
      expect(updated.fullscreen, isTrue);
      expect(updated.activated, isFalse);
      expect(updated.usesCsd, isTrue);
    });
  });
}
