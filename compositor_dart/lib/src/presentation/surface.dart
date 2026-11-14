import 'dart:math' show max, min;

import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/data/models/sub_surface.dart'
    show SubSurface;
import 'package:compositor_dart/src/data/models/surface.dart' show Surface;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/domain/surface/cubit/surface_cubit.dart'
    show SurfaceCubit;
import 'package:compositor_dart/src/domain/surface/cubit/surface_state.dart'
    show SurfaceState;
import 'package:compositor_dart/src/presentation/compositor_platform_view_controller.dart'
    show CompositorPlatformViewController;
import 'package:compositor_dart/src/presentation/measure_size.dart'
    show MeasureSize;
import 'package:compositor_dart/src/presentation/measure_size_render_object.dart'
    show MeasureSizeRenderObject;
import 'package:compositor_dart/src/presentation/popup.dart' show PopupView;
import 'package:flutter/foundation.dart' show Listenable;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocBuilder, BlocProvider, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        BorderRadius,
        BuildContext,
        Center,
        Clip,
        ClipRRect,
        ClipRect,
        ColoredBox,
        Colors,
        FilterQuality,
        HitTestBehavior,
        LayoutBuilder,
        Listener,
        Positioned,
        RepaintBoundary,
        Scrollable,
        Size,
        SizedBox,
        Stack,
        StatelessWidget,
        Texture,
        ValueKey,
        Widget;

/// Renders a Wayland [Surface] with scaling, input forwarding, subsurfaces,
/// and popups.
class SurfaceView extends StatelessWidget {
  /// Creates a new [SurfaceView].
  const new({
    required this.surface,
    super.key,
    this.interactive = true,
    this.filterQuality = FilterQuality.none,
    this.cornerRadius = 0.0,
    this.cubit,
    this.listenable,
  });

  /// The surface to display.
  final Surface surface;

  /// Whether the surface receives user input.
  final bool interactive;

  /// Quality filter for texture scaling.
  final FilterQuality filterQuality;

  /// Effective corner radius applied by parent overview container.
  final double cornerRadius;

  /// Optional custom or injected [SurfaceCubit].
  final SurfaceCubit? cubit;

  /// Optional [Listenable] (e.g. PageController / AnimationController)
  /// that triggers global screen bounds re-evaluation.
  final Listenable? listenable;

  @override
  Widget build(BuildContext context) {
    if (cubit != null) {
      return BlocProvider<SurfaceCubit>.value(
        value: cubit!,
        child: _SurfaceViewContent(
          surface: surface,
          interactive: interactive,
          filterQuality: filterQuality,
          cornerRadius: cornerRadius,
          listenable: listenable,
        ),
      );
    }

    SurfaceCubit? existingCubit;
    try {
      existingCubit = context.read<SurfaceCubit>();
    } on Object catch (_) {}

    if (existingCubit != null &&
        existingCubit.surface.handle == surface.handle) {
      return _SurfaceViewContent(
        surface: surface,
        interactive: interactive,
        filterQuality: filterQuality,
        cornerRadius: cornerRadius,
        listenable: listenable,
      );
    }

    return BlocProvider(
      key: ValueKey(surface.handle),
      create: (_) => SurfaceCubit(surface: surface),
      child: _SurfaceViewContent(
        surface: surface,
        interactive: interactive,
        filterQuality: filterQuality,
        cornerRadius: cornerRadius,
        listenable: listenable,
      ),
    );
  }
}

class _SurfaceViewContent extends StatelessWidget {
  const new({
    required this.surface,
    required this.interactive,
    required this.filterQuality,
    required this.cornerRadius,
    this.listenable,
  });

  final Surface surface;
  final bool interactive;
  final FilterQuality filterQuality;
  final double cornerRadius;
  final Listenable? listenable;

  @override
  Widget build(BuildContext context) {
    final cubit = context.read<SurfaceCubit>()..updateSurface(surface);

    return BlocBuilder<SurfaceCubit, SurfaceState>(
      builder: (context, state) {
        return LayoutBuilder(
          builder: (context, constraints) {
            final surfW = (surface.width ?? 0).toDouble();
            final surfH = (surface.height ?? 0).toDouble();

            if (surfW <= 0 || surfH <= 0) {
              return const SizedBox.shrink();
            }

            final outputs = CompositorRepository().outputs;
            final output = outputs.isEmpty
                ? null
                : (outputs[surface.outputId] ?? outputs.values.first);

            final isRotatedConstraintMatch =
                output != null &&
                constraints.hasBoundedWidth &&
                constraints.hasBoundedHeight &&
                constraints.maxWidth > 0 &&
                constraints.maxHeight > 0 &&
                output.width != output.height &&
                constraints.maxWidth == output.height.toDouble() &&
                constraints.maxHeight == output.width.toDouble();

            final outW = isRotatedConstraintMatch
                ? constraints.maxWidth
                : (output != null && output.width > 0
                      ? output.width.toDouble()
                      : constraints.maxWidth);
            final outH = isRotatedConstraintMatch
                ? constraints.maxHeight
                : (output != null && output.height > 0
                      ? output.height.toDouble()
                      : constraints.maxHeight);

            final isFixedSize = surface.isFixedSize;
            final isDialog = surface.isDialog;

            var effectiveSurfW = surfW;
            var effectiveSurfH = surfH;

            if (state.subsurfaces.isNotEmpty) {
              var maxSubW = 0.0;
              var maxSubH = 0.0;
              for (final sub in state.subsurfaces) {
                final subW = (sub.width != null && sub.width! > 0)
                    ? sub.width!.toDouble()
                    : (sub.bufferWidth != null && sub.bufferWidth! > 0
                          ? sub.bufferWidth!.toDouble()
                          : 0.0);
                final subH = (sub.height != null && sub.height! > 0)
                    ? sub.height!.toDouble()
                    : (sub.bufferHeight != null && sub.bufferHeight! > 0
                          ? sub.bufferHeight!.toDouble()
                          : 0.0);
                final subRight = (sub.x ?? 0).toDouble() + subW;
                final subBottom = (sub.y ?? 0).toDouble() + subH;
                if (subRight > maxSubW) maxSubW = subRight;
                if (subBottom > maxSubH) maxSubH = subBottom;
              }
              if (maxSubW > 0) {
                if (effectiveSurfW <= 0 || maxSubW > effectiveSurfW) {
                  effectiveSurfW = maxSubW;
                }
              }
              if (maxSubH > 0) {
                if (effectiveSurfH <= 0 || maxSubH > effectiveSurfH) {
                  effectiveSurfH = maxSubH;
                }
              }
            }

            final isSmallerThanOutput =
                (effectiveSurfW > 0 && effectiveSurfW < outW) ||
                (effectiveSurfH > 0 && effectiveSurfH < outH);

            final isOrientationMismatch =
                (constraints.maxWidth > constraints.maxHeight) !=
                (effectiveSurfW > effectiveSurfH);

            final isRatioMismatch =
                !isFixedSize &&
                outW > 0 &&
                outH > 0 &&
                effectiveSurfW > 0 &&
                effectiveSurfH > 0 &&
                ((outW / outH) - (effectiveSurfW / effectiveSurfH)).abs() /
                        (effectiveSurfW / effectiveSurfH) >
                    0.05;

            final shouldLetterbox =
                isFixedSize ||
                isDialog ||
                isOrientationMismatch ||
                isRatioMismatch ||
                (isSmallerThanOutput && surface.maximized != true);

            final cardScale = (outW > 0 && outH > 0)
                ? min(
                    1,
                    min(
                      constraints.maxWidth / outW,
                      constraints.maxHeight / outH,
                    ),
                  )
                : 1.0;

            final isFullscreen = surface.fullscreen ?? false;
            final baseScale = shouldLetterbox
                ? (isFullscreen ||
                          effectiveSurfW > outW ||
                          effectiveSurfH > outH)
                      ? min(outW / effectiveSurfW, outH / effectiveSurfH)
                      : 1.0
                : 1.0;
            final scale = baseScale * cardScale;
            final targetW = shouldLetterbox
                ? effectiveSurfW * scale
                : constraints.maxWidth;
            final targetH = shouldLetterbox
                ? effectiveSurfH * scale
                : constraints.maxHeight;
            final scaleX = shouldLetterbox
                ? scale
                : (effectiveSurfW > 0
                      ? constraints.maxWidth / effectiveSurfW
                      : 1.0);
            final scaleY = shouldLetterbox
                ? scale
                : (effectiveSurfH > 0
                      ? constraints.maxHeight / effectiveSurfH
                      : 1.0);

            if (constraints.hasBoundedWidth && constraints.hasBoundedHeight) {
              cubit.controller.size = Size(targetW, targetH);
            }

            final posX = shouldLetterbox
                ? ((constraints.maxWidth - targetW) / 2.0).round()
                : 0;
            final posY = shouldLetterbox
                ? ((constraints.maxHeight - targetH) / 2.0).round()
                : 0;

            if (interactive) {
              cubit.updatePosition(posX, posY);
            }

            final effectiveCornerRadius = cornerRadius;

            final scrollPosition = Scrollable.maybeOf(context)?.position;
            final mergedListenable =
                scrollPosition != null && listenable != null
                ? Listenable.merge([scrollPosition, listenable])
                : (listenable ?? scrollPosition);

            final content = MeasureSize(
              listenable: mergedListenable,
              onChange: (size) {
                if (size != null) {
                  cubit.controller.size = size;
                }
              },
              onBoundsChange: (bounds) {
                final liveOutputs = CompositorRepository().outputs;
                final liveOutput = liveOutputs.isEmpty
                    ? null
                    : (liveOutputs[surface.outputId] ??
                          liveOutputs.values.first);
                final maxOutW = max(
                  outW,
                  max(
                    (liveOutput?.width ?? 0).toDouble(),
                    constraints.hasBoundedWidth ? constraints.maxWidth : 0,
                  ),
                );
                final maxOutH = max(
                  outH,
                  max(
                    (liveOutput?.height ?? 0).toDouble(),
                    constraints.hasBoundedHeight ? constraints.maxHeight : 0,
                  ),
                );

                final pageLeft = bounds != null ? bounds.left - posX : 0.0;
                final pageRight = bounds != null
                    ? (constraints.hasBoundedWidth
                          ? pageLeft + constraints.maxWidth
                          : bounds.right)
                    : 0.0;
                final pageTop = bounds != null ? bounds.top - posY : 0.0;
                final pageBottom = bounds != null
                    ? (constraints.hasBoundedHeight
                          ? pageTop + constraints.maxHeight
                          : bounds.bottom)
                    : 0.0;

                final isOffscreen =
                    bounds == null ||
                    bounds.width <= 0 ||
                    bounds.height <= 0 ||
                    pageRight <= 0 ||
                    pageBottom <= 0 ||
                    (maxOutW > 0 && pageLeft >= maxOutW) ||
                    (maxOutH > 0 && pageTop >= maxOutH);

                if (isOffscreen) {
                  cubit.updateSceneBounds(
                    x: 0,
                    y: 0,
                    width: 0,
                    height: 0,
                    cornerRadius: 0,
                    visible: false,
                  );
                  return;
                }

                final left = bounds.left.floor();
                final top = bounds.top.floor();
                final right = bounds.right.ceil();
                final bottom = bounds.bottom.ceil();
                final sceneCornerRadius = shouldLetterbox
                    ? 0.0
                    : effectiveCornerRadius;
                cubit.updateSceneBounds(
                  x: left,
                  y: top,
                  width: right - left,
                  height: bottom - top,
                  cornerRadius:
                      sceneCornerRadius * MeasureSizeRenderObject.screenZoom,
                  visible: true,
                );
              },
              child: SurfaceTree(
                controller: cubit.controller,
                interactive: interactive,
                surface: surface,
                popups: state.popups,
                subSurfaces: state.subsurfaces,
                scaleX: scaleX,
                scaleY: scaleY,
                filterQuality: filterQuality,
              ),
            );

            final childWidget = Center(
              child: ClipRect(
                child: SizedBox(
                  width: targetW,
                  height: targetH,
                  child: content,
                ),
              ),
            );

            if (!shouldLetterbox) {
              return childWidget;
            }

            return effectiveCornerRadius > 0
                ? ClipRRect(
                    borderRadius: BorderRadius.circular(effectiveCornerRadius),
                    child: ColoredBox(color: Colors.black, child: childWidget),
                  )
                : ColoredBox(color: Colors.black, child: childWidget);
          },
        );
      },
    );
  }
}

/// Renders a full surface tree including the main surface, sub-surfaces,
/// and popups.
class SurfaceTree extends StatelessWidget {
  /// Creates a [SurfaceTree] widget.
  const new({
    required this.surface,
    required this.subSurfaces,
    required this.popups,
    required this.controller,
    this.interactive = true,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The main top-level surface to render.
  final Surface surface;

  /// The list of child sub-surfaces belonging to this surface.
  final List<SubSurface> subSurfaces;

  /// The list of popup surfaces belonging to this surface.
  final List<Popup> popups;

  /// Whether user pointer interactions are dispatched to the surface.
  final bool interactive;

  /// The platform view controller managing pointer event translation.
  final CompositorPlatformViewController controller;

  /// The horizontal scaling factor applied to the surface tree.
  final double scaleX;

  /// The vertical scaling factor applied to the surface tree.
  final double scaleY;

  /// The filter quality used when sampling surface textures.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    final Widget mainSurface = MainSurface(
      surface: surface,
      filterQuality: filterQuality,
    );

    final Widget interactiveMain = Listener(
      onPointerDown: interactive ? controller.dispatchPointerEvent : null,
      onPointerMove: interactive ? controller.dispatchPointerEvent : null,
      onPointerUp: interactive ? controller.dispatchPointerEvent : null,
      onPointerHover: interactive ? controller.dispatchPointerEvent : null,
      onPointerCancel: interactive ? controller.dispatchPointerEvent : null,
      onPointerSignal: interactive ? controller.dispatchPointerEvent : null,
      onPointerPanZoomEnd: interactive ? controller.dispatchPointerEvent : null,
      onPointerPanZoomStart: interactive
          ? controller.dispatchPointerEvent
          : null,
      onPointerPanZoomUpdate: interactive
          ? controller.dispatchPointerEvent
          : null,
      behavior: HitTestBehavior.opaque,
      child: Stack(
        clipBehavior: Clip.none,
        children: [
          RepaintBoundary(child: mainSurface),
          if (subSurfaces.isNotEmpty)
            Positioned.fill(
              child: RepaintBoundary(
                child: SubSurfaces(
                  scaleX: scaleX,
                  scaleY: scaleY,
                  subsurfaces: subSurfaces,
                  filterQuality: filterQuality,
                ),
              ),
            ),
        ],
      ),
    );

    if (popups.isEmpty) {
      return interactiveMain;
    }

    return Stack(
      clipBehavior: Clip.none,
      children: [
        interactiveMain,
        Positioned.fill(
          child: RepaintBoundary(
            child: Popups(
              popups: popups,
              scaleX: scaleX,
              scaleY: scaleY,
              filterQuality: filterQuality,
            ),
          ),
        ),
      ],
    );
  }
}

/// Renders the primary surface texture, handling geometry and CSD offsets.
class MainSurface extends StatelessWidget {
  /// Creates a [MainSurface] widget.
  const new({
    required this.surface,
    super.key,
    this.filterQuality = FilterQuality.none,
  });

  /// The surface entity whose texture is rendered.
  final Surface surface;

  /// The filter quality used when sampling the surface texture.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    final geoX = surface.geoX ?? 0;
    final geoY = surface.geoY ?? 0;
    final bufW = surface.bufferWidth ?? surface.width ?? 0;
    final bufH = surface.bufferHeight ?? surface.height ?? 0;
    final visW = (surface.width != null && surface.width! > 0)
        ? surface.width!
        : bufW;
    final visH = (surface.height != null && surface.height! > 0)
        ? surface.height!
        : bufH;

    return LayoutBuilder(
      builder: (context, constraints) {
        final targetW = constraints.maxWidth;
        final targetH = constraints.maxHeight;

        // Orientation mismatch (e.g. rotation before buffer commit)
        final isOrientationMismatch = (targetW > targetH) != (visW > visH);

        // Aspect ratio difference: check if non-uniform stretch would deform
        final isRatioMismatch =
            targetW > 0 &&
            targetH > 0 &&
            visW > 0 &&
            visH > 0 &&
            ((targetW / targetH) - (visW / visH)).abs() / (visW / visH) > 0.05;

        if (isOrientationMismatch || isRatioMismatch) {
          // Render centered without anamorphic distortion. If the surface
          // exceeds container bounds or is fullscreen, scale it to fit within
          // the viewport.
          final scale =
              ((surface.fullscreen ?? false) ||
                  visW > targetW ||
                  visH > targetH)
              ? min(targetW / visW, targetH / visH)
              : 1.0;
          final scaledVisW = visW * scale;
          final scaledVisH = visH * scale;
          final scaledBufW = (bufW > 0 ? bufW : visW) * scale;
          final scaledBufH = (bufH > 0 ? bufH : visH) * scale;
          final offsetX =
              ((targetW - scaledVisW) / 2.0).round() - (geoX * scale).round();
          final offsetY =
              ((targetH - scaledVisH) / 2.0).round() - (geoY * scale).round();

          return ColoredBox(
            color: Colors.black,
            child: ClipRect(
              child: Stack(
                children: [
                  Positioned(
                    left: offsetX.toDouble(),
                    top: offsetY.toDouble(),
                    width: scaledBufW,
                    height: scaledBufH,
                    child: Texture(
                      textureId: surface.textureId,
                      filterQuality: filterQuality,
                    ),
                  ),
                ],
              ),
            ),
          );
        }

        if (geoX > 0 ||
            geoY > 0 ||
            (bufW > visW && visW > 0) ||
            (bufH > visH && visH > 0)) {
          final scaleX = visW > 0 ? targetW / visW : 1.0;
          final scaleY = visH > 0 ? targetH / visH : 1.0;

          final texW = bufW * scaleX;
          final texH = bufH * scaleY;
          final left = -(geoX * scaleX);
          final top = -(geoY * scaleY);

          return ClipRect(
            child: Stack(
              children: [
                Positioned(
                  left: left,
                  top: top,
                  width: texW,
                  height: texH,
                  child: Texture(
                    textureId: surface.textureId,
                    filterQuality: filterQuality,
                  ),
                ),
              ],
            ),
          );
        }

        return SizedBox.expand(
          child: Texture(
            textureId: surface.textureId,
            filterQuality: filterQuality,
          ),
        );
      },
    );
  }
}

/// Renders positioned popup surfaces (e.g. menus and tooltips) in a stack.
class Popups extends StatelessWidget {
  /// Creates a [Popups] container widget.
  const new({
    required this.popups,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The list of popup surfaces to render.
  final List<Popup> popups;

  /// The horizontal scaling factor applied to the popups.
  final double scaleX;

  /// The vertical scaling factor applied to the popups.
  final double scaleY;

  /// The filter quality used when sampling popup textures.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    if (popups.isEmpty) {
      return const SizedBox.shrink();
    }
    return Stack(
      clipBehavior: Clip.none,
      children: [
        ...popups.map(
          (popup) => Positioned(
            key: ValueKey(popup.handle),
            left: popup.x.toDouble() * scaleX,
            top: popup.y.toDouble() * scaleY,
            width: popup.width > 0 ? popup.width.toDouble() * scaleX : null,
            height: popup.height > 0 ? popup.height.toDouble() * scaleY : null,
            child: RepaintBoundary(
              child: PopupView(
                key: ValueKey(popup.handle),
                popup: popup,
                scaleX: scaleX,
                scaleY: scaleY,
                filterQuality: filterQuality,
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// Renders positioned sub-surfaces nested within a parent surface.
class SubSurfaces extends StatelessWidget {
  /// Creates a [SubSurfaces] container widget.
  const new({
    required this.subsurfaces,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The list of sub-surfaces to render.
  final List<SubSurface> subsurfaces;

  /// The horizontal scaling factor applied to the sub-surfaces.
  final double scaleX;

  /// The vertical scaling factor applied to the sub-surfaces.
  final double scaleY;

  /// The filter quality used when sampling sub-surface textures.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    if (subsurfaces.isEmpty) {
      return const SizedBox.shrink();
    }
    return Stack(
      clipBehavior: Clip.none,
      children: [
        ...subsurfaces.map((subSurface) {
          final visW = (subSurface.width != null && subSurface.width! > 0)
              ? subSurface.width!.toDouble() * scaleX
              : 0.0;
          final visH = (subSurface.height != null && subSurface.height! > 0)
              ? subSurface.height!.toDouble() * scaleY
              : 0.0;
          final bufW =
              (subSurface.bufferWidth != null && subSurface.bufferWidth! > 0)
              ? subSurface.bufferWidth!.toDouble() * scaleX
              : visW;
          final bufH =
              (subSurface.bufferHeight != null && subSurface.bufferHeight! > 0)
              ? subSurface.bufferHeight!.toDouble() * scaleY
              : visH;

          final Widget content;
          if ((bufH > visH && visH > 0) || (bufW > visW && visW > 0)) {
            content = ClipRect(
              child: SizedBox(
                width: visW,
                height: visH,
                child: Stack(
                  clipBehavior: Clip.none,
                  children: [
                    Positioned(
                      left: 0,
                      top: 0,
                      width: bufW,
                      height: bufH,
                      child: Texture(
                        textureId: subSurface.textureId,
                        filterQuality: filterQuality,
                      ),
                    ),
                  ],
                ),
              ),
            );
          } else {
            content = Texture(
              textureId: subSurface.textureId,
              filterQuality: filterQuality,
            );
          }

          return Positioned(
            key: ValueKey(subSurface.handle),
            left: (subSurface.x ?? 0).toDouble() * scaleX,
            top: (subSurface.y ?? 0).toDouble() * scaleY,
            width: visW > 0 ? visW : (bufW > 0 ? bufW : null),
            height: visH > 0 ? visH : (bufH > 0 ? bufH : null),
            child: RepaintBoundary(
              child: SizedBox(
                width: visW > 0 ? visW : (bufW > 0 ? bufW : null),
                height: visH > 0 ? visH : (bufH > 0 ? bufH : null),
                child: content,
              ),
            ),
          );
        }),
      ],
    );
  }
}
