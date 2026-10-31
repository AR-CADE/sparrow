import 'dart:async';

import 'package:bloc_test/bloc_test.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/data/repositories/configuration_repository.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';

class MockConfigurationRepository extends Mock
    implements ConfigurationRepository;

void main() {
  group('ConfigurationCubit', () {
    late MockConfigurationRepository repository;
    late StreamController<ShellConfiguration> configController;

    const initialConfig = ShellConfiguration(
      wallpaper: '/path/wallpaper.jpg',
      launcher: 'grid2',
      terminal: 'alacritty',
    );

    setUp(() {
      repository = MockConfigurationRepository();
      configController = StreamController<ShellConfiguration>.broadcast();

      when(() => repository.init()).thenAnswer((_) async {});
      when(() => repository.currentConfiguration).thenReturn(initialConfig);
      when(() => repository.configurationStream)
          .thenAnswer((_) => configController.stream);
    });

    tearDown(() async {
      await configController.close();
    });

    test('initial state is ConfigurationState with initial status', () {
      final cubit = ConfigurationCubit(repository: repository);
      expect(cubit.state, equals(const ConfigurationState()));
      expect(cubit.state.status, equals(ConfigurationStatus.initial));
    });

    blocTest<ConfigurationCubit, ConfigurationState>(
      'emits [loading, ready] with current configuration when init succeeds',
      build: () => ConfigurationCubit(repository: repository),
      act: (cubit) => cubit.init(),
      expect: () => [
        const ConfigurationState(status: ConfigurationStatus.loading),
        const ConfigurationState(
          status: ConfigurationStatus.ready,
          configuration: initialConfig,
        ),
      ],
      verify: (_) {
        verify(() => repository.init()).called(1);
      },
    );

    blocTest<ConfigurationCubit, ConfigurationState>(
      'emits updated configuration when configurationStream emits new data',
      build: () => ConfigurationCubit(repository: repository),
      act: (cubit) async {
        await cubit.init();
        configController.add(
          const ShellConfiguration(
            wallpaper: '/new/wall.jpg',
            launcher: 'fuzzel',
            terminal: 'foot',
          ),
        );
      },
      expect: () => [
        const ConfigurationState(status: ConfigurationStatus.loading),
        const ConfigurationState(
          status: ConfigurationStatus.ready,
          configuration: initialConfig,
        ),
        const ConfigurationState(
          status: ConfigurationStatus.ready,
          configuration: ShellConfiguration(
            wallpaper: '/new/wall.jpg',
            launcher: 'fuzzel',
            terminal: 'foot',
          ),
        ),
      ],
    );

    blocTest<ConfigurationCubit, ConfigurationState>(
      'emits failure state when configurationStream emits an error',
      build: () => ConfigurationCubit(repository: repository),
      act: (cubit) async {
        await cubit.init();
        configController.addError(Exception('Disk read failure'));
      },
      expect: () => [
        const ConfigurationState(status: ConfigurationStatus.loading),
        const ConfigurationState(
          status: ConfigurationStatus.ready,
          configuration: initialConfig,
        ),
        const ConfigurationState(
          status: ConfigurationStatus.failure,
          configuration: initialConfig,
          errorMessage: 'Exception: Disk read failure',
        ),
      ],
    );

    blocTest<ConfigurationCubit, ConfigurationState>(
      'emits failure state when repository.init() throws an exception',
      setUp: () {
        when(() => repository.init())
            .thenThrow(Exception('Config file not found'));
      },
      build: () => ConfigurationCubit(repository: repository),
      act: (cubit) => cubit.init(),
      expect: () => [
        const ConfigurationState(status: ConfigurationStatus.loading),
        const ConfigurationState(
          status: ConfigurationStatus.failure,
          errorMessage: 'Exception: Config file not found',
        ),
      ],
    );

    test('close cancels stream subscription and closes cubit', () async {
      final cubit = ConfigurationCubit(repository: repository);
      await cubit.init();
      expect(configController.hasListener, isTrue);

      await cubit.close();
      expect(configController.hasListener, isFalse);
    });

    test('syncs secureAppIds on init and stream updates', () async {
      final syncedLists = <List<String>>[];
      when(() => repository.currentConfiguration)
          .thenReturn(const ShellConfiguration(secureAppIds: ['rpcs3']));

      final cubit = ConfigurationCubit(
        repository: repository,
        onSyncSensitiveAppIds: (ids) async {
          syncedLists.add(ids);
        },
      );

      await cubit.init();
      expect(
        syncedLists,
        equals([
          ['rpcs3'],
        ]),
      );

      configController.add(
        const ShellConfiguration(secureAppIds: ['rpcs3', 'keepassxc']),
      );
      await Future<void>.delayed(Duration.zero);
      expect(
        syncedLists,
        equals([
          ['rpcs3'],
          ['rpcs3', 'keepassxc'],
        ]),
      );

      await cubit.close();
    });
  });
}
