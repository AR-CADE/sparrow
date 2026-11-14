import 'dart:async' show StreamSubscription, unawaited;

import 'package:compositor_dart/src/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/src/data/models/layer_surface.dart'
    show LayerSurface;
import 'package:compositor_dart/src/data/models/sub_surface.dart'
    show SubSurface;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/domain/layer_surface/cubit/layer_surface_state.dart';
import 'package:flutter_bloc/flutter_bloc.dart' show Cubit;

/// Cubit managing nested Wayland subsurfaces for a [LayerSurface].
class LayerSurfaceCubit extends Cubit<LayerSurfaceState> {
  /// Creates a new [LayerSurfaceCubit] for [layerSurface].
  new({required this.layerSurface, CompositorRepository? repository})
    : _repository = repository ?? CompositorRepository(),
      super(const LayerSurfaceState()) {
    _refresh();
    _updateSubscription = _repository.events.listen(_onEvent);
  }

  /// Parent handle offset matching `kLayerSubsurfaceParentOffset` in C++.
  static const int parentHandleOffset = 300000;

  /// The layer surface whose child subsurfaces are tracked.
  LayerSurface layerSurface;

  final CompositorRepository _repository;
  StreamSubscription<CompositorEvent>? _updateSubscription;

  int get _parentHandle => parentHandleOffset + layerSurface.handle;

  void _onEvent(CompositorEvent event) {
    if (isClosed) return;
    if (event.type == .subsurfacePositionChange ||
        event.type == .subSurfaceUnMap ||
        event.type == .subSurfaceMap) {
      final subSurface = event.event as SubSurface;
      if (subSurface.parentHandle == _parentHandle) {
        _refresh();
      }
    }
  }

  void _refresh() {
    final subsurfaces = List<SubSurface>.from(
      _repository.subSurfaceSetLookUp(_parentHandle)?.toList() ?? const [],
    );
    emit(state.copyWith(subsurfaces: subsurfaces));
  }

  /// Updates the tracked [layerSurface] when widget properties change.
  void updateLayerSurface(LayerSurface newLayerSurface) {
    if (layerSurface != newLayerSurface) {
      layerSurface = newLayerSurface;
      _refresh();
    }
  }

  @override
  Future<void> close() {
    if (_updateSubscription != null) {
      unawaited(_updateSubscription!.cancel());
      _updateSubscription = null;
    }
    return super.close();
  }
}

