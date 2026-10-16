import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/loader.dart';

void main() {
  group('Loader', () {
    testWidgets('renders CircularProgressIndicator', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: Loader())),
      );

      expect(find.byType(CircularProgressIndicator), findsOneWidget);
    });
  });
}
