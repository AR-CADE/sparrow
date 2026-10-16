import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'surface.g.dart';

/// Represents a top-level Wayland or XWayland application window surface.
@JsonSerializable()
class Surface extends Equatable {
  /// Creates a [Surface] descriptor.
  const new({
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
    int? fullscreen,
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
       fullscreen = (fullscreen ?? 0) != 0,
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

  /// Creates a [Surface] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$SurfaceFromJson(json);

  /// Converts this surface instance to a JSON map.
  Map<String, dynamic> toJson() => _$SurfaceToJson(this);

  /// The unique compositor handle for this surface.
  final int handle;

  /// The GPU texture identifier assigned to this surface.
  @JsonKey(name: 'texture_id')
  final int textureId;

  /// The process ID (PID) of the client application owning this surface.
  @JsonKey(name: 'client_pid')
  final int pid;

  /// The group ID (GID) of the client application owning this surface.
  @JsonKey(name: 'client_gid')
  final int gid;

  /// The user ID (UID) of the client application owning this surface.
  @JsonKey(name: 'client_uid')
  final int uid;

  /// The title of the window surface.
  final String? title;

  /// The desktop application identifier (e.g. `"org.gnome.Nautilus"`).
  @JsonKey(name: 'app_id')
  final String? appId;

  /// The visible width of the surface in logical pixels.
  final int? width;

  /// The visible height of the surface in logical pixels.
  final int? height;

  /// Whether the surface is currently maximized.
  final bool? maximized;

  /// Whether the surface is currently in fullscreen mode.
  final bool? fullscreen;

  /// Whether the surface currently has keyboard focus and is active.
  final bool? activated;

  /// The horizontal geometry offset where visible content starts in the buffer.
  ///
  /// Used by client-side decoration (CSD) apps that include shadows in buffers.
  @JsonKey(name: 'geo_x')
  final int? geoX;

  /// The vertical geometry offset where visible content starts in the buffer.
  ///
  /// Used by client-side decoration (CSD) apps that include shadows in buffers.
  @JsonKey(name: 'geo_y')
  final int? geoY;

  /// Actual buffer width including shadow and margin areas for CSD apps.
  @JsonKey(name: 'buffer_width')
  final int? bufferWidth;

  /// Actual buffer height including shadow and margin areas for CSD apps.
  @JsonKey(name: 'buffer_height')
  final int? bufferHeight;

  /// Whether this surface uses client-side decorations (CSD).
  ///
  /// CSD applications render their own title bars and window control buttons.
  @JsonKey(name: 'uses_csd')
  final bool? usesCsd;

  /// The ID of the display output this surface is currently assigned to.
  ///
  /// A value of `0` indicates no specific display output is assigned.
  @JsonKey(name: 'output_id')
  final int? outputId;

  /// The scale factor of the display output this surface is on.
  ///
  /// Used for correct rendering on HiDPI displays.
  @JsonKey(name: 'output_scale')
  final double? outputScale;

  /// The minimum allowable width requested by the client, or `0` if unset.
  @JsonKey(name: 'min_width')
  final int? minWidth;

  /// The maximum allowable width requested by the client, or `0` if unset.
  @JsonKey(name: 'max_width')
  final int? maxWidth;

  /// The minimum allowable height requested by the client, or `0` if unset.
  @JsonKey(name: 'min_height')
  final int? minHeight;

  /// The maximum allowable height requested by the client, or `0` if unset.
  @JsonKey(name: 'max_height')
  final int? maxHeight;

  /// Whether this surface has fixed dimensions and cannot freely resize.
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

  /// Creates a copy of this surface with optional field replacements.
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
    bool? fullscreen,
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
    final f = fullscreen ?? this.fullscreen;
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
      fullscreen: f != null && f ? 1 : 0,
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
    fullscreen,
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
