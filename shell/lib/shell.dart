import 'package:flutter/widgets.dart'
    show BuildContext, Builder, StatelessWidget, Widget;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocProvider, ReadContext, RepositoryProvider;
import 'package:shell/src/core/shell_controllers.dart' show ShellControllers;
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart' show ShellBloc;
import 'package:shell/src/presentation/shell_view.dart' show ShellView;

export 'src/core/args_helper.dart';
export 'src/core/constants.dart';
export 'src/core/scroll_behavior.dart';
export 'src/core/shell_controllers.dart';
export 'src/core/ticker_provider.dart';
export 'src/core/zoom_controller.dart';
export 'src/data/models/shell_configuration.dart';
export 'src/data/repositories/configuration_repository.dart';
export 'src/domain/anchored_popup/cubit/anchored_popup_cubit.dart';
export 'src/domain/anchored_popup/cubit/anchored_popup_state.dart';
export 'src/domain/background_shader/cubit/background_shader_cubit.dart';
export 'src/domain/background_shader/cubit/background_shader_state.dart';
export 'src/domain/configuration/cubit/configuration_cubit.dart';
export 'src/domain/configuration/cubit/configuration_state.dart';
export 'src/domain/lifecycle/cubit/app_lifecycle_cubit.dart';
export 'src/domain/shell/shell.dart';
export 'src/presentation/animated_surface_wrapper.dart';
export 'src/presentation/background.dart';
export 'src/presentation/background_shader.dart';
export 'src/presentation/background_shader_painter.dart';
export 'src/presentation/bottom_indicator.dart';
export 'src/presentation/compositor_controller.dart';
export 'src/presentation/grid.dart';
export 'src/presentation/hero_page_route.dart';
export 'src/presentation/loader.dart';
export 'src/presentation/navigator_control.dart';
export 'src/presentation/screen_zoom_overlay.dart';
export 'src/presentation/shell_page_view.dart';
export 'src/presentation/shell_view.dart';
export 'src/presentation/surface_page.dart';
export 'src/presentation/system.dart';
export 'src/presentation/system_popover.dart';
export 'src/presentation/taskbar.dart';
export 'src/presentation/terminal.dart';

/// The top-level Sparrow shell widget.
///
/// Automatically hooks into existing [ShellBloc] and
/// [ShellControllers] providers if present in the ancestor tree,
/// or provides self-managed defaults.
class Shell extends StatelessWidget {
  /// Creates a new [Shell] instance.
  const new({this.controllers, super.key});

  /// Optional custom controllers holder.
  final ShellControllers? controllers;

  @override
  Widget build(BuildContext context) {
    if (controllers != null) {
      return _ShellBlocScope(controllers: controllers!);
    }

    ShellControllers? fromContext;
    try {
      fromContext = context.read<ShellControllers>();
    } on Object catch (_) {}

    if (fromContext != null) {
      return _ShellBlocScope(controllers: fromContext);
    }

    return RepositoryProvider<ShellControllers>(
      create: (ctx) {
        ShellBloc? bloc;
        try {
          bloc = ctx.read<ShellBloc>();
        } on Object catch (_) {}
        return ShellControllers(
          initialPage: bloc?.state.currentPageIndex ?? 0,
          initialOverview: bloc?.state.isOverview ?? false,
          initialHandles: bloc?.state.surfaceHandles ?? const [],
        );
      },
      dispose: (c) => c.dispose(),
      child: Builder(
        builder: (ctx) =>
            _ShellBlocScope(controllers: ctx.read<ShellControllers>()),
      ),
    );
  }
}

class _ShellBlocScope extends StatelessWidget {
  const new({required this.controllers});

  final ShellControllers controllers;

  @override
  Widget build(BuildContext context) {
    ShellBloc? bloc;
    try {
      bloc = context.read<ShellBloc>();
    } on Object catch (_) {}

    if (bloc != null) {
      return ShellView(controllers: controllers);
    }

    return BlocProvider(
      create: (_) => ShellBloc(),
      child: ShellView(controllers: controllers),
    );
  }
}
