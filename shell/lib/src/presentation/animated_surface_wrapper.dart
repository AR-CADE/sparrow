import 'dart:math' show min;

import 'package:compositor_dart/compositor_dart.dart'
    show CompositorRepository, DisplayOutput, Surface;
import 'package:flutter/widgets.dart';
import 'package:material_ui/material_ui.dart'
    show
        AnimationController,
        AspectRatio,
        BorderRadius,
        BuildContext,
        Center,
        ClipRRect,
        Dismissible,
        EdgeInsets,
        GestureDetector,
        HitTestBehavior,
        MediaQuery,
        Padding,
        PageController,
        RepaintBoundary,
        StatelessWidget,
        ValueKey,
        Widget;
import 'package:shell/src/core/constants.dart'
    show
        OverviewDragTarget,
        draggableSurfaceHeight,
        forwardVelocity,
        maxFraction,
        minFraction,
        minOpenFraction,
        radiusAnimationAccel;

/// Wraps a surface with animations and gesture detectors for overview mode
/// transitions and swiping gestures.
class AnimatedSurfaceWrapper extends StatelessWidget {
  /// Creates a new [AnimatedSurfaceWrapper].
  const new({
    required this.toggleOverview,
    required this.gap,
    required this.index,
    required this.fraction,
    required this.radius,
    required this.output,
    required this.child,
    required this.surface,
    required this.pageController,
    required this.controller,
    required this.closeOverview,
    required this.openOverview,
    super.key,
  });

  /// Gap spacing around the surface.
  final double gap;

  /// Index of the surface in the page view.
  final int index;

  /// Viewport fraction.
  final double fraction;

  /// Corner radius.
  final double radius;

  /// Display output geometry.
  final DisplayOutput output;

  /// Surface child widget.
  final Widget child;

  /// Underlying compositor surface.
  final Surface surface;

  /// Surface page controller.
  final PageController pageController;

  /// Overview animation controller.
  final AnimationController controller;

  /// Whether vertical gesture point is allowed.
  static bool isVerticalPointAllowed = false;

  /// Callback to close overview.
  final Future<void> Function(Surface? s, {bool center}) closeOverview;

  /// Callback to open overview.
  final Future<void> Function(Surface? s) openOverview;

  /// Callback to toggle overview.
  final Future<void> Function({int index}) toggleOverview;

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: gap),
        child: fraction == minFraction
            ? GestureDetector(
                onTap: () async {
                  await select(index);
                },
                child: DismissableSurface(
                  surface: surface,
                  radius: radius,
                  output: output,
                  child: child,
                ),
              )
            : DraggableSurface(
                radius: radius,
                closeOverview: closeOverview,
                openOverview: openOverview,
                pageController: pageController,
                controller: controller,
                fraction: fraction,
                surface: surface,
                output: output,
                child: child,
              ),
      ),
    );
  }

  /// Selects the surface at [index] by toggling overview.
  Future<void> select(int index) async {
    await toggleOverview(index: index);
  }
}

/// A surface clipped to rounded corners matching the aspect ratio
/// of the output.
class RoundedSurface extends StatelessWidget {
  /// Creates a new [RoundedSurface].
  const new({
    required this.radius,
    required this.output,
    super.key,
    this.child,
  });

  /// Corner radius.
  final double radius;

  /// Display output geometry.
  final DisplayOutput output;

  /// Child widget.
  final Widget? child;

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(
        min(radius * (radius > 1 ? radiusAnimationAccel : 1), 24),
      ),
      child: AspectRatio(
        aspectRatio: output.width / output.height,
        child: child,
      ),
    );
  }
}

/// A surface that can be vertically dragged to open or close the overview.
class DraggableSurface extends StatelessWidget {
  /// Creates a new [DraggableSurface].
  const new({
    required this.radius,
    required this.closeOverview,
    required this.openOverview,
    required this.pageController,
    required this.controller,
    required this.fraction,
    required this.surface,
    required this.output,
    super.key,
    this.child,
  });

  /// Display output.
  final DisplayOutput output;

  /// Viewport fraction.
  final double fraction;

  /// The surface being dragged.
  final Surface surface;

  /// Whether vertical bottom point is allowed.
  static bool isVerticalBottomPointAllowed = false;

  /// Whether vertical top point is allowed.
  static bool isVerticalTopPointAllowed = false;

  /// Whether vertical point is allowed.
  static bool get isVerticalPointAllowed =>
      DraggableSurface.isVerticalTopPointAllowed ||
      DraggableSurface.isVerticalBottomPointAllowed;

  /// Page controller.
  final PageController pageController;

  /// Overview animation controller.
  final AnimationController controller;

  /// Callback to close overview.
  final Future<void> Function(Surface? s, {bool center}) closeOverview;

  /// Callback to open overview.
  final Future<void> Function(Surface? s) openOverview;

  /// Corner radius.
  final double radius;

  /// Child widget.
  final Widget? child;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: GestureDetector(
        behavior: HitTestBehavior.translucent,
        onVerticalDragDown: (details) async {
          isVerticalBottomPointAllowed =
              (output.height - details.globalPosition.dy) <=
              draggableSurfaceHeight;
        },
        onVerticalDragStart: (details) async {
          if (!isVerticalPointAllowed) {
            isVerticalBottomPointAllowed =
                (output.height - details.globalPosition.dy) <=
                draggableSurfaceHeight;
          }

          if (isVerticalPointAllowed) {
            if (isVerticalBottomPointAllowed) {
              CompositorRepository().platform.interactive =
                  fraction == maxFraction;
            }
            await CompositorRepository().platform.forceRenderAllViews(
              force: true,
            );

            await CompositorRepository().platform.clearFocus(surface);
          }
        },
        onVerticalDragUpdate: (details) async {
          if (!isVerticalPointAllowed) {
            return;
          }

          final primaryDelta = details.primaryDelta;
          if (primaryDelta == null) {
            return;
          }

          final height = MediaQuery.of(context).size.height;
          if (height > 0) {
            final delta = primaryDelta / height;

            if (isVerticalBottomPointAllowed) {
              await controller.animateTo(
                controller.value - delta,
                duration: Duration.zero,
              );
            }
          }
        },
        onVerticalDragEnd: (details) async {
          if (!isVerticalPointAllowed) {
            return;
          }

          final height = MediaQuery.of(context).size.height;
          final velocity = details.primaryVelocity! / height;
          if (isVerticalBottomPointAllowed) {
            var target = OverviewDragTarget.closeOverview;

            if (fraction < minOpenFraction) {
              target = .openOverview;
            } else {
              target = velocity > forwardVelocity
                  ? .closeOverview
                  : .openOverview;
            }

            isVerticalBottomPointAllowed = false;
            if (target == .closeOverview) {
              await closeOverview(surface, center: false);
              await CompositorRepository().platform.forceRenderAllViews(
                force: false,
              );
            } else {
              await CompositorRepository().platform.forceRenderAllViews(
                force: true,
              );
              await openOverview(null);
            }
          }
        },
        onVerticalDragCancel: () async {
          if (!isVerticalPointAllowed) {
            return;
          }

          if (isVerticalBottomPointAllowed) {
            var target = OverviewDragTarget.closeOverview;

            if (fraction < minOpenFraction) {
              target = .closeOverview;
            } else {
              target = .openOverview;
            }

            isVerticalBottomPointAllowed = false;
            if (target == .closeOverview) {
              await closeOverview(surface, center: false);
              await CompositorRepository().platform.forceRenderAllViews(
                force: false,
              );
            } else {
              await CompositorRepository().platform.forceRenderAllViews(
                force: true,
              );
              await openOverview(null);
            }
          }
        },
        child: RoundedSurface(radius: radius, output: output, child: child),
      ),
    );
  }
}

/// A surface that can be dismissed by swiping up in overview.
class DismissableSurface extends StatelessWidget {
  /// Creates a new [DismissableSurface].
  const new({
    required this.radius,
    required this.output,
    required this.surface,
    required this.child,
    super.key,
  });

  /// The surface being dismissed.
  final Surface surface;

  /// Child widget.
  final Widget? child;

  /// Corner radius.
  final double radius;

  /// Display output geometry.
  final DisplayOutput output;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Dismissible(
        resizeDuration: const Duration(milliseconds: 140),
        movementDuration: const Duration(milliseconds: 140),
        key: ValueKey<int>(surface.handle),
        direction: .up,
        confirmDismiss: (direction) async {
          await CompositorRepository().platform.surfaceToplevelClose(surface);
          return true;
        },
        child: RoundedSurface(radius: radius, output: output, child: child),
      ),
    );
  }
}
