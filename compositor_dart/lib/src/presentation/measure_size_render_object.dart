import 'package:compositor_dart/src/core/helper.dart' show OnWidgetSizeChange;
import 'package:flutter/rendering.dart' show RenderProxyBox;
import 'package:material_ui/material_ui.dart' show Size, WidgetsBinding;

/// A [RenderProxyBox] that detects layout size changes and notifies a callback.
class MeasureSizeRenderObject extends RenderProxyBox {
  /// Creates a [MeasureSizeRenderObject] with the given [onChange] callback.
  new(this.onChange);

  /// The previous layout size of the child render box.
  Size? oldSize;

  /// Callback invoked when the layout size changes.
  final OnWidgetSizeChange onChange;

  @override
  void performLayout() {
    super.performLayout();

    final newSize = child?.size;
    if (oldSize == newSize) return;

    oldSize = newSize;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      onChange(newSize);
    });
  }
}
