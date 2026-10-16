import 'package:equatable/equatable.dart';
import 'package:flutter/widgets.dart' show Rect;

/// Immutable state holding the dynamically resolved anchor rectangle.
class AnchoredPopupState extends Equatable {
  /// Creates a new [AnchoredPopupState].
  const new({this.anchorRect});

  /// The resolved anchor rectangle in global coordinate space.
  final Rect? anchorRect;

  @override
  List<Object?> get props => [anchorRect];
}
