import 'dart:async' show StreamSubscription, unawaited;
import 'dart:math' show min;

import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/popup.dart' show Popup;
import 'package:compositor_dart/data/models/sub_surface.dart' show SubSurface;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/presentation/compositor_platform_view_controller.dart'
    show CompositorPlatformViewController;
import 'package:compositor_dart/presentation/measure_size.dart'
    show MeasureSize;
import 'package:compositor_dart/presentation/popup.dart' show PopupView;
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
        State,
        StatefulWidget,
        StatelessWidget,
        Texture,
        ValueKey,
        Widget;

class SurfaceView extends StatefulWidget {
  const new({
    required this.surface,
    super.key,
    this.interactive = true,
    this.freeze = false,
    this.filterQuality = FilterQuality.none,
  });
  final Surface surface;
  final bool interactive;
  final bool freeze;
  final FilterQuality filterQuality;

  @override
  State<SurfaceView> createState() => _SurfaceViewState();
}

class _SurfaceViewState extends State<SurfaceView> {
  late CompositorPlatformViewController controller;
  List<Popup> _popups = [];
  StreamSubscription<CompositorEvent>? _updateSubscription;
  List<SubSurface> _subsurfaces = [];
  int? _lastSentX;
  int? _lastSentY;

  @override
  void initState() {
    controller = CompositorPlatformViewController(surface: widget.surface);
    _popups = List.from(
      CompositorRepository().popupSetLookUp(widget.surface.handle)?.toList() ??
          [],
    );
    _subsurfaces = List.from(
      CompositorRepository()
              .subSurfaceSetLookUp(widget.surface.handle)
              ?.toList() ??
          [],
    );
    _updateSubscription = CompositorRepository().events.listen((event) {
      if (event.type == .subsurfacePositionChange ||
          event.type == .subSurfaceUnMap ||
          event.type == .subSurfaceMap ||
          event.type == .popupMap ||
          event.type == .popupUnMap ||
          event.type == .outputChanged) {
        int? handle;

        if (event.type == .popupMap || event.type == .popupUnMap) {
          final popup = event.event as Popup;
          handle = popup.parentHandle;
        }

        if (event.type == .subsurfacePositionChange ||
            event.type == .subSurfaceUnMap ||
            event.type == .subSurfaceMap) {
          final subSurface = event.event as SubSurface;
          handle = subSurface.parentHandle;
        }

        if (event.type == .outputChanged) {
          final out = event.event as DisplayOutput;
          if (widget.surface.outputId == out.id) {
            handle = widget.surface.handle;
          }
        }

        if (handle == widget.surface.handle) {
          setState(() {
            _popups = List.from(
              CompositorRepository()
                      .popupSetLookUp(widget.surface.handle)
                      ?.toList() ??
                  [],
            );
            _subsurfaces = List.from(
              CompositorRepository()
                      .subSurfaceSetLookUp(widget.surface.handle)
                      ?.toList() ??
                  [],
            );
          });
        }
      }
    });
    super.initState();
  }

  @override
  void didUpdateWidget(SurfaceView oldWidget) {
    if (oldWidget.surface != widget.surface) {
      _lastSentX = null;
      _lastSentY = null;
      unawaited(
        controller.dispose().then((onValue) {
          controller = CompositorPlatformViewController(
            surface: widget.surface,
          );
          _popups = List.from(
            CompositorRepository()
                    .popupSetLookUp(widget.surface.handle)
                    ?.toList() ??
                [],
          );
          _subsurfaces = List.from(
            CompositorRepository()
                    .subSurfaceSetLookUp(widget.surface.handle)
                    ?.toList() ??
                [],
          );
          setState(() {});
        }),
      );
    }
    super.didUpdateWidget(oldWidget);
  }

  @override
  void dispose() {
    unawaited(_updateSubscription?.cancel());
    unawaited(controller.dispose());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final surfW = (widget.surface.width ?? 0).toDouble();
        final surfH = (widget.surface.height ?? 0).toDouble();

        if (surfW <= 0 || surfH <= 0) {
          return const SizedBox.shrink();
        }

        final outputs = CompositorRepository().outputs;
        final output = outputs.isEmpty
            ? null
            : (outputs[widget.surface.outputId] ?? outputs.values.first);

        final outW = output != null && output.width > 0
            ? output.width.toDouble()
            : constraints.maxWidth;
        final outH = output != null && output.height > 0
            ? output.height.toDouble()
            : constraints.maxHeight;

        final isFixedSize = widget.surface.isFixedSize;
        final shouldLetterbox = isFixedSize;

        // Scale factor of card relative to display output (e.g. in overview)
        final cardScale = (outW > 0 && outH > 0)
            ? min(
                1,
                min(constraints.maxWidth / outW, constraints.maxHeight / outH),
              )
            : 1.0;

        final baseScale = shouldLetterbox
            ? (surfW > outW || surfH > outH)
                  ? min(outW / surfW, outH / surfH)
                  : 1.0
            : 1.0;
        final scale = baseScale * cardScale;
        final targetW = shouldLetterbox ? surfW * scale : constraints.maxWidth;
        final targetH = shouldLetterbox ? surfH * scale : constraints.maxHeight;
        final scaleX = shouldLetterbox
            ? scale
            : (surfW > 0 ? constraints.maxWidth / surfW : 1.0);
        final scaleY = shouldLetterbox
            ? scale
            : (surfH > 0 ? constraints.maxHeight / surfH : 1.0);

        if (constraints.hasBoundedWidth && constraints.hasBoundedHeight) {
          controller.size = Size(targetW, targetH);
        }

        final posX = shouldLetterbox
            ? ((constraints.maxWidth - targetW) / 2.0).round()
            : 0;
        final posY = shouldLetterbox
            ? ((constraints.maxHeight - targetH) / 2.0).round()
            : 0;

        if (widget.interactive && (_lastSentX != posX || _lastSentY != posY)) {
          _lastSentX = posX;
          _lastSentY = posY;
          unawaited(
            CompositorRepository().platform.surfaceSetPosition(
              widget.surface,
              posX,
              posY,
            ),
          );
        }

        final content = MeasureSize(
          onChange: (size) {
            if (size != null) {
              controller.size = size;
            }
          },
          child: SurfaceTree(
            controller: controller,
            interactive: widget.interactive,
            freeze: widget.freeze,
            surface: widget.surface,
            popups: _popups,
            subSurfaces: _subsurfaces,
            scaleX: scaleX,
            scaleY: scaleY,
            filterQuality: widget.filterQuality,
          ),
        );

        if (shouldLetterbox) {
          return ColoredBox(
            color: Colors.black,
            child: Center(
              child: SizedBox(width: targetW, height: targetH, child: content),
            ),
          );
        }

        return SizedBox.expand(child: content);
      },
    );
  }
}

class SurfaceTree extends StatelessWidget {
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

  final Surface surface;
  final List<SubSurface> subSurfaces;
  final List<Popup> popups;
  final bool freeze;
  final bool interactive;
  final CompositorPlatformViewController controller;
  final double scaleX;
  final double scaleY;
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

class MainSurface extends StatelessWidget {
  const new({
    required this.surface,
    required this.freeze,
    super.key,
    this.filterQuality = FilterQuality.none,
  });

  final Surface surface;
  final bool freeze;
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
          // Render at 1:1 scale centered without anamorphic distortion
          final offsetX = ((targetW - visW) / 2.0).round() - geoX;
          final offsetY = ((targetH - visH) / 2.0).round() - geoY;

          return ClipRect(
            child: Stack(
              children: [
                Positioned(
                  left: offsetX.toDouble(),
                  top: offsetY.toDouble(),
                  width: (bufW > 0 ? bufW : visW).toDouble(),
                  height: (bufH > 0 ? bufH : visH).toDouble(),
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

class Popups extends StatelessWidget {
  const new({
    required this.popups,
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  final List<Popup> popups;
  final bool freeze;
  final double scaleX;
  final double scaleY;
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

class SubSurfaces extends StatelessWidget {
  const new({
    required this.subsurfaces,
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  final List<SubSurface> subsurfaces;
  final bool freeze;
  final double scaleX;
  final double scaleY;
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
