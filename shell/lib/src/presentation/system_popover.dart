import 'package:flutter/widgets.dart'
    show
        Alignment,
        Animation,
        BorderSide,
        BuildContext,
        Clip,
        Color,
        CurvedAnimation,
        Curves,
        EdgeInsets,
        FadeTransition,
        GlobalKey,
        GlobalObjectKey,
        MediaQuery,
        Offset,
        PageRoute,
        Rect,
        RenderBox,
        ScaleTransition,
        SingleChildScrollView,
        Size,
        SizedBox,
        StatelessWidget,
        Tween,
        Widget,
        WidgetBuilder;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocProvider, BlocSelector, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        BorderRadius,
        ButtonStyle,
        Colors,
        Icon,
        IconButton,
        Icons,
        Material,
        Navigator,
        Padding,
        RoundedRectangleBorder,
        WidgetStateProperty;
import 'package:shell/src/data/models/shell_configuration.dart'
    show SystemActions;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/presentation/hero_page_route.dart'
    show AnchoredPopupLayout;
import 'package:shell/src/presentation/system.dart' show SystemGrid;

/// A lightweight desktop popover route with an anchor-aware [ScaleTransition]
/// and [FadeTransition], providing an instant, native desktop menu feel.
class AnchoredPopoverRoute<T> extends PageRoute<T> {
  /// Creates a new [AnchoredPopoverRoute].
  new({
    required this.builder,
    this.anchorRect,
    this.anchorKey,
    super.settings,
    super.fullscreenDialog,
    this.barrierColor = Colors.transparent,
    this.transitionDuration = const Duration(milliseconds: 180),
    this.reverseTransitionDuration = const Duration(milliseconds: 140),
    this.barrierDismissible = true,
    this.opaque = false,
    this.barrierLabel = 'anchored-popover-route',
  });

  /// The widget builder for this route.
  final WidgetBuilder builder;

  /// Anchor rectangle for popover alignment.
  final Rect? anchorRect;

  /// Optional anchor key for dynamic tracking across rotations.
  final GlobalKey? anchorKey;

  Rect? get _currentAnchorRect {
    if (anchorKey != null) {
      final ctx = anchorKey!.currentContext;
      if (ctx != null && ctx.mounted) {
        final box = ctx.findRenderObject() as RenderBox?;
        if (box != null && box.hasSize && box.attached) {
          return box.localToGlobal(Offset.zero) & box.size;
        }
      }
    }
    return anchorRect;
  }

  @override
  final Color barrierColor;

  @override
  final Duration transitionDuration;

  @override
  final Duration reverseTransitionDuration;

  @override
  final bool barrierDismissible;

  @override
  final bool opaque;

  @override
  final String barrierLabel;

  @override
  bool get maintainState => true;

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

  @override
  Widget buildTransitions(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
    Widget child,
  ) {
    final curved = CurvedAnimation(
      parent: animation,
      curve: Curves.easeOutCubic,
      reverseCurve: Curves.easeInCubic,
    );

    final scale = Tween<double>(begin: 0.88, end: 1).animate(curved);
    final fade = Tween<double>(begin: 0, end: 1).animate(curved);

    final anchor = _currentAnchorRect;
    final alignment = anchor != null
        ? _computeAlignment(anchor, MediaQuery.sizeOf(context))
        : Alignment.center;

    return FadeTransition(
      opacity: fade,
      child: ScaleTransition(alignment: alignment, scale: scale, child: child),
    );
  }

  static Alignment _computeAlignment(Rect anchor, Size screen) {
    final isRight = anchor.center.dx > screen.width / 2;
    final isBottom = anchor.center.dy > screen.height / 2;

    if (isBottom && isRight) return Alignment.bottomRight;
    if (isBottom && !isRight) return Alignment.bottomLeft;
    if (!isBottom && isRight) return Alignment.topRight;
    return Alignment.topLeft;
  }
}

/// A sleek desktop popover card wrapping [SystemGrid].
class SystemPopoverCard extends StatelessWidget {
  /// Creates a new [SystemPopoverCard].
  const new({required this.actions, super.key, this.width = 280});

  /// The system actions to display.
  final SystemActions actions;

  /// Card width.
  final double width;

  @override
  Widget build(BuildContext context) {
    ConfigurationCubit? cubit;
    try {
      cubit = context.read<ConfigurationCubit>();
    } on Object catch (_) {
      cubit = null;
    }

    Widget buildCard(SystemActions currentActions) {
      return SizedBox(
        width: width,
        child: Material(
          color: Colors.black.withAlpha(235),
          elevation: 12,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(20),
            side: BorderSide(color: Colors.white.withAlpha(35)),
          ),
          clipBehavior: Clip.antiAlias,
          child: SingleChildScrollView(
            child: SystemGrid(actions: currentActions),
          ),
        ),
      );
    }

    if (cubit != null) {
      return BlocSelector<
        ConfigurationCubit,
        ConfigurationState,
        SystemActions?
      >(
        bloc: cubit,
        selector: (state) => state.configuration.systemActions,
        builder: (context, systemActions) =>
            buildCard(systemActions ?? actions),
      );
    }

    return buildCard(actions);
  }
}

const String _systemPopoverButtonKeyTag = 'system-popover-button-key';

/// Desktop-style power button opening an anchored popover.
class SystemPopoverWidget extends StatelessWidget {
  /// Creates a new [SystemPopoverWidget].
  const new({required this.actions, super.key});

  /// Key uniquely identifying the popover button across builds.
  static const GlobalKey buttonKey = GlobalObjectKey(
    _systemPopoverButtonKeyTag,
  );

  /// System actions to show in the popover.
  final SystemActions actions;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      child: IconButton(
        key: buttonKey,
        color: Colors.white,
        style: ButtonStyle(
          backgroundColor: WidgetStateProperty.all(Colors.black.withAlpha(60)),
        ),
        icon: const Icon(size: 28, Icons.power_settings_new),
        onPressed: () async {
          final renderBox =
              buttonKey.currentContext?.findRenderObject() as RenderBox?;
          final buttonRect = renderBox != null && renderBox.hasSize
              ? renderBox.localToGlobal(Offset.zero) & renderBox.size
              : null;

          ConfigurationCubit? cubit;
          try {
            cubit = context.read<ConfigurationCubit>();
          } on Object catch (_) {
            cubit = null;
          }

          await Navigator.of(context).push(
            AnchoredPopoverRoute<dynamic>(
              anchorRect: buttonRect,
              anchorKey: buttonKey,
              builder: (modalContext) {
                final card = SystemPopoverCard(actions: actions);
                if (cubit != null) {
                  return BlocProvider<ConfigurationCubit>.value(
                    value: cubit,
                    child: card,
                  );
                }
                return card;
              },
            ),
          );
        },
      ),
    );
  }
}
