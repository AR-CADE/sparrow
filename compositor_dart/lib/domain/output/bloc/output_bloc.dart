import 'dart:async' show Future, StreamSubscription;

import 'package:bloc/bloc.dart' show Bloc;
import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:equatable/equatable.dart' show Equatable;
import 'package:flutter_bloc/flutter_bloc.dart' show Emitter;
import 'package:material_ui/material_ui.dart' show visibleForTesting;

part 'output_event.dart';
part 'output_state.dart';

class OutputDispatcherBloc extends Bloc<OutputBlocEvent, OutputBlocState> {
  OutputDispatcherBloc(CompositorRepository compositorRepository)
    : _compositorRepository = compositorRepository,
      super(const OutputBlocState.set()) {
    on<OutputStateChanged>(_onOutputStateChanged);
    on<RemoveAllOutputs>(_onRemoveAllOutputs);

    _eventSubscription = _compositorRepository.events.listen(
      (event) => add(OutputStateChanged(compositorEvent: event)),
    );
  }

  late final CompositorRepository _compositorRepository;
  late final StreamSubscription<CompositorEvent> _eventSubscription;

  Future<void> _onOutputStateChanged(
    OutputStateChanged event,
    Emitter<OutputBlocState> emit,
  ) async {
    if (event.compositorEvent.type == .outputChanged ||
        event.compositorEvent.type == .outputRemoved ||
        event.compositorEvent.type == .outputAdded) {
      if (event.compositorEvent.type == .outputAdded) {
        final outputState = _onOutputAdded(event);
        if (outputState == null) {
          return;
        }
        return emit(outputState);
      }

      if (event.compositorEvent.type == .outputChanged) {
        final outputState = _onOutputChanged(event);
        if (outputState == null) {
          return;
        }
        return emit(outputState);
      }

      if (event.compositorEvent.type == .outputRemoved) {
        final outputState = _onOutputRemoved(event);
        if (outputState == null) {
          return;
        }
        return emit(outputState);
      }
    }
  }

  OutputBlocState? _onOutputAdded(OutputStateChanged event) {
    final out = event.compositorEvent as DisplayOutput;

    state.outputs.add(out);

    /* debugPrint(
      'OUTPUT ADDED: ${out.id} ${out.width} ${out.height}',
    ); */
    return OutputBlocState.set(outputs: state.outputs);
  }

  OutputBlocState? _onOutputChanged(OutputStateChanged event) {
    final out = event.compositorEvent as DisplayOutput;

    /* debugPrint(
      'OUTPUT CHANGED: ${out.id} '
      '${out.width} ${out.height}',
    ); */

    final index = state.outputs.indexWhere((o) => o.id == out.id);

    if (index == -1) {
      return null;
    }

    state.outputs[index] = out;

    return OutputBlocState.set(outputs: state.outputs);
  }

  OutputBlocState? _onOutputRemoved(OutputStateChanged event) {
    final out = event.compositorEvent as DisplayOutput;

    final removed = state.outputs.firstWhereOrNull((o) => o.id == out.id);

    if (removed == null) {
      return null;
    }

    state.outputs.remove(removed);

    return OutputBlocState.set(outputs: state.outputs);
  }

  Future<void> _onRemoveAllOutputs(
    RemoveAllOutputs event,
    Emitter<OutputBlocState> emit,
  ) async {
    state.outputs.clear();

    return emit(OutputBlocState.set(outputs: state.outputs));
  }

  @override
  Future<void> close() async {
    add(const RemoveAllOutputs());
    await _eventSubscription.cancel();
    return super.close();
  }
}
