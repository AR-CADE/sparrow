import 'package:compositor_dart/compositor_dart.dart'
    show MeasureSizeRenderObject;
import 'package:flutter/widgets.dart';
import 'package:shell/src/core/zoom_controller.dart';

/// Fullscreen overlay widget hosting the RawMagnifier.
/// Rebuilds ONLY when the zoom level or cursor changes, completely isolated
/// from the rest of the shell widget tree to ensure constant 60/120/144 FPS.
class ScreenZoomOverlay extends StatelessWidget {
  /// Creates a new [ScreenZoomOverlay].
  const new({required this.controller, required this.size, super.key});

  /// The zoom controller.
  final ZoomController controller;

  /// Display output screen size.
  final Size size;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        if (!controller.isZoomed) {
          MeasureSizeRenderObject.setScreenZoom(
            scale: 1,
            focalPoint: Offset.zero,
          );
          return const SizedBox.shrink();
        }

        final zoom = controller.currentZoom;
        final screenCenter = Offset(size.width / 2, size.height / 2);
        final cursor = controller.cursorPos == Offset.zero
            ? screenCenter
            : controller.cursorPos;

        MeasureSizeRenderObject.setScreenZoom(scale: zoom, focalPoint: cursor);

        // Optical focal point formula: (M - C) * (1 - 1/S)
        // Keeps the exact pixel under the cursor anchored and interactive
        final focalOffset = (cursor - screenCenter) * (1.0 - 1.0 / zoom);

        return IgnorePointer(
          child: RawMagnifier(
            size: size,
            magnificationScale: zoom,
            focalPointOffset: focalOffset,
            child: const SizedBox.expand(),
          ),
        );
      },
    );
  }
}
