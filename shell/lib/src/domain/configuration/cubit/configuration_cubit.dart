import 'dart:async' show Future, StreamSubscription, unawaited;

import 'package:bloc/bloc.dart';
import 'package:compositor_dart/compositor_dart.dart' show CompositorRepository;
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/data/repositories/configuration_repository.dart'
    show ConfigurationRepository;
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';

/// Cubit managing the shell configuration lifecycle and real-time updates.
class ConfigurationCubit extends Cubit<ConfigurationState> {
  /// Creates a new [ConfigurationCubit].
  new({required this.repository, this.onSyncSensitiveAppIds})
    : super(const ConfigurationState());

  /// The repository providing configuration persistence and file watching.
  final ConfigurationRepository repository;

  /// Optional callback invoked to synchronize sensitive application IDs.
  final Future<void> Function(List<String> appIds)? onSyncSensitiveAppIds;

  StreamSubscription<ShellConfiguration>? _subscription;

  Future<void> _syncSensitiveApps(List<String> appIds) async {
    try {
      final syncFn =
          onSyncSensitiveAppIds ??
          CompositorRepository().platform.setSensitiveAppIds;
      await syncFn(appIds);
    } on Object catch (_) {
      // Ignore platform channel errors when running without a live engine
    }
  }

  /// Initializes configuration and starts listening to stream updates.
  Future<void> init() async {
    emit(state.copyWith(status: ConfigurationStatus.loading));
    try {
      await repository.init();
      final initialConfig = repository.currentConfiguration;
      unawaited(_syncSensitiveApps(initialConfig.secureContents));
      emit(
        state.copyWith(
          status: ConfigurationStatus.ready,
          configuration: initialConfig,
        ),
      );

      await _subscription?.cancel();
      _subscription = repository.configurationStream.listen(
        (configuration) {
          unawaited(_syncSensitiveApps(configuration.secureContents));
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
