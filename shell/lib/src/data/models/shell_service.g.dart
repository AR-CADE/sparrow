// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'shell_service.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

ShellService _$ShellServiceFromJson(Map<String, dynamic> json) => ShellService(
  id: json['id'] as String,
  command: json['command'] as String,
  restartable: json['restartable'] as bool? ?? false,
);

Map<String, dynamic> _$ShellServiceToJson(ShellService instance) =>
    <String, dynamic>{
      'id': instance.id,
      'command': instance.command,
      'restartable': instance.restartable,
    };
