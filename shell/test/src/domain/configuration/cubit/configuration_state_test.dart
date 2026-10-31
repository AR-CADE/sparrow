import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';

void main() {
  group('ConfigurationState', () {
    test('supports value equality', () {
      const stateA = ConfigurationState();
      const stateB = ConfigurationState();
      const stateC = ConfigurationState(
        status: ConfigurationStatus.ready,
        configuration: ShellConfiguration(launcher: 'fuzzel'),
        errorMessage: 'err',
      );

      expect(stateA, equals(stateB));
      expect(stateA, isNot(equals(stateC)));
      expect(
        stateC.props,
        equals([
          ConfigurationStatus.ready,
          const ShellConfiguration(launcher: 'fuzzel'),
          'err',
        ]),
      );
    });

    test('default constructor initializes expected values', () {
      const state = ConfigurationState();
      expect(state.status, equals(ConfigurationStatus.initial));
      expect(state.configuration, equals(const ShellConfiguration()));
      expect(state.errorMessage, isNull);
    });

    test('copyWith updates specified fields and retains others', () {
      const initial = ConfigurationState();
      const newConfig = ShellConfiguration(terminal: 'alacritty');

      final updated = initial.copyWith(
        status: ConfigurationStatus.failure,
        configuration: newConfig,
        errorMessage: 'Something went wrong',
      );

      expect(updated.status, equals(ConfigurationStatus.failure));
      expect(updated.configuration, equals(newConfig));
      expect(updated.errorMessage, equals('Something went wrong'));

      final unchanged = initial.copyWith();
      expect(unchanged, equals(initial));
    });
  });
}
