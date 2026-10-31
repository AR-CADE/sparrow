import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show Icons;
import 'package:shell/src/data/models/system_action.dart';
import 'package:shell/src/data/models/system_actions.dart';

void main() {
  group('SystemActions', () {
    const action1 = SystemAction(
      name: 'Logout',
      command: r'loginctl terminate-user $USER',
      icon: Icons.logout,
    );
    const action2 = SystemAction(
      name: 'Power Off',
      command: 'systemctl poweroff',
      icon: Icons.power_settings_new,
    );

    test('supports value equality', () {
      const actionsA = SystemActions(
        logout: action1,
        hibernate: action2,
        suspend: action1,
        poweroff: action2,
        reboot: action1,
      );
      const actionsB = SystemActions(
        logout: action1,
        hibernate: action2,
        suspend: action1,
        poweroff: action2,
        reboot: action1,
      );
      const actionsC = SystemActions(logout: action2);

      expect(actionsA, equals(actionsB));
      expect(actionsA, isNot(equals(actionsC)));
      expect(
        actionsA.props,
        equals([action1, action2, action1, action2, action1]),
      );
    });

    test('supports default null fields', () {
      const actions = SystemActions();
      expect(actions.logout, isNull);
      expect(actions.hibernate, isNull);
      expect(actions.suspend, isNull);
      expect(actions.poweroff, isNull);
      expect(actions.reboot, isNull);
      expect(actions.props, equals([null, null, null, null, null]));
    });

    test('copyWith updates specified fields and retains others', () {
      const initial = SystemActions(logout: action1);

      final updated = initial.copyWith(
        logout: action2,
        hibernate: action1,
        suspend: action2,
        poweroff: action1,
        reboot: action2,
      );

      expect(updated.logout, equals(action2));
      expect(updated.hibernate, equals(action1));
      expect(updated.suspend, equals(action2));
      expect(updated.poweroff, equals(action1));
      expect(updated.reboot, equals(action2));

      final unchanged = initial.copyWith();
      expect(unchanged, equals(initial));
    });

    test('toJson and fromJson support round-trip serialization', () {
      const actions = SystemActions(logout: action1, poweroff: action2);

      final json = actions.toJson();
      final logoutJson = json['logout'] as Map<String, dynamic>?;
      final poweroffJson = json['poweroff'] as Map<String, dynamic>?;
      expect(logoutJson, isNotNull);
      expect(logoutJson?['name'], equals('Logout'));
      expect(poweroffJson?['name'], equals('Power Off'));
      expect(json['hibernate'], isNull);

      final restored = SystemActions.fromJson(json);
      expect(restored.logout?.name, equals(action1.name));
      expect(restored.logout?.command, equals(action1.command));
      expect(restored.logout?.icon?.codePoint, equals(action1.icon?.codePoint));
      expect(restored.poweroff?.name, equals(action2.name));
      expect(restored.poweroff?.command, equals(action2.command));
      expect(restored.hibernate, isNull);
      expect(restored.suspend, isNull);
      expect(restored.reboot, isNull);
    });

    test('toJson and fromJson serialize all populated actions', () {
      const allActions = SystemActions(
        logout: action1,
        hibernate: action2,
        suspend: action1,
        poweroff: action2,
        reboot: action1,
      );

      final json = allActions.toJson();
      final restored = SystemActions.fromJson(json);
      expect(restored, equals(allActions));
    });

    test('fromJson parses empty map with nulls', () {
      final actions = SystemActions.fromJson(const {});
      expect(actions.logout, isNull);
      expect(actions.hibernate, isNull);
      expect(actions.suspend, isNull);
      expect(actions.poweroff, isNull);
      expect(actions.reboot, isNull);
    });
  });
}
