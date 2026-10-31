import 'package:compositor_dart/src/core/helper.dart'
    show OnWidgetBoundsChange, OnWidgetSizeChange;
import 'package:compositor_dart/src/presentation/measure_size_render_object.dart'
    show MeasureSizeRenderObject;
import 'package:flutter/foundation.dart' show Listenable;
import 'package:material_ui/material_ui.dart'
    show BuildContext, RenderObject, SingleChildRenderObjectWidget, Widget;

/// A widget that measures its child widget's rendered size and global bounds,
/// reporting changes.
class MeasureSize extends SingleChildRenderObjectWidget {
  /// Creates a [MeasureSize] widget.
  const new({
    required this.onChange,
    required Widget super.child,
    this.onBoundsChange,
    this.listenable,
    super.key,
  });

  /// Callback invoked whenever the rendered size of [child] changes.
  final OnWidgetSizeChange onChange;

  /// Optional callback invoked whenever the global screen bounds of [child]
  /// change or become detached (`null`).
  final OnWidgetBoundsChange? onBoundsChange;

  /// Optional [Listenable] (e.g. a scroll position) that triggers global
  /// bounds re-evaluation when notified.
  final Listenable? listenable;

  @override
  RenderObject createRenderObject(BuildContext context) {
    return MeasureSizeRenderObject(
      onChange,
      onBoundsChange: onBoundsChange,
      listenable: listenable,
    );
  }

  @override
  void updateRenderObject(
    BuildContext context,
    covariant MeasureSizeRenderObject renderObject,
  ) {
    renderObject
      ..onChange = onChange
      ..onBoundsChange = onBoundsChange
      ..listenable = listenable
      ..oldBounds = null
      ..markNeedsPaint();
  }
}
