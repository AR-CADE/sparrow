import 'dart:async' show Future, StreamSubscription, unawaited;
import 'dart:math';

import 'package:bloc/bloc.dart' show Bloc;
import 'package:collection/collection.dart';
import 'package:compositor_dart/data/models/compositor_event.dart';
import 'package:compositor_dart/data/models/display_output.dart';
import 'package:compositor_dart/data/models/surface.dart';
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:equatable/equatable.dart' show Equatable;
import 'package:flutter_bloc/flutter_bloc.dart' show Emitter;
import 'package:material_ui/material_ui.dart';

part 'compositor_event.dart';
part 'compositor_state.dart';

class CompositorBloc extends Bloc<CompositorBlocEvent, CompositorBlocState> {
  CompositorBloc(PageController pageController)
    : _pageController = pageController,
      super(const CompositorBlocState.set(ready: false)) {
    on<_SetReadyRequested>(_onSetReady);
    on<_CompositorStateChanged>(_onCompositorStateChanged);

    _compositorRepository = CompositorRepository();
    _eventSubscription = _compositorRepository.events.listen(
      (event) => add(_CompositorStateChanged(event: event)),
    );

    add(const _SetReadyRequested(ready: true));
  }

  final PageController _pageController;
  late final CompositorRepository _compositorRepository;
  late final StreamSubscription<CompositorEvent> _eventSubscription;

  CompositorRepository get compositorRepository => _compositorRepository;

  Future<void> _onSetReady(
    _SetReadyRequested event,
    Emitter<CompositorBlocState> emit,
  ) async {
    return emit(CompositorBlocState.set(ready: event.ready));
  }

  Future<void> _onCompositorStateChanged(
    _CompositorStateChanged event,
    Emitter<CompositorBlocState> emit,
  ) async {
    if (event.event.type == .outputChanged ||
        event.event.type == .outputRemoved ||
        event.event.type == .outputAdded) {
      if (event.event.type == .outputAdded) {
        final cstate = _onOutputAdded(event);
        if (cstate == null) {
          return;
        }
        return emit(cstate);
      }

      if (event.event.type == .outputChanged) {
        final cstate = _onOutputChanged(event);
        if (cstate == null) {
          return;
        }
        return emit(cstate);
      }

      if (event.event.type == .outputRemoved) {
        final cstate = _onOutputRemoved(event);
        if (cstate == null) {
          return;
        }
        return emit(cstate);
      }
    }
    if (event.event.type == .surfaceMap ||
        event.event.type == .surfaceUnMap ||
        event.event.type == .surfaceGeometryChange ||
        event.event.type == .surfaceDecorationChange) {
      if (event.event.type == .surfaceMap ||
          event.event.type == .surfaceGeometryChange ||
          event.event.type == .surfaceDecorationChange) {
        final surface = event.event as Surface;
        final output = state.output;
        if (output != null) {
          if (output.width != surface.width ||
              output.height != surface.height) {
            /* debugPrint(
                'CHANGE SURFACE SIZE FROM: ${surface.width} ${surface.height} '
                'to ${_output!.width} ${_output!.height}',
              ); */

            /* await CompositorRepository().platform.surfaceToplevelSetSize(
                surface,
                _output!.width,
                _output!.height,
              );*/

            await CompositorRepository().platform.surfaceToplevelSetMaximized(
              surface,
            );
          }
        }

        final index = state.surfaces.indexWhere(
          (s) => s.handle == surface.handle,
        );
        final isAbsent = index == -1;

        if (isAbsent && event.event.type == .surfaceMap) {
          if (state.surfaces.isEmpty ||
              currentPageIndex + 1 > state.surfaces.length) {
            state.surfaces.add(
              surface,
            );
          } else {
            state.surfaces.insert(
              currentPageIndex + 1,
              surface,
            );
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

          emit(CompositorBlocState.set(surfaces: state.surfaces));

          return _updateCurrentPageIndex(
            state.surfaces.length < 2 ? currentPageIndex : currentPageIndex + 1,
          ).then((_) {
            if (isOverview) {
              //return _controller.reverse()
            }
          });
        } else if ((event.event.type == .surfaceGeometryChange ||
                event.event.type == .surfaceDecorationChange) &&
            index != -1) {
          /*  debugPrint(
              'SURFACE UPDATE: ${surface.handle} '
              '${surface.width} ${surface.height}',
            ); */

          state.surfaces[index] = surface;

          return emit(CompositorBlocState.set(surfaces: state.surfaces));
        }
      }

      if (event.event.type == .surfaceMap ||
          event.event.type == .surfaceGeometryChange ||
          event.event.type == .surfaceDecorationChange) {
        final surface = event.event as Surface;

        await _updateSurfaceSize(surface);

        _insertSurface(surface, event.event, currentPageIndex);

        await _updateSurfaceFocus(surface, isOverview: isOverview);

        await _updateCurrentPageIndex(
          state.surfaces.length < 2 ? currentPageIndex : currentPageIndex + 1,
        );

        return emit(CompositorBlocState.set(surfaces: state.surfaces));
      }

      if (event.event.type == .surfaceUnMap) {
        final surface = event.event as Surface?;
        if (surface == null) {
          return;
        }

        final index = state.surfaces.indexWhere(
          (s) => s.handle == surface.handle,
        );

        final isPresent = index != -1;

        if (!isPresent) {
          return;
        }

        state.surfaces.removeAt(index);

        if (currentPageIndex == index && currentPageIndex > 0) {
          await _updateCurrentPageIndex(currentPageIndex - 1);
        }

        return emit(CompositorBlocState.set(surfaces: state.surfaces));
      }
    }
  }

  int get currentPageIndex => _pageController.hasClients
      ? _pageController.page?.round() ?? _pageController.initialPage
      : 0;

  double get currentPage =>
      _pageController.hasClients ? _pageController.page ?? 0 : 0;

  void _insertSurface(
    Surface surface,
    CompositorEvent event,
    int currentPageIndex,
  ) {
    final isAbsent =
        state.surfaces.firstWhereOrNull((s) => s.handle == surface.handle) ==
        null;

    if (isAbsent && event.type == .surfaceMap) {
      if (state.surfaces.isEmpty ||
          currentPageIndex + 1 > state.surfaces.length) {
        state.surfaces.add(
          surface,
        );
      } else {
        state.surfaces.insert(
          currentPageIndex + 1,
          surface,
        );
      }
    } /*  debugPrint(
              'SURFACE ADDED: ${surface.handle} '
              '${surface.width} ${surface.height}',
            ); */ else {
      /*  debugPrint(
              'SURFACE UPDATE: ${surface.handle} '
              '${surface.width} ${surface.height}',
            ); */
      final index = state.surfaces.indexWhere(
        (s) => s.handle == surface.handle,
      );
      if (index != -1) {
        state.surfaces[index] = surface;
      }
    }
  }

  Future<void> _updateSurfaceFocus(
    Surface surface, {
    bool isOverview = false,
  }) async {
    if (isOverview) {
      await CompositorRepository().platform.clearFocus(surface);
    } else {
      await CompositorRepository().platform.surfaceFocus(surface);
    }
  }

  Future<void> _updateSurfaceSize(Surface surface) async {
    if (state.output != null) {
      if (state.output!.width != surface.width ||
          state.output!.height != surface.height) {
        /* debugPrint(
          'CHANGE SURFACE SIZE FROM: ${surface.width} ${surface.height} '
          'to ${state.output!.width} ${state.output!.height}',
        ); */

        /* await CompositorRepository().platform.surfaceToplevelSetSize(
            surface,
            state.output!.width,
            state.output!.height,
          );*/

        await CompositorRepository().platform.surfaceToplevelSetMaximized(
          surface,
        );
      }
    }
  }

  CompositorBlocState? _onOutputAdded(_CompositorStateChanged event) {
    final out = event.event as DisplayOutput;

    if (state.output != null) {
      return null;
    }

    /* debugPrint(
      'OUTPUT ADDED: ${out.id} ${out.width} ${out.height}',
    ); */
    return CompositorBlocState.set(output: out);
  }

  CompositorBlocState? _onOutputChanged(_CompositorStateChanged event) {
    final out = event.event as DisplayOutput;
    if (out.id != state.output?.id) {
      return null;
    }

    /* debugPrint(
      'OUTPUT CHANGED: ${out.id} '
      '${out.width} ${out.height}',
    ); */

    for (final surface in state.surfaces) {
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

    return CompositorBlocState.set(output: out);
  }

  CompositorBlocState? _onOutputRemoved(_CompositorStateChanged event) {
    final out = event.event as DisplayOutput;
    if (out.id != state.output?.id) {
      return null;
    }

    return const CompositorBlocState.set();
  }

  Future<void> _updateCurrentPageIndex(int index) async {
    if (state.surfaces.isEmpty) {
      if (isOverview) {
        //await _controller.reverse();
      }
      return;
    }

    if (state.surfaces.isNotEmpty) {
      final fixedIndex = max(index, 0);

      await _pageController.animateToPage(
        fixedIndex,
        duration: isOverview
            ? const Duration(milliseconds: 216)
            : const Duration(milliseconds: 230),
        curve: isOverview ? Curves.ease : Curves.linearToEaseOut,
      );
    }
  }

  bool get isOverview => _pageController.viewportFraction == 0.5;

  @override
  Future<void> close() async {
    add(const _SetReadyRequested(ready: false));
    _pageController.dispose();
    await _eventSubscription.cancel();
    await _compositorRepository.close();
    return super.close();
  }
}
