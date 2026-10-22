import 'dart:async' show Timer, unawaited;
import 'dart:io' show Process, ProcessStartMode;
import 'dart:ui' show Offset;

import 'package:compositor_dart/compositor_dart.dart'
    show CompositorRepository, KeyStatus, Surface;
import 'package:flutter/foundation.dart' show ValueNotifier;
import 'package:flutter/services.dart'
    show
        HardwareKeyboard,
        KeyDownEvent,
        KeyEvent,
        KeyRepeatEvent,
        KeyUpEvent,
        LogicalKeyboardKey,
        PhysicalKeyboardKey;
import 'package:flutter/widgets.dart'
    show
        Animation,
        AnimationController,
        AnimationStatus,
        CurvedAnimation,
        Curves,
        FocusManager,
        FocusNode,
        TickerProvider,
        Tween;
import 'package:shell/src/core/constants.dart'
    show CustomPageController, ProcessStarter, maxFraction, minFraction;
import 'package:shell/src/core/ticker_provider.dart' show TickerProviderImpl;
import 'package:shell/src/core/zoom_controller.dart' show ZoomController;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart'
    show ConfigurationCubit;
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart'
    show ShellBloc, ShellCursorIdleTimeout, ShellCursorMoved;

/// Holds and manages controllers, animations, and keyboard handlers for the
/// shell.
class ShellControllers {
  /// Creates a new [ShellControllers] instance.
  new({TickerProvider? vsync, ProcessStarter? processStarter})
    : _processStarter = processStarter ?? Process.start {
    final provider = vsync ?? const TickerProviderImpl();

    overviewController = AnimationController(
      duration: const Duration(milliseconds: 145),
      vsync: provider,
    );

    zoomController = ZoomController(vsync: provider);

    fractionAnim = Tween<double>(
      begin: maxFraction,
      end: minFraction,
    ).animate(CurvedAnimation(parent: overviewController, curve: Curves.ease));

    gapAnim = Tween<double>(
      begin: 0,
      end: 32,
    ).animate(CurvedAnimation(parent: overviewController, curve: Curves.ease));

    radiusAnim = Tween<double>(
      begin: 0,
      end: 24,
    ).animate(CurvedAnimation(parent: overviewController, curve: Curves.ease));

    pageController = CustomPageController(viewportFraction: fractionAnim.value);

    dockRevealController = AnimationController(
      duration: const Duration(milliseconds: 250),
      vsync: provider,
      value: 1,
    );

    dockRevealAnim = CurvedAnimation(
      parent: dockRevealController,
      curve: Curves.easeOutCubic,
      reverseCurve: Curves.easeInCubic,
    );

    focusNode = FocusNode();

    overviewController.addListener(_onAnimationTick);
  }

  /// Animation controller driving the overview transition.
  late final AnimationController overviewController;

  /// Zoom controller for screen magnification.
  late final ZoomController zoomController;

  /// Animation driving viewport fraction.
  late final Animation<double> fractionAnim;

  /// Animation driving gap spacing.
  late final Animation<double> gapAnim;

  /// Animation driving corner radius.
  late final Animation<double> radiusAnim;

  /// Page controller for surfaces.
  late final CustomPageController pageController;

  /// Animation controller driving the dock reveal over fullscreen.
  late final AnimationController dockRevealController;

  /// Animation driving the dock slide-in translation.
  late final Animation<double> dockRevealAnim;

  /// Shell focus node.
  late final FocusNode focusNode;

  /// List of surface handles ordered sequentially for the carousel
  /// [pageController].
  ///
  /// This list tracks the visual order of application surfaces in the shell.
  /// It is updated immediately/synchronously upon surface mapping and unmapping
  /// so that page layout and transitions can begin immediately without waiting
  /// for asynchronous [ShellBloc] state updates.
  List<int> surfaceHandles = [];

  /// Current target or active page index in the carousel.
  ///
  /// Serves as the single source of truth for in-flight page navigation,
  /// preventing duplicate conflicting animations between event handlers and
  /// Bloc listeners.
  int targetPageIndex = 0;

  /// Handle of a surface that was requested to be activated before it was
  /// mapped.
  int? pendingActivateHandle;

  /// Whether an overview swipe gesture is currently active.
  bool isSwipingOverview = false;

  /// The overview controller value when the current swipe gesture began.
  double swipeStartVal = 0;

  Timer? _dockHideTimer;
  Timer? _mouseIdleTimer;
  bool _keyboardHandlerAttached = false;
  bool _superUsedAsModifier = false;
  bool _superDown = false;
  bool _controlDown = false;
  bool _altDown = false;
  final ProcessStarter _processStarter;

  List<Surface> Function()? _getSurfaces;
  ShellBloc? _shellBloc;
  ConfigurationCubit? _configurationCubit;
  Future<void> Function()? _onToggleOverview;
  Future<void> Function(int)? _onUpdateCurrentPageIndex;

  /// Whether the shell is currently in overview mode or animating toward it.
  bool get isOverview =>
      overviewController.value > 0.5 ||
      (pageController.hasClients &&
          pageController.viewportFraction == minFraction) ||
      (_shellBloc?.state.isOverview ?? false);

  /// Whether the shell is currently in fullscreen mode.
  bool isFullscreen = false;

  /// Notifier signaling whether the dock buttons are temporarily revealed.
  final ValueNotifier<bool> dockRevealedNotifier = ValueNotifier<bool>(false);

  /// Whether the dock is currently temporarily revealed.
  bool get dockRevealed => dockRevealedNotifier.value;
  set dockRevealed(bool value) => dockRevealedNotifier.value = value;

  /// Reveals the dock for a temporary duration.
  void revealDock({Duration duration = const Duration(seconds: 2)}) {
    _dockHideTimer?.cancel();
    dockRevealedNotifier.value = true;
    unawaited(
      CompositorRepository().platform.setFullscreenDockRevealed(revealed: true),
    );
    dockRevealController.forward();
    _dockHideTimer = Timer(duration, hideDock);
  }

  /// Resets the auto-hide timer for the temporarily revealed dock.
  void resetDockTimer({Duration duration = const Duration(seconds: 2)}) {
    if (_dockHideTimer != null) {
      _dockHideTimer?.cancel();
      _dockHideTimer = Timer(duration, hideDock);
    }
  }

  /// Hides the temporarily revealed dock and restores direct scanout or the
  /// bar.
  void hideDock({bool animate = true}) {
    _dockHideTimer?.cancel();
    _dockHideTimer = null;
    if (isFullscreen) {
      if (!animate) {
        dockRevealedNotifier.value = false;
        dockRevealController.value = 0;
        unawaited(CompositorRepository().platform.setFullscreenDockRevealed());
        return;
      }
    }

    unawaited(
      dockRevealController.reverse().then((_) {
        if (dockRevealController.status == AnimationStatus.dismissed) {
          dockRevealedNotifier.value = false;
          unawaited(
            CompositorRepository().platform.setFullscreenDockRevealed(),
          );
        }
      }),
    );
  }

  void _onAnimationTick() {
    final page = pageController.hasClients
        ? pageController.page ?? pageController.initialPage.toDouble()
        : 0.0;
    final vf = fractionAnim.value;
    CompositorRepository().platform.interactive = vf == maxFraction;
    pageController.setViewportFraction(vf);
    if (pageController.hasClients) {
      pageController.jumpToPage(page.round());
    }
  }

  /// Resets the mouse idle timer and notifies [ShellBloc].
  void resetMouseIdleTimer(ShellBloc bloc, {bool showImmediately = true}) {
    if (showImmediately && bloc.state.hideCursor) {
      bloc.add(const ShellCursorMoved());
    }
    _mouseIdleTimer?.cancel();
    _mouseIdleTimer = Timer(const Duration(seconds: 3), () {
      bloc.add(const ShellCursorIdleTimeout());
    });
  }

  /// Attaches the global keyboard handler if not already attached.
  void attachKeyboardHandler({
    required List<Surface> Function() getSurfaces,
    required ShellBloc shellBloc,
    required ConfigurationCubit configurationCubit,
    required Future<void> Function() onToggleOverview,
    required Future<void> Function(int) onUpdateCurrentPageIndex,
  }) {
    _getSurfaces = getSurfaces;
    _shellBloc = shellBloc;
    _configurationCubit = configurationCubit;
    _onToggleOverview = onToggleOverview;
    _onUpdateCurrentPageIndex = onUpdateCurrentPageIndex;

    if (!_keyboardHandlerAttached) {
      HardwareKeyboard.instance.addHandler(_onGlobalKeyEvent);
      _keyboardHandlerAttached = true;
    }
  }

  /// Marks the Super key as having been used as a modifier (e.g. for zoom
  /// scroll, gestures, or mouse actions) so releasing Super does not trigger
  /// the launcher.
  void markSuperUsedAsModifier() {
    _superUsedAsModifier = true;
  }

  /// Forwards a [KeyEvent] to the internal global key handler.
  bool handleKeyEvent(KeyEvent event) => _onGlobalKeyEvent(event);

  bool _onGlobalKeyEvent(KeyEvent event) {
    final surfaces = _getSurfaces?.call() ?? const [];
    final config = _configurationCubit?.state.configuration;
    final isOverview = this.isOverview;

    final isSuperKey =
        event.logicalKey == LogicalKeyboardKey.superKey ||
        event.logicalKey == LogicalKeyboardKey.metaLeft ||
        event.logicalKey == LogicalKeyboardKey.metaRight ||
        event.physicalKey == PhysicalKeyboardKey.superKey ||
        event.physicalKey == PhysicalKeyboardKey.metaLeft ||
        event.physicalKey == PhysicalKeyboardKey.metaRight;

    if (isSuperKey) {
      if (event is KeyDownEvent) {
        _superDown = true;
        _superUsedAsModifier = false;
      } else if (event is KeyUpEvent) {
        _superDown = false;
      }
    }

    final isControlKey =
        event.logicalKey == LogicalKeyboardKey.controlLeft ||
        event.logicalKey == LogicalKeyboardKey.controlRight ||
        event.physicalKey == PhysicalKeyboardKey.controlLeft ||
        event.physicalKey == PhysicalKeyboardKey.controlRight;
    if (isControlKey) {
      _controlDown = event is KeyDownEvent || event is KeyRepeatEvent;
    }

    final isAltKey =
        event.logicalKey == LogicalKeyboardKey.altLeft ||
        event.logicalKey == LogicalKeyboardKey.altRight ||
        event.physicalKey == PhysicalKeyboardKey.altLeft ||
        event.physicalKey == PhysicalKeyboardKey.altRight;
    if (isAltKey) {
      _altDown = event is KeyDownEvent || event is KeyRepeatEvent;
    }

    final isSuperPressed =
        _superDown ||
        HardwareKeyboard.instance.isMetaPressed ||
        HardwareKeyboard.instance.isLogicalKeyPressed(
          LogicalKeyboardKey.superKey,
        ) ||
        HardwareKeyboard.instance.isLogicalKeyPressed(
          LogicalKeyboardKey.metaLeft,
        ) ||
        HardwareKeyboard.instance.isLogicalKeyPressed(
          LogicalKeyboardKey.metaRight,
        );

    if (!isSuperKey && isSuperPressed) {
      // If ANY non-super key down or repeat arrives while Super is held,
      // Super has been used in a combination / modifier and must not trigger the launcher on release.
      if (event is KeyDownEvent || event is KeyRepeatEvent) {
        _superUsedAsModifier = true;
      }
    }

    final currentPageIndex = pageController.hasClients
        ? (pageController.page?.round() ?? pageController.initialPage).clamp(
            0,
            surfaces.isNotEmpty ? surfaces.length - 1 : 0,
          )
        : 0;

    if (isSuperPressed) {
      if (event.logicalKey == LogicalKeyboardKey.equal ||
          event.logicalKey == LogicalKeyboardKey.add ||
          event.physicalKey == PhysicalKeyboardKey.equal ||
          event.physicalKey == PhysicalKeyboardKey.numpadAdd) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          zoomController.handleKey(1, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.minus ||
          event.physicalKey == PhysicalKeyboardKey.minus ||
          event.physicalKey == PhysicalKeyboardKey.numpadSubtract) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          zoomController.handleKey(-1, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.digit0 ||
          event.physicalKey == PhysicalKeyboardKey.digit0 ||
          event.physicalKey == PhysicalKeyboardKey.numpad0) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          zoomController.handleKey(0, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.enter ||
          event.physicalKey == PhysicalKeyboardKey.enter ||
          event.logicalKey == LogicalKeyboardKey.numpadEnter ||
          event.physicalKey == PhysicalKeyboardKey.numpadEnter) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent && config != null) {
          final terminalCmd = config.terminal;
          if (terminalCmd != null && terminalCmd.isNotEmpty) {
            final parts = terminalCmd.trim().split(RegExp(r'\s+'));
            if (parts.isNotEmpty && parts.first.isNotEmpty) {
              unawaited(
                _processStarter(
                  parts.first,
                  parts.skip(1).toList(),
                  mode: ProcessStartMode.detached,
                ),
              );
            }
          }
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.keyQ ||
          event.physicalKey == PhysicalKeyboardKey.keyQ) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          FocusManager.instance.primaryFocus?.unfocus();
          final s = surfaces.elementAtOrNull(currentPageIndex);
          if (s != null) {
            unawaited(
              CompositorRepository().platform
                  .surfaceToplevelClose(s)
                  .catchError((Object _) => false),
            );
          }
        }
        return true;
      }
    }

    if (isSuperPressed &&
        (event.logicalKey == LogicalKeyboardKey.keyF ||
            event.physicalKey == PhysicalKeyboardKey.keyF)) {
      _superUsedAsModifier = true;
      if (event is KeyDownEvent) {
        final s = surfaces.elementAtOrNull(currentPageIndex);
        if (s != null) {
          final newFullscreen = !(s.fullscreen ?? false);
          CompositorRepository().surfaceFullscreen(s.handle, newFullscreen);
          unawaited(
            CompositorRepository().platform.surfaceToplevelSetFullscreen(
              s,
              fullscreen: newFullscreen,
            ),
          );
        }
      }
      return true;
    }

    if (isSuperKey) {
      if (event is KeyUpEvent) {
        final hasOtherModifiers =
            _controlDown ||
            _altDown ||
            HardwareKeyboard.instance.isControlPressed ||
            HardwareKeyboard.instance.isAltPressed ||
            HardwareKeyboard.instance.isShiftPressed;

        if (!_superUsedAsModifier && !hasOtherModifiers && config != null) {
          final launcherCmd = config.launcher;
          if (launcherCmd != null && launcherCmd.isNotEmpty) {
            final parts = launcherCmd.trim().split(RegExp(r'\s+'));
            if (parts.isNotEmpty && parts.first.isNotEmpty) {
              unawaited(
                _processStarter(
                  parts.first,
                  parts.skip(1).toList(),
                  mode: ProcessStartMode.detached,
                ),
              );
            }
          }
        }
        _superUsedAsModifier = false;
      }
      return true;
    }

    final isControlPressed =
        _controlDown || HardwareKeyboard.instance.isControlPressed;
    final isAltPressed =
        _altDown ||
        HardwareKeyboard.instance.isPhysicalKeyPressed(
          PhysicalKeyboardKey.altLeft,
        ) ||
        HardwareKeyboard.instance.isLogicalKeyPressed(
          LogicalKeyboardKey.altLeft,
        );

    if (isControlPressed &&
        isAltPressed &&
        (event.logicalKey == LogicalKeyboardKey.delete ||
            event.physicalKey == PhysicalKeyboardKey.delete)) {
      if (event is KeyDownEvent && config != null) {
        final logoutCmd = config.systemActions?.logout?.command;
        if (logoutCmd != null && logoutCmd.isNotEmpty) {
          final parts = logoutCmd.trim().split(RegExp(r'\s+'));
          if (parts.isNotEmpty && parts.first.isNotEmpty) {
            unawaited(
              _processStarter(
                parts.first,
                parts.skip(1).toList(),
                mode: ProcessStartMode.detached,
              ),
            );
          }
        }
      }
      return true;
    }

    if (isControlPressed &&
        (event.logicalKey == LogicalKeyboardKey.arrowLeft ||
            event.physicalKey == PhysicalKeyboardKey.arrowLeft)) {
      if (event is KeyDownEvent && _onUpdateCurrentPageIndex != null) {
        unawaited(_onUpdateCurrentPageIndex!(currentPageIndex - 1));
      }
      return true;
    }

    if (isControlPressed &&
        (event.logicalKey == LogicalKeyboardKey.arrowRight ||
            event.physicalKey == PhysicalKeyboardKey.arrowRight)) {
      if (event is KeyDownEvent &&
          _onUpdateCurrentPageIndex != null &&
          surfaces.length > currentPageIndex + 1) {
        unawaited(_onUpdateCurrentPageIndex!(currentPageIndex + 1));
      }
      return true;
    }

    if (!isControlPressed &&
        !isSuperPressed &&
        (event.physicalKey == PhysicalKeyboardKey.altLeft ||
            event.logicalKey == LogicalKeyboardKey.altLeft)) {
      if (event is KeyDownEvent && _onToggleOverview != null) {
        unawaited(_onToggleOverview!());
      }
      return true;
    }

    if (isOverview || surfaces.isEmpty) {
      if (event.physicalKey == PhysicalKeyboardKey.arrowLeft ||
          event.logicalKey == LogicalKeyboardKey.arrowLeft) {
        if (event is KeyDownEvent && _onUpdateCurrentPageIndex != null) {
          unawaited(_onUpdateCurrentPageIndex!(currentPageIndex - 1));
        }
        return true;
      } else if (event.physicalKey == PhysicalKeyboardKey.arrowRight ||
          event.logicalKey == LogicalKeyboardKey.arrowRight) {
        if (event is KeyDownEvent &&
            _onUpdateCurrentPageIndex != null &&
            surfaces.length > currentPageIndex + 1) {
          unawaited(_onUpdateCurrentPageIndex!(currentPageIndex + 1));
        }
        return true;
      } else if (event.physicalKey == PhysicalKeyboardKey.enter ||
          event.logicalKey == LogicalKeyboardKey.enter ||
          event.physicalKey == PhysicalKeyboardKey.numpadEnter ||
          event.logicalKey == LogicalKeyboardKey.numpadEnter ||
          event.physicalKey == PhysicalKeyboardKey.space ||
          event.logicalKey == LogicalKeyboardKey.space ||
          event.physicalKey == PhysicalKeyboardKey.escape ||
          event.logicalKey == LogicalKeyboardKey.escape ||
          event.physicalKey == PhysicalKeyboardKey.arrowUp ||
          event.logicalKey == LogicalKeyboardKey.arrowUp ||
          event.physicalKey == PhysicalKeyboardKey.arrowDown ||
          event.logicalKey == LogicalKeyboardKey.arrowDown) {
        if (event is KeyDownEvent && _onToggleOverview != null) {
          unawaited(_onToggleOverview!());
        }
        return true;
      }
      return true;
    }

    final currentSurface = surfaces.elementAtOrNull(currentPageIndex);
    if (currentSurface != null) {
      final keycode = CompositorRepository().keyToXkb(
        event.physicalKey.usbHidUsage,
      );
      if (keycode != null) {
        final status = (event is KeyDownEvent || event is KeyRepeatEvent)
            ? KeyStatus.pressed
            : KeyStatus.released;
        unawaited(
          CompositorRepository().platform
              .surfaceSendKey(currentSurface, keycode, status, event.timeStamp)
              .catchError((Object _) {}),
        );
      }
      return true;
    }

    return false;
  }

  /// Disposes all managed controllers, listeners, and timers.
  void dispose() {
    if (_keyboardHandlerAttached) {
      HardwareKeyboard.instance.removeHandler(_onGlobalKeyEvent);
      _keyboardHandlerAttached = false;
    }
    _dockHideTimer?.cancel();
    _mouseIdleTimer?.cancel();
    overviewController.removeListener(_onAnimationTick);
    overviewController.dispose();
    dockRevealController.dispose();
    zoomController.dispose();
    pageController.dispose();
    surfaceHandles.clear();
    targetPageIndex = 0;
    focusNode.dispose();
    dockRevealedNotifier.dispose();
  }
}
