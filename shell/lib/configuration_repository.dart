import 'dart:async' show Future, StreamSubscription, unawaited;
import 'dart:io' show File, Platform;

import 'package:flutter/foundation.dart' show ChangeNotifier;
import 'package:ini/ini.dart' show Config;
import 'package:rxdart/subjects.dart' show BehaviorSubject;
import 'package:watcher/watcher.dart' show FileWatcher, WatchEvent;

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

class ConfigurationRepository extends ChangeNotifier {
  factory ConfigurationRepository() => instance;

  ConfigurationRepository._();

  static final ConfigurationRepository instance = ConfigurationRepository._();

  String? _wallpaper;
  String? _launcher;
  String? _terminal;
  String? _logout;

  String? get wallpaper => _wallpaper;
  String? get launcher => _launcher;
  String? get terminal => _terminal;
  String? get logout => _logout;

  final _status = BehaviorSubject<void>();
  BehaviorSubject<void> get status => _status;

  StreamSubscription<WatchEvent>? _configurationSubscription;

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
    _logout = getOption(
      ['Commands', 'commands', 'Button', 'button'],
      ['logout', 'system'],
    );

    notifyListeners();
    _status.add(null);
  }

  @override
  void dispose() {
    unawaited(_configurationSubscription?.cancel());
    unawaited(_status.close());
    super.dispose();
  }
}
