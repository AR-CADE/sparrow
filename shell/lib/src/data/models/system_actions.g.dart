// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'system_actions.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

SystemActions _$SystemActionsFromJson(Map<String, dynamic> json) =>
    SystemActions(
      logout: json['logout'] == null
          ? null
          : SystemAction.fromJson(json['logout'] as Map<String, dynamic>),
      hibernate: json['hibernate'] == null
          ? null
          : SystemAction.fromJson(json['hibernate'] as Map<String, dynamic>),
      suspend: json['suspend'] == null
          ? null
          : SystemAction.fromJson(json['suspend'] as Map<String, dynamic>),
      poweroff: json['poweroff'] == null
          ? null
          : SystemAction.fromJson(json['poweroff'] as Map<String, dynamic>),
      reboot: json['reboot'] == null
          ? null
          : SystemAction.fromJson(json['reboot'] as Map<String, dynamic>),
    );

Map<String, dynamic> _$SystemActionsToJson(SystemActions instance) =>
    <String, dynamic>{
      'logout': instance.logout?.toJson(),
      'hibernate': instance.hibernate?.toJson(),
      'suspend': instance.suspend?.toJson(),
      'poweroff': instance.poweroff?.toJson(),
      'reboot': instance.reboot?.toJson(),
    };
