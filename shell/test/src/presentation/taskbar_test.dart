import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/taskbar.dart';

void main() {
  group('TaskBar', () {
    testWidgets('renders leading, center, and trailing widgets', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: TaskBar(
              leading: [Text('Leading')],
              center: [Text('Center')],
              trailing: [Text('Trailing')],
            ),
          ),
        ),
      );

      expect(find.text('Leading'), findsOneWidget);
      expect(find.text('Center'), findsWidgets);
      expect(find.text('Trailing'), findsOneWidget);
    });

    testWidgets('renders correctly when centerRelativeToScreen is false', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: TaskBar(
              leading: [Text('Lead')],
              center: [Text('Mid')],
              trailing: [Text('Tail')],
              centerRelativeToScreen: false,
            ),
          ),
        ),
      );

      expect(find.text('Lead'), findsOneWidget);
      expect(find.text('Mid'), findsOneWidget);
      expect(find.text('Tail'), findsOneWidget);
    });
  });
}
