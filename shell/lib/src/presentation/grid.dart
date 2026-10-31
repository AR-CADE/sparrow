import 'dart:async' show unawaited;
import 'dart:io' show Process, ProcessStartMode;

import 'package:flutter/widgets.dart' show FocusManager;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        ButtonStyle,
        Colors,
        EdgeInsets,
        Icon,
        IconButton,
        Icons,
        Padding,
        StatelessWidget,
        Widget,
        WidgetStateProperty;

import 'package:shell/src/core/constants.dart' show ProcessStarter;

/// Applications launcher button widget.
class GridWidget extends StatelessWidget {
  /// Creates a new [GridWidget].
  const new({
    required this.command,
    this.processStarter = Process.start,
    super.key,
  });

  /// Command string to execute.
  final String command;

  /// Injectable process starter for execution.
  final ProcessStarter processStarter;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      child: IconButton(
        tooltip: 'Applications',
        color: Colors.white,
        style: ButtonStyle(
          backgroundColor: WidgetStateProperty.all(Colors.black.withAlpha(60)),
        ),
        icon: const Icon(size: 28, Icons.apps),
        onPressed: () {
          FocusManager.instance.primaryFocus?.unfocus();
          final parts = command.trim().split(RegExp(r'\s+'));
          if (parts.isNotEmpty && parts.first.isNotEmpty) {
            unawaited(
              processStarter(
                parts.first,
                parts.skip(1).toList(),
                mode: ProcessStartMode.detached,
              ),
            );
          }
        },
      ),
    );
  }
}
