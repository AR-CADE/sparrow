import 'dart:io' show Process, ProcessStartMode;

import 'package:material_ui/material_ui.dart' show PageController;

/// Signature for spawning processes, injectable for testing.
typedef ProcessStarter = Future<Process> Function(
  String executable,
  List<String> arguments, {
  ProcessStartMode mode,
});

/// Default maximum viewport fraction (full screen surface).
const double maxFraction = 1;

/// Default minimum viewport fraction (overview mode surface scale).
const double minFraction = 0.5;

/// Drag target indicator when dragging a surface vertically.
enum OverviewDragTarget {
  /// Dragging towards opening the overview.
  openOverview,

  /// Dragging towards closing the overview.
  closeOverview,
}

/// Vertical drag direction.
enum DragDirection {
  /// Dragging upward.
  up,

  /// Dragging downward.
  down,
}

/// Minimum fraction threshold to open overview.
const double minOpenFraction = 0.90;

/// Velocity threshold to kill or open overview.
const double killVelocity = -2;

/// Forward velocity threshold for overview gesture.
const double forwardVelocity = -0.7;

/// Height in logical pixels for the draggable surface handle area.
const int draggableSurfaceHeight = 8;

/// Acceleration factor for corner radius animation.
const int radiusAnimationAccel = 15;

/// Custom [PageController] that supports dynamic viewport fraction updates.
class CustomPageController extends PageController {
  /// Creates a new [CustomPageController].
  new({super.viewportFraction, super.initialPage, super.keepPage})
    : _viewportFraction = viewportFraction;

  @override
  double get viewportFraction => _viewportFraction;
  double _viewportFraction = 1;

  /// Dynamically updates the viewport fraction on the controller and position.
  void setViewportFraction(double value) {
    if (_viewportFraction == value) {
      return;
    }
    _viewportFraction = value;
    if (hasClients) {
      (position as dynamic).viewportFraction = value;
    }
  }
}
