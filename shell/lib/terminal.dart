import 'dart:async' show unawaited;
import 'dart:io' show Process, ProcessStartMode;

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

class TerminalWidget extends StatelessWidget {
  const TerminalWidget({
    required this.command,
    super.key,
  });

  final String command;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      child: IconButton(
        tooltip: 'Terminal',
        color: Colors.white,
        style: ButtonStyle(
          backgroundColor: WidgetStateProperty.all(
            Colors.black.withAlpha(60),
          ),
        ),
        icon: const Icon(
          size: 28,
          Icons.terminal,
        ),
        onPressed: () {
          final parts = command.trim().split(RegExp(r'\s+'));
          if (parts.isNotEmpty && parts.first.isNotEmpty) {
            unawaited(
              Process.start(
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
