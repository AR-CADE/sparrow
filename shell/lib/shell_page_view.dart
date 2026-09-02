import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:material_ui/material_ui.dart'
    show
        AlwaysScrollableScrollPhysics,
        AnimationController,
        BouncingScrollPhysics,
        BuildContext,
        NeverScrollableScrollPhysics,
        PageController,
        PageView,
        SizedBox,
        StatelessWidget,
        ValueKey,
        Widget;
import 'package:shell/animated_surface_wrapper.dart'
    show AnimatedSurfaceWrapper;
import 'package:shell/core.dart'
    show CustomScrollBehavior, maxFraction, minFraction, surfaces;
import 'package:shell/surface_page.dart' show SurfacePage;

class ShellPageView extends StatelessWidget {
  const ShellPageView({
    required this.animationController,
    required this.handlePageViewChanged,
    required this.toggleOverview,
    required this.closeOverview,
    required this.openOverview,
    required this.pageController,
    required this.gap,
    required this.fraction,
    required this.radius,
    required this.output,
    super.key,
  });

  final double gap;
  final double fraction;
  final double radius;
  final DisplayOutput output;
  final PageController pageController;
  final Future<void> Function({int index}) toggleOverview;
  final Future<void> Function(Surface? s, {bool center}) closeOverview;
  final Future<void> Function(Surface? s) openOverview;
  final void Function(int) handlePageViewChanged;
  final AnimationController animationController;

  @override
  Widget build(BuildContext context) {
    final pageview = PageView.builder(
      scrollBehavior: const CustomScrollBehavior(),
      physics: fraction == minFraction
          ? const BouncingScrollPhysics(
              parent: AlwaysScrollableScrollPhysics(),
            )
          : const NeverScrollableScrollPhysics(),
      pageSnapping: fraction != minFraction,
      controller: pageController,
      onPageChanged: handlePageViewChanged,
      itemCount: surfaces.length,
      itemBuilder: (context, index) {
        final s = surfaces.elementAtOrNull(index);

        if (s == null) {
          return const SizedBox.shrink();
        }

        final currentPageIndex = pageController.hasClients
            ? pageController.page?.round() ?? pageController.initialPage
            : pageController.initialPage;

        final isCurrent = currentPageIndex == index;

        return AnimatedSurfaceWrapper(
          key: ValueKey<int>(s.handle),
          gap: gap,
          pageController: pageController,
          index: index,
          fraction: fraction,
          radius: radius,
          output: output,
          surface: s,
          closeOverview: closeOverview,
          openOverview: openOverview,
          controller: animationController,
          // overlayController: overlayController,
          toggleOverview: toggleOverview,
          child: SurfacePage(
            toggleOverview: toggleOverview,
            freeze: fraction == maxFraction && !isCurrent,
            interactive: fraction == maxFraction,
            key: ValueKey<int>(s.handle),
            surface: s,
          ),
        );
      },
    );

    return SizedBox.expand(
      child: pageview,
    );
  }
}
