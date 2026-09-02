// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'compositor_event.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

CompositorEvent _$CompositorEventFromJson(Map<String, dynamic> json) =>
    CompositorEvent(
      type: $enumDecode(_$CompositorEventTypeEnumMap, json['type']),
      event: json['event'],
    );

Map<String, dynamic> _$CompositorEventToJson(CompositorEvent instance) =>
    <String, dynamic>{
      'type': _$CompositorEventTypeEnumMap[instance.type]!,
      'event': ?instance.event,
    };

const _$CompositorEventTypeEnumMap = {
  CompositorEventType.surfaceMap: 'surfaceMap',
  CompositorEventType.surfaceUnMap: 'surfaceUnMap',
  CompositorEventType.surfaceTitleChange: 'surfaceTitleChange',
  CompositorEventType.surfaceGeometryChange: 'surfaceGeometryChange',
  CompositorEventType.surfaceDecorationChange: 'surfaceDecorationChange',
  CompositorEventType.surfacePositionChange: 'surfacePositionChange',
  CompositorEventType.surfaceMinimizeRequest: 'surfaceMinimizeRequest',
  CompositorEventType.surfaceRequestMaximize: 'surfaceRequestMaximize',
  CompositorEventType.surfaceresizeRequest: 'surfaceresizeRequest',
  CompositorEventType.surfaceGrabEnd: 'surfaceGrabEnd',
  CompositorEventType.subSurfaceMap: 'subSurfaceMap',
  CompositorEventType.subSurfaceUnMap: 'subSurfaceUnMap',
  CompositorEventType.subsurfacePositionChange: 'subsurfacePositionChange',
  CompositorEventType.popupMap: 'popupMap',
  CompositorEventType.popupUnMap: 'popupUnMap',
  CompositorEventType.outputAdded: 'outputAdded',
  CompositorEventType.outputRemoved: 'outputRemoved',
  CompositorEventType.outputChanged: 'outputChanged',
  CompositorEventType.gestureSwipeBegin: 'gestureSwipeBegin',
  CompositorEventType.gestureSwipeUpdate: 'gestureSwipeUpdate',
  CompositorEventType.gestureSwipeEnd: 'gestureSwipeEnd',
  CompositorEventType.surfaceRequestActivate: 'surfaceRequestActivate',
};
