import 'package:flutter/foundation.dart' show kDebugMode;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
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
import 'package:shell/core.dart' show surfaces;

class NavigationControllerWidget extends StatelessWidget {
  const NavigationControllerWidget({
    required this.toggleOverview,
    required this.onUpdateCurrentPageIndex,
    required this.pageController,
    super.key,
  });

  final Future<void> Function(int) onUpdateCurrentPageIndex;
  final Future<void> Function() toggleOverview;
  final PageController pageController;

  @override
  Widget build(BuildContext context) {
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
            onPressed: toggleOverview,
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
              final currentPageIndex = pageController.hasClients
                  ? pageController.page?.round() ?? pageController.initialPage
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
              final currentPageIndex = pageController.hasClients
                  ? pageController.page?.round() ?? pageController.initialPage
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
              icon: const Icon(
                size: 28,
                Icons.add,
                color: Colors.white,
              ),
              onPressed: () async {
                try {
                  await CompositorRepository()
                      .platform
                      .channel
                      .invokeMethod('mock_spawn_surface');
                } catch (_) {}
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
            icon: Icon(
              size: 28,
              kDebugMode ? Icons.remove : Icons.close,
              color: Colors.white,
            ),
            onPressed: () async {
              final currentPageIndex = pageController.hasClients
                  ? pageController.page?.round() ?? pageController.initialPage
                  : 0;

              final s = surfaces.elementAtOrNull(currentPageIndex);

              if (s != null) {
                await CompositorRepository().platform.surfaceToplevelClose(s);
              }
            },
          ),
        ),
      ],
    );
  }
}
