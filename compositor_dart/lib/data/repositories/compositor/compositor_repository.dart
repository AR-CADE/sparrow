import 'dart:async' show unawaited;
import 'dart:collection' show HashMap;

import 'package:collection/collection.dart' show IterableExtension;
import 'package:compositor_dart/api/compositor_platform_api.dart'
    show CompositorPlatformApi;
import 'package:compositor_dart/core/constants.dart' show physicalToXkbMap;
import 'package:compositor_dart/data/models/compositor_event.dart'
    show CompositorEvent;
import 'package:compositor_dart/data/models/display_mode.dart' show DisplayMode;
import 'package:compositor_dart/data/models/display_output.dart'
    show DisplayOutput;
import 'package:compositor_dart/data/models/gesture_swipe_event.dart'
    show GestureSwipeBeginEvent, GestureSwipeEndEvent, GestureSwipeUpdateEvent;
import 'package:compositor_dart/data/models/popup.dart' show Popup;
import 'package:compositor_dart/data/models/sub_surface.dart';
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/models/surface_request_activate_event.dart'
    show SurfaceRequestActivateEvent;
import 'package:flutter/services.dart' show PlatformException;
import 'package:material_ui/material_ui.dart' show Rect, debugPrint, immutable;
import 'package:rxdart/subjects.dart' show PublishSubject;

@immutable
class CompositorRepository {
  factory CompositorRepository() {
    return _instance;
  }

  CompositorRepository._internal() {
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
    _handleMessage();

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
        await _platform.channel.invokeMethod('compositor_ready');
        debugPrint('Compositor ready signal sent to C');
      } on Exception catch (e) {
        debugPrint('Error sending compositor_ready: $e');
      }
    });
  }

  void _handleMessage() {
    _platform
      ..addHandler('surface_map', (call) async {
        try {
          final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
            if (k is String) {
              return MapEntry(k, v);
            }
            return MapEntry('$k', v);
          });

          final surface = Surface.fromJson(
            json,
          );

          /*           
          print('surf ${surf.toJson()}');

          final surface = Surface(
            handle: (json['handle'] as num).toInt(),
            textureId: (json['texture_id'] as num?)?.toInt(),
            pid: (json['client_pid'] as num).toInt(),
            gid: (json['client_gid'] as num).toInt(),
            uid: (json['client_uid'] as num).toInt(),
            title: json['title'] as String?,
            appId: json['app_id'] as String?,
            width: (json['width'] as num?)?.toInt(),
            height: (json['height'] as num?)?.toInt(),
            bufferWidth: (json['buffer_width'] as num?)?.toInt(),
            bufferHeight: (json['buffer_height'] as num?)?.toInt(),
            maximized: (json['maximized'] as num?)?.toInt(),
            activated: (json['activated'] as num?)?.toInt(),
            geoX: (json['geo_x'] as num?)?.toInt(),
            geoY: (json['geo_y'] as num?)?.toInt(),
            usesCsd: (json['uses_csd'] as num?)?.toInt(),
            outputId: (json['output_id'] as num?)?.toInt(),
            outputScale: (json['output_scale'] as num?)?.toDouble(),
          );

          print('surface  ${surface.toJson()}'); */

          /* debugPrint(
            'Surface mapped: handle=${surface.handle}, '
            'size=${surface.width}x${surface.height}, '
            'buffer=${surface.bufferWidth}x${surface.bufferHeight}, '
            'geoOffset=(${surface.geoX},${surface.geoY}), '
            'usesCsd=${surface.usesCsd}, '
            'outputId=${surface.outputId}, '
            'outputScale=${surface.outputScale}',
          ); */

          final newSurface = _surfaces.putIfAbsent(
            surface.handle,
            () => surface,
          );
          _subSurfaces.putIfAbsent(surface.handle, () => <SubSurface>{});
          _popups.putIfAbsent(surface.handle, () => <Popup>{});

          _events.add(CompositorEvent(type: .surfaceMap, event: newSurface));
        } on Exception catch (e) {
          debugPrint(e.toString());
        }
      })
      ..addHandler('surface_unmap', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null || !_surfaces.containsKey(handle)) return;

        final surface = _surfaces[handle];

        if (surface == null) return;

        final removedSurface = _surfaces.remove(handle);

        if (removedSurface != null) {
          _events.add(
            CompositorEvent(type: .surfaceUnMap, event: removedSurface),
          );
        }
      })
      ..addHandler('surface_title', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null || !_surfaces.containsKey(handle)) return;

        final surface = _surfaces[handle];

        if (surface == null) return;

        final title = json['title'] as String?;
        final appId = json['app_id'] as String?;

        final newSurface = _surfaces.update(
          handle,
          (_) => surface.copyWith(title: title, appId: appId),
        );

        _events.add(
          CompositorEvent(type: .surfaceTitleChange, event: newSurface),
        );
      })
      ..addHandler('surface_geometry', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null || !_surfaces.containsKey(handle)) return;

        final surface = _surfaces[handle];

        if (surface == null) return;

        final width = json['width'] as int?;
        final height = json['height'] as int?;
        final bufferWidth = json['buffer_width'] as int?;
        final bufferHeight = json['buffer_height'] as int?;
        final geoX = json['geo_x'] as int?;
        final geoY = json['geo_y'] as int?;

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
      })
      ..addHandler('surface_decoration', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null || !_surfaces.containsKey(handle)) return;

        final surface = _surfaces[handle];

        if (surface == null) return;

        final usesCsd = json['uses_csd'] as bool?;

        if (surface.usesCsd != usesCsd) {
          /* debugPrint(
            'Decoration update: handle=$handle, usesCsd changed '
            'from ${surface.usesCsd} to $usesCsd',
          ); */
        }

        final newSurface = _surfaces.update(
          handle,
          (_) => surface.copyWith(usesCsd: usesCsd),
        );

        _events.add(
          CompositorEvent(type: .surfaceDecorationChange, event: newSurface),
        );
      })
      ..addHandler('surface_position', (call) async {
        /* final handle = call.arguments['handle'] as int;

        final surface = _surfaces[handle];
        if (surface == null) return;

        final x = call.arguments['x'] as int?;
        final y = call.arguments['y'] as int?;
        final width = call.arguments['width'] as int? ?? 0;
        final height = call.arguments['height'] as int? ?? 0;

        final newSurface = _surfaces.update(
        handle,
        (_) => surface.copyWith(geoX: x, geoY: y, width: width, height: height),
      );

      _events.add(
        CompositorEvent(
          type: .surfacePositionChange,
          event: SurfacePositionEvent(
            handle: handle,
            surface: newSurface,
            x: x,
            y: y,
            width: width,
            height: height,
          ),
        ),
      ); */
      })
      ..addHandler('surface_grab_end', (call) async {
        /*      final handle = call.arguments['handle'] as int;

        final surface = _surfaces[handle];
        if (surface == null) return;

        final x = call.arguments['x'] as int?;
        final y = call.arguments['y'] as int?;
        final cursorX = (call.arguments['cursor_x'] as num?)?.toDouble();
        final cursorY = (call.arguments['cursor_y'] as num?)?.toDouble();

             _events.add(
        CompositorEvent(
          type: .surfaceGrabEnd,
          event: SurfaceGrabEndEvent(
            handle: handle,
            surface: surface,
            x: x,
            y: y,
            cursorX: cursorX,
            cursorY: cursorY,
          ),
        ),
      ); */
      })
      // Popup handling (menus, dropdowns, tooltips)
      ..addHandler('popup_map', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        final popup = Popup.fromJson(json);

        /* 
        final parentHandle = json['parent_handle'] as int;
        final x = json['x'] as int? ?? 0;
        final y = json['y'] as int? ?? 0;
        final width = json['width'] as int? ?? 0;
        final height = json['height'] as int? ?? 0;
        final textureId = json['texture_id'] as int? ?? (handle + 200000);
        final outputId = json['output_id'] as int? ?? 0;
        final outputScale = (json['output_scale'] as num?)?.toDouble() ?? 1.0;

        final popup = Popup(
          handle: handle,
          textureId: textureId,
          parentHandle: parentHandle,
          x: x,
          y: y,
          width: width,
          height: height,
          outputId: outputId,
          outputScale: outputScale,
        );

        print(popup.toJson()); */

        /* debugPrint(
          'Popup mapped: handle=$handle, parent=${popup.parentHandle}, '
          'pos=(${popup.x},${popup.y}), '
          'size=${popup.width}x${popup.height}, textureId=${popup.textureId}, '
          'outputId=${popup.outputId}, outputScale=${popup.outputScale}',
        ); */

        if (_popups.containsKey(popup.parentHandle)) {
          _popups.update(popup.parentHandle, (popups) {
            popups
              ..removeWhere((p) => p.handle == popup.handle)
              ..add(popup);
            return popups;
          });
        } else {
          _popups.putIfAbsent(popup.parentHandle, () => <Popup>{popup});
        }

        _events.add(CompositorEvent(type: .popupMap, event: popup));
      })
      ..addHandler('popup_unmap', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

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
      })
      ..addHandler('flutter/keyevent', (call) async {})
      // Subsurface handlers
      ..addHandler('subsurface_map', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

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

        _events.add(
          CompositorEvent(type: .subSurfaceMap, event: newSubSurface),
        );
      })
      ..addHandler('subsurface_unmap', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        SubSurface? subsurface;
        int? parentHandle;

        for (final entry in _subSurfaces.entries) {
          subsurface = entry.value.firstWhereOrNull(
            (sub) => sub.handle == handle,
          );
          if (subsurface != null) {
            parentHandle = entry.key;
            entry.value.removeWhere((sub) => sub.handle == handle);
            break;
          }
        }

        if (subsurface == null || parentHandle == null) return;

        _events.add(CompositorEvent(type: .subSurfaceUnMap, event: subsurface));
      })
      ..addHandler('subsurface_position', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        final x = json['x'] as int? ?? 0;
        final y = json['y'] as int? ?? 0;
        final width = json['width'] as int? ?? 0;
        final height = json['height'] as int? ?? 0;
        final bufferWidth = json['buffer_width'] as int? ?? width;
        final bufferHeight = json['buffer_height'] as int? ?? height;

        SubSurface? subsurface;
        Set<SubSurface>? subSurfaceSet;

        for (final set in _subSurfaces.values) {
          subsurface = set.firstWhereOrNull((sub) => sub.handle == handle);
          if (subsurface != null) {
            subSurfaceSet = set;
            break;
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
          CompositorEvent(
            type: .subsurfacePositionChange,
            event: newSubSurface,
          ),
        );
      })
      // CSD app minimize request
      ..addHandler('surface_minimize', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        final surface = _surfaces[handle];

        if (surface == null) return;

        //debugPrint('CSD app requested minimize for surface $handle');

        _events.add(
          CompositorEvent(type: .surfaceMinimizeRequest, event: surface),
        );
      })
      // xdg-activation-v1 activation request
      ..addHandler('surface_request_activate', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;
        if (handle == null) return;

        final surface = _surfaces[handle];
        final activateEvent = SurfaceRequestActivateEvent.fromJson(
          json.cast<String, dynamic>(),
        ).copyWith(surface: surface);

        _events.add(
          CompositorEvent(
            type: .surfaceRequestActivate,
            event: activateEvent,
          ),
        );
      })
      // CSD app maximize request
      ..addHandler('surface_request_maximize', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        // final maximized = (json['maximized'] ?? 0) != 0;

        final surface = _surfaces[handle];
        if (surface == null) return;

        /* debugPrint(
          'CSD app requested maximize for '
          'surface $handle, maximized=$maximized',
        ); */

        /*  _events.add(
        CompositorEvent(
          type: .surfaceRequestMaximize,
          event: SurfaceMaximizeEvent(
            handle: handle,
            surface: surface,
            maximized: maximized,
          ),
        ),
      ); */
      })
      // Synchronized resize: client committed buffer matching requested size
      ..addHandler('resize_ready', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final handle = json['handle'] as int?;

        if (handle == null) return;

        //final requestId = json['request_id'] as int?;
        //final width = json['width'] as int?;
        //final height = json['height'] as int?;

        final surface = _surfaces[handle];
        if (surface == null) return;

        /* debugPrint(
          'Resize ready: handle=$handle, '
          'requestId=$requestId, size=${width}x$height',
        ); */

        /*       _events.add(
        CompositorEvent(
          type: .surfaceMinimizeRequest,
          event: ResizeReadyEvent(
            handle: handle,
            surface: surface,
            requestId: requestId,
            width: width,
            height: height,
          ),
        ),
      ); */
      })
      // Output (monitor) handlers for multi-monitor support
      ..addHandler('output_added', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final output = DisplayOutput.fromJson(json);

        /*    print(o.toJson());

        List<DisplayMode>? availableModes;
        if (json.containsKey('modes')) {
          final modesList = json['modes'] as List<dynamic>;
          availableModes = modesList.map((m) {
            final mode = m;
            return DisplayMode(
              width: mode['width'] as int,
              height: mode['height'] as int,
              refresh: mode['refresh'] as int,
            );
          }).toList();
        }

        final output = DisplayOutput(
          id: json['id'] as int,
          name: json['name'] as String? ?? '',
          make: json['make'] as String? ?? '',
          model: json['model'] as String? ?? '',
          x: json['x'] as int?,
          y: json['y'] as int?,
          width: json['width'] as int?,
          height: json['height'] as int?,
          refreshRate: json['refresh'] as int?,
          scale: (json['scale'] as num?)?.toDouble(),
          transform: json['transform'] as int?,
          availableModes: availableModes,
          isPrimary: false,
        );

        print(output.toJson()); */

        // DisplayOutput? out;

        // Primary is the output at position (0,0) - the leftmost/topmost monitor
        // This handles outputs being registered in any order
        if (output.x == 0 && output.y == 0) {
          // New output is at origin - make it primary, demote others

          if (_outputs.isNotEmpty) {
            _outputs.updateAll((key, value) {
              return value.copyWith(isPrimary: false);
            });
          }

          // First output and not at origin - make primary for now
          // Will be demoted if origin output is added later
          final out = output.copyWith(isPrimary: true);
          _outputs.putIfAbsent(
            output.id,
            () => output.copyWith(isPrimary: true),
          );
          _events.add(CompositorEvent(type: .outputAdded, event: out));
        } else {
          // First output and not at origin - make primary for now
          // Will be demoted if origin output is added later
          final out = output.copyWith(isPrimary: _outputs.isEmpty);
          _outputs.putIfAbsent(
            output.id,
            () => output.copyWith(isPrimary: _outputs.isEmpty),
          );
          _events.add(CompositorEvent(type: .outputAdded, event: out));
        }
      })
      ..addHandler('output_removed', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });

        final outputId = json['id'] as int;

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
      })
      ..addHandler('output_changed', (call) async {
        final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
          if (k is String) {
            return MapEntry(k, v);
          }
          return MapEntry('$k', v);
        });
        final outputId = json['id'] as int;

        final output = _outputs[outputId];
        if (output == null) return;

        List<DisplayMode>? availableModes;
        if ((json as Map).containsKey('modes')) {
          final modesList = json['modes'] as List<dynamic>;
          availableModes = modesList.map((m) {
            return DisplayMode.fromJson(m as Map<String, dynamic>);
            /*  final mode = m;
            return DisplayMode(
              width: mode['width'] as int,
              height: mode['height'] as int,
              refresh: mode['refresh'] as int,
            ); */
          }).toList();
        }

        final out = _outputs.update(
          output.id,
          (_) => output.copyWith(
            x: json['x'] as int?,
            y: json['y'] as int?,
            width: json['width'] as int?,
            height: json['height'] as int?,
            refreshRate: json['refresh'] as int?,
            scale: (json['scale'] as num?)?.toDouble(),
            transform: json['transform'] as int?,
            availableModes: availableModes,
          ),
        );

        _events.add(CompositorEvent(type: .outputChanged, event: out));
      })
      ..addHandler('gesture_swipe_begin', (call) async {
        try {
          final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
            return MapEntry('$k', v);
          });
          final event = GestureSwipeBeginEvent.fromJson(json);
          _events.add(CompositorEvent(type: .gestureSwipeBegin, event: event));
        } on Exception catch (e) {
          debugPrint(e.toString());
        }
      })
      ..addHandler('gesture_swipe_update', (call) async {
        try {
          final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
            return MapEntry('$k', v);
          });
          final event = GestureSwipeUpdateEvent.fromJson(json);
          _events.add(CompositorEvent(type: .gestureSwipeUpdate, event: event));
        } on Exception catch (e) {
          debugPrint(e.toString());
        }
      })
      ..addHandler('gesture_swipe_end', (call) async {
        try {
          final json = (call.arguments as Map<dynamic, dynamic>).map((k, v) {
            return MapEntry('$k', v);
          });
          final event = GestureSwipeEndEvent.fromJson(json);
          _events.add(CompositorEvent(type: .gestureSwipeEnd, event: event));
        } on Exception catch (e) {
          debugPrint(e.toString());
        }
      });
  }

  Future<void> close() async {
    await _events.close();
    _platform.close();
  }

  int? keyToXkb(int physicalKey) => physicalToXkbMap[physicalKey];

  static final CompositorRepository _instance =
      CompositorRepository._internal();
  late final CompositorPlatformApi _platform;

  CompositorPlatformApi get platform => _platform;

  // ============== Multi-Monitor Support ==============

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
