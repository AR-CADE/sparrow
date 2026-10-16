import 'dart:async';

import 'package:bloc/bloc.dart';
import 'package:compositor_dart/src/data/models/compositor_event.dart';
import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:compositor_dart/src/data/models/display_output.dart';
import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:compositor_dart/src/data/models/surface_request_activate_event.dart';
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart';
import 'package:equatable/equatable.dart';

part 'compositor_event.dart';
part 'compositor_state.dart';

/// Manages compositor state by listening to events from [CompositorRepository].
class CompositorBloc extends Bloc<CompositorBlocEvent, CompositorState> {
  /// Creates a [CompositorBloc] backed by the given [repository].
  new({required this.repository}) : super(const CompositorState()) {
    on<CompositorSubscriptionRequested>(_onSubscriptionRequested);
    on<CompositorEventReceived>(_onEventReceived);
    on<CompositorSurfaceFocusRequested>(_onSurfaceFocusRequested);
    on<CompositorErrorReceived>(_onErrorReceived);
  }

  /// The underlying repository providing compositor events and state.
  final CompositorRepository repository;
  StreamSubscription<CompositorEvent>? _eventSubscription;

  Future<void> _onSubscriptionRequested(
    CompositorSubscriptionRequested event,
    Emitter<CompositorState> emit,
  ) async {
    emit(
      state.copyWith(
        status: CompositorStatus.ready,
        surfaces: Map.unmodifiable(repository.surfaces),
        outputs: Map.unmodifiable(repository.outputs),
      ),
    );

    await _eventSubscription?.cancel();
    _eventSubscription = repository.events.listen(
      (compositorEvent) => add(CompositorEventReceived(compositorEvent)),
      onError: (Object error) => add(CompositorErrorReceived(error.toString())),
    );
  }

  void _onErrorReceived(
    CompositorErrorReceived event,
    Emitter<CompositorState> emit,
  ) {
    emit(
      state.copyWith(
        status: CompositorStatus.failure,
        errorMessage: event.message,
      ),
    );
  }

  void _onEventReceived(
    CompositorEventReceived event,
    Emitter<CompositorState> emit,
  ) {
    final compEvent = event.event;
    final updatedSurfaces = Map<int, Surface>.from(state.surfaces);
    final updatedOutputs = Map<int, DisplayOutput>.from(state.outputs);
    var activeId = state.activeSurfaceId;

    switch (compEvent.type) {
      case CompositorEventType.surfaceMap:
        if (compEvent.event is Surface) {
          final s = compEvent.event! as Surface;
          updatedSurfaces[s.handle] = s;
          activeId ??= s.handle;
        }
      case CompositorEventType.surfaceUnMap:
        if (compEvent.event is Surface) {
          final s = compEvent.event! as Surface;
          updatedSurfaces.remove(s.handle);
          if (activeId == s.handle) {
            activeId = updatedSurfaces.keys.lastOrNull;
          }
        }
      case CompositorEventType.surfaceTitleChange:
      case CompositorEventType.surfaceGeometryChange:
      case CompositorEventType.surfaceDecorationChange:
      case CompositorEventType.surfaceFullscreenChange:
      case CompositorEventType.surfacePositionChange:
      case CompositorEventType.surfaceMinimizeRequest:
      case CompositorEventType.surfaceRequestMaximize:
      case CompositorEventType.surfaceresizeRequest:
      case CompositorEventType.surfaceGrabEnd:
        if (compEvent.event is Surface) {
          final s = compEvent.event! as Surface;
          updatedSurfaces[s.handle] = s;
        }
      case CompositorEventType.surfaceRequestActivate:
        if (compEvent.event is SurfaceRequestActivateEvent) {
          final req = compEvent.event! as SurfaceRequestActivateEvent;
          activeId = req.handle;
        }
      case CompositorEventType.outputAdded:
      case CompositorEventType.outputChanged:
        if (compEvent.event is DisplayOutput) {
          final o = compEvent.event! as DisplayOutput;
          updatedOutputs[o.id] = o;
        }
      case CompositorEventType.outputRemoved:
        if (compEvent.event is DisplayOutput) {
          final o = compEvent.event! as DisplayOutput;
          updatedOutputs.remove(o.id);
        }
      case CompositorEventType.subSurfaceMap:
      case CompositorEventType.subSurfaceUnMap:
      case CompositorEventType.subsurfacePositionChange:
      case CompositorEventType.popupMap:
      case CompositorEventType.popupUnMap:
      case CompositorEventType.gestureSwipeBegin:
      case CompositorEventType.gestureSwipeUpdate:
      case CompositorEventType.gestureSwipeEnd:
      case CompositorEventType.zoomScroll:
      case CompositorEventType.zoomKey:
      case CompositorEventType.dockReveal:
        break;
    }

    emit(
      state.copyWith(
        surfaces: Map.unmodifiable(updatedSurfaces),
        outputs: Map.unmodifiable(updatedOutputs),
        activeSurfaceId: activeId,
        lastEvent: compEvent,
      ),
    );
  }

  void _onSurfaceFocusRequested(
    CompositorSurfaceFocusRequested event,
    Emitter<CompositorState> emit,
  ) {
    if (state.surfaces.containsKey(event.surfaceId)) {
      emit(state.copyWith(activeSurfaceId: event.surfaceId));
    }
  }

  @override
  Future<void> close() async {
    await _eventSubscription?.cancel();
    return await super.close();
  }
}
