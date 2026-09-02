import 'package:compositor_dart/core/helper.dart' show OnWidgetSizeChange;
import 'package:compositor_dart/presentation/measure_size_render_object.dart'
    show MeasureSizeRenderObject;
import 'package:material_ui/material_ui.dart'
    show BuildContext, RenderObject, SingleChildRenderObjectWidget, Widget;

class MeasureSize extends SingleChildRenderObjectWidget {
  const MeasureSize({
    required this.onChange,
    required Widget super.child,
    super.key,
  });
  final OnWidgetSizeChange onChange;

  @override
  RenderObject createRenderObject(BuildContext context) {
    return MeasureSizeRenderObject(onChange);
  }
}
