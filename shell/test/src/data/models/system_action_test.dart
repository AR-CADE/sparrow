import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show Icons;
import 'package:shell/src/data/models/system_action.dart';

void main() {
  group('IconDataConverter', () {
    const converter = IconDataConverter();

    test('toJson returns codePoint for non-null IconData', () {
      expect(converter.toJson(Icons.logout), equals(Icons.logout.codePoint));
    });

    test('toJson returns null for null IconData', () {
      expect(converter.toJson(null), isNull);
    });

    test('fromJson returns null for null input', () {
      expect(converter.fromJson(null), isNull);
    });

    test('fromJson constructs IconData from int codePoint', () {
      final icon = converter.fromJson(0xe3b3);
      expect(icon, isNotNull);
      expect(icon?.codePoint, equals(0xe3b3));
      expect(icon?.fontFamily, equals('MaterialIcons'));
    });

    test('fromJson constructs IconData from Map with codePoint', () {
      final icon = converter.fromJson({
        'codePoint': 0xe3b3,
        'fontFamily': 'CustomFamily',
        'fontPackage': 'pkg',
        'matchTextDirection': true,
      });
      expect(icon, isNotNull);
      expect(icon?.codePoint, equals(0xe3b3));
      expect(icon?.fontFamily, equals('CustomFamily'));
      expect(icon?.fontPackage, equals('pkg'));
      expect(icon?.matchTextDirection, isTrue);
    });

    test('fromJson supports snake_case code_point in Map', () {
      final icon = converter.fromJson({'code_point': 0xe3b3});
      expect(icon, isNotNull);
      expect(icon?.codePoint, equals(0xe3b3));
      expect(icon?.fontFamily, equals('MaterialIcons'));
    });

    test('fromJson returns null for Map without codePoint', () {
      expect(converter.fromJson({'name': 'unknown'}), isNull);
    });

    test('fromJson returns null for unsupported object type', () {
      expect(converter.fromJson('invalid_string'), isNull);
    });
  });

  group('SystemAction', () {
    const actionA = SystemAction(
      name: 'Reboot',
      command: 'reboot',
      icon: Icons.restart_alt,
    );

    test('supports value equality', () {
      const actionB = SystemAction(
        name: 'Reboot',
        command: 'reboot',
        icon: Icons.restart_alt,
      );
      const actionC = SystemAction(
        name: 'Power Off',
        command: 'poweroff',
        icon: Icons.power_settings_new,
      );

      expect(actionA, equals(actionB));
      expect(actionA, isNot(equals(actionC)));
      expect(actionA.props, equals(['Reboot', 'reboot', Icons.restart_alt]));
    });

    test('supports null fields in default constructor', () {
      const action = SystemAction();
      expect(action.name, isNull);
      expect(action.command, isNull);
      expect(action.icon, isNull);
      expect(action.props, equals([null, null, null]));
    });

    test('copyWith updates specified fields and retains others', () {
      final updated = actionA.copyWith(
        name: 'Restart System',
        command: 'systemctl reboot',
        icon: Icons.refresh,
      );

      expect(updated.name, equals('Restart System'));
      expect(updated.command, equals('systemctl reboot'));
      expect(updated.icon, equals(Icons.refresh));

      final unchanged = actionA.copyWith();
      expect(unchanged, equals(actionA));
    });

    test('toJson and fromJson support round-trip serialization', () {
      final json = actionA.toJson();
      expect(json['name'], equals('Reboot'));
      expect(json['command'], equals('reboot'));
      expect(json['icon'], equals(Icons.restart_alt.codePoint));

      final restored = SystemAction.fromJson(json);
      expect(restored.name, equals(actionA.name));
      expect(restored.command, equals(actionA.command));
      expect(restored.icon?.codePoint, equals(actionA.icon?.codePoint));
    });

    test('fromJson parses empty map with nulls', () {
      final action = SystemAction.fromJson(const {});
      expect(action.name, isNull);
      expect(action.command, isNull);
      expect(action.icon, isNull);
    });
  });
}
