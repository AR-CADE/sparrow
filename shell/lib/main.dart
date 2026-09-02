import 'dart:io' show Platform;

import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Colors,
        MaterialApp,
        Scaffold,
        StatelessWidget,
        Widget,
        WidgetsFlutterBinding,
        runApp;
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

class ShellApp extends StatelessWidget {
  const ShellApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      scrollBehavior: CustomScrollBehavior(),
      home: Scaffold(
        backgroundColor: Colors.black,
        body: Shell(),
      ),
    );
  }
}
