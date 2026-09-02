import 'dart:io' show File;

import 'package:material_ui/material_ui.dart'
    show
        BoxDecoration,
        BoxFit,
        BuildContext,
        Color,
        ColoredBox,
        DecoratedBox,
        DecorationImage,
        Image,
        ListenableBuilder,
        SizedBox,
        StatelessWidget,
        Widget;
import 'package:shell/configuration_repository.dart'
    show ConfigurationRepository;

class Background extends StatelessWidget {
  const Background({
    super.key,
  });

  /// Soft pastel Flutter blue
  static const Color pastelFlutterBlue = Color(0xFF5482A6);

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: ConfigurationRepository.instance,
      builder: (context, _) {
        final wallpaperPath = ConfigurationRepository.instance.wallpaper;
        if (wallpaperPath != null && wallpaperPath.trim().isNotEmpty) {
          final file = File(wallpaperPath.trim());
          if (file.existsSync()) {
            return SizedBox.expand(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  image: DecorationImage(
                    image: Image.file(file).image,
                    fit: BoxFit.cover,
                  ),
                ),
              ),
            );
          }
        }

        // Fallback: solid pastel Flutter blue
        return const SizedBox.expand(
          child: ColoredBox(
            color: pastelFlutterBlue,
          ),
        );
      },
    );
  }
}
