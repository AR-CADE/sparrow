import 'dart:async' show Future, StreamSubscription;

import 'package:bloc/bloc.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/data/repositories/configuration_repository.dart'
    show ConfigurationRepository;
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';

/// Cubit managing the shell configuration lifecycle and real-time updates.
class ConfigurationCubit extends Cubit<ConfigurationState> {
  /// Creates a new [ConfigurationCubit].
  new({required this.repository}) : super(const ConfigurationState());

  /// The repository providing configuration persistence and file watching.
  final ConfigurationRepository repository;

  StreamSubscription<ShellConfiguration>? _subscription;

  /// Initializes configuration and starts listening to stream updates.
  Future<void> init() async {
    emit(state.copyWith(status: ConfigurationStatus.loading));
    try {
      await repository.init();
      emit(
        state.copyWith(
          status: ConfigurationStatus.ready,
          configuration: repository.currentConfiguration,
        ),
      );

      await _subscription?.cancel();
      _subscription = repository.configurationStream.listen(
        (configuration) {
          emit(
            state.copyWith(
              status: ConfigurationStatus.ready,
              configuration: configuration,
            ),
          );
        },
        onError: (Object error) {
          emit(
            state.copyWith(
              status: ConfigurationStatus.failure,
              errorMessage: error.toString(),
            ),
          );
        },
      );
    } on Object catch (error) {
      emit(
        state.copyWith(
          status: ConfigurationStatus.failure,
          errorMessage: error.toString(),
        ),
      );
    }
  }

  @override
  Future<void> close() async {
    await _subscription?.cancel();
    await super.close();
  }
}
