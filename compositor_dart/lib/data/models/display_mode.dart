import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'display_mode.g.dart';

@JsonSerializable()
class DisplayMode extends Equatable {
  const DisplayMode({
    required this.width,
    required this.height,
    required this.refresh,
  });

  factory DisplayMode.fromJson(Map<String, dynamic> json) =>
      _$DisplayModeFromJson(json);

  final int width;
  final int height;

  /// In `mHz` (e.g., 144000 for 144Hz)
  final int refresh;

  Map<String, dynamic> toJson() => _$DisplayModeToJson(this);

  /// Refresh rate in Hz (e.g., 144.0 for 144Hz)
  double get refreshHz => refresh / 1000.0;

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
