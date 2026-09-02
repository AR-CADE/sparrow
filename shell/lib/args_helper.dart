import 'dart:convert' show jsonEncode;

import 'package:args/args.dart' show ArgParser;

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

bool parserIsFlagSet(String name, {required ArgParser parser}) {
  final option = parser.findByNameOrAlias(name);
  final value = option?.valueOrDefault(null);
  return value != null && value is bool && value;
}
