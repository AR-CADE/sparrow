import 'dart:async'
    show Future, StreamController, StreamSubscription, unawaited;
import 'dart:io' show File, Platform;

import 'package:flutter/foundation.dart' show ChangeNotifier;
import 'package:ini/ini.dart' show Config;
import 'package:material_ui/material_ui.dart' show Icons;
import 'package:rxdart/subjects.dart' show BehaviorSubject;
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:watcher/watcher.dart' show FileWatcher, WatchEvent;

export 'package:shell/src/data/models/shell_configuration.dart'
    show ShellConfiguration, SystemAction, SystemActions;

String? _getenv(String env) {
  return Platform.environment[env];
}

String? _getHomeConfig() {
  final homeConfig = _getenv('XDG_CONFIG_HOME');
  if (homeConfig != null && homeConfig.isNotEmpty) {
    return homeConfig;
  }
  final home = _getenv('HOME');
  return (home != null && home.isNotEmpty) ? '$home/.config' : null;
}

/// Repository managing shell configuration loading, parsing, and file watching.
class ConfigurationRepository extends ChangeNotifier {
  /// Returns the singleton [ConfigurationRepository] instance.
  factory() => instance;

  new _();

  /// The shared singleton instance.
  static final ConfigurationRepository instance = ConfigurationRepository._();

  String? _wallpaper;
  String? _launcher;
  String? _terminal;
  SystemActions? _systemActions;

  /// Configured desktop wallpaper path.
  String? get wallpaper => _wallpaper;

  /// Configured app launcher executable command.
  String? get launcher => _launcher;

  /// Configured terminal executable command.
  String? get terminal => _terminal;

  /// Configured system actions.
  SystemActions? get systemActions => _systemActions;

  /// Returns the current immutable [ShellConfiguration].
  ShellConfiguration get currentConfiguration => ShellConfiguration(
    wallpaper: _wallpaper,
    launcher: _launcher,
    terminal: _terminal,
    systemActions: _systemActions,
  );

  final _configurationController =
      StreamController<ShellConfiguration>.broadcast();

  /// Stream of real-time [ShellConfiguration] updates.
  Stream<ShellConfiguration> get configurationStream =>
      _configurationController.stream;

  final _status = BehaviorSubject<void>();

  /// Stream indicating configuration change status events.
  BehaviorSubject<void> get status => _status;

  StreamSubscription<WatchEvent>? _configurationSubscription;

  /// Returns the resolved config file path, or `null` if not resolvable.
  static String? getConfigFile() {
    final envFile = _getenv('SPARROW_CONFIG');
    if (envFile != null && envFile.isNotEmpty) {
      return envFile;
    }
    final homeConfig = _getHomeConfig();
    if (homeConfig == null) {
      return null;
    }
    return '$homeConfig/sparrow/sparrow.ini';
  }

  /// Initializes configuration from file and sets up file system watcher.
  Future<void> init() async {
    final file = getConfigFile();
    if (file == null) {
      return;
    }

    final f = File(file);
    if (!f.existsSync()) {
      try {
        f.parent.createSync(recursive: true);
        f.writeAsStringSync('''

[Theme]
# wallpaper = /path/to/wallpaper.jpg

[Commands]
launcher = grid2
terminal = alacritty
logout = wayland-logout
''');
      } on Object catch (_) {
        // In case the filesystem is read-only or file creation fails
      }
    }

    if (f.existsSync()) {
      await parseFile(file);

      try {
        final watcher = FileWatcher(file);
        _configurationSubscription = watcher.events.listen((e) async {
          await parseFile(e.path);
        });
      } on Object catch (_) {
        // Ignore watcher initialization errors
      }
    }
  }

  /// Parses configuration file from disk.
  Future<void> parseFile(String file) async {
    try {
      final f = File(file);
      if (!f.existsSync()) {
        return;
      }
      final lines = f.readAsLinesSync();
      final config = Config.fromStrings(lines);
      await parseConfig(config);
    } on Object catch (_) {
      // Ignore parse or transient lock errors
    }
  }

  /// Parses configuration from an ini [Config] object.
  Future<void> parseConfig(Config config) async {
    String? getOption(List<String> sections, List<String> options) {
      for (final section in sections) {
        for (final option in options) {
          if (config.hasOption(section, option)) {
            final val = config.get(section, option);
            if (val != null && val.trim().isNotEmpty) {
              return val.trim();
            }
          }
        }
      }
      return null;
    }

    _wallpaper = getOption(
      ['Theme', 'theme', 'Wallpaper', 'wallpaper'],
      ['wallpaper', 'path'],
    );
    _launcher = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['launcher', 'grid'],
    );
    _terminal = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['terminal'],
    );

    final logout = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['logout'],
    );

    final hibernate = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['hibernate'],
    );

    final suspend = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['suspend'],
    );

    final reboot = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['reboot'],
    );

    final poweroff = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['poweroff'],
    );

    _systemActions = SystemActions(
      logout: logout != null
          ? SystemAction(name: 'Log Out', command: logout, icon: Icons.logout)
          : null,
      hibernate: hibernate != null
          ? SystemAction(
              name: 'Hibernate',
              command: hibernate,
              icon: Icons.ac_unit,
            )
          : null,
      suspend: suspend != null
          ? SystemAction(name: 'Suspend', command: suspend, icon: Icons.pause)
          : null,
      reboot: reboot != null
          ? SystemAction(
              name: 'Reboot',
              command: reboot,
              icon: Icons.restart_alt,
            )
          : null,
      poweroff: poweroff != null
          ? SystemAction(
              name: 'Power Off',
              command: poweroff,
              icon: Icons.power_settings_new,
            )
          : null,
    );

    notifyListeners();
    _status.add(null);
    _configurationController.add(currentConfiguration);
  }

  @override
  void dispose() {
    unawaited(_configurationSubscription?.cancel());
    unawaited(_status.close());
    unawaited(_configurationController.close());
    super.dispose();
  }
}
