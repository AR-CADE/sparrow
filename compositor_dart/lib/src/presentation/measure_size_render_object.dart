import 'package:compositor_dart/src/core/helper.dart'
    show OnWidgetBoundsChange, OnWidgetSizeChange;
import 'package:flutter/foundation.dart' show Listenable;
import 'package:flutter/rendering.dart'
    show
        MatrixUtils,
        Offset,
        PaintingContext,
        PipelineOwner,
        Rect,
        RenderProxyBox;
import 'package:material_ui/material_ui.dart' show Size, WidgetsBinding;

/// A [RenderProxyBox] that detects layout size and global screen bounds
/// changes and notifies callbacks.
class MeasureSizeRenderObject extends RenderProxyBox {
  /// Creates a [MeasureSizeRenderObject] with the given [onChange] callback.
  new(this.onChange, {this.onBoundsChange, Listenable? listenable}) {
    _listenable = listenable;
  }

  /// The previous layout size of the child render box.
  Size? oldSize;

  /// The previous global screen bounds of the render box.
  Rect? oldBounds;

  /// Callback invoked when the layout size changes.
  OnWidgetSizeChange onChange;

  /// Optional callback invoked when the global screen bounds change.
  OnWidgetBoundsChange? onBoundsChange;

  Listenable? _listenable;

  /// Optional [Listenable] (such as a scroll position) that triggers global
  /// bounds re-evaluation.
  Listenable? get listenable => _listenable;

  static final Set<MeasureSizeRenderObject> _attachedObjects =
      <MeasureSizeRenderObject>{};
  static bool _frameCallbackRegistered = false;
  static double _screenZoom = 1;
  static Offset _screenZoomFocalPoint = Offset.zero;

  /// The currently active desktop screen zoom magnification scale.
  static double get screenZoom => _screenZoom;

  /// Updates the active desktop screen zoom magnification scale and focal
  /// point so reported bounds match the magnified screen coordinates.
  static void setScreenZoom({
    required double scale,
    required Offset focalPoint,
  }) {
    final normalizedScale = scale <= 1.001 ? 1.0 : scale;
    final normalizedFocal = normalizedScale == 1.0 ? Offset.zero : focalPoint;
    if (_screenZoom == normalizedScale &&
        _screenZoomFocalPoint == normalizedFocal) {
      return;
    }
    _screenZoom = normalizedScale;
    _screenZoomFocalPoint = normalizedFocal;
    for (final renderObject in _attachedObjects.toList(growable: false)) {
      renderObject._reportBounds();
      if (renderObject.attached) {
        renderObject.markNeedsPaint();
      }
    }
  }

  static void _ensureFrameCallbackRegistered() {
    if (_frameCallbackRegistered) return;
    _frameCallbackRegistered = true;
    WidgetsBinding.instance.addPersistentFrameCallback((_) {
      if (_attachedObjects.isEmpty) return;
      for (final renderObject in _attachedObjects.toList(growable: false)) {
        renderObject._reportBounds();
      }
    });
  }

  set listenable(Listenable? value) {
    if (_listenable == value) return;
    if (attached) {
      _listenable?.removeListener(_onListenableChanged);
    }
    _listenable = value;
    if (attached) {
      _listenable?.addListener(_onListenableChanged);
    }
  }

  @override
  void attach(PipelineOwner owner) {
    super.attach(owner);
    _ensureFrameCallbackRegistered();
    _attachedObjects.add(this);
    _listenable?.addListener(_onListenableChanged);
  }

  @override
  void detach() {
    _attachedObjects.remove(this);
    _listenable?.removeListener(_onListenableChanged);
    oldBounds = null;
    super.detach();
  }

  void _onListenableChanged() {
    _reportBounds();
    if (attached) {
      markNeedsPaint();
    }
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    _reportBounds();
    super.paint(context, offset);
  }

  void _reportBounds() {
    final boundsCb = onBoundsChange;
    if (boundsCb == null || !attached || !hasSize) return;
    final currentSize = child?.size ?? size;
    if (currentSize.width <= 0 || currentSize.height <= 0) return;
    final transform = getTransformTo(null);
    final rawBounds = MatrixUtils.transformRect(
      transform,
      Offset.zero & currentSize,
    );
    final newBounds = _screenZoom > 1.0
        ? Rect.fromLTWH(
            _screenZoomFocalPoint.dx +
                (rawBounds.left - _screenZoomFocalPoint.dx) * _screenZoom,
            _screenZoomFocalPoint.dy +
                (rawBounds.top - _screenZoomFocalPoint.dy) * _screenZoom,
            rawBounds.width * _screenZoom,
            rawBounds.height * _screenZoom,
          )
        : rawBounds;
    if (oldBounds == newBounds) return;
    oldBounds = newBounds;
    boundsCb(newBounds);
  }

  @override
  void performLayout() {
    super.performLayout();

    final newSize = child?.size;
    final sizeChanged = oldSize != newSize;
    oldSize = newSize;

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (sizeChanged) {
        onChange(newSize);
      }
      _reportBounds();
    });
  }
}
