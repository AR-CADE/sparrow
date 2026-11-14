import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;
import 'package:shell/src/data/models/system_action.dart';

part 'system_actions.g.dart';

/// Represents the collection of configured system actions.
@JsonSerializable(explicitToJson: true)
class SystemActions extends Equatable {
  /// Creates a new [SystemActions].
  const new({
    this.logout,
    this.hibernate,
    this.suspend,
    this.poweroff,
    this.reboot,
  });

  /// Creates a [SystemActions] from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$SystemActionsFromJson(json);

  /// Log out action.
  final SystemAction? logout;

  /// Hibernate action.
  final SystemAction? hibernate;

  /// Suspend action.
  final SystemAction? suspend;

  /// Power off action.
  final SystemAction? poweroff;

  /// Reboot action.
  final SystemAction? reboot;

  /// Converts this [SystemActions] to a JSON map.
  Map<String, dynamic> toJson() => _$SystemActionsToJson(this);

  /// Returns a copy of this [SystemActions] with updated fields.
  SystemActions copyWith({
    SystemAction? logout,
    SystemAction? hibernate,
    SystemAction? suspend,
    SystemAction? poweroff,
    SystemAction? reboot,
  }) {
    return SystemActions(
      logout: logout ?? this.logout,
      hibernate: hibernate ?? this.hibernate,
      suspend: suspend ?? this.suspend,
      poweroff: poweroff ?? this.poweroff,
      reboot: reboot ?? this.reboot,
    );
  }

  @override
  List<Object?> get props => [logout, hibernate, suspend, poweroff, reboot];
}
