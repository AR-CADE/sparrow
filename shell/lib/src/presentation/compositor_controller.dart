import 'dart:ui' show Offset;

import 'package:compositor_dart/compositor_dart.dart' show CompositorRepository;
import 'package:flutter/foundation.dart' show kDebugMode;
import 'package:flutter/widgets.dart'
    show
        Alignment,
        AnimatedBuilder,
        AnimatedSwitcher,
        Curves,
        ExcludeFocus,
        FadeTransition,
        GestureDetector,
        HitTestBehavior,
        IgnorePointer,
        Listenable,
        Listener,
        MediaQuery,
        MouseRegion,
        SlideTransition,
        Stack,
        Transform,
        Tween,
        ValueKey,
        Visibility;
import 'package:flutter_bloc/flutter_bloc.dart' show BlocBuilder, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        Align,
        BuildContext,
        Center,
        EdgeInsets,
        Padding,
        PageController,
        SizedBox,
        StatelessWidget,
        Widget;
import 'package:shell/src/core/shell_controllers.dart' show ShellControllers;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart'
    show ShellBloc, ShellOverviewOpened;
import 'package:shell/src/presentation/bottom_indicator.dart'
    show BottomIndicator;
import 'package:shell/src/presentation/grid.dart' show GridWidget;
import 'package:shell/src/presentation/navigator_control.dart'
    show NavigationControllerWidget;
import 'package:shell/src/presentation/system.dart' show SystemWidget;
import 'package:shell/src/presentation/taskbar.dart' show TaskBar;
import 'package:shell/src/presentation/terminal.dart' show TerminalWidget;

/// Controller managing taskbar actions, navigation, system popup, and launcher.
class CompositorController extends StatelessWidget {
  /// Creates a new [CompositorController].
  const new({
    required this.onUpdateCurrentPageIndex,
    required this.toggleOverview,
    required this.pageController,
    this.controllers,
    this.isFullscreen = false,
    this.isOverview = false,
    this.hasSurfaces = false,
    super.key,
  });

  /// Surface page controller.
  final PageController pageController;

  /// Callback to toggle overview mode.
  final Future<void> Function() toggleOverview;

  /// Callback to update the current page index.
  final Future<void> Function(int) onUpdateCurrentPageIndex;

  /// Optional controllers holder.
  final ShellControllers? controllers;

  /// Whether the currently active surface is in fullscreen.
  final bool isFullscreen;

  /// Whether the shell is in overview mode.
  final bool isOverview;

  /// Whether any application surfaces are currently active.
  final bool hasSurfaces;

  @override
  Widget build(BuildContext context) {
    return BlocBuilder<ConfigurationCubit, ConfigurationState>(
      builder: (context, state) {
        final config = state.configuration;
        final launcherCmd = config.launcher;
        final terminalCmd = config.terminal;
        final systemActions = config.systemActions;

        final leading = <Widget>[
          if (launcherCmd != null && launcherCmd.isNotEmpty && !kDebugMode)
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

        final trailing = kDebugMode
            ? <Widget>[]
            : <Widget>[
                if (terminalCmd != null && terminalCmd.isNotEmpty)
                  Padding(
                    padding: const EdgeInsets.only(right: 20),
                    child: Center(child: TerminalWidget(command: terminalCmd)),
                  ),
                if (systemActions != null)
                  Padding(
                    padding: const EdgeInsets.only(right: 2),
                    child: SystemWidget(actions: systemActions),
                  ),
              ];

        final dockButtonsWidget = SizedBox(
          key: const ValueKey('dock_buttons'),
          width: double.maxFinite,
          height: 64,
          child: TaskBar(
            leading: leading,
            center: const [],
            trailing: trailing,
          ),
        );

        const bottomIndicatorWidget = SizedBox(
          key: ValueKey('bottom_indicator'),
          width: 360,
          height: 8,
          child: BottomIndicator(),
        );

        final shellControllers = controllers;
        Widget content;
        if (shellControllers != null) {
          shellControllers.isFullscreen = isFullscreen;
          content = AnimatedBuilder(
            animation: Listenable.merge([
              shellControllers.dockRevealAnim,
              shellControllers.dockShowNotifier,
            ]),
            builder: (context, _) {
              final isRehydrating = shellControllers.isRehydrating;
              final progress = (isOverview || !isFullscreen)
                  ? (isRehydrating && !hasSurfaces ? 0.0 : 1.0)
                  : shellControllers.dockRevealAnim.value;
              final slideOffset = (1.0 - progress) * 80.0;
              final isHidden = progress == 0.0;

              final showDockButtons =
                  isOverview ||
                  (!hasSurfaces && !isRehydrating) ||
                  shellControllers.dockShown;

              final dockContent = AnimatedSwitcher(
                duration: const Duration(milliseconds: 220),
                switchInCurve: Curves.easeOutCubic,
                switchOutCurve: Curves.easeInCubic,
                layoutBuilder: (currentChild, previousChildren) => Stack(
                  alignment: Alignment.bottomCenter,
                  children: [...previousChildren, ?currentChild],
                ),
                transitionBuilder: (child, animation) {
                  return FadeTransition(
                    opacity: animation,
                    child: SlideTransition(
                      position: Tween<Offset>(
                        begin: const Offset(0, 0.2),
                        end: Offset.zero,
                      ).animate(animation),
                      child: child,
                    ),
                  );
                },
                child: showDockButtons
                    ? dockButtonsWidget
                    : bottomIndicatorWidget,
              );

              return IgnorePointer(
                ignoring: isHidden,
                child: Visibility(
                  visible: !isHidden,
                  child: Transform.translate(
                    offset: Offset(0, slideOffset),
                    child: MouseRegion(
                      onHover: (_) => shellControllers.resetDockTimer(),
                      child: Listener(
                        onPointerDown: (_) => shellControllers.resetDockTimer(),
                        child: GestureDetector(
                          behavior: HitTestBehavior.translucent,
                          onVerticalDragDown: (_) {
                            shellControllers.swipeStartVal =
                                shellControllers.overviewController.value;
                          },
                          onVerticalDragStart: (_) async {
                            if (!isOverview) {
                              CompositorRepository().platform.interactive =
                                  false;
                              await CompositorRepository().platform
                                  .forceRenderAllViews(force: true);
                            }
                          },
                          onVerticalDragUpdate: (details) {
                            final primaryDelta = details.primaryDelta;
                            if (primaryDelta == null) return;
                            final height = MediaQuery.of(context).size.height;
                            if (height > 0) {
                              final delta = primaryDelta / height;
                              shellControllers.overviewController.value =
                                  (shellControllers.overviewController.value -
                                          delta)
                                      .clamp(0.0, 1.0);
                            }
                          },
                          onVerticalDragEnd: (details) async {
                            final val =
                                shellControllers.overviewController.value;
                            if (val >= 0.14) {
                              await shellControllers.overviewController
                                  .forward();
                              shellControllers.focusNode.requestFocus();
                              if (context.mounted) {
                                context.read<ShellBloc>().add(
                                  const ShellOverviewOpened(),
                                );
                              }
                            } else {
                              await toggleOverview();
                            }
                          },
                          onTap: () async {
                            if (!showDockButtons) {
                              await toggleOverview();
                            }
                          },
                          child: dockContent,
                        ),
                      ),
                    ),
                  ),
                ),
              );
            },
          );
        } else {
          final showDockButtons = isOverview || !hasSurfaces;
          content = GestureDetector(
            behavior: HitTestBehavior.translucent,
            onTap: () async {
              if (!showDockButtons) {
                await toggleOverview();
              }
            },
            child: showDockButtons ? dockButtonsWidget : bottomIndicatorWidget,
          );
        }

        return ExcludeFocus(
          child: Align(alignment: Alignment.bottomCenter, child: content),
        );
      },
    );
  }
}
