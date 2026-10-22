import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/ticker_provider.dart';

void main() {
  group('TickerProviderImpl', () {
    test('createTicker returns a valid Ticker instance', () {
      const provider = TickerProviderImpl();
      final ticker = provider.createTicker((elapsed) {});

      expect(ticker, isNotNull);
      ticker.dispose();
    });
  });
}
