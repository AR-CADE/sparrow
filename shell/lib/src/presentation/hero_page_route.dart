import 'package:flutter/widgets.dart'
    show
        Animation,
        BoxConstraints,
        BuildContext,
        Color,
        CustomSingleChildLayout,
        EdgeInsets,
        GlobalKey,
        MediaQuery,
        Offset,
        PageRoute,
        Rect,
        SingleChildLayoutDelegate,
        Size,
        StatelessWidget,
        Widget,
        WidgetBuilder;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocBuilder, BlocProvider, ReadContext;
import 'package:material_ui/material_ui.dart' show Colors;
import 'package:shell/src/domain/anchored_popup/cubit/anchored_popup_cubit.dart';
import 'package:shell/src/domain/anchored_popup/cubit/anchored_popup_state.dart';

/// Layout delegate that positions a popup child adjacent to [anchorRect],
/// clamping it within [screenPadding] and flipping above/below as needed.
class AnchoredPopupLayoutDelegate extends SingleChildLayoutDelegate {
  /// Creates a new [AnchoredPopupLayoutDelegate].
  const new({
    this.anchorRect,
    this.screenSize,
    this.screenPadding = const EdgeInsets.all(16),
    this.gap = 12.0,
  });

  /// Anchor rectangle in global coordinate space.
  final Rect? anchorRect;

  /// Current screen size to trigger relayout on orientation or size changes.
  final Size? screenSize;

  /// Insets from the screen edge.
  final EdgeInsets screenPadding;

  /// Gap between anchor and popup.
  final double gap;

  @override
  BoxConstraints getConstraintsForChild(BoxConstraints constraints) {
    final maxW = constraints.maxWidth - screenPadding.horizontal;
    final maxH = constraints.maxHeight - screenPadding.vertical;
    return BoxConstraints(
      maxWidth: maxW > 0 ? maxW : 0,
      maxHeight: maxH > 0 ? maxH : 0,
    );
  }

  @override
  Offset getPositionForChild(Size size, Size childSize) {
    final anchor = anchorRect;
    if (anchor == null) {
      final x = (size.width - childSize.width) / 2;
      final y = (size.height - childSize.height) / 2;
      return Offset(
        x.clamp(screenPadding.left, size.width - screenPadding.right),
        y.clamp(screenPadding.top, size.height - screenPadding.bottom),
      );
    }

    final isAnchorOnRight = anchor.center.dx > size.width / 2;
    double x;
    if (isAnchorOnRight) {
      x = anchor.right - childSize.width;
    } else {
      x = anchor.left;
    }

    final minX = screenPadding.left;
    final maxX = size.width - screenPadding.right - childSize.width;
    if (maxX >= minX) {
      x = x.clamp(minX, maxX);
    } else {
      x = minX;
    }

    final isAnchorInBottomHalf = anchor.center.dy > size.height / 2;
    double y;

    if (isAnchorInBottomHalf) {
      y = anchor.top - gap - childSize.height;
      if (y < screenPadding.top) {
        final yBelow = anchor.bottom + gap;
        final maxAllowed = size.height - screenPadding.bottom;
        if (yBelow + childSize.height <= maxAllowed) {
          y = yBelow;
        } else {
          y = screenPadding.top;
        }
      }
    } else {
      y = anchor.bottom + gap;
      final maxAllowed = size.height - screenPadding.bottom;
      if (y + childSize.height > maxAllowed) {
        final yAbove = anchor.top - gap - childSize.height;
        if (yAbove >= screenPadding.top) {
          y = yAbove;
        } else {
          y = screenPadding.top;
        }
      }
    }

    return Offset(x, y);
  }

  @override
  bool shouldRelayout(AnchoredPopupLayoutDelegate oldDelegate) {
    return anchorRect != oldDelegate.anchorRect ||
        screenSize != oldDelegate.screenSize ||
        screenPadding != oldDelegate.screenPadding ||
        gap != oldDelegate.gap;
  }
}

/// Widget that positions its [child] using [AnchoredPopupLayoutDelegate]
/// and automatically relayouts when [MediaQuery] changes (such as rotation).
class AnchoredPopupLayout extends StatelessWidget {
  /// Creates a new [AnchoredPopupLayout].
  const new({
    required this.child,
    this.anchorRect,
    this.anchorKey,
    this.screenPadding = const EdgeInsets.all(16),
    this.gap = 12.0,
    super.key,
  });

  /// The child widget to position.
  final Widget child;

  /// Optional static anchor rectangle.
  final Rect? anchorRect;

  /// Optional anchor key for dynamic tracking.
  final GlobalKey? anchorKey;

  /// Insets from the screen edge.
  final EdgeInsets screenPadding;

  /// Gap between anchor and popup.
  final double gap;

  @override
  Widget build(BuildContext context) {
    return BlocProvider(
      create: (_) => AnchoredPopupCubit(
        anchorKey: anchorKey,
        initialAnchorRect: anchorRect,
      ),
      child: BlocBuilder<AnchoredPopupCubit, AnchoredPopupState>(
        builder: (context, state) {
          final currentSize = MediaQuery.sizeOf(context);
          context.read<AnchoredPopupCubit>().handleScreenSize(currentSize);

          return CustomSingleChildLayout(
            delegate: AnchoredPopupLayoutDelegate(
              anchorRect: state.anchorRect,
              screenSize: currentSize,
              screenPadding: screenPadding,
              gap: gap,
            ),
            child: child,
          );
        },
      ),
    );
  }
}

/// A transparent [PageRoute] that supports seamless transitions
/// with optional anchored positioning relative to [anchorRect] or [anchorKey].
class HeroPageRoute<T> extends PageRoute<T> {
  /// Creates a new [HeroPageRoute].
  new({
    required this.builder,
    this.anchorRect,
    this.anchorKey,
    super.settings,
    super.fullscreenDialog,
    this.barrierColor = Colors.transparent,
    this.transitionDuration = const Duration(milliseconds: 300),
    this.barrierDismissible = true,
    this.opaque = false,
    this.barrierLabel = 'hero-page-route',
  });

  /// The widget builder for this route.
  final WidgetBuilder builder;

  /// Optional anchor rectangle for positioning.
  final Rect? anchorRect;

  /// Optional anchor key for dynamic positioning.
  final GlobalKey? anchorKey;

  @override
  final Color barrierColor;

  @override
  final Duration transitionDuration;

  @override
  final bool barrierDismissible;

  @override
  final bool opaque;

  @override
  final String barrierLabel;

  @override
  bool get maintainState => true;

  @override
  Widget buildTransitions(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
    Widget child,
  ) {
    return child;
  }

  @override
  Widget buildPage(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
  ) {
    final page = builder(context);
    if (anchorRect != null || anchorKey != null) {
      return AnchoredPopupLayout(
        anchorRect: anchorRect,
        anchorKey: anchorKey,
        child: page,
      );
    }
    return page;
  }
}
