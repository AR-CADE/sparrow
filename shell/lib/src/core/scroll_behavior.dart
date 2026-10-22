import 'dart:ui' show PointerDeviceKind;

import 'package:material_ui/material_ui.dart' show MaterialScrollBehavior;

/// Custom scroll behavior supporting dragging with mouse, stylus, touch,
/// and trackpad devices.
class CustomScrollBehavior extends MaterialScrollBehavior {
  /// Creates a new [CustomScrollBehavior].
  const new();

  @override
  Set<PointerDeviceKind> get dragDevices => {
    PointerDeviceKind.mouse,
    PointerDeviceKind.stylus,
    PointerDeviceKind.touch,
    PointerDeviceKind.invertedStylus,
    PointerDeviceKind.trackpad,
  };
}
