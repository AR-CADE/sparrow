import 'package:equatable/equatable.dart';
import 'package:shell/src/data/models/shell_configuration.dart';

/// Status of the shell configuration.
enum ConfigurationStatus {
  /// Initial uninitialized status.
  initial,

  /// Loading or parsing the configuration.
  loading,

  /// Ready with active configuration.
  ready,

  /// Failure encountered while reading or parsing.
  failure,
}

/// State representing the shell configuration and its lifecycle status.
class ConfigurationState extends Equatable {
  /// Creates a new [ConfigurationState].
  const new({
    this.status = ConfigurationStatus.initial,
    this.configuration = const ShellConfiguration(),
    this.errorMessage,
  });

  /// The current lifecycle status.
  final ConfigurationStatus status;

  /// The active [ShellConfiguration].
  final ShellConfiguration configuration;

  /// Optional error message when status is [ConfigurationStatus.failure].
  final String? errorMessage;

  /// Creates a copy of this state with updated values.
  ConfigurationState copyWith({
    ConfigurationStatus? status,
    ShellConfiguration? configuration,
    String? errorMessage,
  }) {
    return ConfigurationState(
      status: status ?? this.status,
      configuration: configuration ?? this.configuration,
      errorMessage: errorMessage ?? this.errorMessage,
    );
  }

  @override
  List<Object?> get props => [status, configuration, errorMessage];
}
