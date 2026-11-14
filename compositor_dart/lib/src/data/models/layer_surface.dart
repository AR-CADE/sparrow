import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'layer_surface.g.dart';

/// Represents a Wayland layer-shell surface (wlr_layer_surface_v1).
@JsonSerializable()
class LayerSurface extends Equatable {
  /// Creates a [LayerSurface] descriptor.
  const new({
    required this.handle,
    required this.textureId,
    this.layer = 0,
    this.anchor = 0,
    this.x = 0,
    this.y = 0,
    this.width = 0,
    this.height = 0,
    this.bufferWidth = 0,
    this.bufferHeight = 0,
    this.marginTop = 0,
    this.marginBottom = 0,
    this.marginLeft = 0,
    this.marginRight = 0,
    this.exclusiveZone = 0,
    this.keyboardInteractive = false,
    this.outputId = 0,
    this.scope = '',
  });

  /// Creates a [LayerSurface] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$LayerSurfaceFromJson(json);

  /// Converts this layer-surface to a JSON map.
  Map<String, dynamic> toJson() => _$LayerSurfaceToJson(this);

  /// The unique handle of this layer-surface.
  final int handle;

  /// The GPU texture identifier assigned to this layer-surface.
  @JsonKey(name: 'texture_id')
  final int textureId;

  /// The layer index:
  /// 0 = background, 1 = bottom, 2 = top, 3 = overlay.
  final int layer;

  /// The anchor bitmask:
  /// 1 = top, 2 = bottom, 4 = left, 8 = right.
  final int anchor;

  /// Calculated X position on the output.
  final int x;

  /// Calculated Y position on the output.
  final int y;

  /// The width in logical coordinates.
  final int width;

  /// The height in logical coordinates.
  final int height;

  /// The pixel width of the buffer.
  @JsonKey(name: 'buffer_width')
  final int bufferWidth;

  /// The pixel height of the buffer.
  @JsonKey(name: 'buffer_height')
  final int bufferHeight;

  /// Margins in logical coordinates.
  @JsonKey(name: 'margin_top')
  final int marginTop;

  /// Margin bottom in logical coordinates.
  @JsonKey(name: 'margin_bottom')
  final int marginBottom;

  /// Margin left in logical coordinates.
  @JsonKey(name: 'margin_left')
  final int marginLeft;

  /// Margin right in logical coordinates.
  @JsonKey(name: 'margin_right')
  final int marginRight;

  /// Exclusive zone requested by client (informative).
  @JsonKey(name: 'exclusive_zone')
  final int exclusiveZone;

  /// Whether this layer accepts keyboard interactivity.
  @JsonKey(name: 'keyboard_interactive')
  final bool keyboardInteractive;

  /// The output ID this layer surface is attached to.
  @JsonKey(name: 'output_id')
  final int outputId;

  /// Namespace / scope identifier (e.g. "wallpaper", "panel", "osd").
  final String scope;

  /// Helper getters for layer types.
  bool get isBackground => layer == 0;

  /// Whether this layer is bottom.
  bool get isBottom => layer == 1;

  /// Whether this layer is top.
  bool get isTop => layer == 2;

  /// Whether this layer is overlay.
  bool get isOverlay => layer == 3;

  /// Anchor helpers.
  bool get anchorTop => (anchor & 1) != 0;

  /// Whether anchored to bottom.
  bool get anchorBottom => (anchor & 2) != 0;

  /// Whether anchored to left.
  bool get anchorLeft => (anchor & 4) != 0;

  /// Whether anchored to right.
  bool get anchorRight => (anchor & 8) != 0;

  /// Creates a copy of this layer-surface with optional replacements.
  LayerSurface copyWith({
    int? handle,
    int? textureId,
    int? layer,
    int? anchor,
    int? x,
    int? y,
    int? width,
    int? height,
    int? bufferWidth,
    int? bufferHeight,
    int? marginTop,
    int? marginBottom,
    int? marginLeft,
    int? marginRight,
    int? exclusiveZone,
    bool? keyboardInteractive,
    int? outputId,
    String? scope,
  }) {
    return LayerSurface(
      handle: handle ?? this.handle,
      textureId: textureId ?? this.textureId,
      layer: layer ?? this.layer,
      anchor: anchor ?? this.anchor,
      x: x ?? this.x,
      y: y ?? this.y,
      width: width ?? this.width,
      height: height ?? this.height,
      bufferWidth: bufferWidth ?? this.bufferWidth,
      bufferHeight: bufferHeight ?? this.bufferHeight,
      marginTop: marginTop ?? this.marginTop,
      marginBottom: marginBottom ?? this.marginBottom,
      marginLeft: marginLeft ?? this.marginLeft,
      marginRight: marginRight ?? this.marginRight,
      exclusiveZone: exclusiveZone ?? this.exclusiveZone,
      keyboardInteractive: keyboardInteractive ?? this.keyboardInteractive,
      outputId: outputId ?? this.outputId,
      scope: scope ?? this.scope,
    );
  }

  @override
  List<Object?> get props => [
    handle,
    textureId,
    layer,
    anchor,
    x,
    y,
    width,
    height,
    bufferWidth,
    bufferHeight,
    marginTop,
    marginBottom,
    marginLeft,
    marginRight,
    exclusiveZone,
    keyboardInteractive,
    outputId,
    scope,
  ];
}
