import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/compositor_dart.dart'
    show DisplayOutput, Surface;
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
import 'package:shell/src/core/constants.dart' show maxFraction, minFraction;
import 'package:shell/src/core/scroll_behavior.dart' show CustomScrollBehavior;
import 'package:shell/src/presentation/animated_surface_wrapper.dart'
    show AnimatedSurfaceWrapper;
import 'package:shell/src/presentation/surface_page.dart' show SurfacePage;

/// A paginated view of active application surfaces.
class ShellPageView extends StatelessWidget {
  /// Creates a new [ShellPageView].
  const new({
    required this.surfaces,
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

  /// The list of surfaces to render.
  final List<Surface> surfaces;

  /// Gap spacing between surfaces.
  final double gap;

  /// Viewport fraction.
  final double fraction;

  /// Corner radius.
  final double radius;

  /// Display output geometry.
  final DisplayOutput output;

  /// Page controller.
  final PageController pageController;

  /// Callback to toggle overview.
  final Future<void> Function({int? index}) toggleOverview;

  /// Callback to close overview.
  final Future<void> Function(Surface? s, {bool center}) closeOverview;

  /// Callback to open overview.
  final Future<void> Function(Surface? s) openOverview;

  /// Callback when page changes.
  final void Function(int) handlePageViewChanged;

  /// Animation controller driving overview.
  final AnimationController animationController;

  @override
  Widget build(BuildContext context) {
    final pageview = PageView.builder(
      scrollBehavior: const CustomScrollBehavior(),
      physics: fraction == minFraction
          ? const BouncingScrollPhysics(parent: AlwaysScrollableScrollPhysics())
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
            ? (pageController.page?.round() ?? pageController.initialPage)
                  .clamp(0, surfaces.isNotEmpty ? surfaces.length - 1 : 0)
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

    return SizedBox.expand(child: pageview);
  }
}
