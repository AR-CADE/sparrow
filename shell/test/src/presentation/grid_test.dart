import 'dart:io' show Process, ProcessStartMode;

import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/grid.dart';

class FakeProcess implements Process {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  group('GridWidget', () {
    testWidgets('renders and launches command on tap', (tester) async {
      String? launched;

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: GridWidget(
              command: 'grid2 --arg',
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
      expect(find.byIcon(Icons.apps), findsOneWidget);
      expect(find.byTooltip('Applications'), findsOneWidget);

      await tester.tap(button);
      await tester.pump();
      expect(launched, 'grid2 --arg');
    });
  });
}
