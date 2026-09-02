part of 'output_bloc.dart';

class OutputBlocState extends Equatable {
  const OutputBlocState._({
    this.outputs = const [],
  });

  const OutputBlocState.set({
    List<DisplayOutput>? outputs,
  }) : this._(
         outputs: outputs ?? const [],
       );

  final List<DisplayOutput> outputs;

  @override
  List<Object?> get props => [outputs];
}
