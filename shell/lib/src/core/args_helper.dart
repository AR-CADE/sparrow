import 'dart:convert' show jsonEncode;

import 'package:args/args.dart' show ArgParser;

/// Extracts default boolean flag value from arguments.
bool parserDefaultValueForFlag(
  String name, {
  required String? abbr,
  required List<String> args,
}) {
  return (args.firstWhere(
        (arg) => arg == '--$name' || (abbr != null && arg == '-$abbr'),
        orElse: () => jsonEncode(null),
      ) !=
      jsonEncode(null));
}

/// Sets a flag on the [ArgParser].
void parserSetFlag(
  String name, {
  required ArgParser parser,
  required List<String> args,
  String? abbr,
}) {
  parser.addFlag(
    name,
    abbr: abbr,
    defaultsTo: parserDefaultValueForFlag(name, abbr: abbr, args: args),
  );
}

/// Checks whether a named flag was set in the [ArgParser].
bool parserIsFlagSet(String name, {required ArgParser parser}) {
  final option = parser.findByNameOrAlias(name);
  final value = option?.valueOrDefault(null);
  return value != null && value is bool && value;
}
