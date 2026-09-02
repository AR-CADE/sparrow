import 'package:compositor_dart/data/models/display_mode.dart' show DisplayMode;
import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'display_output.g.dart';

@JsonSerializable()
class DisplayOutput extends Equatable {
  const DisplayOutput({
    required this.id,
    String? name,
    String? make,
    String? model,
    int? x,
    int? y,
    int? width = 0,
    int? height = 0,
    int? refreshRate = 60000,
    double? scale = 1.0,
    int? transform = 0,
    List<DisplayMode>? availableModes = const [],
    bool? isPrimary = false,
  }) : name = name ?? '',
       make = make ?? '',
       model = model ?? '',
       x = x ?? 0,
       y = y ?? 0,
       width = width ?? 0,
       height = height ?? 0,
       refreshRate = refreshRate ?? 60000,
       scale = scale ?? 1.0,
       transform = transform ?? 0,
       availableModes = availableModes ?? const [],
       isPrimary = isPrimary ?? false;

  factory DisplayOutput.fromJson(Map<String, dynamic> json) =>
      _$DisplayOutputFromJson(json);

  /// Unique output ID from the compositor.
  final int id;

  /// Output name (e.g., "HDMI-A-1", "DP-2", "eDP-1").
  final String name;

  /// Manufacturer name.
  final String make;

  /// Model name.
  final String model;

  /// Current x Position in the unified coordinate space.
  final int x;

  /// Current y Position in the unified coordinate space.
  final int y;

  /// Current width.
  final int width;

  /// Current height.
  final int height;

  /// Current refresh rate in mHz (e.g., 144000 for 144Hz).
  @JsonKey(name: 'refresh')
  final int refreshRate;

  /// Display scale factor.
  final double scale;

  /// Transform (rotation/flip): 0=normal, 1=90, 2=180, 3=270, 4-7=flipped variants.
  final int transform;

  /// Available display modes.
  @JsonKey(name: 'available_modes')
  final List<DisplayMode> availableModes;

  /// Whether this is the primary display.
  @JsonKey(name: 'is_primary')
  final bool isPrimary;

  Map<String, dynamic> toJson() => _$DisplayOutputToJson(this);

  /// Refresh rate in Hz (e.g., 144.0 for 144Hz).
  double get refreshHz => refreshRate / 1000.0;

  /// Display bounds as a rectangle.
  ({int x, int y, int width, int height}) get bounds =>
      (x: x, y: y, width: width, height: height);

  /// Check if a point is within this display's bounds.
  bool containsPoint(double px, double py) {
    return px >= x && px < x + width && py >= y && py < y + height;
  }

  DisplayOutput copyWith({
    int? id,
    String? name,
    String? make,
    String? model,
    int? x,
    int? y,
    int? width,
    int? height,
    int? refreshRate,
    double? scale,
    int? transform,
    List<DisplayMode>? availableModes,
    bool? isPrimary,
  }) {
    return DisplayOutput(
      id: id ?? this.id,
      name: name ?? this.name,
      make: make ?? this.make,
      model: model ?? this.model,
      x: x ?? this.x,
      y: y ?? this.y,
      width: width ?? this.width,
      height: height ?? this.height,
      refreshRate: refreshRate ?? this.refreshRate,
      scale: scale ?? this.scale,
      transform: transform ?? this.transform,
      availableModes: availableModes ?? this.availableModes,
      isPrimary: isPrimary ?? this.isPrimary,
    );
  }

  @override
  String toString() =>
      'DisplayOutput($name, ${width}x$height '
      '@'
      ' ${refreshHz.toStringAsFixed(0)}Hz, pos=($x,$y))';

  @override
  List<Object?> get props => [
    id,
    name,
    make,
    model,
    x,
    y,
    width,
    height,
    refreshRate,
    scale,
    transform,
    availableModes,
    isPrimary,
  ];
}
