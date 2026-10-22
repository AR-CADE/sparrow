import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show $enumDecodeNullable, JsonSerializable;
import 'package:material_ui/material_ui.dart' show FilterQuality;
import 'package:shell/src/data/models/system_actions.dart';

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

  /// Converts this [ShellConfiguration] to a JSON map.
  Map<String, dynamic> toJson() => _$ShellConfigurationToJson(this);

  /// Returns a copy of this [ShellConfiguration] with updated fields.
  ShellConfiguration copyWith({
    String? wallpaper,
    String? launcher,
    String? terminal,
    SystemActions? systemActions,
    FilterQuality? filterQuality,
  }) {
    return ShellConfiguration(
      wallpaper: wallpaper ?? this.wallpaper,
      launcher: launcher ?? this.launcher,
      terminal: terminal ?? this.terminal,
      systemActions: systemActions ?? this.systemActions,
      filterQuality: filterQuality ?? this.filterQuality,
    );
  }

  @override
  List<Object?> get props => [
    wallpaper,
    launcher,
    terminal,
    systemActions,
    filterQuality,
  ];
}
