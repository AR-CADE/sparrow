// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'shell_configuration.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

ShellConfiguration _$ShellConfigurationFromJson(Map<String, dynamic> json) =>
    ShellConfiguration(
      wallpaper: json['wallpaper'] as String?,
      launcher: json['launcher'] as String?,
      terminal: json['terminal'] as String?,
      systemActions: json['systemActions'] == null
          ? null
          : SystemActions.fromJson(
              json['systemActions'] as Map<String, dynamic>,
            ),
    );

Map<String, dynamic> _$ShellConfigurationToJson(ShellConfiguration instance) =>
    <String, dynamic>{
      'wallpaper': instance.wallpaper,
      'launcher': instance.launcher,
      'terminal': instance.terminal,
      'systemActions': instance.systemActions?.toJson(),
    };
