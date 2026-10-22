import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ini/ini.dart';
import 'package:material_ui/material_ui.dart' show FilterQuality;
import 'package:shell/src/data/repositories/configuration_repository.dart';

void main() {
  group('ConfigurationRepository', () {
    late ConfigurationRepository repository;

    setUp(() {
      repository = ConfigurationRepository.instance;
    });

    test('factory returns singleton instance', () {
      expect(ConfigurationRepository(), same(ConfigurationRepository.instance));
    });

    test('parseConfig parses uppercase and lowercase keys correctly', () async {
      const iniString = '''
[Theme]
wallpaper = /home/user/bg.jpg

[Commands]
launcher = fuzzel
terminal = foot
logout = wlogout
suspend = systemctl suspend
hibernate = systemctl hibernate
reboot = systemctl reboot
poweroff = systemctl poweroff

[Quirks]
filterQuality = high
''';
      final config = Config.fromStrings(iniString.split('\n'));
      await repository.parseConfig(config);

      expect(repository.wallpaper, equals('/home/user/bg.jpg'));
      expect(repository.launcher, equals('fuzzel'));
      expect(repository.terminal, equals('foot'));
      expect(repository.filterQuality, equals(FilterQuality.high));

      final actions = repository.systemActions;
      expect(actions, isNotNull);
      expect(actions?.logout?.command, equals('wlogout'));
      expect(actions?.suspend?.command, equals('systemctl suspend'));
      expect(actions?.hibernate?.command, equals('systemctl hibernate'));
      expect(actions?.reboot?.command, equals('systemctl reboot'));
      expect(actions?.poweroff?.command, equals('systemctl poweroff'));

      final current = repository.currentConfiguration;
      expect(current.wallpaper, equals('/home/user/bg.jpg'));
      expect(current.launcher, equals('fuzzel'));
      expect(current.terminal, equals('foot'));
      expect(current.systemActions, equals(actions));
      expect(current.filterQuality, equals(FilterQuality.high));
    });

    test(
      'parseConfig handles fallback key names and trims whitespace',
      () async {
        const iniString = '''
[theme]
path =   /home/user/fallback.png  

[button]
grid = rofi
''';
        final config = Config.fromStrings(iniString.split('\n'));
        await repository.parseConfig(config);

        expect(repository.wallpaper, equals('/home/user/fallback.png'));
        expect(repository.launcher, equals('rofi'));
        expect(repository.terminal, isNull);
        expect(repository.systemActions?.logout, isNull);
      },
    );

    test('parseFile reads and parses an existing file', () async {
      final tempDir = await Directory.systemTemp.createTemp('sparrow_cfg_');
      final tempFile = File('${tempDir.path}/test_sparrow.ini');
      await tempFile.writeAsString('''
[Commands]
launcher = custom_launcher
''');

      await repository.parseFile(tempFile.path);

      expect(repository.launcher, equals('custom_launcher'));

      await tempDir.delete(recursive: true);
    });

    test(
      'parseFile safely ignores nonexistent files or invalid syntax',
      () async {
        // Nonexistent file
        await repository.parseFile('/path/does/not/exist/999.ini');

        // Invalid syntax file
        final tempDir = await Directory.systemTemp.createTemp(
          'sparrow_invalid_',
        );
        final tempFile = File('${tempDir.path}/invalid.ini');
        await tempFile.writeAsString('not a valid ini line here');

        await repository.parseFile(tempFile.path);
        await tempDir.delete(recursive: true);
      },
    );

    test(
      'configurationStream receives updates when parseConfig runs',
      () async {
        final expectation = expectLater(
          repository.configurationStream,
          emits(
            predicate<ShellConfiguration>(
              (cfg) => cfg.launcher == 'stream_launcher',
            ),
          ),
        );

        const ini = '[Commands]\nlauncher = stream_launcher\n';
        await repository.parseConfig(Config.fromStrings(ini.split('\n')));

        await expectation;
      },
    );

    test('parseConfig parses Quirks filterQuality correctly', () async {
      const iniString = '''
[Quirks]
filterQuality = medium
''';
      await repository.parseConfig(Config.fromStrings(iniString.split('\n')));
      expect(repository.filterQuality, equals(FilterQuality.medium));
      expect(
        repository.currentConfiguration.filterQuality,
        equals(FilterQuality.medium),
      );

      // Unknown value falls back to none
      const invalidIni = '''
[Quirks]
filterQuality = unknown_val
''';
      await repository.parseConfig(Config.fromStrings(invalidIni.split('\n')));
      expect(repository.filterQuality, equals(FilterQuality.none));
    });
  });
}
