import 'dart:developer' as developer;
import 'dart:io' show Platform;

import 'package:flutter/widgets.dart'
    show
        AppLifecycleListener,
        BuildContext,
        State,
        StatefulWidget,
        Widget,
        WidgetsFlutterBinding;
import 'package:material_ui/material_ui.dart'
    show Colors, MaterialApp, Scaffold, runApp;
import 'package:shell/configuration_repository.dart'
    show ConfigurationRepository;
import 'package:shell/core.dart' show CustomScrollBehavior;
import 'package:shell/shell.dart' show Shell;

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  if (!Platform.isLinux) {
    return;
  }

  await ConfigurationRepository.instance.init();

  runApp(const ShellApp());
}

class ShellApp extends StatefulWidget {
  const new({super.key});

  @override
  State<ShellApp> createState() => _ShellAppState();
}

class _ShellAppState extends State<ShellApp> {
  late final AppLifecycleListener _lifecycleListener;

  @override
  void initState() {
    super.initState();
    _lifecycleListener = AppLifecycleListener(
      onStateChange: (state) {
        developer.log('Shell lifecycle state: $state', name: 'flutter');
        // Lifecycle state logging to stdout for compositor console visibility.
        // ignore: avoid_print
        print('[flutter] Shell lifecycle state: $state');
      },
    );
  }

  @override
  void dispose() {
    _lifecycleListener.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      scrollBehavior: CustomScrollBehavior(),
      home: Scaffold(backgroundColor: Colors.black, body: Shell()),
    );
  }
}
