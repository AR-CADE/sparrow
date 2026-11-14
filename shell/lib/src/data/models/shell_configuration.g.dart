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
      filterQuality: $enumDecodeNullable(
        _$FilterQualityEnumMap,
        json['filterQuality'],
      ),
      secureAppIds:
          (json['secureAppIds'] as List<dynamic>?)
              ?.map((e) => e as String)
              .toList() ??
          const [],
      secureOutputNames:
          (json['secureOutputNames'] as List<dynamic>?)
              ?.map((e) => e as String)
              .toList() ??
          const [],
      services:
          (json['services'] as List<dynamic>?)
              ?.map((e) => ShellService.fromJson(e as Map<String, dynamic>))
              .toList() ??
          const [],
    );

Map<String, dynamic> _$ShellConfigurationToJson(ShellConfiguration instance) =>
    <String, dynamic>{
      'wallpaper': instance.wallpaper,
      'launcher': instance.launcher,
      'terminal': instance.terminal,
      'systemActions': instance.systemActions?.toJson(),
      'filterQuality': _$FilterQualityEnumMap[instance.filterQuality],
      'secureAppIds': instance.secureAppIds,
      'secureOutputNames': instance.secureOutputNames,
      'services': instance.services.map((e) => e.toJson()).toList(),
    };

const _$FilterQualityEnumMap = {
  FilterQuality.none: 'none',
  FilterQuality.low: 'low',
  FilterQuality.medium: 'medium',
  FilterQuality.high: 'high',
};
