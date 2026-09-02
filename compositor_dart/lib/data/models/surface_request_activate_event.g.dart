// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'surface_request_activate_event.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

SurfaceRequestActivateEvent _$SurfaceRequestActivateEventFromJson(
  Map<String, dynamic> json,
) => SurfaceRequestActivateEvent(
  handle: (json['handle'] as num).toInt(),
  token: json['token'] as String?,
  appId: json['app_id'] as String?,
);

Map<String, dynamic> _$SurfaceRequestActivateEventToJson(
  SurfaceRequestActivateEvent instance,
) => <String, dynamic>{
  'handle': instance.handle,
  'token': instance.token,
  'app_id': instance.appId,
};
