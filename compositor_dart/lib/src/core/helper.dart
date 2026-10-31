import 'dart:ui' show Rect, Size;

/// Signature for callbacks invoked when a widget's rendered [Size] changes.
typedef OnWidgetSizeChange = void Function(Size? size);

/// Signature for callbacks invoked when a widget's global screen [Rect]
/// changes or becomes detached (`null`).
typedef OnWidgetBoundsChange = void Function(Rect? bounds);
