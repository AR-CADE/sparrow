import 'dart:async' show StreamSubscription, unawaited;

import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
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
        Clip,
        ClipRect,
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
  });
  final Surface surface;
  final bool interactive;
  final bool freeze;

  @override
  State<SurfaceView> createState() => _SurfaceViewState();
}

class _SurfaceViewState extends State<SurfaceView> {
  late CompositorPlatformViewController controller;
  List<Popup> _popups = [];
  StreamSubscription<CompositorEvent>? _updateSubscription;
  List<SubSurface> _subsurfaces = [];

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
          event.type == .popupUnMap) {
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

        if (constraints.hasBoundedWidth && constraints.hasBoundedHeight) {
          controller.size = Size(constraints.maxWidth, constraints.maxHeight);
        }

        final scaleX = constraints.maxWidth / surfW;
        final scaleY = constraints.maxHeight / surfH;
        // final scale = scaleX < scaleY ? scaleX : scaleY;

        return SizedBox.expand(
          child: MeasureSize(
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
            ),
          ),
        );
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

  @override
  Widget build(BuildContext context) {
    final Widget mainSurface = MainSurface(surface: surface, freeze: freeze);

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
            ),
          ),
        ),
      ],
    );
  }
}

class MainSurface extends StatelessWidget {
  const new({required this.surface, required this.freeze, super.key});

  final Surface surface;
  final bool freeze;

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

    if (geoX > 0 ||
        geoY > 0 ||
        (bufW > visW && visW > 0) ||
        (bufH > visH && visH > 0)) {
      return LayoutBuilder(
        builder: (context, constraints) {
          final targetW = constraints.maxWidth;
          final targetH = constraints.maxHeight;
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
                    filterQuality: .none,
                  ),
                ),
              ],
            ),
          );
        },
      );
    }

    return SizedBox.expand(
      child: Texture(
        freeze: freeze,
        textureId: surface.textureId,
        filterQuality: .none,
      ),
    );
  }
}

class Popups extends StatelessWidget {
  const new({
    required this.popups,
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    super.key,
  });

  final List<Popup> popups;
  final bool freeze;
  final double scaleX;
  final double scaleY;

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
                ratio: scaleX < scaleY ? scaleX : scaleY,
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
    super.key,
  });

  final List<SubSurface> subsurfaces;
  final bool freeze;
  final double scaleX;
  final double scaleY;

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
          if (bufH > visH && visH > 0) {
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
                        filterQuality: .none,
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
              filterQuality: .none,
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
