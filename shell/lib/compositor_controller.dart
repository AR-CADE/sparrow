import 'package:material_ui/material_ui.dart'
    show
        Align,
        BuildContext,
        Center,
        EdgeInsets,
        ListenableBuilder,
        Padding,
        PageController,
        SizedBox,
        StatelessWidget,
        Widget;
import 'package:shell/configuration_repository.dart'
    show ConfigurationRepository;
import 'package:shell/grid.dart' show GridWidget;
import 'package:shell/navigator_control.dart' show NavigationControllerWidget;
import 'package:shell/system.dart' show SystemWidget;
import 'package:shell/taskbar.dart' show TaskBar;
import 'package:shell/terminal.dart' show TerminalWidget;

class CompositorController extends StatelessWidget {
  const CompositorController({
    required this.onUpdateCurrentPageIndex,
    required this.toggleOverview,
    required this.pageController,
    super.key,
  });

  final Future<void> Function(int) onUpdateCurrentPageIndex;
  final Future<void> Function() toggleOverview;
  final PageController pageController;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: ConfigurationRepository.instance,
      builder: (context, _) {
        final config = ConfigurationRepository.instance;
        final launcherCmd = config.launcher;
        final terminalCmd = config.terminal;
        final logoutCmd = config.logout;

        final leading = <Widget>[
          if (launcherCmd != null && launcherCmd.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(left: 2),
              child: GridWidget(command: launcherCmd),
            ),
          Padding(
            padding: const EdgeInsets.only(left: 8),
            child: NavigationControllerWidget(
              pageController: pageController,
              onUpdateCurrentPageIndex: onUpdateCurrentPageIndex,
              toggleOverview: toggleOverview,
            ),
          ),
        ];

        final trailing = <Widget>[
          if (terminalCmd != null && terminalCmd.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(right: 20),
              child: Center(
                child: TerminalWidget(command: terminalCmd),
              ),
            ),
          if (logoutCmd != null && logoutCmd.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(right: 2),
              child: SystemWidget(command: logoutCmd),
            ),
        ];

        return Align(
          alignment: .bottomCenter,
          child: SizedBox(
            width: double.maxFinite,
            height: 64,
            child: TaskBar(
              leading: leading,
              center: const [],
              trailing: trailing,
            ),
          ),
        );
      },
    );
  }
}
