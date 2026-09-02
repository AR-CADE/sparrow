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

class TaskBar extends StatelessWidget {
  const TaskBar({
    required this.leading,
    required this.center,
    required this.trailing,
    super.key,
    this.centerRelativeToScreen = true,
  });

  final List<Widget> leading;
  final List<Widget> center;
  final List<Widget> trailing;
  final bool centerRelativeToScreen;

  @override
  Widget build(BuildContext context) {
    return Material(
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
    );
  }
}
