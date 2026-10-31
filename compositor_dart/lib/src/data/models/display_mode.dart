import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'display_mode.g.dart';

/// Represents a supported display resolution and refresh rate for an output.
@JsonSerializable()
class DisplayMode extends Equatable {
  /// Creates a [DisplayMode] with [width], [height], and [refresh] rate.
  const new({required this.width, required this.height, required this.refresh});

  /// Creates a [DisplayMode] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$DisplayModeFromJson(json);

  /// The horizontal resolution in pixels.
  final int width;

  /// The vertical resolution in pixels.
  final int height;

  /// The refresh rate in millihertz (mHz), e.g. 144000 for 144Hz.
  final int refresh;

  /// Converts this display mode to a JSON map.
  Map<String, dynamic> toJson() => _$DisplayModeToJson(this);

  /// The refresh rate converted to Hertz (Hz), e.g. 144.0 for 144Hz.
  double get refreshHz => refresh / 1000.0;

  /// Creates a copy of this display mode with optional field replacements.
  DisplayMode copyWith({int? width, int? height, int? refresh}) {
    return DisplayMode(
      width: width ?? this.width,
      height: height ?? this.height,
      refresh: refresh ?? this.refresh,
    );
  }

  @override
  String toString() => '${width}x$height @ ${refreshHz.toStringAsFixed(0)}Hz';

  @override
  List<Object?> get props => [width, height, refresh];

  // @override
  // int get hashCode => Object.hash(width, height, refresh);
}
