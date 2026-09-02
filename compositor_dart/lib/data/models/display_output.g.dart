// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'display_output.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

DisplayOutput _$DisplayOutputFromJson(Map<String, dynamic> json) =>
    DisplayOutput(
      id: (json['id'] as num).toInt(),
      name: json['name'] as String?,
      make: json['make'] as String?,
      model: json['model'] as String?,
      x: (json['x'] as num?)?.toInt(),
      y: (json['y'] as num?)?.toInt(),
      width: (json['width'] as num?)?.toInt() ?? 0,
      height: (json['height'] as num?)?.toInt() ?? 0,
      refreshRate: (json['refresh'] as num?)?.toInt() ?? 60000,
      scale: (json['scale'] as num?)?.toDouble() ?? 1.0,
      transform: (json['transform'] as num?)?.toInt() ?? 0,
      availableModes:
          (json['available_modes'] as List<dynamic>?)
              ?.map((e) => DisplayMode.fromJson(e as Map<String, dynamic>))
              .toList() ??
          const [],
      isPrimary: json['is_primary'] as bool? ?? false,
    );

Map<String, dynamic> _$DisplayOutputToJson(
  DisplayOutput instance,
) => <String, dynamic>{
  'id': instance.id,
  'name': instance.name,
  'make': instance.make,
  'model': instance.model,
  'x': instance.x,
  'y': instance.y,
  'width': instance.width,
  'height': instance.height,
  'refresh': instance.refreshRate,
  'scale': instance.scale,
  'transform': instance.transform,
  'available_modes': instance.availableModes.map((e) => e.toJson()).toList(),
  'is_primary': instance.isPrimary,
};
