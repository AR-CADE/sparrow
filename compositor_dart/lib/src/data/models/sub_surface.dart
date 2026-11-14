import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'sub_surface.g.dart';

/// Represents a Wayland sub-surface nested within a parent surface.
@JsonSerializable()
class SubSurface extends Equatable {
  /// Creates a [SubSurface] descriptor.
  const new({
    required this.handle,
    required this.textureId,
    required this.parentHandle,
    this.x = 0,
    this.y = 0,
    this.width = 0,
    this.height = 0,
    this.bufferWidth = 0,
    this.bufferHeight = 0,
  });

  /// Creates a [SubSurface] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$SubSurfaceFromJson(json);

  /// Converts this sub-surface to a JSON map.
  Map<String, dynamic> toJson() => _$SubSurfaceToJson(this);

  /// The unique handle of this sub-surface.
  final int handle;

  /// The GPU texture identifier assigned to this sub-surface.
  @JsonKey(name: 'texture_id')
  final int textureId;

  /// The handle of the parent surface.
  @JsonKey(name: 'parent_handle')
  final int parentHandle;

  /// The horizontal position relative to the parent surface.
  final int? x;

  /// The vertical position relative to the parent surface.
  final int? y;

  /// The width of this sub-surface in surface coordinates.
  final int? width;

  /// The height of this sub-surface in surface coordinates.
  final int? height;

  /// The pixel width of the underlying buffer.
  @JsonKey(name: 'buffer_width')
  final int? bufferWidth;

  /// The pixel height of the underlying buffer.
  @JsonKey(name: 'buffer_height')
  final int? bufferHeight;

  /// Creates a copy of this sub-surface with optional field replacements.
  SubSurface copyWith({
    int? handle,
    int? textureId,
    int? parentHandle,
    int? width,
    int? height,
    int? x,
    int? y,
    int? bufferWidth,
    int? bufferHeight,
  }) {
    return SubSurface(
      handle: handle ?? this.handle,
      textureId: textureId ?? this.textureId,
      parentHandle: parentHandle ?? this.parentHandle,
      width: width ?? this.width,
      height: height ?? this.height,
      x: x ?? this.x,
      y: y ?? this.y,
      bufferWidth: bufferWidth ?? this.bufferWidth,
      bufferHeight: bufferHeight ?? this.bufferHeight,
    );
  }

  @override
  List<Object?> get props => [
    handle,
    textureId,
    parentHandle,
    x,
    y,
    width,
    height,
    bufferWidth,
    bufferHeight,
  ];
}
