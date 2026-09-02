// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'popup.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

Popup _$PopupFromJson(Map<String, dynamic> json) => Popup(
  handle: (json['handle'] as num).toInt(),
  textureId: (json['texture_id'] as num).toInt(),
  parentHandle: (json['parent_handle'] as num).toInt(),
  x: (json['x'] as num?)?.toInt() ?? 0,
  y: (json['y'] as num?)?.toInt() ?? 0,
  width: (json['width'] as num?)?.toInt() ?? 0,
  height: (json['height'] as num?)?.toInt() ?? 0,
  outputId: (json['output_id'] as num?)?.toInt() ?? 0,
  outputScale: (json['output_scale'] as num?)?.toDouble() ?? 1.0,
);

Map<String, dynamic> _$PopupToJson(Popup instance) => <String, dynamic>{
  'handle': instance.handle,
  'texture_id': instance.textureId,
  'parent_handle': instance.parentHandle,
  'x': instance.x,
  'y': instance.y,
  'width': instance.width,
  'height': instance.height,
  'output_id': instance.outputId,
  'output_scale': instance.outputScale,
};
