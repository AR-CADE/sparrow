import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/presentation/navigator_control.dart';

import '../../helpers/mock_compositor_api.dart';

class MockCompositorBloc extends MockBloc<CompositorBlocEvent, CompositorState>
    implements CompositorBloc;

void main() {
  group('NavigationControllerWidget', () {
    late MockCompositorBloc compBloc;
    late PageController pageController;

    setUp(() {
      setUpMockCompositorHostApi();
      compBloc = MockCompositorBloc();
      pageController = PageController();

      when(() => compBloc.state).thenReturn(
        const CompositorState(
          surfaces: {
            1: Surface(
              handle: 1,
              pid: 100,
              gid: 100,
              uid: 100,
              title: 'App 1',
              appId: 'app1',
            ),
            2: Surface(
              handle: 2,
              pid: 101,
              gid: 100,
              uid: 100,
              title: 'App 2',
              appId: 'app2',
            ),
          },
        ),
      );
    });

    tearDown(() {
      pageController.dispose();
    });

    testWidgets('renders navigation buttons and triggers callbacks', (
      tester,
    ) async {
      var overviewToggled = false;
      int? updatedPageIndex;

      await tester.pumpWidget(
        MaterialApp(
          home: BlocProvider<CompositorBloc>.value(
            value: compBloc,
            child: Scaffold(
              body: NavigationControllerWidget(
                pageController: pageController,
                toggleOverview: () async {
                  overviewToggled = true;
                },
                onUpdateCurrentPageIndex: (i) async {
                  updatedPageIndex = i;
                },
              ),
            ),
          ),
        ),
      );

      final buttons = find.byType(IconButton);
      expect(buttons, findsWidgets);

      // Tap overview button
      final overviewBtn = find.byTooltip('overview');
      expect(overviewBtn, findsOneWidget);
      await tester.tap(overviewBtn);
      await tester.pump();
      expect(overviewToggled, isTrue);

      // Tap next button
      final nextBtn = find.byTooltip('next');
      expect(nextBtn, findsOneWidget);
      await tester.tap(nextBtn);
      await tester.pump();
      expect(updatedPageIndex, 1);

      // Tap Add Application
      final addBtn = find.byTooltip('Add Application');
      expect(addBtn, findsOneWidget);
      await tester.tap(addBtn);
      await tester.pump();

      // Tap Quit Application / Remove Application
      final removeBtn = find.byTooltip('Remove Application');
      expect(removeBtn, findsOneWidget);
      await tester.tap(removeBtn);
      await tester.pump();
    });

    testWidgets('previous button navigates back when index > 0', (
      tester,
    ) async {
      final activePageController = PageController(initialPage: 1);
      int? updatedIndex;

      await tester.pumpWidget(
        MaterialApp(
          home: BlocProvider<CompositorBloc>.value(
            value: compBloc,
            child: Scaffold(
              body: Column(
                children: [
                  Expanded(
                    child: PageView(
                      controller: activePageController,
                      children: const [SizedBox(), SizedBox()],
                    ),
                  ),
                  NavigationControllerWidget(
                    pageController: activePageController,
                    toggleOverview: () async {},
                    onUpdateCurrentPageIndex: (i) async {
                      updatedIndex = i;
                    },
                  ),
                ],
              ),
            ),
          ),
        ),
      );

      final prevBtn = find.byTooltip('previous');
      expect(prevBtn, findsOneWidget);
      await tester.tap(prevBtn);
      await tester.pump();
      expect(updatedIndex, 0);

      activePageController.dispose();
    });
  });
}
