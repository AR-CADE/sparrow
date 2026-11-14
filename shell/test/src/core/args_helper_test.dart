import 'package:args/args.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/args_helper.dart';

void main() {
  group('ArgsHelper functions', () {
    test(
      'parserDefaultValueForFlag returns true when flag is present in args',
      () {
        expect(
          parserDefaultValueForFlag('help', abbr: 'h', args: ['--help']),
          isTrue,
        );
        expect(
          parserDefaultValueForFlag('help', abbr: 'h', args: ['-h']),
          isTrue,
        );
        expect(
          parserDefaultValueForFlag('help', abbr: 'h', args: ['--other']),
          isFalse,
        );
      },
    );

    test('parserSetFlag adds flag with proper default', () {
      final parser = ArgParser();
      parserSetFlag('verbose', parser: parser, args: ['-v'], abbr: 'v');
      final result = parser.parse([]);
      expect(result['verbose'], isTrue);
    });

    test('parserIsFlagSet checks if flag option is set', () {
      final parser = ArgParser();
      parserSetFlag('debug', parser: parser, args: ['--debug'], abbr: 'd');
      expect(parserIsFlagSet('debug', parser: parser), isTrue);
      expect(parserIsFlagSet('nonexistent', parser: parser), isFalse);
    });
  });
}
