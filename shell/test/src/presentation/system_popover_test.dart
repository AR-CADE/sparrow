import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/presentation/hero_page_route.dart';
import 'package:shell/src/presentation/system_popover.dart';

void main() {
  group('SystemPopoverWidget and AnchoredPopoverRoute', () {
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

    testWidgets('SystemPopoverCard renders actions', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(body: SystemPopoverCard(actions: testActions)),
        ),
      );

      expect(find.text('Logout'), findsOneWidget);
      expect(find.text('Reboot'), findsOneWidget);
      expect(find.byIcon(Icons.logout), findsOneWidget);
    });

    testWidgets('SystemPopoverWidget opens popover and closes', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(body: SystemPopoverWidget(actions: testActions)),
        ),
      );

      final button = find.byIcon(Icons.power_settings_new);
      expect(button, findsOneWidget);

      await tester.tap(button);
      await tester.pumpAndSettle();

      expect(find.text('Logout'), findsOneWidget);
      expect(find.text('Reboot'), findsOneWidget);

      // Tap backdrop to dismiss
      await tester.tapAt(const Offset(10, 10));
      await tester.pumpAndSettle();

      expect(find.text('Logout'), findsNothing);
    });

    testWidgets(
      'SystemPopoverWidget repositions popover dynamically on screen rotation',
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
                child: SystemPopoverWidget(actions: testActions),
              ),
            ),
          ),
        );

        final button = find.byIcon(Icons.power_settings_new);
        await tester.tap(button);
        await tester.pumpAndSettle();

        final initialCardPos = tester.getTopLeft(
          find.byType(SystemPopoverCard),
        );

        // Rotate / change dimensions to portrait: 600 x 1000
        tester.view.physicalSize = const Size(600, 1000);
        await tester.pumpAndSettle();

        final rotatedCardPos = tester.getTopLeft(
          find.byType(SystemPopoverCard),
        );

        expect(rotatedCardPos.dx, isNot(equals(initialCardPos.dx)));
        expect(rotatedCardPos.dy, isNot(equals(initialCardPos.dy)));
        expect(rotatedCardPos.dx, lessThan(600));
        expect(rotatedCardPos.dy, greaterThan(initialCardPos.dy));
      },
    );

    testWidgets('AnchoredPopoverRoute property accessors', (tester) async {
      final route = AnchoredPopoverRoute<void>(
        builder: (_) => const SizedBox.shrink(),
        anchorRect: const Rect.fromLTWH(100, 100, 50, 50),
      );

      expect(route.opaque, isFalse);
      expect(route.barrierDismissible, isTrue);
      expect(route.maintainState, isTrue);
      expect(route.barrierColor, Colors.transparent);
      expect(route.barrierLabel, 'anchored-popover-route');
      expect(route.transitionDuration, const Duration(milliseconds: 180));
      expect(
        route.reverseTransitionDuration,
        const Duration(milliseconds: 140),
      );
    });

    test(
      'AnchoredPopupLayoutDelegate positions correctly across quadrants',
      () {
        // 1. Null anchor (centered)
        const delegateNoAnchor = AnchoredPopupLayoutDelegate();
        final offsetCentered = delegateNoAnchor.getPositionForChild(
          const Size(800, 600),
          const Size(200, 100),
        );
        expect(offsetCentered, const Offset(300, 250));

        // 2. Top-left anchor -> positions below anchor
        const delegateTopLeft = AnchoredPopupLayoutDelegate(
          anchorRect: Rect.fromLTWH(50, 50, 40, 40),
        );
        final offsetTopLeft = delegateTopLeft.getPositionForChild(
          const Size(800, 600),
          const Size(200, 100),
        );
        expect(offsetTopLeft.dx, 50);
        expect(offsetTopLeft.dy, 50 + 40 + 12.0);

        // 3. Bottom-right anchor -> positions above anchor, aligned right
        const delegateBottomRight = AnchoredPopupLayoutDelegate(
          anchorRect: Rect.fromLTWH(700, 500, 40, 40),
        );
        final offsetBottomRight = delegateBottomRight.getPositionForChild(
          const Size(800, 600),
          const Size(200, 100),
        );
        expect(offsetBottomRight.dx, 740 - 200);
        expect(offsetBottomRight.dy, 500 - 12.0 - 100);

        // 4. Clamping and constraints
        const delegateOverflow = AnchoredPopupLayoutDelegate(
          anchorRect: Rect.fromLTWH(750, 550, 40, 40),
          screenPadding: EdgeInsets.all(20),
        );
        final offsetOverflow = delegateOverflow.getPositionForChild(
          const Size(800, 600),
          const Size(200, 100),
        );
        expect(offsetOverflow.dx, lessThanOrEqualTo(800 - 20 - 200));

        final constraints = delegateTopLeft.getConstraintsForChild(
          const BoxConstraints(maxWidth: 800, maxHeight: 600),
        );
        expect(constraints.maxWidth, 800 - 32);
        expect(constraints.maxHeight, 600 - 32);

        // 5. shouldRelayout
        expect(
          delegateTopLeft.shouldRelayout(
            const AnchoredPopupLayoutDelegate(
              anchorRect: Rect.fromLTWH(50, 50, 40, 40),
            ),
          ),
          isFalse,
        );
        expect(
          delegateTopLeft.shouldRelayout(
            const AnchoredPopupLayoutDelegate(
              anchorRect: Rect.fromLTWH(60, 50, 40, 40),
            ),
          ),
          isTrue,
        );
      },
    );
  });
}
