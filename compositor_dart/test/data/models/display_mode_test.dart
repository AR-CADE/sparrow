import 'package:compositor_dart/src/data/models/display_mode.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('DisplayMode', () {
    const mode = DisplayMode(width: 1920, height: 1080, refresh: 60000);

    test('props are correct', () {
      expect(mode.props, equals([1920, 1080, 60000]));
    });

    test('refreshHz computes correct Hz', () {
      expect(mode.refreshHz, equals(60.0));
      const mode144 = DisplayMode(width: 2560, height: 1440, refresh: 144000);
      expect(mode144.refreshHz, equals(144.0));
    });

    test('equality works', () {
      expect(
        const DisplayMode(width: 1920, height: 1080, refresh: 60000),
        equals(const DisplayMode(width: 1920, height: 1080, refresh: 60000)),
      );
      expect(
        const DisplayMode(width: 1920, height: 1080, refresh: 60000),
        isNot(
          equals(const DisplayMode(width: 1920, height: 1080, refresh: 120000)),
        ),
      );
    });

    test('toJson and fromJson serialize properly', () {
      final json = mode.toJson();
      expect(json['width'], equals(1920));
      expect(json['height'], equals(1080));
      expect(json['refresh'], equals(60000));

      final deserialized = DisplayMode.fromJson(json);
      expect(deserialized, equals(mode));
    });

    test('copyWith works with custom parameters', () {
      final updated = mode.copyWith(width: 3840, height: 2160, refresh: 120000);
      expect(updated.width, equals(3840));
      expect(updated.height, equals(2160));
      expect(updated.refresh, equals(120000));
    });

    test('copyWith works with no parameters', () {
      final updated = mode.copyWith();
      expect(updated.width, equals(mode.width));
      expect(updated.height, equals(mode.height));
      expect(updated.refresh, equals(mode.refresh));
    });

    test('toString formats correctly', () {
      expect(mode.toString(), equals('1920x1080 @ 60Hz'));
    });
  });
}
