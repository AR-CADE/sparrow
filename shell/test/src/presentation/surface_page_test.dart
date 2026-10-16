import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/surface_page.dart';

import '../../helpers/mock_compositor_api.dart';

void main() {
  setUp(setUpMockCompositorHostApi);

  group('SurfacePage', () {
    testWidgets('renders SizedBox.shrink when surface is null', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: SurfacePage(surface: null))),
      );

      expect(find.byType(SizedBox), findsOneWidget);
    });

    testWidgets('renders SurfaceView when surface is provided', (tester) async {
      const surface = Surface(
        handle: 1,
        pid: 100,
        gid: 100,
        uid: 100,
        title: 'App 1',
      );

      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(body: SurfacePage(surface: surface)),
        ),
      );

      expect(find.byType(SurfaceView), findsOneWidget);
    });
  });
}
