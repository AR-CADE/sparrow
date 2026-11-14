// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'system_action.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

SystemAction _$SystemActionFromJson(Map<String, dynamic> json) => SystemAction(
  name: json['name'] as String?,
  command: json['command'] as String?,
  icon: const IconDataConverter().fromJson(json['icon']),
);

Map<String, dynamic> _$SystemActionToJson(SystemAction instance) =>
    <String, dynamic>{
      'name': instance.name,
      'command': instance.command,
      'icon': const IconDataConverter().toJson(instance.icon),
    };
