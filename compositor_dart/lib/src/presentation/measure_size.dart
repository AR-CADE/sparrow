import 'package:compositor_dart/src/core/helper.dart' show OnWidgetSizeChange;
import 'package:compositor_dart/src/presentation/measure_size_render_object.dart'
    show MeasureSizeRenderObject;
import 'package:material_ui/material_ui.dart'
    show BuildContext, RenderObject, SingleChildRenderObjectWidget, Widget;

/// A widget that measures its child widget's rendered size and reports changes.
class MeasureSize extends SingleChildRenderObjectWidget {
  /// Creates a [MeasureSize] widget.
  const new({required this.onChange, required Widget super.child, super.key});

  /// Callback invoked whenever the rendered size of [child] changes.
  final OnWidgetSizeChange onChange;

  @override
  RenderObject createRenderObject(BuildContext context) {
    return MeasureSizeRenderObject(onChange);
  }
}
