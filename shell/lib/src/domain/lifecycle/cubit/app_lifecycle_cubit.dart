import 'dart:developer' as developer;

import 'package:flutter/widgets.dart'
    show AppLifecycleListener, AppLifecycleState;
import 'package:flutter_bloc/flutter_bloc.dart' show Cubit;

/// Cubit managing the application lifecycle state and logging transitions.
class AppLifecycleCubit extends Cubit<AppLifecycleState> {
  /// Creates a new [AppLifecycleCubit].
  new({AppLifecycleListener? listener}) : super(AppLifecycleState.resumed) {
    _listener =
        listener ??
        AppLifecycleListener(
          onStateChange: (state) {
            developer.log('Shell lifecycle state: $state', name: 'flutter');
            // Lifecycle state logging to stdout for compositor console
            // visibility.
            // ignore: avoid_print
            print('[flutter] Shell lifecycle state: $state');
            emit(state);
          },
        );
  }

  AppLifecycleListener? _listener;

  @override
  Future<void> close() {
    _listener?.dispose();
    return super.close();
  }
}
