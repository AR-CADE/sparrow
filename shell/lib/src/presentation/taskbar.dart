import 'package:flutter/widgets.dart' show ExcludeFocus;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Expanded,
        MainAxisAlignment,
        Material,
        MaterialType,
        Positioned,
        Row,
        SizedBox,
        Stack,
        StatelessWidget,
        Widget;

/// The desktop taskbar host layout widget.
class TaskBar extends StatelessWidget {
  /// Creates a new [TaskBar].
  const new({
    required this.leading,
    required this.center,
    required this.trailing,
    super.key,
    this.centerRelativeToScreen = true,
  });

  /// Leading taskbar widgets (e.g. app launcher, navigation).
  final List<Widget> leading;

  /// Center taskbar widgets.
  final List<Widget> center;

  /// Trailing taskbar widgets (e.g. terminal, system power menu).
  final List<Widget> trailing;

  /// Whether center items are positioned relative to the entire screen.
  final bool centerRelativeToScreen;

  @override
  Widget build(BuildContext context) {
    return ExcludeFocus(
      child: Material(
        type: MaterialType.transparency,
        child: Stack(
          children: [
            Positioned.fill(
              child: Row(
                children: [
                  Row(children: leading),
                  Expanded(
                    child: !centerRelativeToScreen
                        ? Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: center,
                          )
                        : const SizedBox.shrink(),
                  ),
                  Row(children: trailing),
                ],
              ),
            ),
            if (centerRelativeToScreen)
              Positioned.fill(
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: center,
                ),
              ),
          ],
        ),
      ),
    );
  }
}
