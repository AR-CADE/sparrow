import 'dart:async' show Future, unawaited;
import 'dart:io' show Directory, FileSystemException, Platform;

import 'package:compositor_dart/compositor_dart.dart'
    show CompositorBloc, CompositorRepository, CompositorSubscriptionRequested;
import 'package:flutter/widgets.dart'
    show BuildContext, StatelessWidget, Widget, WidgetsFlutterBinding;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocProvider, MultiBlocProvider;
import 'package:hydrated_bloc/hydrated_bloc.dart'
    show HydratedBloc, HydratedStorage, HydratedStorageDirectory;
import 'package:material_ui/material_ui.dart'
    show Colors, MaterialApp, Scaffold, runApp;
import 'package:shell/shell.dart'
    show ConfigurationRepository, CustomScrollBehavior, Shell;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/lifecycle/cubit/app_lifecycle_cubit.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart' show ShellBloc;

/// Entry point of the Sparrow Shell application.
Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  if (!Platform.isLinux) {
    return;
  }

  var storageDir = Directory(
    Platform.environment['XDG_DATA_HOME'] != null
        ? '${Platform.environment['XDG_DATA_HOME']}/sparrow/shell_storage'
        : '${Platform.environment['HOME'] ?? '/tmp'}/.local/share/sparrow/shell_storage',
  );
  try {
    if (!storageDir.existsSync()) {
      storageDir.createSync(recursive: true);
    }
  } on FileSystemException catch (_) {
    // Read-only $HOME: fall back to a volatile location.
    storageDir = Directory('/tmp/sparrow/shell_storage')
      ..createSync(recursive: true);
  }
  HydratedBloc.storage = await HydratedStorage.build(
    storageDirectory: HydratedStorageDirectory(storageDir.path),
  );

  await ConfigurationRepository.instance.init();

  runApp(const ShellApp());
}

/// The root Sparrow Shell application widget.
class ShellApp extends StatelessWidget {
  /// Creates a new [ShellApp].
  const new({this.configurationRepository, super.key});

  /// Optional custom configuration repository.
  final ConfigurationRepository? configurationRepository;

  @override
  Widget build(BuildContext context) {
    final repo = configurationRepository ?? ConfigurationRepository.instance;

    return MultiBlocProvider(
      providers: [
        BlocProvider<AppLifecycleCubit>(create: (_) => AppLifecycleCubit()),
        BlocProvider<ConfigurationCubit>(
          create: (context) {
            final cubit = ConfigurationCubit(repository: repo);
            unawaited(cubit.init());
            return cubit;
          },
        ),
        BlocProvider<CompositorBloc>(
          create: (context) =>
              CompositorBloc(repository: CompositorRepository())
                ..add(const CompositorSubscriptionRequested()),
        ),
        BlocProvider<ShellBloc>(create: (context) => ShellBloc()),
      ],
      child: const MaterialApp(
        restorationScopeId: 'sparrow_root',
        scrollBehavior: CustomScrollBehavior(),
        home: Scaffold(backgroundColor: Colors.black, body: Shell()),
      ),
    );
  }
}
