import 'dart:math' show min;

import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
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
import 'package:flutter/widgets.dart';
import 'package:shell/core.dart'
    show
        OverviewDragTarget,
        draggableSurfaceHeight,
        forwardVelocity,
        //killVelocity,
        maxFraction,
        minFraction,
        minOpenFraction,
        radiusAnimationAccel;

class AnimatedSurfaceWrapper extends StatelessWidget {
  const AnimatedSurfaceWrapper({
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

  final double gap;
  final int index;
  final double fraction;
  final double radius;
  final DisplayOutput output;
  final Widget child;
  final Surface surface;
  final PageController pageController;
  final AnimationController controller;
  static bool isVerticalPointAllowed = false;
  final Future<void> Function(Surface? s, {bool center}) closeOverview;
  final Future<void> Function(Surface? s) openOverview;
  final Future<void> Function({int index}) toggleOverview;

  @override
  Widget build(BuildContext context) {
    // final currentPageIndex = pageController.page?.round() ?? 0;

    return RepaintBoundary(
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: gap),
        child: fraction == minFraction
            ? GestureDetector(
                onTap: () async => select(index),
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
                //overlayController: overlayController,
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

  Future<void> select(int index) async {
    await toggleOverview(index: index);
  }
}

class RoundedSurface extends StatelessWidget {
  const RoundedSurface({
    required this.radius,
    required this.output,
    super.key,
    this.child,
  });

  final double radius;
  final DisplayOutput output;
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

class DraggableSurface extends StatelessWidget {
  const DraggableSurface({
    required this.radius,
    required this.closeOverview,
    required this.openOverview,
    //required this.overlayController,
    required this.pageController,
    required this.controller,
    required this.fraction,
    required this.surface,
    required this.output,
    super.key,
    this.child,
  });
  final DisplayOutput output;
  final double fraction;
  final Surface surface;

  static bool isVerticalTopPointAllowed = false;
  static bool isVerticalBottomPointAllowed = false;
  static bool get isVerticalPointAllowed =>
      DraggableSurface.isVerticalTopPointAllowed ||
      DraggableSurface.isVerticalBottomPointAllowed;

  final PageController pageController;
  final AnimationController controller;
  //final AnimationController overlayController;

  final Future<void> Function(Surface? s, {bool center}) closeOverview;
  final Future<void> Function(Surface? s) openOverview;

  final double radius;
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

          //isVerticalTopPointAllowed =
          //    details.globalPosition.dy <= draggableSurfaceHeight;
        },
        onVerticalDragStart: (details) async {
          if (!isVerticalPointAllowed) {
            isVerticalBottomPointAllowed =
                (output.height - details.globalPosition.dy) <=
                draggableSurfaceHeight;

            // isVerticalTopPointAllowed =
            //     details.globalPosition.dy <= draggableSurfaceHeight;
          }

          if (isVerticalPointAllowed) {
            if (isVerticalBottomPointAllowed) {
              CompositorRepository().platform.interactive =
                  fraction == maxFraction;
            } else if (isVerticalTopPointAllowed) {
              // CompositorRepository().platform.interactive = true;
            }
            await CompositorRepository().platform.forceRenderAllViews(true);

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
                duration: .zero,
              );
            } else if (isVerticalTopPointAllowed) {
              /* print('overlay delta $delta');
              await overlayController.animateTo(
                overlayController.value + (delta * 2),
                duration: .zero,
              ); */
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
              // final kill = velocity < killVelocity;
              // print('target $target, kill $kill');
            } else {
              target = velocity > forwardVelocity
                  ? .closeOverview
                  : .openOverview;
              // final kill = velocity < killVelocity;
              // print('target sw $target, velocity $velocity,  kill $kill');
            }

            //print('target $target');
            isVerticalBottomPointAllowed = false;
            if (target == .closeOverview) {
              await closeOverview(surface, center: false);
              await CompositorRepository().platform.forceRenderAllViews(false);
            } else {
              await CompositorRepository().platform.forceRenderAllViews(true);
              await openOverview(null);
            }
          } else if (isVerticalTopPointAllowed) {
            /*  if (details.globalPosition.dy <= output.height / 5) {
              await overlayController.reverse();
              isVerticalTopPointAllowed = false;
              CompositorRepository().platform.interactive = true;
              await CompositorRepository().platform.surfaceFocus(surface);
              print("overlay velocity on reverse $velocity");
            } else {
              await overlayController.forward();
              isVerticalTopPointAllowed = false;
              CompositorRepository().platform.interactive = true;
              await CompositorRepository().platform.surfaceFocus(surface);
              print("overlay velocity on forward $velocity");
            } */
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

            //print('target $target');
            isVerticalBottomPointAllowed = false;
            if (target == .closeOverview) {
              await closeOverview(surface, center: false);
              await CompositorRepository().platform.forceRenderAllViews(false);
            } else {
              await CompositorRepository().platform.forceRenderAllViews(true);
              await openOverview(null);
            }
          } else if (isVerticalTopPointAllowed) {
            /* isVerticalTopPointAllowed = false;
            CompositorRepository().platform.interactive = true;
            await CompositorRepository().platform.surfaceFocus(surface);
            print("overlay velocity on cancel"); */
          }
        },
        child: RoundedSurface(radius: radius, output: output, child: child),
      ),
    );
  }
}

class DismissableSurface extends StatelessWidget {
  const DismissableSurface({
    required this.radius,
    required this.output,
    required this.surface,
    required this.child,
    super.key,
  });

  final Surface surface;
  final Widget? child;

  final double radius;
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
          await CompositorRepository().platform.surfaceToplevelClose(
            surface,
          );
          return true;
        },
        child: RoundedSurface(radius: radius, output: output, child: child),
      ),
    );
  }
}
