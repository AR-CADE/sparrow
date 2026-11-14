import 'package:flutter/scheduler.dart'
    show Ticker, TickerCallback, TickerProvider;

/// A lightweight [TickerProvider] implementation that creates [Ticker]s
/// directly without requiring a stateful widget mixin.
class TickerProviderImpl implements TickerProvider {
  /// Creates a new [TickerProviderImpl].
  const new();

  @override
  Ticker createTicker(TickerCallback onTick) => Ticker(onTick);
}
