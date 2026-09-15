import 'dart:async' show unawaited;
import 'dart:collection' show HashMap;

import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/api/compositor_platform_api.dart'
    show CompositorPlatformApi;
import 'package:compositor_dart/core/constants.dart' show physicalToXkbMap;
import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/gesture_swipe_event.dart'
    show GestureSwipeBeginEvent, GestureSwipeEndEvent, GestureSwipeUpdateEvent;
import 'package:compositor_dart/data/models/popup.dart' show Popup;
import 'package:compositor_dart/data/models/sub_surface.dart';
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/models/surface_request_activate_event.dart'
    show SurfaceRequestActivateEvent;
import 'package:compositor_dart/data/models/zoom_event.dart'
    show ZoomKeyEvent, ZoomScrollEvent;
import 'package:flutter/services.dart' show PlatformException;
import 'package:material_ui/material_ui.dart' show Rect, debugPrint, immutable;
import 'package:pigeon_compositor/pigeon_compositor.dart'
    show CompositorFlutterApi;
import 'package:rxdart/subjects.dart' show PublishSubject;

@immutable
class CompositorRepository implements CompositorFlutterApi {
  factory() {
    return _instance;
  }

  new _internal() {
    _initCompositor();
  }
  final HashMap<int, Surface> _surfaces = HashMap();
  final HashMap<int, Set<SubSurface>> _subSurfaces = HashMap();
  final HashMap<int, Set<Popup>> _popups = HashMap();
  final HashMap<int, DisplayOutput> _outputs = HashMap();
  final _events = PublishSubject<CompositorEvent>();
  PublishSubject<CompositorEvent> get events => _events;

  HashMap<int, Surface> get surfaces => _surfaces;
  HashMap<int, Set<SubSurface>> get subSurfaces => _subSurfaces;
  HashMap<int, Set<Popup>> get popups => _popups;
  HashMap<int, DisplayOutput> get outputs => _outputs;

  Surface? surfaceLookUp(int surfaceHandle) {
    return _surfaces.values.firstWhereOrNull(
      (surface) => surface.handle == surfaceHandle,
    );
  }

  Set<SubSurface>? subSurfaceSetLookUp(int surfaceHandle) {
    return _subSurfaces[surfaceHandle];
  }

  Set<Popup>? popupSetLookUp(int surfaceHandle) {
    return _popups[surfaceHandle];
  }

  void _initCompositor() {
    _platform = CompositorPlatformApi();
    CompositorFlutterApi.setUp(this);

    /// Signal to C that Dart is ready to receive messages
    /// This triggers sending of existing outputs that were detected
    /// before Dart initialized
    unawaited(_signalReady());
  }

  Future<void> _signalReady() async {
    // Use Future.microtask to ensure all constructor initialization is complete
    // and handlers are registered before signaling ready
    await Future.microtask(() async {
      try {
        await _platform.compositorReady();
        debugPrint('Compositor ready signal sent to C (via Pigeon)');
      } on Exception catch (e) {
        debugPrint('Error sending compositor_ready: $e');
      }
    });
  }

  @override
  void surfaceMap(Map<String, Object?> surface) {
    try {
      final json = surface.cast<String, dynamic>();
      final newSurface = Surface.fromJson(json);

      _surfaces.putIfAbsent(newSurface.handle, () => newSurface);
      _subSurfaces.putIfAbsent(newSurface.handle, () => <SubSurface>{});
      _popups.putIfAbsent(newSurface.handle, () => <Popup>{});

      _events.add(CompositorEvent(type: .surfaceMap, event: newSurface));
    } on Exception catch (e) {
      debugPrint(e.toString());
    }
  }

  @override
  void surfaceUnmap(int handle) {
    if (!_surfaces.containsKey(handle)) return;

    final removedSurface = _surfaces.remove(handle);

    if (removedSurface != null) {
      _events.add(CompositorEvent(type: .surfaceUnMap, event: removedSurface));
    }
  }

  @override
  void surfaceTitle(int handle, String title, String appId) {
    if (!_surfaces.containsKey(handle)) return;

    final surface = _surfaces[handle];
    if (surface == null) return;

    final newSurface = _surfaces.update(
      handle,
      (_) => surface.copyWith(title: title, appId: appId),
    );

    _events.add(CompositorEvent(type: .surfaceTitleChange, event: newSurface));
  }

  @override
  void surfaceGeometry(
    int handle,
    int width,
    int height,
    int bufferWidth,
    int bufferHeight,
    int geoX,
    int geoY,
  ) {
    if (!_surfaces.containsKey(handle)) return;

    final surface = _surfaces[handle];
    if (surface == null) return;

    final newSurface = _surfaces.update(
      handle,
      (_) => surface.copyWith(
        width: width,
        height: height,
        bufferWidth: bufferWidth,
        bufferHeight: bufferHeight,
        geoX: geoX,
        geoY: geoY,
      ),
    );

    _events.add(
      CompositorEvent(type: .surfaceGeometryChange, event: newSurface),
    );
  }

  @override
  void surfaceDecoration(int handle, bool usesSsd, bool usesCsd) {
    if (!_surfaces.containsKey(handle)) return;

    final surface = _surfaces[handle];
    if (surface == null) return;

    final newSurface = _surfaces.update(
      handle,
      (_) => surface.copyWith(usesCsd: usesCsd),
    );

    _events.add(
      CompositorEvent(type: .surfaceDecorationChange, event: newSurface),
    );
  }

  @override
  void surfaceMinimize(int handle) {
    final surface = _surfaces[handle];
    if (surface == null) return;

    _events.add(CompositorEvent(type: .surfaceMinimizeRequest, event: surface));
  }

  @override
  void surfaceRequestActivate(int handle, String token, String appId) {
    final surface = _surfaces[handle];
    final activateEvent = SurfaceRequestActivateEvent(
      handle: handle,
      token: token,
      appId: appId,
      surface: surface,
    );

    _events.add(
      CompositorEvent(type: .surfaceRequestActivate, event: activateEvent),
    );
  }

  @override
  void resizeReady(int handle, int requestId) {}

  @override
  void subsurfaceMap(Map<String, Object?> subsurface) {
    final json = subsurface.cast<String, dynamic>();
    final newSubSurface = SubSurface.fromJson(json);

    if (_subSurfaces.containsKey(newSubSurface.parentHandle)) {
      _subSurfaces.update(newSubSurface.parentHandle, (subSurfaces) {
        subSurfaces
          ..removeWhere((s) => s.handle == newSubSurface.handle)
          ..add(newSubSurface);
        return subSurfaces;
      });
    } else {
      _subSurfaces.putIfAbsent(
        newSubSurface.parentHandle,
        () => <SubSurface>{newSubSurface},
      );
    }

    _events.add(CompositorEvent(type: .subSurfaceMap, event: newSubSurface));
  }

  @override
  void subsurfaceUnmap(int handle, int parentHandle) {
    SubSurface? subsurface;
    int? foundParent;

    if (_subSurfaces.containsKey(parentHandle)) {
      subsurface = _subSurfaces[parentHandle]?.firstWhereOrNull(
        (sub) => sub.handle == handle,
      );
      if (subsurface != null) {
        foundParent = parentHandle;
        _subSurfaces[parentHandle]?.removeWhere((sub) => sub.handle == handle);
      }
    } else {
      for (final entry in _subSurfaces.entries) {
        subsurface = entry.value.firstWhereOrNull(
          (sub) => sub.handle == handle,
        );
        if (subsurface != null) {
          foundParent = entry.key;
          entry.value.removeWhere((sub) => sub.handle == handle);
          break;
        }
      }
    }

    if (subsurface == null || foundParent == null) return;

    _events.add(CompositorEvent(type: .subSurfaceUnMap, event: subsurface));
  }

  @override
  void subsurfacePosition(
    int handle,
    int parentHandle,
    int x,
    int y,
    int width,
    int height,
    int bufferWidth,
    int bufferHeight,
  ) {
    SubSurface? subsurface;
    Set<SubSurface>? subSurfaceSet;

    if (_subSurfaces.containsKey(parentHandle)) {
      subSurfaceSet = _subSurfaces[parentHandle];
      subsurface = subSurfaceSet?.firstWhereOrNull(
        (sub) => sub.handle == handle,
      );
    }
    if (subsurface == null || subSurfaceSet == null) {
      for (final set in _subSurfaces.values) {
        subsurface = set.firstWhereOrNull((sub) => sub.handle == handle);
        if (subsurface != null) {
          subSurfaceSet = set;
          break;
        }
      }
    }

    if (subsurface == null || subSurfaceSet == null) return;

    final newSubSurface = subsurface.copyWith(
      x: x,
      y: y,
      width: width,
      height: height,
      bufferWidth: bufferWidth,
      bufferHeight: bufferHeight,
    );

    subSurfaceSet
      ..removeWhere((s) => s.handle == handle)
      ..add(newSubSurface);

    _subSurfaces.update(newSubSurface.parentHandle, (_) => subSurfaceSet!);

    _events.add(
      CompositorEvent(type: .subsurfacePositionChange, event: newSubSurface),
    );
  }

  @override
  void popupMap(Map<String, Object?> popup) {
    final popupObj = Popup.fromJson(popup.cast<String, dynamic>());

    if (_popups.containsKey(popupObj.parentHandle)) {
      _popups.update(popupObj.parentHandle, (popups) {
        popups
          ..removeWhere((p) => p.handle == popupObj.handle)
          ..add(popupObj);
        return popups;
      });
    } else {
      _popups.putIfAbsent(popupObj.parentHandle, () => <Popup>{popupObj});
    }

    _events.add(CompositorEvent(type: .popupMap, event: popupObj));
  }

  @override
  void popupUnmap(int handle) {
    Popup? popup;
    int? parentHandle;

    for (final entry in _popups.entries) {
      popup = entry.value.firstWhereOrNull((p) => p.handle == handle);
      if (popup != null) {
        parentHandle = entry.key;
        entry.value.removeWhere((p) => p.handle == handle);
        break;
      }
    }

    if (popup == null || parentHandle == null) return;

    _events.add(CompositorEvent(type: .popupUnMap, event: popup));
  }

  @override
  void outputAdded(Map<String, Object?> output) {
    final displayOutput = DisplayOutput.fromJson(output);

    // Primary is the output at position (0,0) - the leftmost/topmost monitor
    // This handles outputs being registered in any order
    if (displayOutput.x == 0 && displayOutput.y == 0) {
      // New output is at origin - make it primary, demote others
      if (_outputs.isNotEmpty) {
        _outputs.updateAll((key, value) {
          return value.copyWith(isPrimary: false);
        });
      }

      final out = displayOutput.copyWith(isPrimary: true);
      _outputs.putIfAbsent(
        displayOutput.id,
        () => displayOutput.copyWith(isPrimary: true),
      );
      _events.add(CompositorEvent(type: .outputAdded, event: out));
    } else {
      // First output and not at origin - make primary for now
      // Will be demoted if origin output is added later
      final out = displayOutput.copyWith(isPrimary: _outputs.isEmpty);
      _outputs.putIfAbsent(
        displayOutput.id,
        () => displayOutput.copyWith(isPrimary: _outputs.isEmpty),
      );
      _events.add(CompositorEvent(type: .outputAdded, event: out));
    }
  }

  @override
  void outputRemoved(int outputId) {
    final output = _outputs[outputId];
    if (output == null) return;

    _outputs.remove(outputId);
    _events.add(CompositorEvent(type: .outputRemoved, event: output));

    // If primary was removed, make another output primary
    if (output.isPrimary && _outputs.isNotEmpty) {
      final primaryOutput = _outputs.values.firstOrNull;

      if (primaryOutput != null) {
        final out = _outputs.update(
          primaryOutput.id,
          (_) => primaryOutput.copyWith(isPrimary: true),
        );

        _events.add(CompositorEvent(type: .outputChanged, event: out));
      }
    }
  }

  @override
  void outputChanged(Map<String, Object?> output) {
    final displayOutput = DisplayOutput.fromJson(output);
    final currentOutput = _outputs[displayOutput.id];
    if (currentOutput == null) return;

    final out = _outputs.update(
      displayOutput.id,
      (_) => displayOutput.copyWith(isPrimary: currentOutput.isPrimary),
    );

    _events.add(CompositorEvent(type: .outputChanged, event: out));
  }

  @override
  void gestureSwipeBegin(int fingers, int timeMsec) {
    final event = GestureSwipeBeginEvent(fingers: fingers, timeMsec: timeMsec);
    _events.add(CompositorEvent(type: .gestureSwipeBegin, event: event));
  }

  @override
  void gestureSwipeUpdate(double dx, double dy, int timeMsec) {
    final event = GestureSwipeUpdateEvent(dx: dx, dy: dy, timeMsec: timeMsec);
    _events.add(CompositorEvent(type: .gestureSwipeUpdate, event: event));
  }

  @override
  void gestureSwipeEnd(bool cancelled, int timeMsec) {
    final event = GestureSwipeEndEvent(
      cancelled: cancelled,
      timeMsec: timeMsec,
    );
    _events.add(CompositorEvent(type: .gestureSwipeEnd, event: event));
  }

  @override
  void zoomScroll(double delta, double x, double y) {
    final event = ZoomScrollEvent(delta: delta, x: x, y: y);
    _events.add(CompositorEvent(type: .zoomScroll, event: event));
  }

  @override
  void zoomKey(int action, double x, double y) {
    final event = ZoomKeyEvent(action: action, x: x, y: y);
    _events.add(CompositorEvent(type: .zoomKey, event: event));
  }

  Future<void> close() async {
    CompositorFlutterApi.setUp(null);
    await _events.close();
    _platform.close();
    _surfaces.clear();
    _subSurfaces.clear();
    _popups.clear();
    _outputs.clear();
  }

  int? keyToXkb(int physicalKey) =>
      physicalToXkbMap[physicalKey] ??
      (((physicalKey & 0xFFFFFFFF00000000) == 0x01500000000)
          ? (physicalKey & 0xFFFFFFFF)
          : null);

  static final CompositorRepository _instance =
      CompositorRepository._internal();
  late final CompositorPlatformApi _platform;

  CompositorPlatformApi get platform => _platform;

  /// Get the primary display output.
  DisplayOutput? get getPrimaryOutput =>
      _outputs.values.cast<DisplayOutput?>().firstWhere(
        (o) => o?.isPrimary == true,
        orElse: () =>
            _outputs.values.isNotEmpty ? _outputs.values.firstOrNull : null,
      );

  /// Get total bounds across all outputs (unified coordinate space).
  Rect get totalBounds {
    if (_outputs.isEmpty) {
      return Rect.zero;
    }

    final minX = _outputs.values
        .map((o) => o.x)
        .reduce((a, b) => a < b ? a : b);
    final minY = _outputs.values
        .map((o) => o.y)
        .reduce((a, b) => a < b ? a : b);
    final maxX = _outputs.values
        .map((o) => o.x + o.width)
        .reduce((a, b) => a > b ? a : b);
    final maxY = _outputs.values
        .map((o) => o.y + o.height)
        .reduce((a, b) => a > b ? a : b);

    return Rect.fromLTRB(
      minX.toDouble(),
      minY.toDouble(),
      maxX.toDouble(),
      maxY.toDouble(),
    );
  }

  /// Get the output containing a given point.
  DisplayOutput? getOutputAtPoint(double x, double y) {
    for (final output in _outputs.values) {
      if (output.containsPoint(x, y)) {
        return output;
      }
    }
    return null;
  }

  /// Get the output that contains most of the given rectangle.
  DisplayOutput? getOutputForRect(Rect rect) {
    DisplayOutput? bestOutput;
    double bestOverlap = 0;

    for (final output in _outputs.values) {
      final outputRect = Rect.fromLTWH(
        output.x.toDouble(),
        output.y.toDouble(),
        output.width.toDouble(),
        output.height.toDouble(),
      );

      final intersection = rect.intersect(outputRect);
      if (!intersection.isEmpty) {
        final overlap = intersection.width * intersection.height;
        if (overlap > bestOverlap) {
          bestOverlap = overlap;
          bestOutput = output;
        }
      }
    }

    return bestOutput ?? getPrimaryOutput;
  }

  /// Set which output is primary.
  /// Returns true on success, false on failure.
  Future<bool> setPrimaryOutput(int outputId) async {
    try {
      final output = _outputs.values.firstWhereOrNull((o) => o.id == outputId);
      if (output == null) {
        return false;
      }

      if (output.isPrimary) {
        return true;
      }

      _outputs.updateAll((key, value) {
        return value.copyWith(isPrimary: false);
      });

      final out = _outputs.update(
        output.id,
        (_) => output.copyWith(isPrimary: true),
      );

      await platform.setPrimaryOutput(out.id);

      return true;
    } on PlatformException catch (e) {
      debugPrint('Failed to set primary output: $e');
      return false;
    }
  }
}
