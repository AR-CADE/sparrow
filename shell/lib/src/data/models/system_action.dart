// Dynamic IconData is constructed from runtime JSON/INI configurations.
// ignore_for_file: non_const_argument_for_const_parameter

import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonConverter, JsonSerializable;
import 'package:material_ui/material_ui.dart' show IconData;

part 'system_action.g.dart';

/// JSON converter supporting [IconData] serialization as codePoint or Map.
class IconDataConverter implements JsonConverter<IconData?, Object?> {
  /// Creates a new [IconDataConverter].
  const new();

  @override
  IconData? fromJson(Object? json) {
    if (json == null) return null;
    if (json is int) {
      return IconData(json, fontFamily: 'MaterialIcons');
    }
    if (json is Map<String, dynamic>) {
      final codePoint = json['codePoint'] as int? ?? json['code_point'] as int?;
      if (codePoint != null) {
        return IconData(
          codePoint,
          fontFamily: json['fontFamily'] as String? ?? 'MaterialIcons',
          fontPackage: json['fontPackage'] as String?,
          matchTextDirection: json['matchTextDirection'] as bool? ?? false,
        );
      }
    }
    return null;
  }

  @override
  Object? toJson(IconData? object) => object?.codePoint;
}

/// Represents an individual system action (e.g. reboot, suspend).
@JsonSerializable()
class SystemAction extends Equatable {
  /// Creates a new [SystemAction].
  const new({this.name, this.command, this.icon});

  /// Creates a [SystemAction] from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$SystemActionFromJson(json);

  /// The human-readable name of the action.
  final String? name;

  /// The command string executed when triggered.
  final String? command;

  /// The icon displayed for this action.
  @IconDataConverter()
  final IconData? icon;

  /// Converts this [SystemAction] to a JSON map.
  Map<String, dynamic> toJson() => _$SystemActionToJson(this);

  /// Returns a copy of this [SystemAction] with updated fields.
  SystemAction copyWith({String? name, String? command, IconData? icon}) {
    return SystemAction(
      name: name ?? this.name,
      command: command ?? this.command,
      icon: icon ?? this.icon,
    );
  }

  @override
  List<Object?> get props => [name, command, icon];
}
