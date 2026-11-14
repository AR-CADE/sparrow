// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'layer_surface.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

LayerSurface _$LayerSurfaceFromJson(Map json) => LayerSurface(
  handle: (json['handle'] as num).toInt(),
  textureId: (json['texture_id'] as num).toInt(),
  layer: (json['layer'] as num?)?.toInt() ?? 0,
  anchor: (json['anchor'] as num?)?.toInt() ?? 0,
  x: (json['x'] as num?)?.toInt() ?? 0,
  y: (json['y'] as num?)?.toInt() ?? 0,
  width: (json['width'] as num?)?.toInt() ?? 0,
  height: (json['height'] as num?)?.toInt() ?? 0,
  bufferWidth: (json['buffer_width'] as num?)?.toInt() ?? 0,
  bufferHeight: (json['buffer_height'] as num?)?.toInt() ?? 0,
  marginTop: (json['margin_top'] as num?)?.toInt() ?? 0,
  marginBottom: (json['margin_bottom'] as num?)?.toInt() ?? 0,
  marginLeft: (json['margin_left'] as num?)?.toInt() ?? 0,
  marginRight: (json['margin_right'] as num?)?.toInt() ?? 0,
  exclusiveZone: (json['exclusive_zone'] as num?)?.toInt() ?? 0,
  keyboardInteractive: json['keyboard_interactive'] as bool? ?? false,
  outputId: (json['output_id'] as num?)?.toInt() ?? 0,
  scope: json['scope'] as String? ?? '',
);

Map<String, dynamic> _$LayerSurfaceToJson(LayerSurface instance) =>
    <String, dynamic>{
      'handle': instance.handle,
      'texture_id': instance.textureId,
      'layer': instance.layer,
      'anchor': instance.anchor,
      'x': instance.x,
      'y': instance.y,
      'width': instance.width,
      'height': instance.height,
      'buffer_width': instance.bufferWidth,
      'buffer_height': instance.bufferHeight,
      'margin_top': instance.marginTop,
      'margin_bottom': instance.marginBottom,
      'margin_left': instance.marginLeft,
      'margin_right': instance.marginRight,
      'exclusive_zone': instance.exclusiveZone,
      'keyboard_interactive': instance.keyboardInteractive,
      'output_id': instance.outputId,
      'scope': instance.scope,
    };
