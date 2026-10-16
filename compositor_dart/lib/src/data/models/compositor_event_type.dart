/// Types of events emitted by the native compositor.
enum CompositorEventType {
  /// Emitted when a new top-level surface is mapped.
  surfaceMap,

  /// Emitted when a top-level surface is unmapped.
  surfaceUnMap,

  /// Emitted when a surface's title changes.
  surfaceTitleChange,

  /// Emitted when a surface's geometry changes.
  surfaceGeometryChange,

  /// Emitted when a surface's window decorations change.
  surfaceDecorationChange,

  /// Emitted when a surface enters or leaves fullscreen mode.
  surfaceFullscreenChange,

  /// Emitted when a surface's coordinates or position change.
  surfacePositionChange,

  /// Emitted when a surface requests to be minimized.
  surfaceMinimizeRequest,

  /// Emitted when a surface requests to be maximized.
  surfaceRequestMaximize,

  /// Emitted when a surface requests an interactive resize.
  surfaceresizeRequest,

  /// Emitted when an interactive surface grab ends.
  surfaceGrabEnd,

  /// Emitted when a sub-surface is mapped.
  subSurfaceMap,

  /// Emitted when a sub-surface is unmapped.
  subSurfaceUnMap,

  /// Emitted when a sub-surface position changes relative to its parent.
  subsurfacePositionChange,

  /// Emitted when a popup surface is mapped.
  popupMap,

  /// Emitted when a popup surface is unmapped.
  popupUnMap,

  /// Emitted when a display output is connected.
  outputAdded,

  /// Emitted when a display output is disconnected.
  outputRemoved,

  /// Emitted when display output properties (e.g. modes or scale) change.
  outputChanged,

  /// Emitted when a multi-finger swipe gesture begins.
  gestureSwipeBegin,

  /// Emitted when a multi-finger swipe gesture updates with motion delta.
  gestureSwipeUpdate,

  /// Emitted when a multi-finger swipe gesture ends or is cancelled.
  gestureSwipeEnd,

  /// Emitted when a surface requests activation (focus).
  surfaceRequestActivate,

  /// Emitted when a zoom action is triggered via scroll wheel.
  zoomScroll,

  /// Emitted when a zoom action is triggered via keyboard shortcut.
  zoomKey,

  /// Emitted when an edge gesture or server request triggers revealing the
  /// dock.
  dockReveal,
}
