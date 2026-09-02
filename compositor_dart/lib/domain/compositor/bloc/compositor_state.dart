part of 'compositor_bloc.dart';

class CompositorBlocState extends Equatable {
  const CompositorBlocState._({
    this.ready = false,
    this.output,
    this.surfaces = const [],
    this.pageIndex = 0,
  });

  const CompositorBlocState.set({
    bool? ready,
    DisplayOutput? output,
    List<Surface>? surfaces,
    int? pageIndex,
  }) : this._(
         ready: ready ?? false,
         output: output,
         surfaces: surfaces ?? const [],
         pageIndex: pageIndex ?? 0,
       );

  final bool ready;
  final DisplayOutput? output;
  final List<Surface> surfaces;
  final int pageIndex;

  @override
  List<Object?> get props => [ready, output, surfaces, pageIndex];
}
