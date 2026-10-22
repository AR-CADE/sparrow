import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Center,
        CircularProgressIndicator,
        StatelessWidget,
        Widget;

/// Fullscreen loading spinner.
class Loader extends StatelessWidget {
  /// Creates a new [Loader].
  const new({super.key});

  @override
  Widget build(BuildContext context) {
    return const Center(child: CircularProgressIndicator());
  }
}
