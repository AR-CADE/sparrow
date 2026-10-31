import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show FilterQuality, Icons;
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
      filterQuality: FilterQuality.medium,
      secureAppIds: ['rpcs3', 'org.keepassxc.KeePassXC'],
    );

    test('supports value equality', () {
      const configB = ShellConfiguration(
        wallpaper: '/path/to/wall.jpg',
        launcher: 'fuzzel',
        terminal: 'alacritty',
        systemActions: actions,
        filterQuality: FilterQuality.medium,
        secureAppIds: ['rpcs3', 'org.keepassxc.KeePassXC'],
      );
      const configC = ShellConfiguration(launcher: 'other');

      expect(configA, equals(configB));
      expect(configA, isNot(equals(configC)));
      expect(
        configA.props,
        equals([
          '/path/to/wall.jpg',
          'fuzzel',
          'alacritty',
          actions,
          FilterQuality.medium,
          ['rpcs3', 'org.keepassxc.KeePassXC'],
          const <String>[],
          const <ShellService>[],
        ]),
      );
    });

    test('supports default constructor', () {
      const config = ShellConfiguration();
      expect(config.wallpaper, isNull);
      expect(config.launcher, isNull);
      expect(config.terminal, isNull);
      expect(config.systemActions, isNull);
      expect(config.filterQuality, isNull);
      expect(config.secureAppIds, isEmpty);
      expect(config.secureOutputNames, isEmpty);
      expect(config.services, isEmpty);
      expect(config.secureContents, isEmpty);
      expect(
        config.props,
        equals([
          null,
          null,
          null,
          null,
          null,
          const <String>[],
          const <String>[],
          const <ShellService>[],
        ]),
      );
    });

    test('copyWith updates specified fields and retains others', () {
      final updated = configA.copyWith(
        wallpaper: '/path/to/other.png',
        launcher: 'wofi',
        terminal: 'foot',
        systemActions: const SystemActions(),
        filterQuality: FilterQuality.high,
        secureAppIds: const ['bitwarden'],
      );

      expect(updated.wallpaper, equals('/path/to/other.png'));
      expect(updated.launcher, equals('wofi'));
      expect(updated.terminal, equals('foot'));
      expect(updated.systemActions, equals(const SystemActions()));
      expect(updated.filterQuality, equals(FilterQuality.high));
      expect(updated.secureAppIds, equals(const ['bitwarden']));

      final unchanged = configA.copyWith();
      expect(unchanged, equals(configA));
    });

    test(
      'secureContents combines secureAppIds and formatted secureOutputNames',
      () {
        final config = configA.copyWith(
          secureOutputNames: const ['DP-1', 'HDMI-A-1'],
        );
        expect(
          config.secureContents,
          equals([
            'rpcs3',
            'org.keepassxc.KeePassXC',
            'output:DP-1',
            'output:HDMI-A-1',
          ]),
        );
      },
    );

    test('toJson and fromJson support round-trip serialization', () {
      final json = configA.toJson();
      expect(json['wallpaper'], equals('/path/to/wall.jpg'));
      expect(json['launcher'], equals('fuzzel'));
      expect(json['terminal'], equals('alacritty'));
      expect(json['filterQuality'], equals('medium'));
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
      expect(restored.filterQuality, equals(configA.filterQuality));
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
      expect(config.filterQuality, isNull);
    });
  });
}
