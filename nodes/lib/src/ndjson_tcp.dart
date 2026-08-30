/// Newline-delimited JSON-RPC over a raw TCP socket — the binding an embedded
/// board speaks (`specs/platform/17-device-discovery.md` §3, `proto=ndjson`).
///
/// A host reaches a board by dialling `tcp://host:port` and framing every
/// message as one UTF-8 line. Serving that same framing from a sample node is
/// what lets these samples be opened the way a board is opened, on a laptop,
/// with no hardware in the room.
///
/// Written here rather than imported: the framing is small and fixed, and the
/// only other copy lives inside a recipe's `src/`, which is not ours to reach
/// into.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:mcp_server/mcp_server.dart';

/// One line is one message. Chunk boundaries carry no meaning, so a line may
/// span many chunks and a chunk may hold many lines; buffering is at the byte
/// level so a multi-byte character split across chunks survives.
class _Framer {
  _Framer(this._onMessage, this._onError);

  static const int _newline = 0x0A;

  final void Function(dynamic message) _onMessage;
  final void Function(Object error, StackTrace stack) _onError;
  final List<int> _buffer = <int>[];

  void feed(List<int> chunk) {
    _buffer.addAll(chunk);
    int at;
    while ((at = _buffer.indexOf(_newline)) != -1) {
      final bytes = _buffer.sublist(0, at);
      _buffer.removeRange(0, at + 1);
      final line = utf8.decode(bytes, allowMalformed: true).trim();
      if (line.isEmpty) continue;
      try {
        _onMessage(jsonDecode(line));
      } catch (error, stack) {
        _onError(error, stack);
      }
    }
  }

  static List<int> encode(dynamic message) =>
      utf8.encode('${jsonEncode(message)}\n');
}

/// Serves one MCP session over a TCP socket.
///
/// One client at a time, which is what a board does — it has one link and a
/// second caller waits. Accepting a second connection here would let a sample
/// answer two sessions at once and quietly stop resembling the thing it is
/// standing in for.
class NdjsonTcpServerTransport implements ServerTransport {
  NdjsonTcpServerTransport._(this._server);

  /// Binds and returns before accepting, so the caller can announce the port
  /// before the first byte arrives. Loopback only: this node has no
  /// authentication, and a routable address would put it on the network of
  /// whoever is nearby.
  static Future<NdjsonTcpServerTransport> bind(int port) async {
    final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, port);
    return NdjsonTcpServerTransport._(server);
  }

  final ServerSocket _server;
  final _messages = StreamController<dynamic>.broadcast();
  final _closed = Completer<void>();

  Socket? _socket;
  StreamSubscription<Socket>? _accepts;
  late final _Framer _framer = _Framer(
    _messages.add,
    _messages.addError,
  );

  /// Starts accepting.
  void start() {
    _accepts = _server.listen((socket) {
      if (_socket != null) {
        // Busy. Closed rather than queued: a caller told nothing is
        // indistinguishable from a node that hung.
        socket.destroy();
        return;
      }
      _socket = socket;
      socket.listen(
        _framer.feed,
        onError: _messages.addError,
        onDone: () => _socket = null,
        cancelOnError: false,
      );
    });
  }

  @override
  Stream<dynamic> get onMessage => _messages.stream;

  @override
  Future<void> get onClose => _closed.future;

  @override
  void send(dynamic message) {
    final socket = _socket;
    // A reply with nobody to receive it is dropped rather than thrown: a link
    // dropping mid-session is ordinary for a board, and it is not this node's
    // failure.
    if (socket == null) return;
    socket.add(_Framer.encode(message));
  }

  @override
  void close() {
    if (_closed.isCompleted) return;
    _accepts?.cancel();
    _socket?.destroy();
    _socket = null;
    _server.close();
    _closed.complete();
    if (!_messages.isClosed) _messages.close();
  }
}
