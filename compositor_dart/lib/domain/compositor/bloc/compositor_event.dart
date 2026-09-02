part of 'compositor_bloc.dart';

sealed class CompositorBlocEvent extends Equatable {
  const CompositorBlocEvent();
}

final class _SetReadyRequested extends CompositorBlocEvent {
  const _SetReadyRequested({required this.ready});
  final bool ready;

  @override
  List<Object?> get props => [ready];
}

final class _CompositorStateChanged extends CompositorBlocEvent {
  const _CompositorStateChanged({required this.event});
  final CompositorEvent event;

  @override
  List<Object?> get props => [event];
}
