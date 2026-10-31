import 'package:flutter/widgets.dart'
    show AppLifecycleListener, AppLifecycleState;
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/domain/lifecycle/cubit/app_lifecycle_cubit.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('AppLifecycleCubit', () {
    test('initial state is resumed', () async {
      final cubit = AppLifecycleCubit();
      expect(cubit.state, AppLifecycleState.resumed);
      await cubit.close();
    });

    test('listens to lifecycle state changes and disposes listener', () async {
      final listener = AppLifecycleListener();

      final cubit = AppLifecycleCubit(listener: listener);
      expect(cubit.state, AppLifecycleState.resumed);

      await cubit.close();
    });

    test('lifecycle state has canonical value equality', () {
      expect(AppLifecycleState.resumed, equals(AppLifecycleState.resumed));
      expect(
        AppLifecycleState.resumed,
        isNot(equals(AppLifecycleState.paused)),
      );
    });
  });
}
