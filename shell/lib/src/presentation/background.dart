import 'dart:io' show File;

import 'package:flutter_bloc/flutter_bloc.dart' show BlocSelector;
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
        SizedBox,
        StatelessWidget,
        Widget;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';

/// Renders desktop background image or fallback color based on configuration.
class Background extends StatelessWidget {
  /// Creates a new [Background] widget.
  const new({super.key});

  /// Soft pastel Flutter blue
  static const Color pastelFlutterBlue = Color(0xFF5482A6);

  @override
  Widget build(BuildContext context) {
    return BlocSelector<ConfigurationCubit, ConfigurationState, String?>(
      selector: (state) => state.configuration.wallpaper,
      builder: (context, wallpaperPath) {
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

        // TODO(cparm): use default color from configs
        return const SizedBox.expand(
          child: ColoredBox(color: pastelFlutterBlue),
        );
      },
    );
  }
}
