/// The documents these nodes serve are validated when a host loads them, so a
/// shape that drifts does not degrade — the screen does not open at all. These
/// pin the parts a node author can get wrong.
library;

import 'dart:convert';
import 'dart:io';

import 'package:async/async.dart';

import 'package:test/test.dart';

/// Runs a node over stdio and asks it for a resource, which is the same path a
/// host takes. Reading the document out of the running server rather than
/// calling a helper is deliberate: what a host sees is what is on the wire.
Future<Map<String, dynamic>> read(String node, String uri) async {
  final p = await Process.start('dart', ['run', 'bin/$node.dart']);
  final out = p.stdout.transform(utf8.decoder).transform(const LineSplitter());
  final replies = StreamQueue<String>(out);

  void send(Map<String, dynamic> m) => p.stdin.writeln(jsonEncode(m));

  send({
    'jsonrpc': '2.0',
    'id': 1,
    'method': 'initialize',
    'params': {
      'protocolVersion': '2025-06-18',
      'capabilities': <String, dynamic>{},
      'clientInfo': {'name': 'test', 'version': '1'},
    },
  });
  await replies.next;
  send({'jsonrpc': '2.0', 'method': 'notifications/initialized'});
  send({
    'jsonrpc': '2.0',
    'id': 2,
    'method': 'resources/read',
    'params': {'uri': uri},
  });

  late Map<String, dynamic> body;
  while (true) {
    final line = await replies.next;
    final m = jsonDecode(line) as Map<String, dynamic>;
    if (m['id'] != 2) continue;
    final contents = (m['result'] as Map)['contents'] as List;
    body = jsonDecode((contents.first as Map)['text'] as String)
        as Map<String, dynamic>;
    break;
  }
  p.kill();
  await replies.cancel();
  return body;
}

const nodes = ['laundry', 'locker', 'parking', 'charger', 'gate'];

void main() {
  for (final node in nodes) {
    group(node, () {
      test('serves an application a host can route', () async {
        final app = await read(node, 'ui://app');
        expect(app['type'], 'application');
        expect(app['version'], '1.4');
        expect((app['routes'] as Map)['/'], 'ui://pages/main');
        // Every binding the page reads must exist before the first frame, or
        // the text renders as the literal handlebars.
        final initial = ((app['state'] as Map)['initial'] as Map);
        expect(initial.keys, containsAll(['seller', 'last', 'code', 'amount']));
      });

      test('serves a page, and the page is a page', () async {
        final page = await read(node, 'ui://pages/main');
        expect(page['type'], 'page');
        expect(page['content'], isA<Map<String, dynamic>>());
      });

      test('an amount is sent only where the customer typed one', () async {
        final page = await read(node, 'ui://pages/main');
        final payments = _find(page, (m) => m['type'] == 'payment');
        for (final pay in payments) {
          if (pay.containsKey('amount')) {
            // The only legitimate source is the field the customer typed into.
            expect(pay['amount'], '{{amount}}');
            final fields = _find(page, (m) => m['type'] == 'textField');
            expect(
              fields.any((f) =>
                  (f['onChange'] as Map?)?['binding'] == 'amount'),
              isTrue,
              reason: 'an amount is sent with no field to type it in',
            );
          }
        }
      });

      test('every payment names a seller and an item', () async {
        final page = await read(node, 'ui://pages/main');
        for (final pay in _find(page, (m) => m['type'] == 'payment')) {
          expect(pay['seller'], '{{seller}}');
          expect(pay['itemId'], isA<String>());
          expect((pay['itemId'] as String).isNotEmpty, isTrue);
          // A refusal has to reach the document, or a host that declines is
          // indistinguishable from one that hung.
          expect(pay['onError'], isNotNull);
        }
      });
    });
  }

  test('the gate charges nothing at all', () async {
    final page = await read('gate', 'ui://pages/main');
    expect(_find(page, (m) => m['type'] == 'payment'), isEmpty,
        reason: 'the no-payment node must not carry a payment action');
  });

  test('a node that takes payment has at least one', () async {
    for (final node in nodes.where((n) => n != 'gate')) {
      final page = await read(node, 'ui://pages/main');
      expect(_find(page, (m) => m['type'] == 'payment'), isNotEmpty,
          reason: '$node offers nothing to authorise');
    }
  });
}

/// Every map in the tree matching [where].
List<Map<String, dynamic>> _find(
  Object? node,
  bool Function(Map<String, dynamic>) where,
) {
  final found = <Map<String, dynamic>>[];
  void walk(Object? n) {
    if (n is Map<String, dynamic>) {
      if (where(n)) found.add(n);
      n.values.forEach(walk);
    } else if (n is List) {
      n.forEach(walk);
    }
  }

  walk(node);
  return found;
}
