import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/scroll_behavior.dart';

void main() {
  group('CustomScrollBehavior', () {
    test(
      'dragDevices includes mouse, stylus, touch, invertedStylus, trackpad',
      () {
        const behavior = CustomScrollBehavior();
        final devices = behavior.dragDevices;

        expect(devices, contains(PointerDeviceKind.mouse));
        expect(devices, contains(PointerDeviceKind.stylus));
        expect(devices, contains(PointerDeviceKind.touch));
        expect(devices, contains(PointerDeviceKind.invertedStylus));
        expect(devices, contains(PointerDeviceKind.trackpad));
      },
    );
  });
}
