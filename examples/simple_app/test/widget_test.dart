import 'package:flutter_test/flutter_test.dart';
import 'package:sparrow_demo_app/main.dart';

void main() {
  testWidgets('SparrowDemoApp smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const SparrowDemoApp());

    expect(find.text('Sparrow App Runner'), findsOneWidget);
    expect(
      find.text('Wayland & Embedder Status (sparrow/system)'),
      findsOneWidget,
    );
  });
}
