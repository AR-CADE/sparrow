// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'gesture_swipe_event.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

GestureSwipeBeginEvent _$GestureSwipeBeginEventFromJson(Map json) =>
    GestureSwipeBeginEvent(
      fingers: (json['fingers'] as num?)?.toInt(),
      timeMsec: (json['time_msec'] as num?)?.toInt(),
    );

Map<String, dynamic> _$GestureSwipeBeginEventToJson(
  GestureSwipeBeginEvent instance,
) => <String, dynamic>{
  'fingers': instance.fingers,
  'time_msec': instance.timeMsec,
};

GestureSwipeUpdateEvent _$GestureSwipeUpdateEventFromJson(Map json) =>
    GestureSwipeUpdateEvent(
      dx: (json['dx'] as num?)?.toDouble(),
      dy: (json['dy'] as num?)?.toDouble(),
      timeMsec: (json['time_msec'] as num?)?.toInt(),
    );

Map<String, dynamic> _$GestureSwipeUpdateEventToJson(
  GestureSwipeUpdateEvent instance,
) => <String, dynamic>{
  'dx': instance.dx,
  'dy': instance.dy,
  'time_msec': instance.timeMsec,
};

GestureSwipeEndEvent _$GestureSwipeEndEventFromJson(Map json) =>
    GestureSwipeEndEvent(
      cancelled: _readCancelled(json, 'cancelled') as bool?,
      timeMsec: (json['time_msec'] as num?)?.toInt(),
    );

Map<String, dynamic> _$GestureSwipeEndEventToJson(
  GestureSwipeEndEvent instance,
) => <String, dynamic>{
  'cancelled': instance.cancelled,
  'time_msec': instance.timeMsec,
};
