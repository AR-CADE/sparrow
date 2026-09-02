import 'package:compositor_dart/core/helper.dart' show OnWidgetSizeChange;
import 'package:flutter/rendering.dart' show RenderProxyBox;
import 'package:material_ui/material_ui.dart' show Size, WidgetsBinding;

class MeasureSizeRenderObject extends RenderProxyBox {
  MeasureSizeRenderObject(this.onChange);
  Size? oldSize;
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
