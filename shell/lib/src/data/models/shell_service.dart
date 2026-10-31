import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'shell_service.g.dart';

/// Represents a managed startup or background service configured in
/// `sparrow.ini`.
@JsonSerializable()
class ShellService extends Equatable {
  /// Creates a new [ShellService].
  const new({
    required this.id,
    required this.command,
    this.restartable = false,
  });

  /// Creates a [ShellService] from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$ShellServiceFromJson(json);

  /// Unique identifier of the service.
  final String id;

  /// Shell command to execute.
  final String command;

  /// Whether the process should be restarted automatically upon exit.
  final bool restartable;

  /// Converts this [ShellService] to a JSON map.
  Map<String, dynamic> toJson() => _$ShellServiceToJson(this);

  @override
  List<Object?> get props => [id, command, restartable];
}
