import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show $enumDecodeNullable, JsonSerializable;
import 'package:material_ui/material_ui.dart' show FilterQuality;
import 'package:shell/src/data/models/shell_service.dart';
import 'package:shell/src/data/models/system_actions.dart';

export 'package:shell/src/data/models/shell_service.dart';
export 'package:shell/src/data/models/system_action.dart';
export 'package:shell/src/data/models/system_actions.dart';

part 'shell_configuration.g.dart';

/// Immutable configuration model for the shell.
@JsonSerializable(explicitToJson: true)
class ShellConfiguration extends Equatable {
  /// Creates a new [ShellConfiguration].
  const new({
    this.wallpaper,
    this.launcher,
    this.terminal,
    this.systemActions,
    this.filterQuality,
    this.secureAppIds = const [],
    this.secureOutputNames = const [],
    this.services = const [],
  });

  /// Creates a [ShellConfiguration] from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$ShellConfigurationFromJson(json);

  /// Wallpaper path or URI.
  final String? wallpaper;

  /// Command to launch the application launcher grid.
  final String? launcher;

  /// Command to launch the terminal.
  final String? terminal;

  /// Actions configured for the power menu / system widget.
  final SystemActions? systemActions;

  /// Configured quality of textures.
  final FilterQuality? filterQuality;

  /// Application IDs or title patterns forced into secure-content protection.
  final List<String> secureAppIds;

  /// Output display names forced into secure-content protection.
  final List<String> secureOutputNames;

  /// Startup and background services managed by the supervisor.
  final List<ShellService> services;

  /// Combined list of secure content identifiers, merging application IDs
  /// ([secureAppIds]) and formatted output display names ([secureOutputNames]
  /// prefixed with `output:`).
  List<String> get secureContents => [
    ...secureAppIds,
    ...secureOutputNames.map((o) => 'output:$o'),
  ];

  /// Converts this [ShellConfiguration] to a JSON map.
  Map<String, dynamic> toJson() => _$ShellConfigurationToJson(this);

  /// Returns a copy of this [ShellConfiguration] with updated fields.
  ShellConfiguration copyWith({
    String? wallpaper,
    String? launcher,
    String? terminal,
    SystemActions? systemActions,
    FilterQuality? filterQuality,
    List<String>? secureAppIds,
    List<String>? secureOutputNames,
    List<ShellService>? services,
  }) {
    return ShellConfiguration(
      wallpaper: wallpaper ?? this.wallpaper,
      launcher: launcher ?? this.launcher,
      terminal: terminal ?? this.terminal,
      systemActions: systemActions ?? this.systemActions,
      filterQuality: filterQuality ?? this.filterQuality,
      secureAppIds: secureAppIds ?? this.secureAppIds,
      secureOutputNames: secureOutputNames ?? this.secureOutputNames,
      services: services ?? this.services,
    );
  }

  @override
  List<Object?> get props => [
    wallpaper,
    launcher,
    terminal,
    systemActions,
    filterQuality,
    secureAppIds,
    secureOutputNames,
    services,
  ];
}
