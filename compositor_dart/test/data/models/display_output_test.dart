import 'package:compositor_dart/src/data/models/display_mode.dart';
import 'package:compositor_dart/src/data/models/display_output.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('DisplayOutput', () {
    const mode = DisplayMode(width: 1920, height: 1080, refresh: 60000);
    const output = DisplayOutput(
      id: 1,
      name: 'eDP-1',
      make: 'BOE',
      model: '0x098f',
      x: 0,
      y: 0,
      width: 1920,
      height: 1080,
      scale: 1.25,
      availableModes: [mode],
      isPrimary: true,
    );

    test('props are correct', () {
      expect(
        output.props,
        equals([
          1,
          'eDP-1',
          'BOE',
          '0x098f',
          0,
          0,
          1920,
          1080,
          60000,
          1.25,
          0,
          [mode],
          true,
        ]),
      );
    });

    test('equality works', () {
      expect(
        output,
        equals(
          const DisplayOutput(
            id: 1,
            name: 'eDP-1',
            make: 'BOE',
            model: '0x098f',
            x: 0,
            y: 0,
            width: 1920,
            height: 1080,
            scale: 1.25,
            availableModes: [mode],
            isPrimary: true,
          ),
        ),
      );
      expect(output, isNot(equals(output.copyWith(id: 2))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = output.toJson();
      expect(json['id'], equals(1));
      expect(json['name'], equals('eDP-1'));
      expect(json['make'], equals('BOE'));
      expect(json['refresh'], equals(60000));
      expect(json['scale'], equals(1.25));
      expect(json['is_primary'], isTrue);

      final deserialized = DisplayOutput.fromJson(json);
      expect(deserialized, equals(output));
    });

    test('refreshHz computes correct Hz', () {
      expect(output.refreshHz, equals(60.0));
    });

    test('bounds returns correct record', () {
      expect(output.bounds, equals((x: 0, y: 0, width: 1920, height: 1080)));
    });

    test('containsPoint checks coordinate inclusion', () {
      expect(output.containsPoint(100, 100), isTrue);
      expect(output.containsPoint(0, 0), isTrue);
      expect(output.containsPoint(1919, 1079), isTrue);
      expect(output.containsPoint(1920, 1080), isFalse);
      expect(output.containsPoint(-1, 0), isFalse);
    });

    test('copyWith updates all properties', () {
      final updated = output.copyWith(
        id: 2,
        name: 'DP-1',
        make: 'LG',
        model: 'UltraFine',
        x: 1920,
        y: 0,
        width: 2560,
        height: 1440,
        refreshRate: 144000,
        scale: 1,
        transform: 1,
        availableModes: const [],
        isPrimary: false,
      );

      expect(updated.id, equals(2));
      expect(updated.name, equals('DP-1'));
      expect(updated.make, equals('LG'));
      expect(updated.model, equals('UltraFine'));
      expect(updated.x, equals(1920));
      expect(updated.y, equals(0));
      expect(updated.width, equals(2560));
      expect(updated.height, equals(1440));
      expect(updated.refreshRate, equals(144000));
      expect(updated.scale, equals(1.0));
      expect(updated.transform, equals(1));
      expect(updated.availableModes, isEmpty);
      expect(updated.isPrimary, isFalse);
    });

    test('toString formats correctly', () {
      expect(output.toString(), contains('eDP-1'));
      expect(output.toString(), contains('1920x1080'));
    });
  });
}
