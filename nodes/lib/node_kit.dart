/// A node declares what it offers and under what conditions. That declaration
/// is the product — everything else here is the machinery that puts it on a
/// wire.
///
/// The platform's own words: *a node is not a separate object. It is an MCP
/// server, and therefore an app.* So a node sample is a server that answers
/// `ui://app` with a document a host can render, and payment is one action
/// inside it rather than a mode the host enters.
///
/// The patent this axis rests on calls this first stage **declaration** — the
/// device states an identity claim and a specification of the actions it
/// offers with their conditions. This file builds exactly that specification
/// and renders it, so a node author writes conditions, not widgets.
library;

import 'dart:convert';
import 'dart:io';

import 'package:mcp_server/mcp_server.dart';

import 'src/ndjson_tcp.dart';

/// How an action, once authorised, is carried out. These are the shapes the
/// patent enumerates as dependent forms of the performance step, and each
/// sample node in `bin/` exists to make one of them concrete.
enum Performance {
  /// Runs once and ends. A wash cycle.
  once,

  /// Runs for the authorised interval and stops when it expires. A locker
  /// rented until 18:00.
  interval,

  /// Enters a state and stays there, the authority renewing each period with
  /// no further act by the user, until a release event. A monthly locker.
  renewing,

  /// Measures elapsed time or quantity during the interval and reports it at
  /// the closing proximity event, where the excess is settled. Parking, or a
  /// charger billed by kWh.
  metered,

  /// Performed only when a policy inside the device is satisfied by several
  /// parties. No payment is involved at all.
  quorum;

  String get label => switch (this) {
        Performance.once => 'one run, then done',
        Performance.interval => 'runs for the authorised interval',
        Performance.renewing => 'renews until released',
        Performance.metered => 'measured, settled when you leave',
        Performance.quorum => 'needs more than one authority',
      };
}

/// What the node says it is.
///
/// The identity claim is carried here because the declaration is meaningless
/// without it — an offer from nobody cannot be bound to anything. On these
/// samples it is **declared and not signed**: no node in this repository holds
/// a private key yet, and a sample that looked signed would be the most
/// expensive kind of demo. `signed` stays false until an actual secure element
/// answers a challenge.
class NodeIdentity {
  const NodeIdentity({
    required this.deviceId,
    required this.name,
    required this.operator,
    this.signed = false,
  });

  /// Stable per physical unit. A server-side mapping turns this into an
  /// operator profile, which is why the node itself stores no operator
  /// identifier and no credential.
  final String deviceId;

  /// What a person reading the screen should see.
  final String name;

  /// Shown to the customer, resolved from [deviceId] on the service side. Kept
  /// here only so the sample can print it; the node does not decide it.
  final String operator;

  /// Whether the identity claim carries a signature from a key the node holds.
  /// False on every sample in this package.
  final bool signed;
}

/// One thing the node offers, and the conditions attached to it.
class Offer {
  const Offer({
    required this.id,
    required this.title,
    required this.detail,
    required this.performance,
    required this.price,
    this.amountFromCustomer = false,
    this.duration,
    this.meterUnit,
  });

  /// The seller-side item this maps to. The document never carries a price for
  /// it: the seller priced it, and an amount sent from here would be ignored.
  final String id;

  final String title;

  /// The condition, in the words a customer needs. Not marketing copy — the
  /// thing they are agreeing to.
  final String detail;

  final Performance performance;

  /// Shown, not sent. Display only, so the screen can be read on its own.
  final String price;

  /// True when the customer types the amount (a tip, a top-up). Then and only
  /// then does the document send one.
  final bool amountFromCustomer;

  /// For [Performance.interval] and [Performance.renewing].
  final String? duration;

  /// For [Performance.metered] — what is counted.
  final String? meterUnit;
}

/// The whole declaration: who I am, what I offer, how you reached me.
class NodeDeclaration {
  const NodeDeclaration({
    required this.identity,
    required this.seller,
    required this.offers,
    required this.channel,
    required this.summary,
    this.entryCode,
    this.status,
  });

  final NodeIdentity identity;

  /// The seller the authorisation is obtained for. A node holds no credential
  /// — this is a reference the service resolves.
  final String seller;

  final List<Offer> offers;

  /// How a person arrived: `QR`, `NFC`, `BLE`, `Wi-Fi`. The transport is not a
  /// product axis; the same node changes channels.
  final String channel;

  /// One line under the title. What this machine is.
  final String summary;

  /// The code printed on the machine, when there is one. Rendered as an actual
  /// QR on the page so the sample can be filmed without extra props.
  final String? entryCode;

  /// A line of live machine state — free slots, power delivered, whatever this
  /// machine knows about itself.
  final String? status;
}

/// Serves [declaration] as an application a host can open.
///
///   dart run bin/<node>.dart                     # stdio
///   dart run bin/<node>.dart --http --port 8940  # http, the shape a host dials
///   dart run bin/<node>.dart --tcp  --port 8950  # ndjson over TCP, the board wire
///
/// Both, because hosts differ in how they reach a server, not in what they
/// find there.
Future<void> serve(NodeDeclaration declaration, List<String> args) async {
  final config = McpServerConfig(
    name: declaration.identity.name,
    version: '1.0.0',
    capabilities: const ServerCapabilities(
      resources: ResourcesCapability(listChanged: true),
      logging: LoggingCapability(),
    ),
    enableDebugLogging: false,
  );

  final server = McpServer.createServer(config);
  final application = _application(declaration);
  final page = _page(declaration);

  server.addResource(
    uri: 'ui://app',
    name: declaration.identity.name,
    description: declaration.summary,
    mimeType: 'application/json',
    handler: (uri, params) async => _json(uri, application),
  );

  server.addResource(
    uri: 'ui://app/info',
    name: 'App Info',
    description: 'Lightweight application metadata',
    mimeType: 'application/json',
    handler: (uri, params) async => _json(uri, _info(declaration)),
  );

  server.addResource(
    uri: 'ui://pages/main',
    name: 'main',
    description: 'What this node offers',
    mimeType: 'application/json',
    handler: (uri, params) async => _json(uri, page),
  );

  // A board is reached over `tcp://host:port` with newline-delimited JSON-RPC.
  // Serving that here is what lets a sample be opened exactly the way a board
  // is opened — same host path, same dial, no hardware.
  if (args.contains('--tcp')) {
    final transport = await NdjsonTcpServerTransport.bind(_portFrom(args));
    server.connect(transport);
    transport.start();
    stderr.writeln('listening tcp://127.0.0.1:${_portFrom(args)}');
    await ProcessSignal.sigint.watch().first;
    exit(0);
  }

  if (!args.contains('--http')) {
    final transport = McpServer.createStdioTransport().get();
    server.connect(transport);
    await transport.onClose;
    exit(0);
  }

  final port = _portFrom(args);
  final transport = (await McpServer.createStreamableHttpTransportAsync(
    port,
    host: '127.0.0.1',
    endpoint: '/mcp',
    isJsonResponseEnabled: true,
  ))
      .get();
  server.connect(transport);
  // On stderr and flushed: a harness that waits for this line needs it before
  // it dials, and a line still sitting in a buffer is a harness that hangs.
  stderr.writeln('listening http://127.0.0.1:$port/mcp');
  await ProcessSignal.sigint.watch().first;
  exit(0);
}

/// Bound to loopback. These servers have no authentication, and a routable
/// address would put a stranger's machine screen on the local network.
int _portFrom(List<String> args) {
  final i = args.indexOf('--port');
  if (i < 0 || i + 1 >= args.length) return 8940;
  return int.tryParse(args[i + 1]) ?? 8940;
}

Map<String, dynamic> _application(NodeDeclaration d) => {
      'type': 'application',
      'version': '1.4',
      'title': d.identity.name,
      'initialRoute': '/',
      'routes': {'/': 'ui://pages/main'},
      'state': {
        'initial': {
          'seller': d.seller,
          // Not empty: an empty result card reads as a broken screen, and
          // this one is on camera before anyone has pressed anything.
          'last': 'nothing yet',
          'code': '',
          'amount': '',
        },
      },
    };

Map<String, dynamic> _info(NodeDeclaration d) => {
      'id': 'com.makemind.node.${d.identity.deviceId}',
      'name': d.identity.name,
      'version': '1.0.0',
      'description': d.summary,
      'publisher': d.identity.operator,
    };

ReadResourceResult _json(String uri, Map<String, dynamic> body) =>
    ReadResourceResult(contents: [
      ResourceContentInfo(
        uri: uri,
        mimeType: 'application/json',
        text: jsonEncode(body),
      ),
    ]);

/// The screen. Built from the declaration rather than authored per node, so a
/// node author writes conditions and gets a page — which is the point being
/// demonstrated: the machine states its terms, and the host renders them.
Map<String, dynamic> _page(NodeDeclaration d) => {
      'type': 'page',
      'title': d.identity.name,
      'content': {
        'type': 'singleChildScrollView',
        'child': {
          'type': 'linear',
          'direction': 'vertical',
          'padding': 20,
          'spacing': 16,
          'children': [
            _heading(d),
            if (d.entryCode != null) _codeCard(d),
            if (d.status != null) _statusCard(d),
            ...d.offers.map((o) => _offerCard(d, o)),
            _resultCard(),
          ],
        },
      },
    };

Map<String, dynamic> _heading(NodeDeclaration d) => {
      'type': 'linear',
      'direction': 'vertical',
      'spacing': 4,
      'children': [
        {'type': 'text', 'text': d.identity.name, 'variant': 'headlineSmall'},
        {'type': 'text', 'text': d.summary, 'variant': 'bodyMedium'},
        {
          'type': 'text',
          // The operator is resolved from the device id on the service side.
          // Printed together so a viewer can see that the machine itself is
          // not the merchant.
          'text': '${d.identity.operator} · ${d.identity.deviceId} · '
              'arrived by ${d.channel}',
          'variant': 'bodySmall',
        },
        {
          'type': 'text',
          // Said plainly. These samples hold no key, and a demo that looked
          // signed would be the most expensive kind.
          'text': d.identity.signed
              ? 'identity claim — signed by the node'
              : 'identity claim — declared, not signed (no key on this sample)',
          'variant': 'labelSmall',
        },
      ],
    };

Map<String, dynamic> _codeCard(NodeDeclaration d) => {
      'type': 'card',
      'child': {
        'type': 'linear',
        'direction': 'vertical',
        'padding': 16,
        'spacing': 10,
        'children': [
          {
            'type': 'text',
            'text': 'The code on the machine',
            'variant': 'labelLarge',
          },
          {
            'type': 'center',
            'child': {
              'type': 'qrCode',
              'value': d.entryCode,
              'size': 168,
              'errorCorrection': 'M',
            },
          },
          {'type': 'text', 'text': d.entryCode, 'variant': 'bodySmall'},
          {
            'type': 'text',
            'text': 'The same code reaches this machine however it is carried '
                '— printed, tapped or advertised. The channel is transport, '
                'not a different product.',
            'variant': 'bodySmall',
          },
        ],
      },
    };

Map<String, dynamic> _statusCard(NodeDeclaration d) => {
      'type': 'card',
      'child': {
        'type': 'linear',
        'direction': 'vertical',
        'padding': 16,
        'spacing': 4,
        'children': [
          {'type': 'text', 'text': 'Right now', 'variant': 'labelLarge'},
          {'type': 'text', 'text': d.status, 'variant': 'bodyMedium'},
        ],
      },
    };

Map<String, dynamic> _offerCard(NodeDeclaration d, Offer o) => {
      'type': 'card',
      'child': {
        'type': 'linear',
        'direction': 'vertical',
        'padding': 16,
        'spacing': 8,
        'children': [
          {
            'type': 'linear',
            'direction': 'horizontal',
            'spacing': 12,
            'children': [
              {
                'type': 'expanded',
                'child': {
                  'type': 'text',
                  'text': o.title,
                  'variant': 'titleMedium',
                },
              },
              {'type': 'text', 'text': o.price, 'variant': 'titleMedium'},
            ],
          },
          {'type': 'text', 'text': o.detail, 'variant': 'bodySmall'},
          {
            'type': 'text',
            'text': _conditionLine(o),
            'variant': 'labelSmall',
          },
          if (o.amountFromCustomer) ...[
            {
              'type': 'textField',
              'label': 'Amount',
              'value': '{{amount}}',
              'onChange': {
                'type': 'state',
                'action': 'set',
                'binding': 'amount',
                'value': '{{event.value}}',
              },
            },
          ],
          {
            'type': 'button',
            // The quorum node has nothing to pay, so the button carries the
            // act itself. Prefixing a verb here produced 'Request Open the
            // door', which reads as a typo on a screen someone is filming.
            'label': switch (o) {
              // Nothing to pay: the button carries the act itself. A verb
              // prefix here produced 'Request Open the door'.
              _ when o.performance == Performance.quorum => o.title,
              // The price is in the field, not on the button — 'Pay · you
              // choose' is not a sentence.
              _ when o.amountFromCustomer => 'Pay the amount above',
              _ => 'Pay · ${o.price}',
            },
            'onTap': _payment(o),
          },
        ],
      },
    };

/// The condition, spelled out. The performance kind is not jargon on the
/// screen — a person renting a locker needs to know it stops at 18:00, not
/// that this is `Performance.interval`.
String _conditionLine(Offer o) {
  final parts = <String>[o.performance.label];
  if (o.duration != null) parts.add(o.duration!);
  if (o.meterUnit != null) parts.add('counted in ${o.meterUnit}');
  return parts.join(' · ');
}

/// A payment action, or — for the quorum node — nothing to pay for at all.
///
/// The amount is sent only where the customer typed one. A seller-priced item
/// that sent an amount would have it ignored, so sending one is a lie the
/// screen tells its reader.
Map<String, dynamic> _payment(Offer o) {
  if (o.performance == Performance.quorum) {
    return {
      'type': 'state',
      'action': 'set',
      'binding': 'last',
      'value': '${o.title} — waiting for the second authority',
    };
  }
  return {
    'type': 'payment',
    'seller': '{{seller}}',
    'itemId': o.id,
    if (o.amountFromCustomer) 'amount': '{{amount}}',
    'onSuccess': {
      'type': 'state',
      'action': 'set',
      'binding': 'last',
      'value': '${o.title} — authorised ({{event.status}})',
    },
    'onError': {
      'type': 'batch',
      'actions': [
        {
          'type': 'state',
          'action': 'set',
          'binding': 'last',
          'value': '${o.title} — {{event.message}}',
        },
        {
          'type': 'state',
          'action': 'set',
          'binding': 'code',
          'value': '{{event.code}}',
        },
      ],
    },
  };
}

/// What came back. Kept as its own card because in a demo the outcome is the
/// thing the camera has to be able to read.
Map<String, dynamic> _resultCard() => {
      'type': 'card',
      'child': {
        'type': 'linear',
        'direction': 'vertical',
        'padding': 16,
        'spacing': 4,
        'children': [
          {'type': 'text', 'text': 'Last result', 'variant': 'labelLarge'},
          {
            'type': 'text',
            'text': '{{last}}',
            'variant': 'bodyMedium',
          },
          {
            'type': 'conditional',
            'condition': '{{code}}',
            'then': {
              'type': 'text',
              'text': 'code: {{code}}',
              'variant': 'bodySmall',
            },
          },
        ],
      },
    };
