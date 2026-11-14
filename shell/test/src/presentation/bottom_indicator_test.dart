import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show MaterialApp, Scaffold;
import 'package:shell/src/presentation/bottom_indicator.dart';

void main() {
  group('BottomIndicator', () {
    testWidgets('renders capsule with default dimensions', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: BottomIndicator())),
      );

      final indicator = tester.widget<BottomIndicator>(
        find.byType(BottomIndicator),
      );
      expect(indicator.width, 280);
      expect(find.byType(BottomIndicator), findsOneWidget);
    });

    testWidgets('renders capsule with custom dimensions', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: BottomIndicator(width: 160, height: 6, bottomPadding: 12),
          ),
        ),
      );

      final indicator = tester.widget<BottomIndicator>(
        find.byType(BottomIndicator),
      );
      expect(indicator.width, 160);
      expect(indicator.height, 6);
      expect(indicator.bottomPadding, 12);
    });
  });
}
