import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:pigeon_runner/pigeon_runner.dart'
    show
        RunnerHostApi,
        RunnerHostIpcApi,
        RunnerFlutterIpcApi,
        RunnerSystemInfoData,
        RunnerPongData,
        CompositorSystemInfoData,
        CompositorPongData,
        CompositorSurfaceData;

class HostApi {
  HostApi({RunnerHostApi? hostApi, RunnerHostIpcApi? ipcApi})
    : _hostApi = hostApi ?? RunnerHostApi(),
      _ipcApi = ipcApi ?? RunnerHostIpcApi();

  final RunnerHostApi _hostApi;
  final RunnerHostIpcApi _ipcApi;
  RunnerHostApi get hostApi => _hostApi;
  RunnerHostIpcApi get ipcApi => _ipcApi;

  Future<RunnerSystemInfoData?> getSystemInfo() {
    return _hostApi.getSystemInfo();
  }

  Future<void> setWindowTitle(String title) {
    return _hostApi.setWindowTitle(title);
  }

  Future<void> setFullscreen(bool fullscreen) {
    return _hostApi.setFullscreen(enabled: fullscreen);
  }

  Future<void> setMaximized(bool maximized) {
    return _hostApi.setMaximized(enabled: maximized);
  }

  Future<void> minimize() {
    return _hostApi.minimize();
  }

  Future<RunnerPongData> ping() {
    return _hostApi.ping();
  }

  // Compositor IPC methods
  Future<CompositorSystemInfoData?> getCompositorInfo() {
    return _ipcApi.getCompositorInfo();
  }

  Future<CompositorPongData?> pingCompositor() {
    return _ipcApi.pingCompositor();
  }

  Future<List<CompositorSurfaceData>?> listSurfaces() {
    return _ipcApi.listSurfaces();
  }

  Future<bool> closeSurface(int handle) {
    return _ipcApi.closeSurface(handle);
  }
}

void main() {
  runApp(const SparrowDemoApp());
}

class SparrowDemoApp extends StatelessWidget {
  const SparrowDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Sparrow Demo App',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        brightness: Brightness.dark,
        colorSchemeSeed: Colors.cyan,
        scaffoldBackgroundColor: const Color(0xFF0D1117),
        cardTheme: const CardThemeData(color: Color(0xFF161B22), elevation: 2),
      ),
      home: const DemoHomePage(),
    );
  }
}

class DemoHomePage extends StatefulWidget {
  const DemoHomePage({super.key});

  @override
  State<DemoHomePage> createState() => _DemoHomePageState();
}

class _DemoHomePageState extends State<DemoHomePage>
    implements RunnerFlutterIpcApi {
  final _runnerApi = HostApi();

  RunnerSystemInfoData? _systemInfo;
  CompositorSystemInfoData? _compositorInfo;
  List<CompositorSurfaceData> _compositorSurfaces = [];
  String _pingResult = 'Not tested';
  String _compositorPingResult = 'Not tested';
  bool _ipcConnected = false;
  bool _isFullscreen = false;
  bool _isMaximized = false;
  final TextEditingController _textController = TextEditingController();
  final TextEditingController _titleController = TextEditingController(
    text: 'Sparrow Native App',
  );
  String _clipboardStatus = '';

  @override
  void initState() {
    super.initState();
    RunnerFlutterIpcApi.setUp(this);
    _fetchSystemInfo();
    _fetchCompositorInfo();
  }

  @override
  void dispose() {
    RunnerFlutterIpcApi.setUp(null);
    _textController.dispose();
    _titleController.dispose();
    super.dispose();
  }

  @override
  void onNotification(String method, String? paramsJson) {
    debugPrint('[Compositor Notification] $method: $paramsJson');
    if (method == 'ipcConnected') {
      _fetchCompositorInfo();
      return;
    }
    if (method == 'ipcDisconnected') {
      setState(() {
        _compositorInfo = null;
        _ipcConnected = false;
        _compositorSurfaces = [];
      });
      return;
    }
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Compositor Notification: $method'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
  }

  Future<void> _fetchSystemInfo() async {
    final res = await _runnerApi.getSystemInfo();
    if (res != null) {
      setState(() {
        _systemInfo = res;
        _isFullscreen = res.fullscreen;
        _isMaximized = res.maximized;
      });
    }
  }

  Future<void> _pingRunner() async {
    final start = DateTime.now().millisecondsSinceEpoch;
    try {
      final res = await _runnerApi.ping();
      final elapsed = DateTime.now().millisecondsSinceEpoch - start;
      setState(() {
        _pingResult =
            'Pong! Roundtrip: ${elapsed}ms (Payload: pong=${res.pong}, time=${res.timestampMs}ms)';
      });
    } catch (e) {
      setState(() {
        _pingResult = 'Ping error: $e';
      });
    }
  }

  Future<void> _fetchCompositorInfo() async {
    try {
      final res = await _runnerApi.getCompositorInfo();
      setState(() {
        _compositorInfo = res;
        _ipcConnected = res != null;
      });
    } catch (e) {
      setState(() {
        _compositorInfo = null;
        _ipcConnected = false;
      });
    }
  }

  Future<void> _pingCompositor() async {
    final start = DateTime.now().millisecondsSinceEpoch;
    try {
      final res = await _runnerApi.pingCompositor();
      final elapsed = DateTime.now().millisecondsSinceEpoch - start;
      setState(() {
        if (res != null) {
          _compositorPingResult =
              'Pong! Latency: ${elapsed}ms (pid: ${res.peerPid}, timestamp: ${res.timestampUs}µs)';
          _ipcConnected = true;
        } else {
          _compositorPingResult = 'Ping returned null (not connected)';
          _ipcConnected = false;
        }
      });
    } catch (e) {
      setState(() {
        _compositorPingResult = 'Error: $e';
        _ipcConnected = false;
      });
    }
  }

  Future<void> _fetchCompositorSurfaces() async {
    try {
      final res = await _runnerApi.listSurfaces();
      setState(() {
        if (res != null) {
          _compositorSurfaces = res;
          _ipcConnected = true;
        } else {
          _compositorSurfaces = [];
          _ipcConnected = false;
        }
      });
    } catch (e) {
      setState(() {
        _compositorSurfaces = [];
        _ipcConnected = false;
      });
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error listing compositor surfaces: $e')),
        );
      }
    }
  }

  Future<void> _closeSurface(int handle) async {
    try {
      final closed = await _runnerApi.closeSurface(handle);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              closed
                  ? 'Surface #$handle closed successfully'
                  : 'Failed to close surface #$handle',
            ),
          ),
        );
      }
      _fetchCompositorSurfaces();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error closing surface #$handle: $e')),
        );
      }
    }
  }

  Future<void> _updateTitle() async {
    try {
      await _runnerApi.setWindowTitle(_titleController.text);
      _fetchSystemInfo();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Error updating title: $e')));
      }
    }
  }

  Future<void> _toggleFullscreen() async {
    final next = !_isFullscreen;
    try {
      await _runnerApi.setFullscreen(next);
      setState(() {
        _isFullscreen = next;
      });
      _fetchSystemInfo();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error toggling fullscreen: $e')),
        );
      }
    }
  }

  Future<void> _toggleMaximized() async {
    final next = !_isMaximized;
    try {
      await _runnerApi.setMaximized(next);
      setState(() {
        _isMaximized = next;
      });
      _fetchSystemInfo();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error toggling maximized: $e')));
      }
    }
  }

  Future<void> _minimize() async {
    try {
      await _runnerApi.minimize();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Error minimizing: $e')));
      }
    }
  }

  Future<void> _copyText() async {
    if (_textController.text.isNotEmpty) {
      await Clipboard.setData(ClipboardData(text: _textController.text));
      setState(() {
        _clipboardStatus = 'Copied to clipboard!';
      });
    }
  }

  Future<void> _pasteText() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    if (data != null && data.text != null) {
      setState(() {
        _textController.text = data.text!;
        _clipboardStatus = 'Pasted: "${data.text}"';
      });
    }
  }

  Future<void> _closeApp() async {
    await SystemNavigator.pop();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Row(
          children: [
            Icon(Icons.flutter_dash, color: Colors.cyanAccent),
            SizedBox(width: 10),
            Flexible(
              child: Text(
                'Sparrow App Runner',
                style: TextStyle(fontWeight: FontWeight.bold),
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ),
        backgroundColor: const Color(0xFF161B22),
        elevation: 0,
        actions: [
          IconButton(
            tooltip: 'Refresh Info',
            icon: const Icon(Icons.refresh),
            onPressed: _fetchSystemInfo,
          ),
          IconButton(
            tooltip: 'Quit App',
            icon: const Icon(Icons.close, color: Colors.redAccent),
            onPressed: _closeApp,
          ),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 850),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // 1. Wayland / System Info Card
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Row(
                          children: [
                            Icon(Icons.info_outline, color: Colors.cyan),
                            SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                'Wayland & Embedder Status (sparrow/system)',
                                style: TextStyle(
                                  fontSize: 16,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24, color: Colors.white12),
                        Wrap(
                          spacing: 16,
                          runSpacing: 10,
                          children: [
                            _InfoChip(
                              label: 'Runner',
                              value: _systemInfo?.runner ?? 'Unknown',
                              icon: Icons.memory,
                            ),
                            _InfoChip(
                              label: 'Version',
                              value: _systemInfo?.version ?? 'N/A',
                              icon: Icons.tag,
                            ),
                            _InfoChip(
                              label: 'Display',
                              value:
                                  (_systemInfo?.waylandDisplay.isNotEmpty ==
                                      true)
                                  ? _systemInfo!.waylandDisplay
                                  : 'None',
                              icon: Icons.desktop_windows,
                            ),
                            _InfoChip(
                              label: 'Window Size',
                              value: _systemInfo != null
                                  ? '${_systemInfo!.width} x ${_systemInfo!.height}'
                                  : '0 x 0',
                              icon: Icons.aspect_ratio,
                            ),
                            _InfoChip(
                              label: 'Pixel Ratio',
                              value: _systemInfo != null
                                  ? '${_systemInfo!.pixelRatio.toStringAsFixed(1)}x'
                                  : '1.0x',
                              icon: Icons.photo_size_select_actual,
                            ),
                            _InfoChip(
                              label: 'Fullscreen',
                              value: _isFullscreen ? 'Yes' : 'No',
                              icon: Icons.fullscreen,
                              color: _isFullscreen ? Colors.green : null,
                            ),
                            _InfoChip(
                              label: 'Maximized',
                              value: _isMaximized ? 'Yes' : 'No',
                              icon: Icons.crop_square,
                              color: _isMaximized ? Colors.green : null,
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // 2. MethodChannel IPC Test Card
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Row(
                          children: [
                            Icon(Icons.sync_alt, color: Colors.cyan),
                            SizedBox(width: 8),
                            Text(
                              'IPC Latency & Ping Test',
                              style: TextStyle(
                                fontSize: 16,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 12),
                        Row(
                          children: [
                            ElevatedButton.icon(
                              icon: const Icon(Icons.speed),
                              label: const Text('Ping Runner'),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: Colors.cyan.shade800,
                                foregroundColor: Colors.white,
                              ),
                              onPressed: _pingRunner,
                            ),
                            const SizedBox(width: 16),
                            Expanded(
                              child: Text(
                                _pingResult,
                                style: TextStyle(
                                  color: Colors.grey.shade300,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // 3. Zero-Trust Compositor IPC Card (Pigeon)
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            const Icon(
                              Icons.security,
                              color: Colors.greenAccent,
                            ),
                            const SizedBox(width: 8),
                            const Expanded(
                              child: Text(
                                'Zero-Trust Compositor IPC (Pigeon)',
                                style: TextStyle(
                                  fontSize: 16,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                            ),
                            Padding(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 10,
                                vertical: 4,
                              ),
                              child: DecoratedBox(
                                decoration: BoxDecoration(
                                  color: _ipcConnected
                                      ? Colors.green.withAlpha(5)
                                      : Colors.orange.withAlpha(5),
                                  borderRadius: BorderRadius.circular(12),
                                  border: Border.all(
                                    color: _ipcConnected
                                        ? Colors.greenAccent
                                        : Colors.orangeAccent,
                                  ),
                                ),
                                child: Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: [
                                    Icon(
                                      _ipcConnected
                                          ? Icons.lock_outline
                                          : Icons.lock_open,
                                      size: 14,
                                      color: _ipcConnected
                                          ? Colors.greenAccent
                                          : Colors.orangeAccent,
                                    ),
                                    const SizedBox(width: 4),
                                    Text(
                                      _ipcConnected
                                          ? 'Kernel socketpair'
                                          : 'Not connected',
                                      style: TextStyle(
                                        fontSize: 12,
                                        fontWeight: FontWeight.bold,
                                        color: _ipcConnected
                                            ? Colors.greenAccent
                                            : Colors.orangeAccent,
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24, color: Colors.white12),
                        Wrap(
                          spacing: 12,
                          runSpacing: 10,
                          children: [
                            _InfoChip(
                              label: 'Compositor',
                              value: _compositorInfo != null
                                  ? '${_compositorInfo!.compositor} v${_compositorInfo!.version}'
                                  : 'N/A',
                              icon: Icons.computer,
                            ),
                            _InfoChip(
                              label: 'IPC Channel',
                              value:
                                  _compositorInfo?.ipcChannel ??
                                  'Kernel Isolated',
                              icon: Icons.vpn_key,
                              color: Colors.greenAccent,
                            ),
                            _InfoChip(
                              label: 'Active Surfaces',
                              value: '${_compositorInfo?.surfacesCount ?? 0}',
                              icon: Icons.layers,
                            ),
                            _InfoChip(
                              label: 'Compositor FPS',
                              value: _compositorInfo != null
                                  ? _compositorInfo!.clientFps.toStringAsFixed(
                                      1,
                                    )
                                  : 'N/A',
                              icon: Icons.speed,
                            ),
                            _InfoChip(
                              label: 'Compositor PID',
                              value: '${_compositorInfo?.peerPid ?? 'N/A'}',
                              icon: Icons.fingerprint,
                            ),
                          ],
                        ),
                        const SizedBox(height: 16),
                        Wrap(
                          spacing: 10,
                          runSpacing: 8,
                          children: [
                            ElevatedButton.icon(
                              icon: const Icon(Icons.bolt),
                              label: const Text('Compositor Ping'),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: Colors.teal.shade800,
                                foregroundColor: Colors.white,
                              ),
                              onPressed: _pingCompositor,
                            ),
                            FilledButton.tonalIcon(
                              icon: const Icon(Icons.refresh),
                              label: const Text('Refresh Info'),
                              onPressed: _fetchCompositorInfo,
                            ),
                            FilledButton.tonalIcon(
                              icon: const Icon(Icons.list_alt),
                              label: const Text('List Surfaces'),
                              onPressed: _fetchCompositorSurfaces,
                            ),
                          ],
                        ),
                        if (_compositorPingResult != 'Not tested') ...[
                          const SizedBox(height: 10),
                          Text(
                            _compositorPingResult,
                            style: TextStyle(
                              color: Colors.tealAccent.shade100,
                              fontFamily: 'monospace',
                              fontSize: 12,
                            ),
                          ),
                        ],
                        if (_compositorSurfaces.isNotEmpty) ...[
                          const SizedBox(height: 14),
                          const Text(
                            'Wayland Surfaces in Compositor:',
                            style: TextStyle(
                              fontWeight: FontWeight.bold,
                              fontSize: 13,
                              color: Colors.grey,
                            ),
                          ),
                          const SizedBox(height: 6),
                          ..._compositorSurfaces.map(
                            (s) => Container(
                              margin: const EdgeInsets.only(bottom: 6),
                              padding: const EdgeInsets.symmetric(
                                horizontal: 12,
                                vertical: 8,
                              ),
                              child: DecoratedBox(
                                decoration: BoxDecoration(
                                  color: const Color(0xFF21262D),
                                  borderRadius: BorderRadius.circular(6),
                                  border: Border.all(color: Colors.white10),
                                ),
                                child: Row(
                                  children: [
                                    const Icon(
                                      Icons.window,
                                      size: 16,
                                      color: Colors.cyan,
                                    ),
                                    const SizedBox(width: 8),
                                    Expanded(
                                      child: Text(
                                        '#${s.handle} "${s.title}" [${s.appId}] (${s.width}x${s.height})',
                                        style: const TextStyle(
                                          fontFamily: 'monospace',
                                          fontSize: 12,
                                        ),
                                      ),
                                    ),
                                    if (s.fullscreen)
                                      const Chip(
                                        label: Text(
                                          'FS',
                                          style: TextStyle(fontSize: 10),
                                        ),
                                        padding: EdgeInsets.zero,
                                        visualDensity: VisualDensity.compact,
                                      ),
                                    IconButton(
                                      icon: const Icon(
                                        Icons.close,
                                        size: 16,
                                        color: Colors.redAccent,
                                      ),
                                      tooltip: 'Close surface',
                                      visualDensity: VisualDensity.compact,
                                      onPressed: () => _closeSurface(s.handle),
                                    ),
                                  ],
                                ),
                              ),
                            ),
                          ),
                        ],
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // 4. Wayland Window Management Controls Card
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Row(
                          children: [
                            Icon(Icons.dashboard_customize, color: Colors.cyan),
                            SizedBox(width: 8),
                            Text(
                              'Wayland Window Management (xdg_toplevel)',
                              style: TextStyle(
                                fontSize: 16,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 16),
                        Row(
                          children: [
                            Expanded(
                              child: TextField(
                                controller: _titleController,
                                decoration: const InputDecoration(
                                  labelText: 'Window Title',
                                  isDense: true,
                                  border: OutlineInputBorder(),
                                  prefixIcon: Icon(Icons.title),
                                ),
                              ),
                            ),
                            const SizedBox(width: 10),
                            FilledButton(
                              onPressed: _updateTitle,
                              child: const Text('Apply Title'),
                            ),
                          ],
                        ),
                        const SizedBox(height: 16),
                        Wrap(
                          spacing: 12,
                          runSpacing: 10,
                          children: [
                            OutlinedButton.icon(
                              icon: Icon(
                                _isFullscreen
                                    ? Icons.fullscreen_exit
                                    : Icons.fullscreen,
                              ),
                              label: Text(
                                _isFullscreen
                                    ? 'Exit Fullscreen'
                                    : 'Enter Fullscreen',
                              ),
                              onPressed: _toggleFullscreen,
                            ),
                            OutlinedButton.icon(
                              icon: Icon(
                                _isMaximized
                                    ? Icons.filter_none
                                    : Icons.crop_square,
                              ),
                              label: Text(
                                _isMaximized ? 'Unmaximize' : 'Maximize',
                              ),
                              onPressed: _toggleMaximized,
                            ),
                            OutlinedButton.icon(
                              icon: const Icon(Icons.minimize),
                              label: const Text('Minimize Window'),
                              onPressed: _minimize,
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // 4. Keyboard & Clipboard Integration Card
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Row(
                          children: [
                            Icon(Icons.keyboard, color: Colors.cyan),
                            SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                'Keyboard & Clipboard (textinput & platform)',
                                style: TextStyle(
                                  fontSize: 16,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 14),
                        TextField(
                          controller: _textController,
                          decoration: const InputDecoration(
                            hintText: 'Type here to test Wayland xkbcommon keyboard input...',
                            border: OutlineInputBorder(),
                            prefixIcon: Icon(Icons.edit),
                          ),
                        ),
                        const SizedBox(height: 12),
                        Row(
                          children: [
                            OutlinedButton.icon(
                              icon: const Icon(Icons.copy),
                              label: const Text('Copy'),
                              onPressed: _copyText,
                            ),
                            const SizedBox(width: 10),
                            OutlinedButton.icon(
                              icon: const Icon(Icons.paste),
                              label: const Text('Paste'),
                              onPressed: _pasteText,
                            ),
                            const SizedBox(width: 16),
                            Text(
                              _clipboardStatus,
                              style: TextStyle(
                                color: Colors.cyan.shade300,
                                fontStyle: FontStyle.italic,
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 20),

                // 5. Exit App Button
                Center(
                  child: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.red.shade900,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 24,
                        vertical: 12,
                      ),
                    ),
                    icon: const Icon(Icons.power_settings_new),
                    label: const Text(
                      'Quit Application (SystemNavigator.pop)',
                      style: TextStyle(fontWeight: FontWeight.bold),
                    ),
                    onPressed: _closeApp,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _InfoChip extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;
  final Color? color;

  const _InfoChip({
    required this.label,
    required this.value,
    required this.icon,
    this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: const Color(0xFF21262D),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: Colors.white10),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 18, color: color ?? Colors.cyanAccent),
            const SizedBox(width: 8),
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: const TextStyle(fontSize: 10, color: Colors.grey),
                ),
                Text(
                  value,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.bold,
                    color: color ?? Colors.white,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
