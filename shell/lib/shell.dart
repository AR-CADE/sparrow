import 'dart:async' show StreamSubscription, Timer, unawaited;
import 'dart:math' show max;

import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/gesture_swipe_event.dart'
    show GestureSwipeEndEvent, GestureSwipeUpdateEvent;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/models/surface_request_activate_event.dart'
    show SurfaceRequestActivateEvent;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:flutter/gestures.dart' show PointerDeviceKind;
import 'package:flutter/services.dart' show KeyDownEvent, PointerHoverEvent;
import 'package:flutter/widgets.dart';
import 'package:shell/background.dart' show Background;
import 'package:shell/compositor_controller.dart' show CompositorController;
import 'package:shell/core.dart'
    show CustomPageController, maxFraction, minFraction, surfaces;
import 'package:shell/shell_page_view.dart' show ShellPageView;

class Shell extends StatefulWidget {
  const Shell({super.key});

  @override
  State<Shell> createState() => _ShellState();
}

class _ShellState extends State<Shell> with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final CustomPageController _pageController;
  late final Animation<double> _fractionAnim;
  late final Animation<double> _gapAnim;
  bool get isOverview => _pageController.viewportFraction == minFraction;
  late final Animation<double> _radiusAnim;
  final CompositorRepository _compositorRepository = CompositorRepository();
  DisplayOutput? _output;

  bool _isSwipingOverview = false;
  double _swipeStartVal = 0;
  // double _totalSwipeDy = 0;
  // double _totalSwipeDx = 0;
  int get _currentPageIndex => _pageController.hasClients
      ? _pageController.page?.round() ?? _pageController.initialPage
      : 0;

  Timer? _mouseIdleTimer;
  bool _hideCursor = false;

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
    _controller = AnimationController(
      duration: const Duration(milliseconds: 145),
      vsync: this,
    );

    _fractionAnim =
        Tween<double>(
          begin: maxFraction,
          end: minFraction,
        ).animate(
          CurvedAnimation(parent: _controller, curve: Curves.ease),
        );

    _gapAnim =
        Tween<double>(
          begin: 0,
          end: 32,
        ).animate(
          CurvedAnimation(parent: _controller, curve: Curves.ease),
        );

    _radiusAnim =
        Tween<double>(
          begin: 0,
          end: 24,
        ).animate(
          CurvedAnimation(parent: _controller, curve: Curves.ease),
        );

    _pageController = CustomPageController(
      viewportFraction: _fractionAnim.value,
    );

    _controller.addListener(animationListener);

    _compositoListener = _compositorRepository.events.listen((event) async {
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

          /* debugPrint(
            'OUTPUT ADDED: ${_output!.id} ${_output!.width} ${_output!.height}',
          ); */
          return;
        }

        if (event.type == .outputChanged) {
          final out = event.event as DisplayOutput;
          if (_output != null && out.id != _output!.id) {
            return;
          }
          /* 
          debugPrint(
            'OUTPUT CHANGED: ${out.id} '
            '${out.width} ${out.height}',
          );
 */
          for (final surface in surfaces) {
            if (out.width != surface.width || out.height != surface.height) {
              /* debugPrint(
                'CHANGE SURFACE SIZE FROM: ${surface.width} '
                '${surface.height} '
                'to ${out.width} ${out.height}',
              ); */

              /*  unawaited(
                CompositorRepository().platform.surfaceRequestResize(
                  ds.surface.handle,
                  out.width,
                  out.height,
                  Random().nextInt(5000),
                ),
              );*/

              unawaited(
                CompositorRepository().platform.surfaceToplevelSetMaximized(
                  surface,
                ),
              );
            }
          }

          setState(() {
            _output = out;
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

          final index = surfaces.indexWhere(
            (s) => s.handle == surface.handle,
          );
          final isAbsent = index == -1;

          if (isAbsent && event.type == .surfaceMap) {
            var target = 0;

            if (surfaces.isEmpty || currentPageIndex + 1 > surfaces.length) {
              surfaces.add(
                surface,
              );
              target = surfaces.length - 1;
            } else {
              surfaces.insert(
                currentPageIndex + 1,
                surface,
              );
              target = currentPageIndex + 1;
            }

            await CompositorRepository().platform
                .surfaceToplevelSetMaximized(
                  surface,
                )
                .then(
                  (_) async {
                    await CompositorRepository().platform.surfaceFocus(
                      surface,
                    );
                  },
                );

            await _updateCurrentPageIndex(
              target,
              setstate: true,
            );

            if (isOverview) {
              setState(() {
                _controller.reverse();
              });
            }

            /*  debugPrint(
              'SURFACE ADDED: ${surface.handle} '
              '${surface.width} ${surface.height}',
            ); */
          } else {
            /*  debugPrint(
              'SURFACE UPDATE: ${surface.handle} '
              '${surface.width} ${surface.height}',
            ); */
            if (index != -1) {
              surfaces[index] = surface;
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

          final index = surfaces.indexWhere(
            (s) => s.handle == surface.handle,
          );

          final isPresent = index != -1;

          if (!isPresent) {
            return;
          }

          surfaces.removeAt(index);

          /*if (surfaces.isEmpty) {
            if (isOverview) {
              await CompositorRepository().platform.forceRenderAllViews(false);
              await _controller.animateTo(0, duration: .zero);
            }
          } else {
            if (currentPageIndex >= surfaces.length) {
              _pageController.jumpToPage(surfaces.length - 1);
              if (!isOverview) {
                final newFocused = surfaces[surfaces.length - 1];
                await CompositorRepository().platform.surfaceFocus(newFocused);
              }
            } else if (currentPageIndex == index) {
              if (currentPageIndex > 0) {
                setState(() {});
                await _pageController.previousPage(
                  duration: isOverview
                      ? const Duration(milliseconds: 216)
                      : const Duration(milliseconds: 230),
                  curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
                );
              } else if (!isOverview) {
                final surface = surfaces.elementAtOrNull(0);
                if (surface != null) {
                  await CompositorRepository().platform.surfaceFocus(surface);
                }
              }
            }
          }*/

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
            await CompositorRepository().platform.forceRenderAllViews(false);
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
          unawaited(CompositorRepository().platform.forceRenderAllViews(true));
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

    await CompositorRepository().platform.forceRenderAllViews(true);

    if (s != null) {
      await CompositorRepository().platform.clearFocus(s);
    }

    await _controller.forward();
    _resetMouseIdleTimer(showImmediately: false);
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
      duration: const Duration(
        milliseconds: 62,
      ),
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

    await CompositorRepository().platform.forceRenderAllViews(false);

    if (s != null) {
      await CompositorRepository().platform.surfaceFocus(s);
    }
    CompositorRepository().platform.interactive = true;

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
          .surfaceToplevelSetMaximized(
            s,
          )
          .then(
            (_) => CompositorRepository().platform.surfaceFocus(s),
          );
    }
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
    _pageController.dispose();
    await _compositoListener.cancel();
    await _compositorRepository.close();
    super.dispose();
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
              autofocus: !_pageController.hasClients,
              canRequestFocus: !_pageController.hasClients,
              onKeyEvent: (node, event) {
                if (event is KeyDownEvent) {
                  /* final keycode = CompositorRepository().keyToXkb(
                    event.physicalKey.usbHidUsage,
                  ); */
                  print('onKeyEvent');

                  //print('keycode shell pressed $keycode');
                  // print('physicalKey shell pressed ${event.physicalKey}');
                  // print('event shell pressed ${event}');

                  if (event.physicalKey == .arrowLeft) {
                    final currentPageIndex = _pageController.hasClients
                        ? _pageController.page?.round() ??
                              _pageController.initialPage
                        : 0;
                    if (currentPageIndex == 0) {}
                    unawaited(_updateCurrentPageIndex(currentPageIndex - 1));
                  } else if (event.physicalKey == .arrowRight) {
                    final currentPageIndex = _pageController.hasClients
                        ? _pageController.page?.round() ??
                              _pageController.initialPage
                        : 0;
                    if (surfaces.length <= currentPageIndex + 1) {
                      return KeyEventResult.handled;
                    }
                    unawaited(_updateCurrentPageIndex(currentPageIndex + 1));
                  } else if (event.physicalKey == .enter ||
                      event.physicalKey == .numpadEnter ||
                      event.physicalKey == .space ||
                      event.physicalKey == .altLeft) {
                    unawaited(_toggleOverview());
                  } else if (event.physicalKey == .f12) {
                    unawaited(() async {
                      final current = await CompositorRepository()
                          .platform
                          .debugGetDamageVisualization();
                      await CompositorRepository()
                          .platform
                          .debugSetDamageVisualization(!current);
                    }());
                  }
                }

                return KeyEventResult.handled;
              },
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
