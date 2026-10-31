import 'dart:async';

import 'package:hydrated_bloc/hydrated_bloc.dart';
import 'package:mocktail/mocktail.dart';

class MockStorage extends Mock implements Storage;

Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  final storage = MockStorage();
  when(() => storage.read(any())).thenReturn(null);
  when(() => storage.write(any(), any<dynamic>())).thenAnswer((_) async {});
  when(() => storage.delete(any())).thenAnswer((_) async {});
  when(storage.clear).thenAnswer((_) async {});
  when(storage.close).thenAnswer((_) async {});
  HydratedBloc.storage = storage;

  await testMain();
}
