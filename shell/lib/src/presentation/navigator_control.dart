import 'package:compositor_dart/compositor_dart.dart'
    show CompositorBloc, CompositorRepository, CompositorState, Surface;
import 'package:flutter/foundation.dart' show kDebugMode;
import 'package:flutter/services.dart' show MethodChannel;
import 'package:flutter/widgets.dart' show FocusManager;
import 'package:flutter_bloc/flutter_bloc.dart' show BlocSelector, WatchContext;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        ButtonStyle,
        Colors,
        EdgeInsets,
        Icon,
        IconButton,
        Icons,
        Padding,
        PageController,
        Row,
        StatelessWidget,
        Widget,
        WidgetStateProperty;
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart' show ShellBloc;

/// Navigation controls on the taskbar (overview toggle, prev, next, close).
class NavigationControllerWidget extends StatelessWidget {
  /// Creates a new [NavigationControllerWidget].
  const new({
    required this.toggleOverview,
    required this.onUpdateCurrentPageIndex,
    required this.pageController,
    super.key,
  });

  /// Callback to update the current page index.
  final Future<void> Function(int) onUpdateCurrentPageIndex;

  /// Callback to toggle overview mode.
  final Future<void> Function() toggleOverview;

  /// The surface page controller.
  final PageController pageController;

  @override
  Widget build(BuildContext context) {
    final shellBloc = context.watch<ShellBloc?>();
    return BlocSelector<CompositorBloc, CompositorState, List<Surface>>(
      selector: (state) {
        return shellBloc?.state.orderedSurfaces(state.surfaces) ??
            state.surfaces.values.toList();
      },
      builder: (context, surfaces) {
        return Row(
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8),
              child: IconButton(
                tooltip: 'overview',
                style: ButtonStyle(
                  backgroundColor: WidgetStateProperty.all(
                    Colors.black.withAlpha(60),
                  ),
                ),
                icon: const Icon(
                  size: 28,
                  Icons.auto_awesome_motion,
                  color: Colors.white,
                ),
                onPressed: () async {
                  FocusManager.instance.primaryFocus?.unfocus();
                  await toggleOverview();
                },
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4),
              child: IconButton(
                tooltip: 'previous',
                style: ButtonStyle(
                  backgroundColor: WidgetStateProperty.all(
                    Colors.black.withAlpha(60),
                  ),
                ),
                icon: const Icon(
                  size: 28,
                  Icons.arrow_back,
                  color: Colors.white,
                ),
                onPressed: () async {
                  FocusManager.instance.primaryFocus?.unfocus();
                  final currentPageIndex = pageController.hasClients
                      ? pageController.page?.round() ??
                            pageController.initialPage
                      : 0;
                  if (currentPageIndex == 0) {
                    return;
                  }
                  await onUpdateCurrentPageIndex(currentPageIndex - 1);
                },
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4),
              child: IconButton(
                tooltip: 'next',
                style: ButtonStyle(
                  backgroundColor: WidgetStateProperty.all(
                    Colors.black.withAlpha(60),
                  ),
                ),
                icon: const Icon(
                  size: 28,
                  Icons.arrow_forward,
                  color: Colors.white,
                ),
                onPressed: () async {
                  FocusManager.instance.primaryFocus?.unfocus();
                  final currentPageIndex = pageController.hasClients
                      ? pageController.page?.round() ??
                            pageController.initialPage
                      : 0;
                  if (surfaces.length <= currentPageIndex + 1) {
                    return;
                  }
                  await onUpdateCurrentPageIndex(currentPageIndex + 1);
                },
              ),
            ),
            if (kDebugMode)
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 4),
                child: IconButton(
                  tooltip: 'Add Application',
                  style: ButtonStyle(
                    backgroundColor: WidgetStateProperty.all(
                      Colors.black.withAlpha(60),
                    ),
                  ),
                  icon: const Icon(size: 28, Icons.add, color: Colors.white),
                  onPressed: () async {
                    FocusManager.instance.primaryFocus?.unfocus();
                    try {
                      await const MethodChannel('wlroots')
                          .invokeMethod('mock_spawn_surface');
                    } on Exception catch (_) {}
                  },
                ),
              ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4),
              child: IconButton(
                tooltip: kDebugMode ? 'Remove Application' : 'Quit Application',
                style: ButtonStyle(
                  backgroundColor: WidgetStateProperty.all(
                    Colors.black.withAlpha(60),
                  ),
                ),
                icon: const Icon(
                  size: 28,
                  kDebugMode ? Icons.remove : Icons.close,
                  color: Colors.white,
                ),
                onPressed: () async {
                  FocusManager.instance.primaryFocus?.unfocus();
                  final currentPageIndex = pageController.hasClients
                      ? pageController.page?.round() ??
                            pageController.initialPage
                      : 0;

                  final s = surfaces.elementAtOrNull(currentPageIndex);

                  if (s != null) {
                    await CompositorRepository().platform.surfaceToplevelClose(
                      s,
                    );
                  }
                },
              ),
            ),
          ],
        );
      },
    );
  }
}
