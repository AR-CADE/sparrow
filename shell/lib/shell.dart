import 'dart:async' show StreamSubscription, Timer, unawaited;
import 'dart:io' show Process, ProcessStartMode;
import 'dart:math' show max;

import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/core/constants.dart' show KeyStatus;
import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/gesture_swipe_event.dart'
    show GestureSwipeEndEvent, GestureSwipeUpdateEvent;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/models/surface_request_activate_event.dart'
    show SurfaceRequestActivateEvent;
import 'package:compositor_dart/data/models/zoom_event.dart'
    show ZoomKeyEvent, ZoomScrollEvent;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:flutter/gestures.dart' show PointerDeviceKind;
import 'package:flutter/services.dart'
    show
        HardwareKeyboard,
        KeyDownEvent,
        KeyRepeatEvent,
        KeyUpEvent,
        LogicalKeyboardKey,
        PhysicalKeyboardKey,
        PointerHoverEvent;
import 'package:flutter/widgets.dart';
import 'package:shell/background.dart' show Background;
import 'package:shell/compositor_controller.dart' show CompositorController;
import 'package:shell/configuration_repository.dart'
    show ConfigurationRepository;
import 'package:shell/core.dart'
    show CustomPageController, maxFraction, minFraction, surfaces;
import 'package:shell/screen_zoom_overlay.dart';
import 'package:shell/shell_page_view.dart' show ShellPageView;
import 'package:shell/zoom_controller.dart' show ZoomController;

class Shell extends StatefulWidget {
  const new({super.key});

  @override
  State<Shell> createState() => _ShellState();
}

class _ShellState extends State<Shell> with TickerProviderStateMixin {
  late final AnimationController _controller;
  late final CustomPageController _pageController;
  late final Animation<double> _fractionAnim;
  late final Animation<double> _gapAnim;
  bool get isOverview => _pageController.viewportFraction == minFraction;
  late final Animation<double> _radiusAnim;
  final CompositorRepository _compositorRepository = CompositorRepository();
  DisplayOutput? _output;

  // Smooth screen zoom controller
  late final ZoomController _zoomController;
  final FocusNode _shellFocusNode = FocusNode();

  bool _isSwipingOverview = false;
  double _swipeStartVal = 0;
  // double _totalSwipeDy = 0;
  // double _totalSwipeDx = 0;
  int get _currentPageIndex {
    if (!_pageController.hasClients) return 0;
    final p = _pageController.page?.round() ?? _pageController.initialPage;
    return surfaces.isNotEmpty ? p.clamp(0, surfaces.length - 1) : 0;
  }

  Timer? _mouseIdleTimer;
  bool _hideCursor = false;
  bool _superUsedAsModifier = false;

  void _resetMouseIdleTimer({bool showImmediately = true}) {
    if (showImmediately && _hideCursor) {
      setState(() {
        _hideCursor = false;
      });
    }
    _mouseIdleTimer?.cancel();
    _mouseIdleTimer = Timer(const Duration(seconds: 3), () {
      if (mounted) {
        setState(() {
          _hideCursor = true;
        });
      }
    });
  }

  void _onPointerHover(PointerHoverEvent event) {
    if (event.kind == PointerDeviceKind.touch) {
      return;
    }
    _zoomController.updateCursor(event.localPosition);
    _resetMouseIdleTimer();
  }

  void animationListener() {
    setState(() {
      final page = _pageController.hasClients
          ? _pageController.page ?? _pageController.initialPage.toDouble()
          : 0.0;
      final vf = _fractionAnim.value;
      CompositorRepository().platform.interactive = vf == maxFraction;
      _pageController
        ..setViewportFraction(vf)
        ..jumpToPage(page.round());
    });
  }

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      duration: const Duration(milliseconds: 145),
      vsync: this,
    );

    _zoomController = ZoomController(vsync: this);

    _fractionAnim = Tween<double>(
      begin: maxFraction,
      end: minFraction,
    ).animate(CurvedAnimation(parent: _controller, curve: Curves.ease));

    _gapAnim = Tween<double>(
      begin: 0,
      end: 32,
    ).animate(CurvedAnimation(parent: _controller, curve: Curves.ease));

    _radiusAnim = Tween<double>(
      begin: 0,
      end: 24,
    ).animate(CurvedAnimation(parent: _controller, curve: Curves.ease));

    _pageController = CustomPageController(
      viewportFraction: _fractionAnim.value,
    );

    _controller.addListener(animationListener);
    HardwareKeyboard.instance.addHandler(_onGlobalKeyEvent);

    _compositoListener = _compositorRepository.events.listen((event) async {
      if (event.type == .zoomScroll) {
        _superUsedAsModifier = true;
        final zoomEvent = event.event as ZoomScrollEvent;
        _zoomController.handleScroll(
          zoomEvent.delta,
          Offset(zoomEvent.x, zoomEvent.y),
        );
        return;
      }

      if (event.type == .zoomKey) {
        _superUsedAsModifier = true;
        final zoomEvent = event.event as ZoomKeyEvent;
        _zoomController.handleKey(
          zoomEvent.action,
          Offset(zoomEvent.x, zoomEvent.y),
        );
        return;
      }
      if (event.type == .surfaceMap ||
          event.type == .surfaceUnMap ||
          event.type == .surfaceGeometryChange ||
          event.type == .surfaceDecorationChange ||
          event.type == .surfaceRequestActivate ||
          event.type == .outputChanged ||
          event.type == .outputRemoved ||
          event.type == .outputAdded) {
        final currentPageIndex = _pageController.hasClients
            ? _pageController.page?.round() ?? _pageController.initialPage
            : 0;
        if (event.type == .outputAdded) {
          final out = event.event as DisplayOutput;

          setState(() {
            _output ??= out;
          });

          return;
        }

        if (event.type == .outputChanged) {
          final out = event.event as DisplayOutput;
          if (_output != null && out.id != _output!.id) {
            return;
          }

          for (final surface in surfaces) {
            if (out.width != surface.width || out.height != surface.height) {
              unawaited(
                CompositorRepository().platform.surfaceToplevelSetMaximized(
                  surface,
                ),
              );
            }
          }

          final targetIndex = _currentPageIndex;
          final targetSurface = surfaces.elementAtOrNull(targetIndex);

          setState(() {
            _output = out;
          });

          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (!mounted) return;
            if (_pageController.hasClients) {
              _pageController.jumpToPage(targetIndex);
            }
            if (!isOverview) {
              CompositorRepository().platform.interactive = true;
              if (targetSurface != null) {
                unawaited(
                  CompositorRepository().platform.surfaceFocus(targetSurface),
                );
              }
              _shellFocusNode.unfocus();
              FocusManager.instance.primaryFocus?.unfocus();
            }
          });
          return;
        }

        if (event.type == .outputRemoved) {
          final out = event.event as DisplayOutput;
          if (out.id != _output?.id) {
            return;
          }

          setState(() {
            _output = null;
          });
          return;
        }

        if (event.type == .surfaceMap ||
            event.type == .surfaceGeometryChange ||
            event.type == .surfaceDecorationChange) {
          final surface = event.event as Surface;

          final index = surfaces.indexWhere((s) => s.handle == surface.handle);
          final isAbsent = index == -1;

          if (isAbsent && event.type == .surfaceMap) {
            var target = 0;

            if (surfaces.isEmpty || currentPageIndex + 1 > surfaces.length) {
              surfaces.add(surface);
              target = surfaces.length - 1;
            } else {
              surfaces.insert(currentPageIndex + 1, surface);
              target = currentPageIndex + 1;
            }

            await CompositorRepository().platform
                .surfaceToplevelSetMaximized(surface)
                .then((_) async {
                  await CompositorRepository().platform.surfaceFocus(surface);
                });
            FocusManager.instance.primaryFocus?.unfocus();

            await _updateCurrentPageIndex(target, setstate: true);

            if (isOverview) {
              setState(() {
                _controller.reverse();
              });
            }
          } else {
            if (index != -1) {
              surfaces[index] = surface;
            }
            if (!isOverview) {
              FocusManager.instance.primaryFocus?.unfocus();
              CompositorRepository().platform.interactive = true;
            }
            setState(() {});
          }
          return;
        }

        if (event.type == .surfaceUnMap) {
          final surface = event.event as Surface?;
          if (surface == null) {
            return;
          }

          final index = surfaces.indexWhere((s) => s.handle == surface.handle);

          final isPresent = index != -1;

          if (!isPresent) {
            return;
          }

          surfaces.removeAt(index);

          if (currentPageIndex == index && currentPageIndex > 0) {
            setState(() {});
            await _pageController.previousPage(
              duration: isOverview
                  ? const Duration(milliseconds: 216)
                  : const Duration(milliseconds: 230),
              curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
            );
          } else if (currentPageIndex == index &&
              currentPageIndex == 0 &&
              !isOverview) {
            final surface = surfaces.elementAtOrNull(0);
            if (surface != null) {
              await CompositorRepository().platform.surfaceFocus(surface);
            }
          }

          if (surfaces.isEmpty && isOverview) {
            await _controller.animateTo(0, duration: .zero);
            await CompositorRepository().platform.forceRenderAllViews(
              force: false,
            );
          }

          setState(() {});

          return;
        }

        if (event.type == .surfaceRequestActivate) {
          final req = event.event as SurfaceRequestActivateEvent?;
          if (req == null) return;

          final targetIndex = surfaces.indexWhere(
            (s) => s.handle == req.handle,
          );

          if (targetIndex != -1) {
            final targetSurface = surfaces[targetIndex];

            if (isOverview) {
              await _closeOverview(targetSurface, index: targetIndex);
            } else {
              await _updateCurrentPageIndex(targetIndex, setstate: true);
              await CompositorRepository().platform.surfaceFocus(targetSurface);
            }
          }
          return;
        }
      }

      if (event.type == .gestureSwipeBegin) {
        if (surfaces.isEmpty) return;
        _isSwipingOverview = true;
        _swipeStartVal = _controller.value;
        //_totalSwipeDy = 0.0;
        //_totalSwipeDx = 0.0;
        if (_swipeStartVal == 0.0) {
          CompositorRepository().platform.interactive = false;
          unawaited(
            CompositorRepository().platform.forceRenderAllViews(force: true),
          );
          final s = surfaces.elementAtOrNull(_currentPageIndex);
          if (s != null) {
            unawaited(CompositorRepository().platform.clearFocus(s));
          }
        }
        return;
      }

      if (event.type == .gestureSwipeUpdate) {
        if (!_isSwipingOverview || surfaces.isEmpty) return;
        final swipe = event.event as GestureSwipeUpdateEvent;
        //_totalSwipeDy += swipe.dy;
        //_totalSwipeDx += swipe.dx;

        // Swiping up (dy < 0) opens overview,
        // swiping down (dy > 0) closes overview
        // Sensitivity: 280px of finger travel for full 0.0 -> 1.0 transition
        final delta = -swipe.dy / 280.0;
        _controller.value = (_controller.value + delta).clamp(0.0, 1.0);

        return;
      }

      if (event.type == .gestureSwipeEnd) {
        if (!_isSwipingOverview) return;
        _isSwipingOverview = false;
        final swipe = event.event as GestureSwipeEndEvent;
        if (surfaces.isEmpty) return;

        final s = surfaces.elementAtOrNull(_currentPageIndex);

        if (swipe.cancelled) {
          if (_swipeStartVal >= 0.1) {
            _controller.forward();
          } else {
            unawaited(_closeOverview(s, center: false));
          }
          return;
        }

        if (_swipeStartVal == 0) {
          if (_controller.value >= 0.14) {
            await _controller.forward();
            setState(() {});
          } else {
            await _closeOverview(s, center: false);
          }
        } else {
          if (_controller.value > 0.75) {
            await _controller.forward();
            setState(() {});
          } else {
            await _closeOverview(s, center: false);
          }
        }

        return;
      }
    });
    super.initState();
  }

  late final StreamSubscription<CompositorEvent> _compositoListener;

  Future<void> _toggleOverview({bool center = true, int? index}) async {
    if (surfaces.isEmpty) {
      return;
    }
    final i =
        index ??
        (_pageController.hasClients
            ? _pageController.page?.round() ?? _pageController.initialPage
            : 0);
    final s = surfaces.elementAtOrNull(i);

    if (isOverview) {
      await _closeOverview(s, center: center, index: i);
    } else {
      await _openOverview(s);
    }
  }

  Future<void> _openOverview(Surface? s) async {
    if (surfaces.isEmpty) {
      return;
    }
    CompositorRepository().platform.interactive = false;

    await CompositorRepository().platform.forceRenderAllViews(force: true);

    if (s != null) {
      await CompositorRepository().platform.clearFocus(s);
    }

    await _controller.forward();
    _resetMouseIdleTimer(showImmediately: false);
    _shellFocusNode.requestFocus();
    setState(() {});
  }

  Future<void> _centerPageview(int? index) async {
    if (surfaces.isEmpty) {
      return;
    }
    final i =
        index ??
        (_pageController.hasClients
            ? _pageController.page?.round() ?? _pageController.initialPage
            : 0);

    await _pageController.animateToPage(
      i,
      duration: const Duration(milliseconds: 62),
      curve: Curves.ease,
    );
  }

  Future<void> _closeOverview(
    Surface? s, {
    bool center = true,
    int? index,
  }) async {
    if (surfaces.isEmpty) {
      return;
    }
    if (center) {
      await _centerPageview(index);
    }
    await _controller.reverse();

    await CompositorRepository().platform.forceRenderAllViews(force: false);

    if (s != null) {
      await CompositorRepository().platform.surfaceFocus(s);
    }
    CompositorRepository().platform.interactive = true;
    _shellFocusNode.unfocus();
    FocusManager.instance.primaryFocus?.unfocus();

    _resetMouseIdleTimer(showImmediately: false);
    setState(() {});
  }

  Future<void> _handlePageViewChanged(int i) async {
    _resetMouseIdleTimer(showImmediately: false);
    setState(() {});

    if (isOverview) {
      return;
    }

    final currentPageIndex = _pageController.hasClients
        ? _pageController.page?.round() ?? _pageController.initialPage
        : 0;

    final s = surfaces.elementAtOrNull(currentPageIndex);

    if (s != null) {
      await CompositorRepository().platform
          .surfaceToplevelSetMaximized(s)
          .then((_) => CompositorRepository().platform.surfaceFocus(s));
    }
    FocusManager.instance.primaryFocus?.unfocus();
  }

  Future<void> _updateCurrentPageIndex(
    int index, {
    bool setstate = false,
  }) async {
    if (surfaces.isNotEmpty) {
      final fixedIndex = max(index, 0);

      if (setstate) {
        setState(() {});
      }
      if (_pageController.hasClients) {
        await _pageController.animateToPage(
          fixedIndex,
          duration: isOverview
              ? const Duration(milliseconds: 216)
              : const Duration(milliseconds: 230),
          curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
        );
      }
      _resetMouseIdleTimer(showImmediately: false);
      setState(() {});
    }
  }

  @override
  Future<void> dispose() async {
    _controller
      ..removeListener(animationListener)
      ..dispose();
    _zoomController.dispose();
    _pageController.dispose();
    _shellFocusNode.dispose();
    HardwareKeyboard.instance.removeHandler(_onGlobalKeyEvent);
    await _compositoListener.cancel();
    await _compositorRepository.close();
    super.dispose();
  }

  bool _onGlobalKeyEvent(KeyEvent event) {
    final isSuperKey =
        event.logicalKey == LogicalKeyboardKey.superKey ||
        event.logicalKey == LogicalKeyboardKey.metaLeft ||
        event.logicalKey == LogicalKeyboardKey.metaRight ||
        event.physicalKey == PhysicalKeyboardKey.superKey ||
        event.physicalKey == PhysicalKeyboardKey.metaLeft ||
        event.physicalKey == PhysicalKeyboardKey.metaRight;

    if (isSuperKey && event is KeyDownEvent) {
      _superUsedAsModifier = false;
    }

    final isSuperPressed =
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

    if (isSuperPressed) {
      if (event.logicalKey == LogicalKeyboardKey.equal ||
          event.logicalKey == LogicalKeyboardKey.add ||
          event.physicalKey == PhysicalKeyboardKey.equal ||
          event.physicalKey == PhysicalKeyboardKey.numpadAdd) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          _zoomController.handleKey(1, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.minus ||
          event.physicalKey == PhysicalKeyboardKey.minus ||
          event.physicalKey == PhysicalKeyboardKey.numpadSubtract) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          _zoomController.handleKey(-1, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.digit0 ||
          event.physicalKey == PhysicalKeyboardKey.digit0 ||
          event.physicalKey == PhysicalKeyboardKey.numpad0) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          _zoomController.handleKey(0, Offset.zero);
        }
        return true;
      } else if (event.logicalKey == LogicalKeyboardKey.enter ||
          event.physicalKey == PhysicalKeyboardKey.enter ||
          event.logicalKey == LogicalKeyboardKey.numpadEnter ||
          event.physicalKey == PhysicalKeyboardKey.numpadEnter) {
        _superUsedAsModifier = true;
        if (event is KeyDownEvent) {
          final config = ConfigurationRepository.instance;
          final terminalCmd = config.terminal;
          if (terminalCmd != null && terminalCmd.isNotEmpty) {
            final parts = terminalCmd.trim().split(RegExp(r'\s+'));
            if (parts.isNotEmpty && parts.first.isNotEmpty) {
              unawaited(
                Process.start(
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
          final currentPageIndex = _pageController.hasClients
              ? _pageController.page?.round() ?? _pageController.initialPage
              : 0;

          final s = surfaces.elementAtOrNull(currentPageIndex);

          if (s != null) {
            unawaited(CompositorRepository().platform.surfaceToplevelClose(s));
          }
        }
        return true;
      }
    }

    if (isSuperKey) {
      if (event is KeyUpEvent) {
        if (!_superUsedAsModifier) {
          final config = ConfigurationRepository.instance;
          final launcherCmd = config.launcher;
          if (launcherCmd != null && launcherCmd.isNotEmpty) {
            final parts = launcherCmd.trim().split(RegExp(r'\s+'));
            if (parts.isNotEmpty && parts.first.isNotEmpty) {
              unawaited(
                Process.start(
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

    if (HardwareKeyboard.instance.isControlPressed &&
        (HardwareKeyboard.instance.isPhysicalKeyPressed(
              PhysicalKeyboardKey.altLeft,
            ) ||
            HardwareKeyboard.instance.isLogicalKeyPressed(
              LogicalKeyboardKey.altLeft,
            )) &&
        (event.logicalKey == LogicalKeyboardKey.delete ||
            event.physicalKey == PhysicalKeyboardKey.delete)) {
      if (event is KeyDownEvent) {
        final config = ConfigurationRepository.instance;
        final logoutCmd = config.logout;
        if (logoutCmd != null && logoutCmd.isNotEmpty) {
          final parts = logoutCmd.trim().split(RegExp(r'\s+'));
          if (parts.isNotEmpty && parts.first.isNotEmpty) {
            unawaited(
              Process.start(
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

    // already done on the wlroots side
    // if (event.physicalKey == PhysicalKeyboardKey.f12 ||
    //     event.logicalKey == LogicalKeyboardKey.f12) {
    //   if (event is KeyDownEvent) {
    //     //stderr.writeln('Damage Toggle Key Pressed');
    //     unawaited(() async {
    //       final current = await CompositorRepository().platform
    //           .debugGetDamageVisualization();
    //       await CompositorRepository().platform.debugSetDamageVisualization(
    //         enabled: !current,
    //       );
    //     }());
    //   }
    //   return true;
    // }

    if (HardwareKeyboard.instance.isControlPressed &&
        (event.logicalKey == LogicalKeyboardKey.arrowLeft ||
            event.physicalKey == PhysicalKeyboardKey.arrowLeft)) {
      if (event is KeyDownEvent) {
        unawaited(_updateCurrentPageIndex(_currentPageIndex - 1));
      }
      return true;
    }

    if (HardwareKeyboard.instance.isControlPressed &&
        (event.logicalKey == LogicalKeyboardKey.arrowRight ||
            event.physicalKey == PhysicalKeyboardKey.arrowRight)) {
      if (event is KeyDownEvent) {
        if (surfaces.length > _currentPageIndex + 1) {
          unawaited(_updateCurrentPageIndex(_currentPageIndex + 1));
        }
      }
      return true;
    }

    if (!HardwareKeyboard.instance.isControlPressed &&
        !HardwareKeyboard.instance.isMetaPressed &&
        (event.physicalKey == PhysicalKeyboardKey.altLeft ||
            event.logicalKey == LogicalKeyboardKey.altLeft)) {
      if (event is KeyDownEvent) {
        // stderr.writeln('Toggle Overview Key Pressed');
        unawaited(_toggleOverview());
      }
      return true;
    }

    if (isOverview || surfaces.isEmpty) {
      if (event.physicalKey == PhysicalKeyboardKey.arrowLeft ||
          event.logicalKey == LogicalKeyboardKey.arrowLeft) {
        if (event is KeyDownEvent) {
          // stderr.writeln('Arrow Left Key Pressed');
          unawaited(_updateCurrentPageIndex(_currentPageIndex - 1));
        }
        return true;
      } else if (event.physicalKey == PhysicalKeyboardKey.arrowRight ||
          event.logicalKey == LogicalKeyboardKey.arrowRight) {
        if (event is KeyDownEvent) {
          // stderr.writeln('Arrow Right Key Pressed');
          if (surfaces.length > _currentPageIndex + 1) {
            unawaited(_updateCurrentPageIndex(_currentPageIndex + 1));
          }
        }
        return true;
      } else if (event.physicalKey == PhysicalKeyboardKey.enter ||
          event.logicalKey == LogicalKeyboardKey.enter ||
          event.physicalKey == PhysicalKeyboardKey.numpadEnter ||
          event.logicalKey == LogicalKeyboardKey.numpadEnter ||
          event.physicalKey == PhysicalKeyboardKey.space ||
          event.logicalKey == LogicalKeyboardKey.space) {
        if (event is KeyDownEvent) {
          // stderr.writeln('Toggle Overview Key Pressed');
          unawaited(_toggleOverview());
        }
        return true;
      }
      return true;
    }

    final pageIndex = _currentPageIndex.clamp(0, surfaces.length - 1);
    final currentSurface = surfaces.elementAtOrNull(pageIndex);
    if (currentSurface != null) {
      final keycode = CompositorRepository().keyToXkb(
        event.physicalKey.usbHidUsage,
      );
      if (keycode != null) {
        final status = (event is KeyDownEvent || event is KeyRepeatEvent)
            ? KeyStatus.pressed
            : KeyStatus.released;
        unawaited(
          CompositorRepository().platform.surfaceSendKey(
            currentSurface,
            keycode,
            status,
            event.timeStamp,
          ),
        );
      }
      return true;
    }

    return false;
  }

  @override
  Widget build(BuildContext context) {
    final gap = _gapAnim.value;
    final radius = _radiusAnim.value;
    final fraction = _fractionAnim.value;
    final output = _output;

    final isBackgroundVisible =
        surfaces.isEmpty ||
        (_fractionAnim.value < maxFraction) ||
        _controller.isAnimating;

    return LayoutBuilder(
      builder: (context, constaints) {
        final maxw = output?.width.toDouble() ?? constaints.maxWidth;
        final maxh = output?.height.toDouble() ?? constaints.maxHeight;

        return SafeArea(
          child: SizedBox(
            width: maxw,
            height: maxh,
            child: Focus(
              focusNode: _shellFocusNode,
              autofocus: !_pageController.hasClients,
              canRequestFocus: isOverview || surfaces.isEmpty,
              child: MouseRegion(
                cursor: _hideCursor
                    ? SystemMouseCursors.none
                    : MouseCursor.defer,
                onHover: _onPointerHover,
                child: Stack(
                  fit: .expand,
                  children: <Widget>[
                    TickerMode(
                      enabled: isBackgroundVisible,
                      child: const RepaintBoundary(child: Background()),
                    ),
                    if (output != null)
                      RepaintBoundary(
                        child: ShellPageView(
                          handlePageViewChanged: _handlePageViewChanged,
                          toggleOverview: _toggleOverview,
                          closeOverview: _closeOverview,
                          openOverview: _openOverview,
                          pageController: _pageController,
                          animationController: _controller,
                          gap: gap,
                          fraction: fraction,
                          radius: radius,
                          output: output,
                        ),
                      ),
                    RepaintBoundary(
                      child: CompositorController(
                        pageController: _pageController,
                        toggleOverview: _toggleOverview,
                        onUpdateCurrentPageIndex: _updateCurrentPageIndex,
                      ),
                    ),
                    Positioned.fill(
                      child: ScreenZoomOverlay(
                        controller: _zoomController,
                        size: Size(maxw, maxh),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}
