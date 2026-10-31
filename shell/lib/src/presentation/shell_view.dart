import 'dart:async' show unawaited;
import 'dart:ui' show Offset, Size;

import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/compositor_dart.dart'
    show
        CompositorBloc,
        CompositorEventType,
        CompositorRepository,
        CompositorState,
        DisplayOutput,
        GestureSwipeEndEvent,
        GestureSwipeUpdateEvent,
        Surface,
        SurfaceRequestActivateEvent,
        ZoomKeyEvent,
        ZoomScrollEvent;
import 'package:flutter/gestures.dart' show PointerDeviceKind;
import 'package:flutter/services.dart' show MouseCursor, SystemMouseCursors;
import 'package:flutter/widgets.dart'
    show
        AnimatedBuilder,
        BuildContext,
        Curves,
        Focus,
        FocusManager,
        LayoutBuilder,
        Listener,
        MouseRegion,
        Positioned,
        RepaintBoundary,
        SafeArea,
        SizedBox,
        Stack,
        StackFit,
        State,
        StatefulWidget,
        TickerMode,
        Widget,
        WidgetsBinding;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocConsumer, BlocSelector, ReadContext;
import 'package:shell/src/core/constants.dart' show maxFraction;
import 'package:shell/src/core/shell_controllers.dart' show ShellControllers;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart'
    show ConfigurationCubit;
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart'
    show
        ShellBloc,
        ShellOutputChanged,
        ShellOutputRemoved,
        ShellOverviewClosed,
        ShellOverviewOpened,
        ShellPageChanged,
        ShellState,
        ShellSurfaceMapped,
        ShellSurfaceUnmapped;
import 'package:shell/src/presentation/background.dart' show Background;
import 'package:shell/src/presentation/compositor_controller.dart'
    show CompositorController;
import 'package:shell/src/presentation/screen_zoom_overlay.dart'
    show ScreenZoomOverlay;
import 'package:shell/src/presentation/shell_page_view.dart' show ShellPageView;

/// The primary shell view rendering the desktop background, surface pageview,
/// taskbar compositor controller, and zoom overlay.
class ShellView extends StatefulWidget {
  /// Creates a new [ShellView].
  const new({this.controllers, super.key});

  /// Optional controllers holder, predominantly used for dependency injection
  /// and unit testing.
  final ShellControllers? controllers;

  @override
  State<ShellView> createState() => _ShellViewState();
}

class _ShellViewState extends State<ShellView> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      final shellBloc = context.read<ShellBloc>();
      final controllers =
          widget.controllers ?? context.read<ShellControllers>();
      final targetPage = shellBloc.state.currentPageIndex;
      final isOverview = shellBloc.state.isOverview;
      if (targetPage > 0) {
        controllers.targetPageIndex = targetPage;
        if (controllers.pageController.hasClients &&
            controllers.pageController.position.haveDimensions) {
          final maxPages =
              (controllers.pageController.position.maxScrollExtent /
                      controllers.pageController.position.viewportDimension)
                  .round();
          if (maxPages >= targetPage) {
            controllers.pageController.jumpToPage(targetPage);
          }
        }
      }
      if (isOverview) {
        controllers.overviewController.value = 1.0;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final shellBloc = context.read<ShellBloc>();
    final configurationCubit = context.read<ConfigurationCubit>();
    // Active controllers holder for the shell (pageView, animations, zoom,
    // focus, etc.). Either provided via constructor (e.g. for testing /
    // dependency injection) or retrieved from the ambient BuildContext.
    final shellControllers =
        widget.controllers ?? context.read<ShellControllers>();

    return BlocConsumer<CompositorBloc, CompositorState>(
      listenWhen: (previous, current) =>
          previous.lastEvent != current.lastEvent,
      listener: (context, compositorState) async {
        final compEvent = compositorState.lastEvent;
        if (compEvent == null) {
          return;
        }

        // Maps raw compositor surfaces into the shell's carousel order.
        // overrideHandles supplies the controller's synchronous handle list
        // so transitions use the latest order before the asynchronous
        // ShellBloc state update settles.
        final surfaces = shellBloc.state.orderedSurfaces(
          compositorState.surfaces,
          overrideHandles: shellControllers.surfaceHandles.isNotEmpty
              ? shellControllers.surfaceHandles
              : null,
        );
        final isOverview =
            shellBloc.state.isOverview || shellControllers.isOverview;
        final currentPageIndex = shellControllers.targetPageIndex;

        switch (compEvent.type) {
          case CompositorEventType.zoomScroll:
            shellControllers.markSuperUsedAsModifier();
            final zoomEvent = compEvent.event as ZoomScrollEvent;
            shellControllers.zoomController.handleScroll(
              zoomEvent.delta,
              Offset(zoomEvent.x, zoomEvent.y),
            );

          case CompositorEventType.zoomKey:
            shellControllers.markSuperUsedAsModifier();
            final zoomEvent = compEvent.event as ZoomKeyEvent;
            shellControllers.zoomController.handleKey(
              zoomEvent.action,
              Offset(zoomEvent.x, zoomEvent.y),
            );

          case CompositorEventType.gestureSwipeBegin:
            if (surfaces.isEmpty) {
              return;
            }
            shellControllers.isSwipingOverview = true;
            shellControllers.swipeStartVal =
                shellControllers.overviewController.value;
            if (shellControllers.swipeStartVal == 0.0) {
              CompositorRepository().platform.interactive = false;
              unawaited(
                CompositorRepository().platform.forceRenderAllViews(
                  force: true,
                ),
              );
              final s = surfaces.elementAtOrNull(
                currentPageIndex.clamp(
                  0,
                  surfaces.isNotEmpty ? surfaces.length - 1 : 0,
                ),
              );
              if (s != null) {
                unawaited(CompositorRepository().platform.clearFocus(s));
              }
            }

          case CompositorEventType.gestureSwipeUpdate:
            if (!shellControllers.isSwipingOverview || surfaces.isEmpty) {
              return;
            }
            final swipe = compEvent.event as GestureSwipeUpdateEvent;
            final delta = -swipe.dy / 280.0;
            shellControllers.overviewController.value =
                (shellControllers.overviewController.value + delta).clamp(
                  0.0,
                  1.0,
                );

          case CompositorEventType.gestureSwipeEnd:
            if (!shellControllers.isSwipingOverview) {
              return;
            }
            shellControllers.isSwipingOverview = false;
            final swipe = compEvent.event as GestureSwipeEndEvent;
            if (surfaces.isEmpty) {
              return;
            }
            final s = surfaces.elementAtOrNull(
              currentPageIndex.clamp(
                0,
                surfaces.isNotEmpty ? surfaces.length - 1 : 0,
              ),
            );
            final val = shellControllers.overviewController.value;
            final startVal = shellControllers.swipeStartVal;

            if (swipe.cancelled) {
              if (startVal >= 0.1) {
                unawaited(
                  shellControllers.overviewController.forward().then((_) {
                    shellControllers.focusNode.requestFocus();
                    shellBloc.add(const ShellOverviewOpened());
                  }),
                );
              } else {
                unawaited(
                  _closeOverview(
                    controllers: shellControllers,
                    shellBloc: shellBloc,
                    surfaces: surfaces,
                    s: s,
                    center: false,
                  ),
                );
              }
              return;
            }

            if (startVal == 0.0) {
              if (val >= 0.14) {
                unawaited(
                  shellControllers.overviewController.forward().then((_) {
                    shellControllers.focusNode.requestFocus();
                    shellBloc.add(const ShellOverviewOpened());
                  }),
                );
              } else {
                unawaited(
                  _closeOverview(
                    controllers: shellControllers,
                    shellBloc: shellBloc,
                    surfaces: surfaces,
                    s: s,
                    center: false,
                  ),
                );
              }
            } else {
              if (val > 0.75) {
                unawaited(
                  shellControllers.overviewController.forward().then((_) {
                    shellControllers.focusNode.requestFocus();
                    shellBloc.add(const ShellOverviewOpened());
                  }),
                );
              } else {
                unawaited(
                  _closeOverview(
                    controllers: shellControllers,
                    shellBloc: shellBloc,
                    surfaces: surfaces,
                    s: s,
                    center: false,
                  ),
                );
              }
            }

          case CompositorEventType.outputAdded:
            final out = compEvent.event as DisplayOutput;
            shellBloc.add(ShellOutputChanged(out));
            WidgetsBinding.instance.addPostFrameCallback((_) {
              if (shellControllers.pageController.hasClients) {
                final target = shellControllers.targetPageIndex;
                if (target > 0) {
                  shellControllers.pageController.jumpToPage(target);
                }
              }
            });

          case CompositorEventType.outputChanged:
            final out = compEvent.event as DisplayOutput;
            shellBloc.add(ShellOutputChanged(out));
            for (final surface in surfaces) {
              if (out.width != surface.width || out.height != surface.height) {
                if (surface.fullscreen ?? false) {
                  unawaited(
                    CompositorRepository().platform.surfaceToplevelSetSize(
                      surface,
                      out.width,
                      out.height,
                    ),
                  );
                } else {
                  unawaited(
                    CompositorRepository().platform.surfaceToplevelSetMaximized(
                      surface,
                    ),
                  );
                }
              }
            }
            if (shellControllers.pageController.hasClients) {
              shellControllers.pageController.jumpToPage(
                currentPageIndex.clamp(
                  0,
                  surfaces.isNotEmpty ? surfaces.length - 1 : 0,
                ),
              );
            }
            if (!isOverview) {
              CompositorRepository().platform.interactive = true;
              final targetSurface = surfaces.elementAtOrNull(
                currentPageIndex.clamp(
                  0,
                  surfaces.isNotEmpty ? surfaces.length - 1 : 0,
                ),
              );
              if (targetSurface != null) {
                unawaited(
                  CompositorRepository().platform.surfaceFocus(targetSurface),
                );
              }
              shellControllers.focusNode.unfocus();
              FocusManager.instance.primaryFocus?.unfocus();
            }

          case CompositorEventType.outputRemoved:
            final out = compEvent.event as DisplayOutput;
            shellBloc.add(ShellOutputRemoved(out.id));

          case CompositorEventType.surfaceMap:
            final surface = compEvent.event as Surface;

            if (shellControllers.pendingActivateHandle == surface.handle) {
              shellControllers.pendingActivateHandle = null;
            }

            // Outside of a soft-reset rehydration, any handle that is not a
            // live compositor surface is stale (e.g. persisted from a previous
            // engine run). Prune it so it can neither shift the insertion
            // index nor make a fresh surface look "already placed".
            if (!shellControllers.isRehydrating) {
              shellControllers.surfaceHandles.removeWhere(
                (h) => !compositorState.surfaces.containsKey(h),
              );
            }

            final currentHandles = shellControllers.surfaceHandles.isNotEmpty
                ? shellControllers.surfaceHandles
                : (shellControllers.isRehydrating &&
                          shellBloc.state.surfaceHandles.isNotEmpty
                      ? shellBloc.state.surfaceHandles
                      : <int>[]);
            final isAbsent = !currentHandles.contains(surface.handle);
            final int targetIndex;
            if (isAbsent) {
              if (currentHandles.isEmpty ||
                  currentPageIndex + 1 > currentHandles.length) {
                targetIndex = currentHandles.length;
              } else {
                targetIndex = currentPageIndex + 1;
              }
              shellBloc.add(
                ShellSurfaceMapped(
                  surfaceHandle: surface.handle,
                  targetIndex: targetIndex,
                  liveHandles: shellControllers.isRehydrating
                      ? null
                      : compositorState.surfaces.keys.toList(),
                ),
              );
              if (!shellControllers.surfaceHandles.contains(surface.handle)) {
                shellControllers.surfaceHandles.insert(
                  targetIndex.clamp(0, shellControllers.surfaceHandles.length),
                  surface.handle,
                );
              }
            } else {
              targetIndex = currentHandles.indexOf(surface.handle);
            }

            if (isAbsent) {
              shellControllers.targetPageIndex = targetIndex;
            }

            final platform = CompositorRepository().platform;
            if (surface.fullscreen ?? false) {
              shellControllers.hideDock(animate: false);
            } else {
              unawaited(platform.setImmersive(sticky: false));
              shellControllers.dockRevealController.value = 1;
              unawaited(platform.surfaceToplevelSetMaximized(surface));
            }
            FocusManager.instance.primaryFocus?.unfocus();

            WidgetsBinding.instance.addPostFrameCallback((_) async {
              if (!shellControllers.pageController.hasClients) {
                if (surface.fullscreen ?? false) {
                  shellControllers.hideDock(animate: false);
                } else {
                  unawaited(platform.setImmersive());
                }
                return;
              }
              final currentCompositorState = context.mounted
                  ? context.read<CompositorBloc>().state
                  : compositorState;
              final currentShellState = context.mounted
                  ? context.read<ShellBloc>().state
                  : shellBloc.state;
              final orderedSurfaces = currentShellState.orderedSurfaces(
                currentCompositorState.surfaces,
                overrideHandles: shellControllers.surfaceHandles,
              );

              if (isAbsent) {
                final target = orderedSurfaces.indexWhere(
                  (s) => s.handle == surface.handle,
                );
                final actualTargetIndex = target != -1 ? target : targetIndex;
                final safeTargetIndex = actualTargetIndex.clamp(
                  0,
                  orderedSurfaces.isNotEmpty ? orderedSurfaces.length - 1 : 0,
                );
                shellControllers.targetPageIndex = safeTargetIndex;
                final targetSurface = orderedSurfaces.elementAtOrNull(
                  safeTargetIndex,
                );
                if (targetSurface != null && !shellControllers.isOverview) {
                  if (targetSurface.fullscreen ?? false) {
                    shellControllers.hideDock(animate: false);
                  } else {
                    shellControllers.dockRevealController.forward();
                    unawaited(platform.setImmersive());
                  }
                  unawaited(platform.surfaceFocus(targetSurface));
                } else if (targetSurface == null) {
                  unawaited(platform.setImmersive());
                }

                if (shellControllers.pageController.hasClients) {
                  final currentPage = shellControllers.pageController.page
                      ?.round();
                  if (currentPage != safeTargetIndex) {
                    await shellControllers.pageController.animateToPage(
                      safeTargetIndex,
                      duration: shellControllers.isOverview
                          ? const Duration(milliseconds: 216)
                          : const Duration(milliseconds: 230),
                      curve: shellControllers.isOverview
                          ? Curves.ease
                          : Curves.linearToEaseOut,
                    );
                  }
                }
                shellBloc.add(ShellPageChanged(safeTargetIndex));
                if (shellControllers.isOverview) {
                  await shellControllers.overviewController.reverse();
                  await platform.forceRenderAllViews(force: false);
                  if (targetSurface != null) {
                    if (targetSurface.fullscreen ?? false) {
                      shellControllers.hideDock(animate: false);
                    } else {
                      shellControllers.dockRevealController.forward();
                      unawaited(platform.setImmersive());
                    }
                    unawaited(platform.surfaceFocus(targetSurface));
                  }
                  platform.interactive = true;
                  shellControllers.focusNode.unfocus();
                  FocusManager.instance.primaryFocus?.unfocus();
                  shellBloc.add(
                    ShellOverviewClosed(targetIndex: safeTargetIndex),
                  );
                }
              } else {
                final desiredIndex = currentShellState.currentPageIndex;
                // Rehydration is complete only once every expected handle has
                // been re-mapped by the compositor (otherwise the 2 s safety
                // timer in ShellControllers ends it).
                if (shellControllers.surfaceHandles.every(
                  currentCompositorState.surfaces.containsKey,
                )) {
                  shellControllers.isRehydrating = false;
                }
                if (orderedSurfaces.length > desiredIndex) {
                  shellControllers.targetPageIndex = desiredIndex;
                  if (shellControllers.pageController.hasClients) {
                    final currentPage = shellControllers.pageController.page
                        ?.round();
                    if (currentPage != desiredIndex) {
                      shellControllers.pageController.jumpToPage(desiredIndex);
                    }
                  }
                  final targetSurface = orderedSurfaces.elementAtOrNull(
                    desiredIndex,
                  );
                  if (targetSurface != null && !shellControllers.isOverview) {
                    if (targetSurface.fullscreen ?? false) {
                      shellControllers.hideDock(animate: false);
                    } else {
                      shellControllers.dockRevealController.forward();
                      unawaited(platform.setImmersive());
                    }
                    unawaited(platform.surfaceFocus(targetSurface));
                  }
                }
              }
            });
            WidgetsBinding.instance.scheduleFrame();

          case CompositorEventType.surfaceUnMap:
            final surface = compEvent.event as Surface?;
            if (surface == null) {
              return;
            }

            final unmappedHandle = surface.handle;
            final unmappedIndex = shellControllers.surfaceHandles.indexOf(
              unmappedHandle,
            );

            if (unmappedIndex != -1 &&
                unmappedIndex == currentPageIndex &&
                currentPageIndex > 0 &&
                shellControllers.pageController.hasClients) {
              final prevIndex = currentPageIndex - 1;
              shellControllers.targetPageIndex = prevIndex;
              await shellControllers.pageController.animateToPage(
                prevIndex,
                duration: isOverview
                    ? const Duration(milliseconds: 216)
                    : const Duration(milliseconds: 230),
                curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
              );
            }

            shellControllers.surfaceHandles.remove(unmappedHandle);
            shellBloc.add(ShellSurfaceUnmapped(unmappedHandle));
            CompositorRepository().platform.clearSurfaceState(unmappedHandle);

            final remainingSurfaces = shellBloc.state.orderedSurfaces(
              compositorState.surfaces,
              overrideHandles: shellControllers.surfaceHandles,
            );

            if (remainingSurfaces.isEmpty) {
              shellControllers.dockRevealController.value = 1;
              unawaited(CompositorRepository().platform.setImmersive());
            }

            if (remainingSurfaces.isEmpty && isOverview) {
              await shellControllers.overviewController.animateTo(
                0,
                duration: Duration.zero,
              );
              await CompositorRepository().platform.forceRenderAllViews(
                force: false,
              );
              shellBloc.add(const ShellOverviewClosed());
              return;
            }

            if (remainingSurfaces.isEmpty) {
              return;
            }

            int newTargetIndex;
            if (unmappedIndex != -1 && unmappedIndex <= currentPageIndex) {
              newTargetIndex = (currentPageIndex > 0 ? currentPageIndex - 1 : 0)
                  .clamp(0, remainingSurfaces.length - 1);
            } else {
              newTargetIndex = currentPageIndex.clamp(
                0,
                remainingSurfaces.length - 1,
              );
            }

            shellControllers.targetPageIndex = newTargetIndex;
            if (shellControllers.pageController.hasClients) {
              final activePage = shellControllers.pageController.page?.round();
              if (activePage != newTargetIndex) {
                shellControllers.pageController.jumpToPage(newTargetIndex);
              }
            }
            shellBloc.add(ShellPageChanged(newTargetIndex));

            if (!isOverview) {
              final targetSurface = remainingSurfaces.elementAtOrNull(
                newTargetIndex,
              );
              if (targetSurface != null) {
                if (targetSurface.fullscreen ?? false) {
                  shellControllers.hideDock(animate: false);
                } else {
                  shellControllers.dockRevealController.value = 1;
                  unawaited(CompositorRepository().platform.setImmersive());
                }
                await CompositorRepository().platform.surfaceFocus(
                  targetSurface,
                );
              } else {
                shellControllers.dockRevealController.value = 1;
                unawaited(CompositorRepository().platform.setImmersive());
              }
            }

          case CompositorEventType.surfaceRequestActivate:
            final activateEvent =
                compEvent.event as SurfaceRequestActivateEvent;
            final targetHandle = activateEvent.handle;
            final currentSurfaces = surfaces.isNotEmpty
                ? surfaces
                : shellBloc.state.orderedSurfaces(
                    compositorState.surfaces,
                    overrideHandles: shellControllers.surfaceHandles,
                  );
            final target = currentSurfaces.indexWhere(
              (s) => s.handle == targetHandle,
            );
            if (target != -1) {
              shellControllers
                ..pendingActivateHandle = null
                ..targetPageIndex = target;
              final targetSurface = currentSurfaces.elementAtOrNull(target);
              if (isOverview) {
                await _closeOverview(
                  controllers: shellControllers,
                  shellBloc: shellBloc,
                  surfaces: currentSurfaces,
                  s: targetSurface,
                  index: target,
                );
              } else {
                final platform = CompositorRepository().platform;
                if (targetSurface?.fullscreen ?? false) {
                  shellControllers.hideDock(animate: false);
                } else {
                  unawaited(platform.setImmersive(sticky: false));
                }
                WidgetsBinding.instance.addPostFrameCallback((_) async {
                  shellControllers.targetPageIndex = target;
                  if (targetSurface != null) {
                    if (targetSurface.fullscreen ?? false) {
                      shellControllers.hideDock(animate: false);
                    } else {
                      shellControllers.dockRevealController.forward();
                      unawaited(platform.setImmersive());
                      unawaited(
                        platform.surfaceToplevelSetMaximized(targetSurface),
                      );
                    }
                    unawaited(platform.surfaceFocus(targetSurface));
                  } else {
                    shellControllers.dockRevealController.forward();
                    unawaited(platform.setImmersive());
                  }
                  if (shellControllers.pageController.hasClients) {
                    final currentPage = shellControllers.pageController.page
                        ?.round();
                    if (currentPage != target) {
                      await shellControllers.pageController.animateToPage(
                        target,
                        duration: const Duration(milliseconds: 230),
                        curve: Curves.linearToEaseOut,
                      );
                    }
                  }
                  shellBloc.add(ShellPageChanged(target));
                });
                WidgetsBinding.instance.scheduleFrame();
              }
            } else {
              shellControllers.pendingActivateHandle = targetHandle;
            }

          case CompositorEventType.dockReveal:
            shellControllers.showDock();

          case CompositorEventType.surfaceFullscreenChange:
            final surface = compEvent.event as Surface;
            final platform = CompositorRepository().platform
              ..syncSurfaceFullscreen(
                surface.handle,
                fullscreen: surface.fullscreen ?? false,
              );
            if (surface.fullscreen ?? false) {
              final target = surfaces.indexWhere(
                (s) => s.handle == surface.handle,
              );
              if (target != -1 && target != currentPageIndex) {
                shellControllers.targetPageIndex = target;
                if (shellControllers.pageController.hasClients) {
                  await shellControllers.pageController.animateToPage(
                    target,
                    duration: const Duration(milliseconds: 230),
                    curve: Curves.linearToEaseOut,
                  );
                }
                shellBloc.add(ShellPageChanged(target));
              }
              final isCurrent =
                  target == -1 ||
                  target == currentPageIndex ||
                  shellControllers.targetPageIndex == target;
              if (isCurrent) {
                shellControllers.hideDock();
              }
            } else {
              final isCurrent =
                  surfaces.isNotEmpty &&
                  surfaces.elementAtOrNull(currentPageIndex)?.handle ==
                      surface.handle;
              final hasCurrentFullscreen =
                  surfaces.isNotEmpty &&
                  (surfaces.elementAtOrNull(currentPageIndex)?.fullscreen ??
                      false);
              if (isCurrent || !hasCurrentFullscreen) {
                shellControllers.dockShown = false;
                shellControllers.dockRevealController.forward();
                unawaited(platform.setImmersive());
              }
            }

          case CompositorEventType.surfaceTitleChange:
          case CompositorEventType.surfaceGeometryChange:
          case CompositorEventType.surfaceDecorationChange:
          case CompositorEventType.surfacePositionChange:
          case CompositorEventType.surfaceMinimizeRequest:
          case CompositorEventType.surfaceRequestMaximize:
          case CompositorEventType.surfaceresizeRequest:
          case CompositorEventType.surfaceGrabEnd:
          case CompositorEventType.subSurfaceMap:
          case CompositorEventType.subSurfaceUnMap:
          case CompositorEventType.subsurfacePositionChange:
          case CompositorEventType.popupMap:
          case CompositorEventType.popupUnMap:
            break;
        }
      },
      builder: (context, compositorState) {
        return BlocConsumer<ShellBloc, ShellState>(
          listenWhen: (previous, current) {
            if (previous.currentPageIndex == current.currentPageIndex &&
                previous.isOverview == current.isOverview) {
              return false;
            }
            if (shellControllers.targetPageIndex == current.currentPageIndex &&
                previous.isOverview == current.isOverview) {
              return false;
            }
            return true;
          },
          listener: (context, shellState) {
            shellControllers.targetPageIndex = shellState.currentPageIndex;
            final currentHandles = shellControllers.surfaceHandles.isNotEmpty
                ? shellControllers.surfaceHandles
                : (shellState.surfaceHandles.isNotEmpty
                      ? shellState.surfaceHandles
                      : compositorState.surfaces.keys.toList());
            final currentSurfaces = shellState.orderedSurfaces(
              compositorState.surfaces,
              overrideHandles: currentHandles,
            );
            final curSurface = currentSurfaces.elementAtOrNull(
              shellState.currentPageIndex,
            );
            if (curSurface?.fullscreen ?? false) {
              shellControllers.hideDock();
            } else {
              shellControllers.dockRevealController.forward();
              unawaited(CompositorRepository().platform.setImmersive());
            }
            WidgetsBinding.instance.addPostFrameCallback((_) async {
              if (!shellControllers.pageController.hasClients) {
                return;
              }
              final currentPage = shellControllers.pageController.page?.round();
              if (currentPage != shellState.currentPageIndex) {
                final isOverview =
                    shellControllers.isOverview || shellState.isOverview;
                await shellControllers.pageController.animateToPage(
                  shellState.currentPageIndex,
                  duration: isOverview
                      ? const Duration(milliseconds: 216)
                      : const Duration(milliseconds: 230),
                  curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
                );
              }
            });
            WidgetsBinding.instance.scheduleFrame();
          },
          builder: (context, shellState) {
            // Outside of rehydration, only live compositor surfaces may appear
            // in the carousel order; never resurrect persisted handles.
            var baseHandles = shellControllers.surfaceHandles.isNotEmpty
                ? shellControllers.surfaceHandles
                : shellState.surfaceHandles;
            if (!shellControllers.isRehydrating) {
              baseHandles = baseHandles
                  .where(compositorState.surfaces.containsKey)
                  .toList();
            }
            final currentHandles = baseHandles.isNotEmpty
                ? baseHandles
                : compositorState.surfaces.keys.toList();
            final surfaces = shellState.orderedSurfaces(
              compositorState.surfaces,
              overrideHandles: currentHandles,
            );
            shellControllers.surfaceHandles = List<int>.from(currentHandles);

            shellControllers.attachKeyboardHandler(
              getSurfaces: () => surfaces,
              shellBloc: shellBloc,
              configurationCubit: configurationCubit,
              onToggleOverview: () => _toggleOverview(
                controllers: shellControllers,
                shellBloc: shellBloc,
                surfaces: surfaces,
              ),
              onUpdateCurrentPageIndex: (index) => _updateCurrentPageIndex(
                shellControllers,
                shellBloc,
                surfaces,
                index,
              ),
            );

            return BlocSelector<ShellBloc, ShellState, DisplayOutput?>(
              selector: (state) {
                return state.activeOutput ??
                    compositorState.outputs.values.firstOrNull;
              },
              builder: (context, output) {
                return LayoutBuilder(
                  builder: (context, constraints) {
                    final maxw =
                        output?.width.toDouble() ?? constraints.maxWidth;
                    final maxh =
                        output?.height.toDouble() ?? constraints.maxHeight;
                    final currentSurface = surfaces.elementAtOrNull(
                      shellControllers.targetPageIndex.clamp(
                        0,
                        surfaces.isNotEmpty ? surfaces.length - 1 : 0,
                      ),
                    );
                    final isFullscreen = currentSurface?.fullscreen ?? false;
                    shellControllers.isFullscreen = isFullscreen;
                    final isOverview =
                        shellControllers.isOverview ||
                        shellBloc.state.isOverview;

                    return SafeArea(
                      top: !isFullscreen || isOverview,
                      bottom: !isFullscreen || isOverview,
                      child: SizedBox(
                        width: maxw,
                        height: maxh,
                        child: Focus(
                          focusNode: shellControllers.focusNode,
                          autofocus:
                              !shellControllers.pageController.hasClients,
                          canRequestFocus:
                              shellControllers.isOverview || surfaces.isEmpty,
                          child: BlocSelector<ShellBloc, ShellState, bool>(
                            selector: (state) {
                              return state.hideCursor;
                            },
                            builder: (context, hideCursor) {
                              return MouseRegion(
                                cursor: hideCursor
                                    ? SystemMouseCursors.none
                                    : MouseCursor.defer,
                                onHover: (event) {
                                  if (event.kind == PointerDeviceKind.touch) {
                                    return;
                                  }
                                  shellControllers.zoomController.updateCursor(
                                    event.localPosition,
                                  );
                                  shellControllers.resetMouseIdleTimer(
                                    shellBloc,
                                  );
                                },
                                child: Listener(
                                  onPointerDown: (_) {
                                    shellControllers.markSuperUsedAsModifier();
                                  },
                                  onPointerSignal: (_) {
                                    shellControllers.markSuperUsedAsModifier();
                                  },
                                  onPointerMove: (event) {
                                    if (event.kind == PointerDeviceKind.touch) {
                                      return;
                                    }
                                    shellControllers.zoomController
                                        .updateCursor(event.localPosition);
                                    shellControllers.resetMouseIdleTimer(
                                      shellBloc,
                                    );
                                  },
                                  child: AnimatedBuilder(
                                    animation:
                                        shellControllers.overviewController,
                                    builder: (context, _) {
                                      final gap =
                                          shellControllers.gapAnim.value;
                                      final radius =
                                          shellControllers.radiusAnim.value;
                                      final fraction =
                                          shellControllers.fractionAnim.value;
                                      final isBackgroundVisible =
                                          surfaces.isEmpty ||
                                          (fraction < maxFraction) ||
                                          shellControllers
                                              .overviewController
                                              .isAnimating;

                                      return Stack(
                                        fit: StackFit.expand,
                                        children: <Widget>[
                                          TickerMode(
                                            enabled: isBackgroundVisible,
                                            child: const RepaintBoundary(
                                              child: Background(),
                                            ),
                                          ),
                                          if (output != null)
                                            RepaintBoundary(
                                              child: ShellPageView(
                                                surfaces: surfaces,
                                                handlePageViewChanged:
                                                    (index) =>
                                                        _handlePageViewChanged(
                                                          controllers:
                                                              shellControllers,
                                                          shellBloc: shellBloc,
                                                          surfaces: surfaces,
                                                          index: index,
                                                        ),
                                                toggleOverview: ({index}) =>
                                                    _toggleOverview(
                                                      controllers:
                                                          shellControllers,
                                                      shellBloc: shellBloc,
                                                      surfaces: surfaces,
                                                      index: index,
                                                    ),
                                                closeOverview:
                                                    (s, {center = true}) =>
                                                        _closeOverview(
                                                          controllers:
                                                              shellControllers,
                                                          shellBloc: shellBloc,
                                                          surfaces: surfaces,
                                                          s: s,
                                                          center: center,
                                                        ),
                                                openOverview: (s) =>
                                                    _openOverview(
                                                      controllers:
                                                          shellControllers,
                                                      shellBloc: shellBloc,
                                                      surfaces: surfaces,
                                                      s: s,
                                                    ),
                                                pageController: shellControllers
                                                    .pageController,
                                                animationController:
                                                    shellControllers
                                                        .overviewController,
                                                gap: gap,
                                                fraction: fraction,
                                                radius: radius,
                                                output: output,
                                              ),
                                            ),
                                          RepaintBoundary(
                                            child: CompositorController(
                                              pageController: shellControllers
                                                  .pageController,
                                              controllers: shellControllers,
                                              isFullscreen: isFullscreen,
                                              isOverview: isOverview,
                                              hasSurfaces: surfaces.isNotEmpty,
                                              toggleOverview: () =>
                                                  _toggleOverview(
                                                    controllers:
                                                        shellControllers,
                                                    shellBloc: shellBloc,
                                                    surfaces: surfaces,
                                                  ),
                                              onUpdateCurrentPageIndex:
                                                  (index) =>
                                                      _updateCurrentPageIndex(
                                                        shellControllers,
                                                        shellBloc,
                                                        surfaces,
                                                        index,
                                                      ),
                                            ),
                                          ),
                                          Positioned.fill(
                                            child: ScreenZoomOverlay(
                                              controller: shellControllers
                                                  .zoomController,
                                              size: Size(maxw, maxh),
                                            ),
                                          ),
                                        ],
                                      );
                                    },
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                      ),
                    );
                  },
                );
              },
            );
          },
        );
      },
    );
  }

  static Future<void> _toggleOverview({
    required ShellControllers controllers,
    required ShellBloc shellBloc,
    required List<Surface> surfaces,
    int? index,
  }) async {
    if (surfaces.isEmpty) {
      return;
    }
    final i =
        index ??
        (controllers.pageController.hasClients
            ? controllers.pageController.page?.round() ??
                  controllers.pageController.initialPage
            : 0);
    final s = surfaces.elementAtOrNull(i);

    if (controllers.isOverview) {
      await _closeOverview(
        controllers: controllers,
        shellBloc: shellBloc,
        surfaces: surfaces,
        s: s,
        index: i,
      );
    } else {
      await _openOverview(
        controllers: controllers,
        shellBloc: shellBloc,
        surfaces: surfaces,
        s: s,
      );
    }
  }

  static Future<void> _openOverview({
    required ShellControllers controllers,
    required ShellBloc shellBloc,
    required List<Surface> surfaces,
    required Surface? s,
  }) async {
    if (surfaces.isEmpty) {
      return;
    }
    CompositorRepository().platform.interactive = false;
    await CompositorRepository().platform.forceRenderAllViews(force: true);

    if (s != null) {
      await CompositorRepository().platform.clearFocus(s);
    }

    await controllers.overviewController.forward();
    controllers.focusNode.requestFocus();
    shellBloc.add(const ShellOverviewOpened());
  }

  static Future<void> _closeOverview({
    required ShellControllers controllers,
    required ShellBloc shellBloc,
    required List<Surface> surfaces,
    required Surface? s,
    bool center = true,
    int? index,
  }) async {
    if (surfaces.isEmpty) {
      return;
    }
    final target =
        index ??
        controllers.pageController.page?.round() ??
        controllers.pageController.initialPage;
    controllers.targetPageIndex = target;
    if (center && controllers.pageController.hasClients) {
      await controllers.pageController.animateToPage(
        target,
        duration: const Duration(milliseconds: 216),
        curve: Curves.ease,
      );
    }
    await controllers.overviewController.reverse();

    final targetSurface = surfaces.elementAtOrNull(target) ?? s;
    if (targetSurface != null) {
      final isFullscreen = targetSurface.fullscreen ?? false;
      controllers.isFullscreen = isFullscreen;
      final platform = CompositorRepository().platform;
      if (isFullscreen) {
        controllers.hideDock(animate: false);
      } else {
        controllers.dockRevealController.forward();
        unawaited(platform.setImmersive());
        unawaited(platform.surfaceToplevelSetMaximized(targetSurface));
      }
      await platform.surfaceFocus(targetSurface);
    } else {
      controllers.dockRevealController.forward();
      unawaited(CompositorRepository().platform.setImmersive());
    }
    await CompositorRepository().platform.forceRenderAllViews(force: false);
    CompositorRepository().platform.interactive = true;
    controllers.focusNode.unfocus();
    FocusManager.instance.primaryFocus?.unfocus();

    shellBloc.add(ShellOverviewClosed(targetIndex: index));
  }

  static Future<void> _handlePageViewChanged({
    required ShellControllers controllers,
    required ShellBloc shellBloc,
    required List<Surface> surfaces,
    required int index,
  }) async {
    if (controllers.isRehydrating && index != controllers.targetPageIndex) {
      return;
    }
    controllers.targetPageIndex = index;
    shellBloc.add(ShellPageChanged(index));

    if (shellBloc.state.isOverview) {
      return;
    }

    final s = surfaces.elementAtOrNull(index);
    if (s != null) {
      final platform = CompositorRepository().platform;
      if (s.fullscreen ?? false) {
        controllers.hideDock();
      } else {
        controllers.dockRevealController.forward();
        unawaited(platform.setImmersive());
        unawaited(platform.surfaceToplevelSetMaximized(s));
      }
      unawaited(platform.surfaceFocus(s));
    } else {
      controllers.dockRevealController.forward();
      unawaited(CompositorRepository().platform.setImmersive());
    }
    FocusManager.instance.primaryFocus?.unfocus();
  }

  static Future<void> _updateCurrentPageIndex(
    ShellControllers controllers,
    ShellBloc shellBloc,
    List<Surface> surfaces,
    int index,
  ) async {
    if (surfaces.isNotEmpty) {
      final target = index.clamp(0, surfaces.length - 1);
      controllers.targetPageIndex = target;
      final isOverview = controllers.isOverview || shellBloc.state.isOverview;
      final platform = CompositorRepository().platform;
      if (!isOverview) {
        unawaited(platform.setImmersive(sticky: false));
      }
      if (controllers.pageController.hasClients) {
        await controllers.pageController.animateToPage(
          target,
          duration: isOverview
              ? const Duration(milliseconds: 216)
              : const Duration(milliseconds: 230),
          curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
        );
      }
      shellBloc.add(ShellPageChanged(target));

      if (!isOverview) {
        final targetSurface = surfaces.elementAtOrNull(target);
        if (targetSurface != null) {
          if (targetSurface.fullscreen ?? false) {
            controllers.hideDock();
          } else {
            controllers.dockRevealController.forward();
            unawaited(platform.setImmersive());
            unawaited(platform.surfaceToplevelSetMaximized(targetSurface));
          }
          unawaited(platform.surfaceFocus(targetSurface));
        } else {
          controllers.dockRevealController.forward();
          unawaited(platform.setImmersive());
        }
      }
    } else {
      controllers.dockRevealController.forward();
      unawaited(CompositorRepository().platform.setImmersive());
    }
  }
}
