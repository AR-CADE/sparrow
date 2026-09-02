import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'surface_request_activate_event.g.dart';

@JsonSerializable()
class SurfaceRequestActivateEvent extends Equatable {
  const SurfaceRequestActivateEvent({
    required this.handle,
    String? token,
    String? appId,
    this.surface,
  }) : token = token ?? '',
       appId = appId ?? '';

  factory SurfaceRequestActivateEvent.fromJson(Map<String, dynamic> json) =>
      _$SurfaceRequestActivateEventFromJson(json);

  final int handle;
  final String token;
  @JsonKey(name: 'app_id')
  final String appId;

  @JsonKey(includeFromJson: false, includeToJson: false)
  final Surface? surface;

  Map<String, dynamic> toJson() => _$SurfaceRequestActivateEventToJson(this);

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
