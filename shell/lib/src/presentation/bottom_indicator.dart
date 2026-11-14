import 'package:flutter/widgets.dart'
    show
        Align,
        Alignment,
        Border,
        BorderRadius,
        BoxDecoration,
        BoxShadow,
        BuildContext,
        Color,
        DecoratedBox,
        EdgeInsets,
        ExcludeFocus,
        Offset,
        Padding,
        SizedBox,
        StatelessWidget,
        Widget;

/// A capsule home / navigation bottom indicator displayed in edgeToEdge mode.
class BottomIndicator extends StatelessWidget {
  /// Creates a new [BottomIndicator].
  const new({
    this.width = 280.0,
    this.height = 4.0,
    this.bottomPadding = 2.0,
    super.key,
  });

  /// The width of the indicator capsule.
  final double width;

  /// The height of the indicator capsule.
  final double height;

  /// The bottom padding spacing from the screen edge.
  final double bottomPadding;

  @override
  Widget build(BuildContext context) {
    return ExcludeFocus(
      child: Align(
        alignment: Alignment.bottomCenter,
        child: Padding(
          padding: EdgeInsets.only(bottom: bottomPadding),
          child: DecoratedBox(
            decoration: BoxDecoration(
              color: const Color(0xE6FFFFFF),
              borderRadius: BorderRadius.circular(height / 2),
              border: Border.all(color: const Color(0x33000000), width: 0.5),
              boxShadow: const [
                BoxShadow(
                  color: Color(0x66000000),
                  blurRadius: 4,
                  offset: Offset(0, 1),
                ),
              ],
            ),
            child: SizedBox(width: width, height: height),
          ),
        ),
      ),
    );
  }
}
