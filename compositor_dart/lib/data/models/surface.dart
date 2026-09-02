import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'surface.g.dart';

@JsonSerializable()
class Surface extends Equatable {
  const Surface({
    required this.handle,
    required this.pid,
    required this.gid,
    required this.uid,
    int? textureId,
    this.title,
    this.appId,
    int? width,
    int? height,
    int? bufferWidth,
    int? bufferHeight,
    int? maximized,
    int? activated,
    int? geoX,
    int? geoY,
    int? usesCsd,
    int? outputId,
    double? outputScale,
    int? minWidth,
    int? maxWidth,
    int? minHeight,
    int? maxHeight,
  }) : textureId = textureId ?? handle,
       width = width ?? 0,
       height = height ?? 0,
       bufferWidth = bufferWidth ?? width,
       bufferHeight = bufferHeight ?? height,
       maximized = (maximized ?? 0) != 0,
       activated = (activated ?? 0) != 0,
       geoX = geoX ?? 0,
       geoY = geoY ?? 0,
       usesCsd = (usesCsd ?? 0) != 0,
       outputId = outputId ?? 0,
       outputScale = outputScale ?? 1.0,
       minWidth = minWidth ?? 0,
       maxWidth = maxWidth ?? 0,
       minHeight = minHeight ?? 0,
       maxHeight = maxHeight ?? 0;

  factory Surface.fromJson(Map<String, dynamic> json) =>
      _$SurfaceFromJson(json);

  Map<String, dynamic> toJson() => _$SurfaceToJson(this);

  final int handle;
  @JsonKey(name: 'texture_id')
  final int textureId;

  @JsonKey(name: 'client_pid')
  final int pid;
  @JsonKey(name: 'client_gid')
  final int gid;
  @JsonKey(name: 'client_uid')
  final int uid;

  final String? title;
  @JsonKey(name: 'app_id')
  final String? appId;
  final int? width;
  final int? height;
  final bool? maximized;
  final bool? activated;

  /// Geometry offset - where visible content starts within the buffer.
  /// Used by CSD apps that include shadows in their buffer.
  @JsonKey(name: 'geo_x')
  final int? geoX;
  @JsonKey(name: 'geo_y')
  final int? geoY;

  /// Actual buffer dimensions (includes shadow area for CSD apps).
  /// For CSD apps: bufferWidth >= width + geoX, bufferHeight >= height + geoY
  @JsonKey(name: 'buffer_width')
  final int? bufferWidth;
  @JsonKey(name: 'buffer_height')
  final int? bufferHeight;

  /// True if this surface uses client-side decorations (CSD).
  /// CSD apps draw their own title bar and window controls.
  /// This can be updated after surface_map when decoration
  /// negotiation completes.
  @JsonKey(name: 'uses_csd')
  final bool? usesCsd;

  /// Multi-monitor support: ID of the output (monitor) this surface is on.
  /// 0 means no specific output assigned.
  @JsonKey(name: 'output_id')
  final int? outputId;

  /// Multi-monitor support: scale factor of the output this surface is on.
  /// Used for correct rendering on HiDPI displays.
  @JsonKey(name: 'output_scale')
  final double? outputScale;

  @JsonKey(name: 'min_width')
  final int? minWidth;
  @JsonKey(name: 'max_width')
  final int? maxWidth;
  @JsonKey(name: 'min_height')
  final int? minHeight;
  @JsonKey(name: 'max_height')
  final int? maxHeight;

  /// True if this surface has fixed dimensions or cannot freely resize.
  bool get isFixedSize {
    if (maxWidth != null &&
        maxWidth! > 0 &&
        minWidth != null &&
        minWidth! > 0 &&
        maxWidth == minWidth) {
      return true;
    }
    if (maxHeight != null &&
        maxHeight! > 0 &&
        minHeight != null &&
        minHeight! > 0 &&
        maxHeight == minHeight) {
      return true;
    }
    return false;
  }

  Surface copyWith({
    int? handle,
    int? textureId,
    int? pid,
    int? gid,
    int? uid,
    String? title,
    String? appId,
    int? width,
    int? height,
    int? bufferWidth,
    int? bufferHeight,
    bool? maximized,
    bool? activated,
    int? geoX,
    int? geoY,
    bool? usesCsd,
    int? outputId,
    double? outputScale,
    int? minWidth,
    int? maxWidth,
    int? minHeight,
    int? maxHeight,
  }) {
    final m = maximized ?? this.maximized;
    final a = activated ?? this.activated;
    final c = usesCsd ?? this.usesCsd;
    return Surface(
      handle: handle ?? this.handle,
      textureId: textureId ?? this.textureId,
      pid: pid ?? this.pid,
      gid: gid ?? this.gid,
      uid: uid ?? this.uid,
      title: title ?? this.title,
      appId: appId ?? this.appId,
      width: width ?? this.width,
      height: height ?? this.height,
      bufferWidth: bufferWidth ?? this.bufferWidth,
      bufferHeight: bufferHeight ?? this.bufferHeight,
      maximized: m != null && m ? 1 : 0,
      activated: a != null && a ? 1 : 0,
      geoX: geoX ?? this.geoX,
      geoY: geoY ?? this.geoY,
      usesCsd: c != null && c ? 1 : 0,
      outputId: outputId ?? this.outputId,
      outputScale: outputScale ?? this.outputScale,
      minWidth: minWidth ?? this.minWidth,
      maxWidth: maxWidth ?? this.maxWidth,
      minHeight: minHeight ?? this.minHeight,
      maxHeight: maxHeight ?? this.maxHeight,
    );
  }

  @override
  List<Object?> get props => [
    handle,
    textureId,
    pid,
    gid,
    uid,
    title,
    appId,
    width,
    height,
    maximized,
    activated,
    geoX,
    geoY,
    bufferWidth,
    bufferHeight,
    usesCsd,
    outputId,
    outputScale,
    minWidth,
    maxWidth,
    minHeight,
    maxHeight,
  ];
}
