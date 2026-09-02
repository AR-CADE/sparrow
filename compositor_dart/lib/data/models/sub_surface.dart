import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'sub_surface.g.dart';

@JsonSerializable()
class SubSurface extends Equatable {
  const SubSurface({
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

  factory SubSurface.fromJson(Map<String, dynamic> json) =>
      _$SubSurfaceFromJson(json);

  Map<String, dynamic> toJson() => _$SubSurfaceToJson(this);

  final int handle;
  @JsonKey(name: 'texture_id')
  final int textureId;
  @JsonKey(name: 'parent_handle')
  final int parentHandle;

  final int? x;
  final int? y;
  final int? width;
  final int? height;
  @JsonKey(name: 'buffer_width')
  final int? bufferWidth;
  @JsonKey(name: 'buffer_height')
  final int? bufferHeight;

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
