import 'package:compositor_dart/src/data/models/surface.dart' show Surface;
import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'surface_request_activate_event.g.dart';

/// Represents a request by a client or surface to be activated (focused).
@JsonSerializable()
class SurfaceRequestActivateEvent extends Equatable {
  /// Creates a [SurfaceRequestActivateEvent] descriptor.
  const new({required this.handle, String? token, String? appId, this.surface})
    : token = token ?? '',
      appId = appId ?? '';

  /// Creates a [SurfaceRequestActivateEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$SurfaceRequestActivateEventFromJson(json);

  /// The unique handle of the surface requesting activation.
  final int handle;

  /// The activation token used for xdg-activation validation.
  final String token;

  /// The application identifier requesting activation.
  @JsonKey(name: 'app_id')
  final String appId;

  /// The resolved [Surface] entity associated with this activation request.
  @JsonKey(includeFromJson: false, includeToJson: false)
  final Surface? surface;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$SurfaceRequestActivateEventToJson(this);

  /// Creates a copy of this event with optional field replacements.
  SurfaceRequestActivateEvent copyWith({
    int? handle,
    String? token,
    String? appId,
    Surface? surface,
  }) {
    return SurfaceRequestActivateEvent(
      handle: handle ?? this.handle,
      token: token ?? this.token,
      appId: appId ?? this.appId,
      surface: surface ?? this.surface,
    );
  }

  @override
  List<Object?> get props => [handle, token, appId, surface];
}
