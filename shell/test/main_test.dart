import 'package:flutter_test/flutter_test.dart';
import 'package:shell/main.dart';
import 'package:shell/shell.dart';

import 'helpers/mock_compositor_api.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('ShellApp builds and renders Shell without errors', (
    tester,
  ) async {
    setUpMockCompositorHostApi();

    await tester.pumpWidget(const ShellApp());
    await tester.pump();

    expect(find.byType(Shell), findsOneWidget);
  });
}
