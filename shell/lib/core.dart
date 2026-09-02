import 'dart:ui' show PointerDeviceKind;

import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:material_ui/material_ui.dart'
    show MaterialScrollBehavior, PageController;

const double maxFraction = 1;
const double minFraction = 0.5;
List<Surface> surfaces = [];

class CustomScrollBehavior extends MaterialScrollBehavior {
  const CustomScrollBehavior();
  @override
  Set<PointerDeviceKind> get dragDevices => {
    PointerDeviceKind.mouse,
    PointerDeviceKind.stylus,
    PointerDeviceKind.touch,
    PointerDeviceKind.invertedStylus,
    PointerDeviceKind.trackpad,
  };
}

enum OverviewDragTarget { openOverview, closeOverview }

enum DragDirection { up, down }

const minOpenFraction = .90;
const killVelocity = -2.0;
const forwardVelocity = -.7;
const draggableSurfaceHeight = 30;
const radiusAnimationAccel = 15;

class CustomPageController extends PageController {
  CustomPageController({super.viewportFraction});
  @override
  double get viewportFraction => _viewportFraction;
  double _viewportFraction = 1;

  void setViewportFraction(double value) {
    (position as dynamic).viewportFraction = value;
    _viewportFraction = value;
  }
}
