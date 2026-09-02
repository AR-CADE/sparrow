import 'dart:async' show unawaited;

import 'package:compositor_dart/core/constants.dart' show KeyStatus;
import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/presentation/surface.dart' show SurfaceView;
import 'package:flutter/services.dart' show KeyDownEvent;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Focus,
        KeyEventResult,
        RepaintBoundary,
        SizedBox,
        StatelessWidget,
        Widget;

class SurfacePage extends StatelessWidget {
  const SurfacePage({
    required this.surface,
    required this.toggleOverview,
    this.interactive = true,
    this.freeze = false,
    super.key,
  });

  final Surface? surface;
  final bool interactive;
  final bool freeze;
  final Future<void> Function() toggleOverview;

  @override
  Widget build(BuildContext context) {
    final surf = surface;
    return surf != null
        ? Focus(
            onKeyEvent: interactive
                ? (node, event) {
                    final keycode = CompositorRepository().keyToXkb(
                      event.physicalKey.usbHidUsage,
                    );

                    // print('keycode ui pressed $keycode');

                    if (event.physicalKey == .altLeft) {
                      unawaited(toggleOverview());
                    }

                    if (keycode != null) {
                      unawaited(
                        CompositorRepository().platform.surfaceSendKey(
                          surf,
                          keycode,
                          event is KeyDownEvent
                              ? KeyStatus.pressed
                              : KeyStatus.released,
                          event.timeStamp,
                        ),
                      );
                    }
                    return KeyEventResult.handled;
                  }
                : null,
            autofocus: true,
            child: RepaintBoundary(
              child: SurfaceView(
                freeze: freeze,
                surface: surf,
                interactive: interactive,
              ),
            ),
          )
        : const SizedBox.shrink();
  }
}
