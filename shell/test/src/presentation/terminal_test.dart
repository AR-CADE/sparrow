import 'dart:io' show Process, ProcessStartMode;

import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/terminal.dart';

class FakeProcess implements Process {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  group('TerminalWidget', () {
    testWidgets('renders and launches command on tap', (tester) async {
      String? launched;

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: TerminalWidget(
              command: 'alacritty -e zsh',
              processStarter:
                  (exe, args, {mode = ProcessStartMode.normal}) async {
                    launched = '$exe ${args.join(' ')}';
                    return FakeProcess();
                  },
            ),
          ),
        ),
      );

      final button = find.byType(IconButton);
      expect(button, findsOneWidget);
      expect(find.byIcon(Icons.terminal), findsOneWidget);
      expect(find.byTooltip('Terminal'), findsOneWidget);

      await tester.tap(button);
      await tester.pump();
      expect(launched, 'alacritty -e zsh');
    });
  });
}
