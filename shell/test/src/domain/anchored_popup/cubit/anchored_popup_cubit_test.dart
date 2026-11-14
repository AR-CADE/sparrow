import 'package:flutter/widgets.dart' show GlobalKey, Rect, Size;
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/domain/anchored_popup/cubit/anchored_popup_cubit.dart';
import 'package:shell/src/domain/anchored_popup/cubit/anchored_popup_state.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('AnchoredPopupCubit and AnchoredPopupState', () {
    test('initial state holds provided anchorRect', () async {
      const initialRect = Rect.fromLTWH(10, 20, 30, 40);
      final cubit = AnchoredPopupCubit(initialAnchorRect: initialRect);

      expect(cubit.state.anchorRect, initialRect);
      await cubit.close();
    });

    test('state supports value equality and props', () {
      const rect1 = Rect.fromLTWH(10, 20, 30, 40);
      const rect2 = Rect.fromLTWH(10, 20, 30, 40);
      const rect3 = Rect.fromLTWH(50, 60, 70, 80);

      expect(
        const AnchoredPopupState(anchorRect: rect1),
        equals(const AnchoredPopupState(anchorRect: rect2)),
      );
      expect(
        const AnchoredPopupState(anchorRect: rect1),
        isNot(equals(const AnchoredPopupState(anchorRect: rect3))),
      );
      expect(
        const AnchoredPopupState(anchorRect: rect1).props,
        equals([rect1]),
      );
    });

    test(
      'handleScreenSize adjusts anchorRect proportionally on resize',
      () async {
        const initialRect = Rect.fromLTWH(100, 200, 50, 50);
        final cubit = AnchoredPopupCubit(initialAnchorRect: initialRect)
          ..handleScreenSize(const Size(1000, 1000));
        expect(cubit.state.anchorRect, initialRect);

        // Rotate / resize to double width and half height
        cubit.handleScreenSize(const Size(2000, 500));
        expect(cubit.state.anchorRect, const Rect.fromLTWH(200, 100, 50, 50));

        await cubit.close();
      },
    );

    test(
      'scheduleAnchorUpdate safely handles null or unmounted anchorKey',
      () async {
        final key = GlobalKey();
        final cubit = AnchoredPopupCubit(anchorKey: key)
          ..scheduleAnchorUpdate()
          ..didChangeMetrics();

        await cubit.close();
      },
    );
  });
}
