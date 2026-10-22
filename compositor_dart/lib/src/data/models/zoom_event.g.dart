// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'zoom_event.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

ZoomScrollEvent _$ZoomScrollEventFromJson(Map json) => ZoomScrollEvent(
  delta: (json['delta'] as num?)?.toDouble(),
  x: (json['x'] as num?)?.toDouble(),
  y: (json['y'] as num?)?.toDouble(),
);

Map<String, dynamic> _$ZoomScrollEventToJson(ZoomScrollEvent instance) =>
    <String, dynamic>{
      'delta': instance.delta,
      'x': instance.x,
      'y': instance.y,
    };

ZoomKeyEvent _$ZoomKeyEventFromJson(Map json) => ZoomKeyEvent(
  action: (json['action'] as num?)?.toInt(),
  x: (json['x'] as num?)?.toDouble(),
  y: (json['y'] as num?)?.toDouble(),
);

Map<String, dynamic> _$ZoomKeyEventToJson(ZoomKeyEvent instance) =>
    <String, dynamic>{
      'action': instance.action,
      'x': instance.x,
      'y': instance.y,
    };
