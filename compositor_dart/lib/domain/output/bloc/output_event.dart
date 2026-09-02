part of 'output_bloc.dart';

sealed class OutputBlocEvent extends Equatable {
  const OutputBlocEvent();
}

@visibleForTesting
final class RemoveAllOutputs extends OutputBlocEvent {
  const RemoveAllOutputs();

  @override
  List<Object?> get props => [];
}

@visibleForTesting
final class OutputStateChanged extends OutputBlocEvent {
  const OutputStateChanged({required this.compositorEvent});
  final CompositorEvent compositorEvent;

  @override
  List<Object?> get props => [compositorEvent];
}
