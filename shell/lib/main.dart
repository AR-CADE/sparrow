import 'dart:async' show unawaited;
import 'dart:io' show Platform;

import 'package:compositor_dart/compositor_dart.dart'
    show CompositorBloc, CompositorRepository, CompositorSubscriptionRequested;
import 'package:flutter/widgets.dart'
    show BuildContext, StatelessWidget, Widget, WidgetsFlutterBinding;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocProvider, MultiBlocProvider;
import 'package:material_ui/material_ui.dart'
    show Colors, MaterialApp, Scaffold, runApp;
import 'package:shell/shell.dart'
    show ConfigurationRepository, CustomScrollBehavior, Shell;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/lifecycle/cubit/app_lifecycle_cubit.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart' show ShellBloc;

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  if (!Platform.isLinux) {
    return;
  }

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
        scrollBehavior: CustomScrollBehavior(),
        home: Scaffold(backgroundColor: Colors.black, body: Shell()),
      ),
    );
  }
}
