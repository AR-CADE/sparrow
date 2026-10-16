import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show Icons;
import 'package:shell/src/data/models/shell_configuration.dart';

void main() {
  group('ShellConfiguration', () {
    const actions = SystemActions(
      logout: SystemAction(
        name: 'Logout',
        command: r'loginctl terminate-user $USER',
        icon: Icons.logout,
      ),
      poweroff: SystemAction(
        name: 'Power Off',
        command: 'systemctl poweroff',
        icon: Icons.power_settings_new,
      ),
    );

    const configA = ShellConfiguration(
      wallpaper: '/path/to/wall.jpg',
      launcher: 'fuzzel',
      terminal: 'alacritty',
      systemActions: actions,
    );

    test('supports value equality', () {
      const configB = ShellConfiguration(
        wallpaper: '/path/to/wall.jpg',
        launcher: 'fuzzel',
        terminal: 'alacritty',
        systemActions: actions,
      );
      const configC = ShellConfiguration(launcher: 'other');

      expect(configA, equals(configB));
      expect(configA, isNot(equals(configC)));
      expect(
        configA.props,
        equals(['/path/to/wall.jpg', 'fuzzel', 'alacritty', actions]),
      );
    });

    test('supports default constructor', () {
      const config = ShellConfiguration();
      expect(config.wallpaper, isNull);
      expect(config.launcher, isNull);
      expect(config.terminal, isNull);
      expect(config.systemActions, isNull);
      expect(config.props, equals([null, null, null, null]));
    });

    test('copyWith updates specified fields and retains others', () {
      final updated = configA.copyWith(
        wallpaper: '/path/to/other.png',
        launcher: 'wofi',
        terminal: 'foot',
        systemActions: const SystemActions(),
      );

      expect(updated.wallpaper, equals('/path/to/other.png'));
      expect(updated.launcher, equals('wofi'));
      expect(updated.terminal, equals('foot'));
      expect(updated.systemActions, equals(const SystemActions()));

      final unchanged = configA.copyWith();
      expect(unchanged, equals(configA));
    });

    test('toJson and fromJson support round-trip serialization', () {
      final json = configA.toJson();
      expect(json['wallpaper'], equals('/path/to/wall.jpg'));
      expect(json['launcher'], equals('fuzzel'));
      expect(json['terminal'], equals('alacritty'));
      final sysActionsJson = json['systemActions'] as Map<String, dynamic>?;
      expect(sysActionsJson, isNotNull);
      final logoutJson = sysActionsJson?['logout'] as Map<String, dynamic>?;
      expect(logoutJson, isNotNull);
      expect(logoutJson?['name'], equals('Logout'));
      expect(logoutJson?['icon'], equals(Icons.logout.codePoint));

      final restored = ShellConfiguration.fromJson(json);
      expect(restored.wallpaper, equals(configA.wallpaper));
      expect(restored.launcher, equals(configA.launcher));
      expect(restored.terminal, equals(configA.terminal));
      expect(
        restored.systemActions?.logout?.name,
        equals(configA.systemActions?.logout?.name),
      );
      expect(
        restored.systemActions?.logout?.command,
        equals(configA.systemActions?.logout?.command),
      );
      expect(
        restored.systemActions?.logout?.icon?.codePoint,
        equals(configA.systemActions?.logout?.icon?.codePoint),
      );
      expect(
        restored.systemActions?.poweroff?.name,
        equals(configA.systemActions?.poweroff?.name),
      );
    });

    test('fromJson parses empty map with nulls', () {
      final config = ShellConfiguration.fromJson(const {});
      expect(config.wallpaper, isNull);
      expect(config.launcher, isNull);
      expect(config.terminal, isNull);
      expect(config.systemActions, isNull);
    });
  });
}
