import 'dart:io' show Process, ProcessStartMode;

import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/presentation/system.dart';

class FakeProcess implements Process {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  group('SystemGrid and SystemWidget', () {
    const testActions = SystemActions(
      logout: SystemAction(
        name: 'Logout',
        command: 'wayland-logout --now',
        icon: Icons.logout,
      ),
      reboot: SystemAction(
        name: 'Reboot',
        command: 'systemctl reboot',
        icon: Icons.restart_alt,
      ),
    );

    testWidgets('SystemGrid renders actions and runs command on tap', (
      tester,
    ) async {
      String? launched;

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SystemGrid(
              actions: testActions,
              processStarter:
                  (exe, args, {mode = ProcessStartMode.normal}) async {
                    launched = '$exe ${args.join(' ')}';
                    return FakeProcess();
                  },
            ),
          ),
        ),
      );

      expect(find.text('Logout'), findsOneWidget);
      expect(find.text('Reboot'), findsOneWidget);
      expect(find.byIcon(Icons.logout), findsOneWidget);

      await tester.tap(find.byIcon(Icons.logout));
      await tester.pump();
      expect(launched, 'wayland-logout --now');
    });

    testWidgets('SystemWidget renders power icon button and opens dialog', (
      tester,
    ) async {
      String? launched;

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SystemWidget(
              actions: testActions,
              processStarter:
                  (exe, args, {mode = ProcessStartMode.normal}) async {
                    launched = '$exe ${args.join(' ')}';
                    return FakeProcess();
                  },
            ),
          ),
        ),
      );

      final button = find.byIcon(Icons.power_settings_new);
      expect(button, findsOneWidget);

      await tester.tap(button);
      await tester.pumpAndSettle();

      expect(find.text('Logout'), findsOneWidget);

      await tester.tap(find.byIcon(Icons.logout));
      await tester.pumpAndSettle();
      expect(launched, 'wayland-logout --now');
    });

    testWidgets(
      'SystemWidget repositions dialog dynamically on screen rotation',
      (tester) async {
        tester.view.physicalSize = const Size(1000, 600);
        tester.view.devicePixelRatio = 1.0;
        addTearDown(tester.view.resetPhysicalSize);
        addTearDown(tester.view.resetDevicePixelRatio);

        await tester.pumpWidget(
          const MaterialApp(
            home: Scaffold(
              body: Align(
                alignment: Alignment.bottomRight,
                child: SystemWidget(actions: testActions),
              ),
            ),
          ),
        );

        final button = find.byIcon(Icons.power_settings_new);
        await tester.tap(button);
        await tester.pumpAndSettle();

        final initialDialogPos = tester.getTopLeft(find.byType(SystemGrid));

        // Rotate / change dimensions to portrait: 600 x 1000
        tester.view.physicalSize = const Size(600, 1000);
        await tester.pumpAndSettle();

        final rotatedDialogPos = tester.getTopLeft(find.byType(SystemGrid));

        expect(rotatedDialogPos.dx, isNot(equals(initialDialogPos.dx)));
        expect(rotatedDialogPos.dy, isNot(equals(initialDialogPos.dy)));
        expect(rotatedDialogPos.dx, lessThan(600));
        expect(rotatedDialogPos.dy, greaterThan(initialDialogPos.dy));
      },
    );
  });
}
