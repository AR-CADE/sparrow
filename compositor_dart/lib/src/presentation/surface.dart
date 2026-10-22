import 'dart:math' show min;

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
import 'package:compositor_dart/src/presentation/popup.dart' show PopupView;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocBuilder, BlocProvider, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Center,
        Clip,
        ClipRect,
        ColoredBox,
        Colors,
        FilterQuality,
        HitTestBehavior,
        LayoutBuilder,
        Listener,
        Positioned,
        RepaintBoundary,
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
    this.freeze = false,
    this.filterQuality = FilterQuality.none,
    this.cubit,
  });

  /// The surface to display.
  final Surface surface;

  /// Whether the surface receives user input.
  final bool interactive;

  /// Whether rendering of the texture is frozen.
  final bool freeze;

  /// Quality filter for texture scaling.
  final FilterQuality filterQuality;

  /// Optional custom or injected [SurfaceCubit].
  final SurfaceCubit? cubit;

  @override
  Widget build(BuildContext context) {
    if (cubit != null) {
      return BlocProvider<SurfaceCubit>.value(
        value: cubit!,
        child: _SurfaceViewContent(
          surface: surface,
          interactive: interactive,
          freeze: freeze,
          filterQuality: filterQuality,
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
        freeze: freeze,
        filterQuality: filterQuality,
      );
    }

    return BlocProvider(
      key: ValueKey(surface.handle),
      create: (_) => SurfaceCubit(surface: surface),
      child: _SurfaceViewContent(
        surface: surface,
        interactive: interactive,
        freeze: freeze,
        filterQuality: filterQuality,
      ),
    );
  }
}

class _SurfaceViewContent extends StatelessWidget {
  const new({
    required this.surface,
    required this.interactive,
    required this.freeze,
    required this.filterQuality,
  });

  final Surface surface;
  final bool interactive;
  final bool freeze;
  final FilterQuality filterQuality;

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

            final outW = output != null && output.width > 0
                ? output.width.toDouble()
                : constraints.maxWidth;
            final outH = output != null && output.height > 0
                ? output.height.toDouble()
                : constraints.maxHeight;

            final isFixedSize = surface.isFixedSize;
            final isDialog = surface.isDialog;
            final isNotMaximized = surface.maximized == false;
            final isSmallerThanOutput =
                (surfW > 0 && surfW < outW) || (surfH > 0 && surfH < outH);

            final isOrientationMismatch =
                (constraints.maxWidth > constraints.maxHeight) !=
                (surfW > surfH);

            final isRatioMismatch =
                !isFixedSize &&
                outW > 0 &&
                outH > 0 &&
                surfW > 0 &&
                surfH > 0 &&
                ((outW / outH) - (surfW / surfH)).abs() / (surfW / surfH) >
                    0.05;

            final shouldLetterbox =
                isFixedSize ||
                isDialog ||
                isOrientationMismatch ||
                (isSmallerThanOutput &&
                    (surface.maximized != true || isRatioMismatch));

            var effectiveSurfW = surfW;
            var effectiveSurfH = surfH;

            if (state.subsurfaces.isNotEmpty && (isDialog || isNotMaximized)) {
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
              if (maxSubW > 0 &&
                  maxSubH > 0 &&
                  (maxSubW < effectiveSurfW || maxSubH < effectiveSurfH)) {
                effectiveSurfW = maxSubW;
                effectiveSurfH = maxSubH;
              }
            }

            final cardScale = (outW > 0 && outH > 0)
                ? min(
                    1,
                    min(
                      constraints.maxWidth / outW,
                      constraints.maxHeight / outH,
                    ),
                  )
                : 1.0;

            final baseScale = shouldLetterbox
                ? (effectiveSurfW > outW || effectiveSurfH > outH)
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

            final content = MeasureSize(
              onChange: (size) {
                if (size != null) {
                  cubit.controller.size = size;
                }
              },
              child: SurfaceTree(
                controller: cubit.controller,
                interactive: interactive,
                freeze: freeze,
                surface: surface,
                popups: state.popups,
                subSurfaces: state.subsurfaces,
                scaleX: scaleX,
                scaleY: scaleY,
                filterQuality: filterQuality,
              ),
            );

            if (shouldLetterbox) {
              return ColoredBox(
                color: Colors.black,
                child: Center(
                  child: ClipRect(
                    child: SizedBox(
                      width: targetW,
                      height: targetH,
                      child: content,
                    ),
                  ),
                ),
              );
            }

            return SizedBox.expand(child: content);
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
    required this.freeze,
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

  /// Whether to freeze texture updates on all surfaces in this tree.
  final bool freeze;

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
      freeze: freeze,
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
                  freeze: freeze,
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
              freeze: freeze,
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
    required this.freeze,
    super.key,
    this.filterQuality = FilterQuality.none,
  });

  /// The surface entity whose texture is rendered.
  final Surface surface;

  /// Whether to freeze texture updates on this surface.
  final bool freeze;

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
          // exceeds container bounds, scale it down to fit within the viewport.
          final scale = (visW > targetW || visH > targetH)
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
                      freeze: freeze,
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
                    freeze: freeze,
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
            freeze: freeze,
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
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The list of popup surfaces to render.
  final List<Popup> popups;

  /// Whether to freeze texture updates on the popups.
  final bool freeze;

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
                freeze: freeze,
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
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The list of sub-surfaces to render.
  final List<SubSurface> subsurfaces;

  /// Whether to freeze texture updates on the sub-surfaces.
  final bool freeze;

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
                        freeze: freeze,
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
              freeze: freeze,
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
