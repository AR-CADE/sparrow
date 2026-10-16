import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/core/constants.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/presentation/shell_page_view.dart';

import '../../helpers/mock_compositor_api.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('ShellPageView', () {
    late AnimationController animController;
    late PageController pageController;
    const testSurface1 = Surface(
      handle: 1,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'Surface 1',
    );
    const testSurface2 = Surface(
      handle: 2,
      pid: 101,
      gid: 100,
      uid: 100,
      title: 'Surface 2',
    );
    const testOutput = DisplayOutput(id: 1, width: 1920, height: 1080);

    setUp(() {
      setUpMockCompositorHostApi();
      animController = AnimationController(
        duration: const Duration(milliseconds: 100),
        vsync: const TickerProviderImpl(),
      );
      pageController = PageController();
    });

    tearDown(() {
      animController.dispose();
      pageController.dispose();
    });

    testWidgets('renders PageView with given surfaces', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ShellPageView(
              surfaces: const [testSurface1, testSurface2],
              animationController: animController,
              handlePageViewChanged: (i) {},
              toggleOverview: ({index}) async {},
              closeOverview: (s, {center = true}) async {},
              openOverview: (s) async {},
              pageController: pageController,
              gap: 0,
              fraction: maxFraction,
              radius: 0,
              output: testOutput,
            ),
          ),
        ),
      );

      expect(find.byType(PageView), findsOneWidget);
    });
  });
}
