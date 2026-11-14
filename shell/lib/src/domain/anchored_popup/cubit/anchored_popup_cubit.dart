import 'package:flutter/widgets.dart'
    show
        GlobalKey,
        Offset,
        Rect,
        RenderBox,
        Size,
        WidgetsBinding,
        WidgetsBindingObserver;
import 'package:flutter_bloc/flutter_bloc.dart' show Cubit;
import 'package:shell/src/domain/anchored_popup/cubit/anchored_popup_state.dart';

/// Cubit managing dynamic anchor resolution across screen rotations.
class AnchoredPopupCubit extends Cubit<AnchoredPopupState>
    with WidgetsBindingObserver {
  /// Creates a new [AnchoredPopupCubit].
  new({this.anchorKey, Rect? initialAnchorRect})
    : super(AnchoredPopupState(anchorRect: initialAnchorRect)) {
    WidgetsBinding.instance.addObserver(this);
    scheduleAnchorUpdate();
  }

  /// The anchor widget's key.
  final GlobalKey? anchorKey;
  Size? _lastScreenSize;

  @override
  void didChangeMetrics() {
    scheduleAnchorUpdate();
  }

  /// Handles screen size changes to trigger proportional updates.
  void handleScreenSize(Size currentSize) {
    if (_lastScreenSize != null && _lastScreenSize != currentSize) {
      scheduleAnchorUpdate();
      final currentRect = state.anchorRect;
      if (currentRect != null &&
          _lastScreenSize!.width > 0 &&
          _lastScreenSize!.height > 0) {
        final scaleX = currentSize.width / _lastScreenSize!.width;
        final scaleY = currentSize.height / _lastScreenSize!.height;
        emit(
          AnchoredPopupState(
            anchorRect: Rect.fromLTWH(
              currentRect.left * scaleX,
              currentRect.top * scaleY,
              currentRect.width,
              currentRect.height,
            ),
          ),
        );
      }
    }
    _lastScreenSize = currentSize;
  }

  /// Schedules a post-frame callback to re-measure the anchor widget.
  void scheduleAnchorUpdate() {
    if (anchorKey == null) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (isClosed) return;
      final context = anchorKey!.currentContext;
      if (context != null && context.mounted) {
        final box = context.findRenderObject() as RenderBox?;
        if (box != null && box.hasSize && box.attached) {
          final newRect = box.localToGlobal(Offset.zero) & box.size;
          if (newRect != state.anchorRect) {
            emit(AnchoredPopupState(anchorRect: newRect));
          }
        }
      }
    });
  }

  @override
  Future<void> close() {
    WidgetsBinding.instance.removeObserver(this);
    return super.close();
  }
}
