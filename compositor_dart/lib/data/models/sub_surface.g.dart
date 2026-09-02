// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'sub_surface.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

SubSurface _$SubSurfaceFromJson(Map<String, dynamic> json) => SubSurface(
  handle: (json['handle'] as num).toInt(),
  textureId: (json['texture_id'] as num).toInt(),
  parentHandle: (json['parent_handle'] as num).toInt(),
  x: (json['x'] as num?)?.toInt() ?? 0,
  y: (json['y'] as num?)?.toInt() ?? 0,
  width: (json['width'] as num?)?.toInt() ?? 0,
  height: (json['height'] as num?)?.toInt() ?? 0,
  bufferWidth: (json['buffer_width'] as num?)?.toInt() ?? 0,
  bufferHeight: (json['buffer_height'] as num?)?.toInt() ?? 0,
);

Map<String, dynamic> _$SubSurfaceToJson(SubSurface instance) =>
    <String, dynamic>{
      'handle': instance.handle,
      'texture_id': instance.textureId,
      'parent_handle': instance.parentHandle,
      'x': ?instance.x,
      'y': ?instance.y,
      'width': ?instance.width,
      'height': ?instance.height,
      'buffer_width': ?instance.bufferWidth,
      'buffer_height': ?instance.bufferHeight,
    };
