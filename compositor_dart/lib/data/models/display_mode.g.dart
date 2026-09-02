// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'display_mode.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

DisplayMode _$DisplayModeFromJson(Map<String, dynamic> json) => DisplayMode(
  width: (json['width'] as num).toInt(),
  height: (json['height'] as num).toInt(),
  refresh: (json['refresh'] as num).toInt(),
);

Map<String, dynamic> _$DisplayModeToJson(DisplayMode instance) =>
    <String, dynamic>{
      'width': instance.width,
      'height': instance.height,
      'refresh': instance.refresh,
    };
