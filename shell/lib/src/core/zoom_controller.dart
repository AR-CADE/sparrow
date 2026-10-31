import 'dart:math' as math;

import 'package:flutter/scheduler.dart' show Ticker, TickerProvider;
import 'package:flutter/widgets.dart';

/// Smooth Wayfire / KDE-style desktop screen zoom controller.
/// Uses frame-rate-independent exponential decay driven by a Flutter Ticker,
/// preventing animation restarts, velocity drops, and micro-stuttering.
class ZoomController extends ChangeNotifier {
  /// Creates a new [ZoomController] using the provided [vsync].
  new({required TickerProvider vsync}) {
    _ticker = vsync.createTicker(_onTick);
  }

  late final Ticker _ticker;
  double _currentZoom = 1;
  double _targetZoom = 1;
  Offset _cursorPos = Offset.zero;
  Duration? _lastTick;

  /// The currently rendered zoom level.
  double get currentZoom => _currentZoom;

  /// The target zoom level being animated toward.
  double get targetZoom => _targetZoom;

  /// The active cursor offset for zoom magnification focal point.
  Offset get cursorPos => _cursorPos;

  /// Whether the screen is currently zoomed in.
  bool get isZoomed => _currentZoom > 1.001;

  /// Whether zoom is active (animating or currently zoomed in).
  bool get isZoomActive => _currentZoom > 1.001 || _targetZoom > 1.001;

  /// Handles scroll-wheel zoom gestures.
  void handleScroll(double delta, Offset cursor) {
    if (cursor != Offset.zero) {
      _cursorPos = cursor;
    }
    // delta < 0: wheel up -> zoom in
    // delta > 0: wheel down -> zoom out
    // Normalized against a standard notch (~15px) with step clamping
    final steps = (-delta / 15.0).clamp(-3.0, 3.0);
    final factor = math.pow(1.09, steps).toDouble();
    _targetZoom = (_targetZoom * factor).clamp(1.0, 5.0);
    if (delta > 0 && _targetZoom < 1.03) {
      _targetZoom = 1;
    }
    if (_targetZoom == 1 && _currentZoom == 1) {
      _ticker.stop();
      _lastTick = null;
    } else {
      _ensureTicking();
    }
    notifyListeners();
  }

  /// Handles keyboard shortcuts (+, -, 0) for zooming.
  void handleKey(int action, Offset cursor) {
    if (cursor != Offset.zero) {
      _cursorPos = cursor;
    }
    if (action > 0) {
      _targetZoom = (_targetZoom * 1.2).clamp(1.0, 5.0);
    } else if (action < 0) {
      _targetZoom = (_targetZoom / 1.2).clamp(1.0, 5.0);
      if (_targetZoom < 1.03) {
        _targetZoom = 1;
      }
    } else {
      _targetZoom = 1;
    }
    if (_targetZoom == 1 && _currentZoom == 1) {
      _ticker.stop();
      _lastTick = null;
    } else {
      _ensureTicking();
    }
    notifyListeners();
  }

  /// Snaps back to 1.0 if zoom is close to baseline when the modifier key
  /// (Super) is released.
  void snapToBaseline({double threshold = 1.08}) {
    if (_targetZoom <= threshold) {
      if (_targetZoom != 1 || _currentZoom != 1) {
        _targetZoom = 1;
        _ensureTicking();
        notifyListeners();
      }
    }
  }

  /// Updates the cursor position.
  void updateCursor(Offset pos) {
    _cursorPos = pos;
    if (isZoomed) {
      notifyListeners();
    }
  }

  void _ensureTicking() {
    if (!_ticker.isActive) {
      _lastTick = null;
      _ticker.start();
    }
  }

  void _onTick(Duration elapsed) {
    final last = _lastTick;
    _lastTick = elapsed;
    if (last == null) {
      return;
    }

    final dt = (elapsed - last).inMicroseconds / 1000000.0;
    if (dt <= 0) return;

    final diff = _targetZoom - _currentZoom;
    if (diff.abs() < 0.001) {
      _currentZoom = _targetZoom;
      _ticker.stop();
      _lastTick = null;
      notifyListeners();
      return;
    }

    // Frame-rate-independent exponential damping (60/120/144+ Hz)
    final factor = 1.0 - math.exp(-dt * 20.0);
    _currentZoom += diff * factor;
    notifyListeners();
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }
}
