import 'dart:async' show StreamSubscription, unawaited;

import 'package:compositor_dart/src/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/src/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/data/models/sub_surface.dart'
    show SubSurface;
import 'package:compositor_dart/src/data/models/surface.dart' show Surface;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/domain/surface/cubit/surface_state.dart';
import 'package:compositor_dart/src/presentation/compositor_platform_view_controller.dart'
    show CompositorPlatformViewController;
import 'package:flutter_bloc/flutter_bloc.dart' show Cubit;

/// Cubit managing popups, subsurfaces, and platform view controller
/// for a [Surface].
class SurfaceCubit extends Cubit<SurfaceState> {
  /// Creates a new [SurfaceCubit].
  new({
    required this.surface,
    CompositorRepository? repository,
    CompositorPlatformViewController? controller,
  }) : _repository = repository ?? CompositorRepository(),
       controller =
           controller ?? CompositorPlatformViewController(surface: surface),
       super(const SurfaceState()) {
    _init();
  }

  /// The surface being displayed.
  Surface surface;

  final CompositorRepository _repository;

  /// The platform view controller for dispatching input events.
  final CompositorPlatformViewController controller;

  StreamSubscription<CompositorEvent>? _updateSubscription;
  int? _lastSentX;
  int? _lastSentY;
  int? _lastBoundsX;
  int? _lastBoundsY;
  int? _lastBoundsW;
  int? _lastBoundsH;
  double? _lastCornerRadius;
  bool? _lastVisible;

  static final Map<int, SurfaceCubit> _activeCubits = <int, SurfaceCubit>{};

  /// The last X position sent to the compositor platform.
  int? get lastSentX => _lastSentX;

  /// The last Y position sent to the compositor platform.
  int? get lastSentY => _lastSentY;

  void _init() {
    _activeCubits[surface.handle] = this;
    _refresh();
    _updateSubscription = _repository.events.listen(_onEvent);
  }

  void _onEvent(CompositorEvent event) {
    if (isClosed) return;
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
        if (surface.outputId == out.id) {
          handle = surface.handle;
        }
      }

      if (handle == surface.handle) {
        _refresh();
      }
    }
  }

  void _refresh() {
    final popups = List<Popup>.from(
      _repository.popupSetLookUp(surface.handle)?.toList() ?? const [],
    );
    final subsurfaces = List<SubSurface>.from(
      _repository.subSurfaceSetLookUp(surface.handle)?.toList() ?? const [],
    );
    emit(state.copyWith(popups: popups, subsurfaces: subsurfaces));
  }

  /// Updates the surface model when widget properties change.
  void updateSurface(Surface newSurface) {
    if (surface != newSurface) {
      if (surface.handle != newSurface.handle &&
          _activeCubits[surface.handle] == this) {
        _activeCubits.remove(surface.handle);
      }
      surface = newSurface;
      _activeCubits[surface.handle] = this;
      _lastSentX = null;
      _lastSentY = null;
      _lastBoundsX = null;
      _lastBoundsY = null;
      _lastBoundsW = null;
      _lastBoundsH = null;
      _lastCornerRadius = null;
      _lastVisible = null;
      _refresh();
    }
  }

  /// Updates surface position on the platform if changed.
  void updatePosition(int posX, int posY) {
    if (_lastSentX != posX || _lastSentY != posY) {
      _lastSentX = posX;
      _lastSentY = posY;
      unawaited(_repository.platform.surfaceSetPosition(surface, posX, posY));
    }
  }

  /// Updates the global screen-space bounds, [cornerRadius], and [visible]
  /// status of the surface on the platform.
  void updateSceneBounds({
    required int x,
    required int y,
    required int width,
    required int height,
    required double cornerRadius,
    required bool visible,
  }) {
    final active = _activeCubits[surface.handle];
    if (!visible && active != null && active != this) {
      return;
    }
    if (visible) {
      _activeCubits[surface.handle] = this;
    }
    if (_lastBoundsX == x &&
        _lastBoundsY == y &&
        _lastBoundsW == width &&
        _lastBoundsH == height &&
        _lastCornerRadius == cornerRadius &&
        _lastVisible == visible) {
      return;
    }
    _lastBoundsX = x;
    _lastBoundsY = y;
    _lastBoundsW = width;
    _lastBoundsH = height;
    _lastCornerRadius = cornerRadius;
    _lastVisible = visible;
    unawaited(
      _repository.platform.surfaceSetSceneBounds(
        surface,
        x,
        y,
        width,
        height,
        cornerRadius,
        visible: visible,
      ),
    );
  }

  @override
  Future<void> close() async {
    if (_activeCubits[surface.handle] == this) {
      _activeCubits.remove(surface.handle);
    }
    await _updateSubscription?.cancel();
    await controller.dispose();
    await super.close();
  }
}
