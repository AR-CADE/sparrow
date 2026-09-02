// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'surface.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

Surface _$SurfaceFromJson(Map<String, dynamic> json) => Surface(
  handle: (json['handle'] as num).toInt(),
  pid: (json['client_pid'] as num).toInt(),
  gid: (json['client_gid'] as num).toInt(),
  uid: (json['client_uid'] as num).toInt(),
  textureId: (json['texture_id'] as num?)?.toInt(),
  title: json['title'] as String?,
  appId: json['app_id'] as String?,
  width: (json['width'] as num?)?.toInt(),
  height: (json['height'] as num?)?.toInt(),
  bufferWidth: (json['buffer_width'] as num?)?.toInt(),
  bufferHeight: (json['buffer_height'] as num?)?.toInt(),
  maximized: (json['maximized'] as num?)?.toInt(),
  activated: (json['activated'] as num?)?.toInt(),
  geoX: (json['geo_x'] as num?)?.toInt(),
  geoY: (json['geo_y'] as num?)?.toInt(),
  usesCsd: (json['uses_csd'] as num?)?.toInt(),
  outputId: (json['output_id'] as num?)?.toInt(),
  outputScale: (json['output_scale'] as num?)?.toDouble(),
  minWidth: (json['min_width'] as num?)?.toInt(),
  maxWidth: (json['max_width'] as num?)?.toInt(),
  minHeight: (json['min_height'] as num?)?.toInt(),
  maxHeight: (json['max_height'] as num?)?.toInt(),
);

Map<String, dynamic> _$SurfaceToJson(Surface instance) => <String, dynamic>{
  'handle': instance.handle,
  'texture_id': instance.textureId,
  'client_pid': instance.pid,
  'client_gid': instance.gid,
  'client_uid': instance.uid,
  'title': ?instance.title,
  'app_id': ?instance.appId,
  'width': ?instance.width,
  'height': ?instance.height,
  'maximized': ?instance.maximized,
  'activated': ?instance.activated,
  'geo_x': ?instance.geoX,
  'geo_y': ?instance.geoY,
  'buffer_width': ?instance.bufferWidth,
  'buffer_height': ?instance.bufferHeight,
  'uses_csd': ?instance.usesCsd,
  'output_id': ?instance.outputId,
  'output_scale': ?instance.outputScale,
  'min_width': ?instance.minWidth,
  'max_width': ?instance.maxWidth,
  'min_height': ?instance.minHeight,
  'max_height': ?instance.maxHeight,
};
