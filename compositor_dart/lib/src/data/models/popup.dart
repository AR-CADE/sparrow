import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'popup.g.dart';

/// Represents a popup surface such as a context menu, dropdown, or tooltip.
@JsonSerializable()
class Popup extends Equatable {
  /// Creates a [Popup] surface descriptor.
  const new({
    required this.handle,
    required this.textureId,
    required this.parentHandle,
    int? x = 0,
    int? y = 0,
    int? width = 0,
    int? height = 0,
    int? outputId = 0,
    double? outputScale = 1.0,
    bool? acceptsInput = true,
    int? inputX = 0,
    int? inputY = 0,
    this.inputWidth,
    this.inputHeight,
  }) : x = x ?? 0,
       y = y ?? 0,
       width = width ?? 0,
       height = height ?? 0,
       outputId = outputId ?? 0,
       outputScale = outputScale ?? 1.0,
       acceptsInput = acceptsInput ?? true,
       inputX = inputX ?? 0,
       inputY = inputY ?? 0;

  /// Creates a [Popup] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$PopupFromJson(json);

  /// The unique handle of this popup surface.
  final int handle;

  /// The GPU texture identifier assigned to this popup.
  @JsonKey(name: 'texture_id')
  final int textureId;

  /// The handle of the parent surface that owns this popup.
  @JsonKey(name: 'parent_handle')
  final int parentHandle;

  /// The horizontal position relative to the parent surface.
  final int x;

  /// The vertical position relative to the parent surface.
  final int y;

  /// The width of this popup in pixels.
  final int width;

  /// The height of this popup in pixels.
  final int height;

  /// Multi-monitor support: ID of the output (monitor) this popup is on.
  ///
  /// Inherited from parent surface.
  @JsonKey(name: 'output_id')
  final int outputId;

  /// Multi-monitor support: scale factor of the output this popup is on.
  ///
  /// Used for correct rendering on HiDPI displays.
  @JsonKey(name: 'output_scale')
  final double outputScale;

  /// Whether this popup surface (or any mapped subsurface) accepts pointer
  /// input.
  @JsonKey(name: 'accepts_input', defaultValue: true)
  final bool acceptsInput;

  /// Horizontal offset of the active Wayland input region within the texture.
  @JsonKey(name: 'input_x', defaultValue: 0)
  final int inputX;

  /// Vertical offset of the active Wayland input region within the texture.
  @JsonKey(name: 'input_y', defaultValue: 0)
  final int inputY;

  /// Width of the active Wayland input region (`null` defaults to full width).
  @JsonKey(name: 'input_width')
  final int? inputWidth;

  /// Height of the active Wayland input region (`null` defaults to full
  /// height).
  @JsonKey(name: 'input_height')
  final int? inputHeight;

  /// Converts this popup instance to a JSON map.
  Map<String, dynamic> toJson() => _$PopupToJson(this);

  /// Creates a copy of this popup with optional field replacements.
  Popup copyWith({
    int? handle,
    int? textureId,
    int? parentHandle,
    int? gid,
    int? uid,
    String? title,
    String? appId,
    int? width,
    int? height,
    int? x,
    int? y,
    int? outputId,
    double? outputScale,
    bool? acceptsInput,
    int? inputX,
    int? inputY,
    int? inputWidth,
    int? inputHeight,
  }) {
    return Popup(
      handle: handle ?? this.handle,
      textureId: textureId ?? this.textureId,
      parentHandle: parentHandle ?? this.parentHandle,
      width: width ?? this.width,
      height: height ?? this.height,
      x: x ?? this.x,
      y: y ?? this.y,
      outputId: outputId ?? this.outputId,
      outputScale: outputScale ?? this.outputScale,
      acceptsInput: acceptsInput ?? this.acceptsInput,
      inputX: inputX ?? this.inputX,
      inputY: inputY ?? this.inputY,
      inputWidth: inputWidth ?? this.inputWidth,
      inputHeight: inputHeight ?? this.inputHeight,
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
    outputId,
    outputScale,
    acceptsInput,
    inputX,
    inputY,
    inputWidth,
    inputHeight,
  ];
}
