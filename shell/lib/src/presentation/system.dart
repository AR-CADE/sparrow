import 'dart:async' show unawaited;
import 'dart:io' show Process, ProcessStartMode;

import 'package:flutter/widgets.dart'
    show
        Alignment,
        AnimatedBuilder,
        Animation,
        BorderSide,
        Clip,
        CurvedAnimation,
        Curves,
        FocusManager,
        GlobalKey,
        GlobalObjectKey,
        Offset,
        Opacity,
        OverflowBox,
        RectTween,
        RenderBox,
        Stack,
        StackFit;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocProvider, BlocSelector, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        Axis,
        BorderRadius,
        BuildContext,
        ButtonStyle,
        Center,
        Colors,
        EdgeInsets,
        Flex,
        Flexible,
        FontWeight,
        GestureDetector,
        GridView,
        Hero,
        Icon,
        IconButton,
        Icons,
        Material,
        Navigator,
        NeverScrollableScrollPhysics,
        Padding,
        RoundedRectangleBorder,
        SingleChildScrollView,
        SizedBox,
        SliverGridDelegateWithMaxCrossAxisExtent,
        StatelessWidget,
        Text,
        TextStyle,
        Widget,
        WidgetStateProperty;
import 'package:shell/src/core/constants.dart' show ProcessStarter;
import 'package:shell/src/data/models/shell_configuration.dart'
    show SystemAction, SystemActions;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/presentation/hero_page_route.dart' show HeroPageRoute;

const String _heroSystemButton = 'system-button-hero';

/// Grid of system action icons (logout, reboot, poweroff, etc.).
class SystemGrid extends StatelessWidget {
  /// Creates a new [SystemGrid].
  const new({
    required this.actions,
    this.processStarter = Process.start,
    super.key,
    this.scrollDirection = Axis.vertical,
  });

  /// Function used to start system processes.
  final ProcessStarter processStarter;

  /// Scroll direction of the grid.
  final Axis scrollDirection;

  /// Configured system actions.
  final SystemActions actions;

  @override
  Widget build(BuildContext context) {
    final commands =
        <SystemAction>[
          if (actions.logout != null) actions.logout!,
          if (actions.suspend != null) actions.suspend!,
          if (actions.hibernate != null) actions.hibernate!,
          if (actions.reboot != null) actions.reboot!,
          if (actions.poweroff != null) actions.poweroff!,
        ].where(
          (action) =>
              action.command != null &&
              action.name != null &&
              action.icon != null,
        );

    final widgets = commands.map((action) {
      final command = action.command!;
      final name = action.name!;
      final icon = action.icon!;

      return GestureDetector(
        onTap: () => processCommand(context, command),
        child: Flex(
          spacing: 8,
          direction: .vertical,
          children: [
            IconButton(
              color: Colors.white,
              style: ButtonStyle(
                backgroundColor: WidgetStateProperty.all(
                  Colors.white.withAlpha(45),
                ),
              ),
              icon: Padding(
                padding: const EdgeInsets.all(6),
                child: Icon(size: 44, icon),
              ),
              onPressed: () => processCommand(context, command),
            ),
            Flexible(
              child: Text(
                overflow: .ellipsis,
                maxLines: 1,
                name,
                style: TextStyle(
                  fontWeight: FontWeight.bold,
                  fontSize: 14,
                  color: Colors.white.withAlpha(220),
                ),
              ),
            ),
          ],
        ),
      );
    });

    return GridView(
      padding: const EdgeInsets.all(20),
      scrollDirection: scrollDirection,
      physics: const NeverScrollableScrollPhysics(),
      shrinkWrap: true,
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
        maxCrossAxisExtent: 130,
        mainAxisSpacing: 12,
        crossAxisSpacing: 12,
      ),
      children: widgets.toList(),
    );
  }

  /// Executes the system [command] in detached mode and closes the dialog.
  void processCommand(BuildContext context, String command) {
    Navigator.of(context).pop();
    FocusManager.instance.primaryFocus?.unfocus();
    final parts = command.trim().split(RegExp(r'\s+'));
    if (parts.isNotEmpty && parts.first.isNotEmpty) {
      unawaited(
        processStarter(
          parts.first,
          parts.skip(1).toList(),
          mode: ProcessStartMode.detached,
        ),
      );
    }
  }
}

class _SystemPopupCard extends StatelessWidget {
  const new({required this.actions, this.processStarter = Process.start});

  static const double _cardWidth = 280;
  final SystemActions actions;
  final ProcessStarter processStarter;

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
        width: _cardWidth,
        child: Hero(
          tag: _heroSystemButton,
          createRectTween: (begin, end) => RectTween(begin: begin, end: end),
          flightShuttleBuilder:
              (
                flightContext,
                animation,
                flightDirection,
                fromHeroContext,
                toHeroContext,
              ) {
                return _buildFlightShuttle(
                  animation: animation,
                  actions: currentActions,
                  targetWidth: _cardWidth,
                );
              },
          child: Material(
            color: Colors.black.withAlpha(235),
            elevation: 8,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(24),
              side: BorderSide(color: Colors.white.withAlpha(35)),
            ),
            clipBehavior: Clip.antiAlias,
            child: SingleChildScrollView(
              child: SystemGrid(
                actions: currentActions,
                processStarter: processStarter,
              ),
            ),
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
        builder: (context, systemActions) {
          return buildCard(systemActions ?? actions);
        },
      );
    }

    return buildCard(actions);
  }

  static Widget _buildFlightShuttle({
    required Animation<double> animation,
    required SystemActions actions,
    required double targetWidth,
  }) {
    final curvedAnimation = CurvedAnimation(
      parent: animation,
      curve: Curves.fastOutSlowIn,
      reverseCurve: Curves.fastOutSlowIn.flipped,
    );

    return AnimatedBuilder(
      animation: curvedAnimation,
      builder: (context, _) {
        final t = curvedAnimation.value;
        // Icon fades out in the first 30% of opening
        final iconOpacity = (1.0 - (t / 0.3)).clamp(0.0, 1.0);
        // Popup grid fades in from 25% to 100%
        final contentOpacity = ((t - 0.25) / 0.75).clamp(0.0, 1.0);

        final borderRadius = BorderRadius.circular(24);

        return Material(
          color: Colors.black.withAlpha((235 * t + 60 * (1 - t)).round()),
          elevation: 8 * t,
          shape: RoundedRectangleBorder(
            borderRadius: borderRadius,
            side: BorderSide(color: Colors.white.withAlpha((35 * t).round())),
          ),
          clipBehavior: Clip.antiAlias,
          child: Stack(
            fit: StackFit.expand,
            children: [
              if (iconOpacity > 0)
                Opacity(
                  opacity: iconOpacity,
                  child: const Center(
                    child: Icon(
                      size: 28,
                      Icons.power_settings_new,
                      color: Colors.white,
                    ),
                  ),
                ),
              if (contentOpacity > 0)
                Opacity(
                  opacity: contentOpacity,
                  child: OverflowBox(
                    alignment: Alignment.topCenter,
                    minWidth: targetWidth,
                    maxWidth: targetWidth,
                    minHeight: 0,
                    maxHeight: double.infinity,
                    child: SingleChildScrollView(
                      physics: const NeverScrollableScrollPhysics(),
                      child: SizedBox(
                        width: targetWidth,
                        child: SystemGrid(actions: actions),
                      ),
                    ),
                  ),
                ),
            ],
          ),
        );
      },
    );
  }
}

const String _systemButtonKeyTag = 'system-button-key';

/// Taskbar button that triggers the power menu / system actions.
class SystemWidget extends StatelessWidget {
  /// Creates a new [SystemWidget].
  const new({
    required this.actions,
    this.processStarter = Process.start,
    super.key,
  });

  /// Key uniquely identifying the system button across builds.
  static const GlobalKey buttonKey = GlobalObjectKey(_systemButtonKeyTag);

  /// Configured system actions.
  final SystemActions actions;

  /// Function used to start system processes.
  final ProcessStarter processStarter;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      child: Hero(
        tag: _heroSystemButton,
        createRectTween: (begin, end) => RectTween(begin: begin, end: end),
        placeholderBuilder: (context, size, child) => child,
        child: IconButton(
          key: buttonKey,
          color: Colors.white,
          style: ButtonStyle(
            backgroundColor: WidgetStateProperty.all(
              Colors.black.withAlpha(60),
            ),
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
              HeroPageRoute<dynamic>(
                anchorRect: buttonRect,
                anchorKey: buttonKey,
                builder: (modalContext) {
                  final card = _SystemPopupCard(
                    actions: actions,
                    processStarter: processStarter,
                  );
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
      ),
    );
  }
}
