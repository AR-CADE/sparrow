import 'dart:async'
    show Future, StreamController, StreamSubscription, unawaited;
import 'dart:io' show File, Platform;

import 'package:collection/collection.dart';
import 'package:compositor_dart/compositor_dart.dart' show CompositorRepository;
import 'package:flutter/foundation.dart' show ChangeNotifier;
import 'package:ini/ini.dart' show Config;
import 'package:material_ui/material_ui.dart' show FilterQuality, Icons;
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

FilterQuality? _filterQualityFromString(String? name) {
  if (name == null) {
    return null;
  }
  return FilterQuality.values.firstWhereOrNull((value) => value.name == name);
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
  FilterQuality? _filterQuality;
  List<String> _secureAppIds = const [];
  List<String> _secureOutputNames = const [];
  List<ShellService> _services = const [];
  bool _hasStartedServices = false;

  /// Configured desktop wallpaper path.
  String? get wallpaper => _wallpaper;

  /// Configured app launcher executable command.
  String? get launcher => _launcher;

  /// Configured terminal executable command.
  String? get terminal => _terminal;

  /// Configured system actions.
  SystemActions? get systemActions => _systemActions;

  /// Configured quality of textures.
  FilterQuality get filterQuality => _filterQuality ?? FilterQuality.none;

  /// Configured application IDs or title patterns for `[Secure-Content]`.
  List<String> get secureAppIds => _secureAppIds;

  /// Configured output names for `[Secure-Content]`.
  List<String> get secureOutputNames => _secureOutputNames;

  /// Configured startup and background services managed by the supervisor.
  List<ShellService> get services => _services;

  /// Returns the current immutable [ShellConfiguration].
  ShellConfiguration get currentConfiguration => ShellConfiguration(
    wallpaper: _wallpaper,
    launcher: _launcher,
    terminal: _terminal,
    systemActions: _systemActions,
    filterQuality: filterQuality,
    secureAppIds: _secureAppIds,
    secureOutputNames: _secureOutputNames,
    services: _services,
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
launcher = [YOUR_LAUNCHER_APPLICATION]
terminal = [YOUR_TERMINAL_APPLICATION] (ex. alacritty or foot or kitty or weston-terminal or xterm)
logout = [YOUR_LOGOUT_APPLICATION] (ex. wayland-logout)
suspend = [YOUR_SUSPEND_APPLICATION]
reboot = [YOUR_REBOOT_APPLICATION]
poweroff = [YOUR_POWEROFF_APPLICATION]

[Quirks]
filterQuality = [FILTER_QUALITY] (ex. none, low, medium or high )

[Secure-Content]
# app_id = rpcs3

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
      await parseConfig(config, rawLines: lines);
    } on Object catch (_) {
      // Ignore parse or transient lock errors
    }
  }

  static const _secureSectionNames = <String>{
    'secure-content',
    'secure_content',
    'securecontent',
    'sensitive-content',
    'sensitive_content',
    'sensitivecontent',
  };

  static void _addSecureAppPattern(String rawValue, Set<String> target) {
    var cleaned = rawValue.trim();
    if (cleaned.isEmpty) {
      return;
    }
    if ((cleaned.startsWith('"') && cleaned.endsWith('"')) ||
        (cleaned.startsWith("'") && cleaned.endsWith("'"))) {
      cleaned = cleaned.substring(1, cleaned.length - 1).trim();
    }
    for (final part in cleaned.split(',')) {
      var token = part.trim();
      if ((token.startsWith('"') && token.endsWith('"')) ||
          (token.startsWith("'") && token.endsWith("'"))) {
        token = token.substring(1, token.length - 1).trim();
      }
      if (token.isNotEmpty && !token.startsWith('[')) {
        target.add(token);
      }
    }
  }

  static bool _isOutputKey(String key) {
    final k = key.trim().toLowerCase();
    return k == 'output_name' || k == 'output' || k == 'outputs';
  }

  static List<String> _extractSecureAppIds(
    Config config, {
    List<String>? rawLines,
  }) {
    final result = <String>{};

    if (rawLines != null) {
      var inSecureSection = false;
      for (final rawLine in rawLines) {
        final trimmed = rawLine.trim();
        if (trimmed.isEmpty ||
            trimmed.startsWith('#') ||
            trimmed.startsWith(';')) {
          continue;
        }
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
          final section = trimmed
              .substring(1, trimmed.length - 1)
              .trim()
              .toLowerCase();
          inSecureSection = _secureSectionNames.contains(section);
          continue;
        }
        if (!inSecureSection) {
          continue;
        }
        final eqIndex = trimmed.indexOf('=');
        if (eqIndex <= 0) {
          continue;
        }
        final keyPart = trimmed.substring(0, eqIndex);
        if (_isOutputKey(keyPart)) {
          continue;
        }
        final valuePart = trimmed.substring(eqIndex + 1).trim();
        _addSecureAppPattern(valuePart, result);
      }
    }

    for (final section in config.sections()) {
      if (_secureSectionNames.contains(section.trim().toLowerCase())) {
        final items = config.items(section);
        if (items != null) {
          for (final item in items) {
            if (item.length >= 2 && item[0] != null && item[1] != null) {
              if (!_isOutputKey(item[0]!)) {
                _addSecureAppPattern(item[1]!, result);
              }
            } else if (item.length >= 2 && item[1] != null) {
              _addSecureAppPattern(item[1]!, result);
            }
          }
        }
      }
    }

    return List<String>.unmodifiable(result);
  }

  static List<String> _extractSecureOutputNames(
    Config config, {
    List<String>? rawLines,
  }) {
    final result = <String>{};

    if (rawLines != null) {
      var inSecureSection = false;
      for (final rawLine in rawLines) {
        final trimmed = rawLine.trim();
        if (trimmed.isEmpty ||
            trimmed.startsWith('#') ||
            trimmed.startsWith(';')) {
          continue;
        }
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
          final section = trimmed
              .substring(1, trimmed.length - 1)
              .trim()
              .toLowerCase();
          inSecureSection = _secureSectionNames.contains(section);
          continue;
        }
        if (!inSecureSection) {
          continue;
        }
        final eqIndex = trimmed.indexOf('=');
        if (eqIndex <= 0) {
          continue;
        }
        final keyPart = trimmed.substring(0, eqIndex);
        if (_isOutputKey(keyPart)) {
          final valuePart = trimmed.substring(eqIndex + 1).trim();
          _addSecureAppPattern(valuePart, result);
        }
      }
    }

    for (final section in config.sections()) {
      if (_secureSectionNames.contains(section.trim().toLowerCase())) {
        final items = config.items(section);
        if (items != null) {
          for (final item in items) {
            if (item.length >= 2 && item[0] != null && item[1] != null) {
              if (_isOutputKey(item[0]!)) {
                _addSecureAppPattern(item[1]!, result);
              }
            }
          }
        }
      }
    }

    return List<String>.unmodifiable(result);
  }

  static const _serviceSectionNames = <String>{
    'services',
    'service',
    'init',
    'startup',
  };

  static String _stripQuotes(String value) {
    var v = value.trim();
    if ((v.startsWith('"') && v.endsWith('"')) ||
        (v.startsWith("'") && v.endsWith("'"))) {
      if (v.length >= 2) {
        v = v.substring(1, v.length - 1).trim();
      }
    }
    return v;
  }

  static List<ShellService> _extractServices(
    Config config, {
    List<String>? rawLines,
  }) {
    final services = <String, ShellService>{};
    var execCounter = 1;
    var execAlwaysCounter = 1;

    void processEntry(String rawKey, String rawValue) {
      final key = rawKey.trim();
      final val = _stripQuotes(rawValue.trim());
      if (key.isEmpty || val.isEmpty) {
        return;
      }

      var restartable = false;
      var command = val;
      final valLower = val.toLowerCase();

      if (valLower.startsWith('restartable:')) {
        restartable = true;
        command = val.substring(12).trim();
      } else if (valLower.startsWith('respawn:')) {
        restartable = true;
        command = val.substring(8).trim();
      } else if (valLower.startsWith('normal:')) {
        restartable = false;
        command = val.substring(7).trim();
      } else if (valLower.startsWith('once:')) {
        restartable = false;
        command = val.substring(5).trim();
      } else {
        final keyLower = key.toLowerCase();
        if (keyLower == 'exec_always' ||
            keyLower.startsWith('exec_always_') ||
            keyLower.startsWith('restartable_')) {
          restartable = true;
        } else if (keyLower == 'exec' || keyLower.startsWith('exec_')) {
          restartable = false;
        }
      }

      command = _stripQuotes(command);
      if (command.isEmpty) {
        return;
      }

      var id = key;
      final keyLower = key.toLowerCase();
      if (keyLower == 'exec') {
        id = 'exec_${execCounter++}';
      } else if (keyLower == 'exec_always') {
        id = 'exec_always_${execAlwaysCounter++}';
      }

      services[id] = ShellService(
        id: id,
        command: command,
        restartable: restartable,
      );
    }

    if (rawLines != null) {
      var inServiceSection = false;
      for (final rawLine in rawLines) {
        final trimmed = rawLine.trim();
        if (trimmed.isEmpty ||
            trimmed.startsWith('#') ||
            trimmed.startsWith(';')) {
          continue;
        }
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
          final section = trimmed
              .substring(1, trimmed.length - 1)
              .trim()
              .toLowerCase();
          inServiceSection = _serviceSectionNames.contains(section);
          continue;
        }
        if (!inServiceSection) {
          continue;
        }
        final eqIndex = trimmed.indexOf('=');
        if (eqIndex <= 0) {
          continue;
        }
        final keyPart = trimmed.substring(0, eqIndex);
        final valuePart = trimmed.substring(eqIndex + 1);
        processEntry(keyPart, valuePart);
      }
    } else {
      for (final section in config.sections()) {
        if (_serviceSectionNames.contains(section.trim().toLowerCase())) {
          final items = config.items(section);
          if (items != null) {
            for (final item in items) {
              if (item.length >= 2 && item[0] != null && item[1] != null) {
                processEntry(item[0]!, item[1]!);
              }
            }
          }
        }
      }
    }

    return List<ShellService>.unmodifiable(services.values);
  }

  /// Parses configuration from an ini [Config] object.
  Future<void> parseConfig(Config config, {List<String>? rawLines}) async {
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

    _filterQuality = _filterQualityFromString(
      getOption(['Quirks', 'quirks'], ['filterQuality']),
    );

    _secureAppIds = _extractSecureAppIds(config, rawLines: rawLines);
    _secureOutputNames = _extractSecureOutputNames(config, rawLines: rawLines);

    // NOTE: [Secure-Content] MUST be sent to the compositor BEFORE [Services]
    try {
      await CompositorRepository().platform.setSensitiveAppIds(
        currentConfiguration.secureContents,
      );
      await CompositorRepository().platform.setSensitiveOutputNames(
        _secureOutputNames,
      );
    } on Object catch (_) {
      // Ignore platform channel errors in pure unit test environments
    }

    _services = _extractServices(config, rawLines: rawLines);
    try {
      for (final service in _services) {
        await CompositorRepository().platform.addOrUpdateService(
          service.id,
          service.command,
          restartable: service.restartable,
        );
      }

      if (!_hasStartedServices) {
        _hasStartedServices = true;
        await CompositorRepository().platform.startAllServices();
      }
    } on Object catch (_) {
      // Ignore platform channel errors in pure unit test environments
    }

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
