/// A door to a plant room, opened by two people and no money.
///
///   dart run bin/gate.dart --http --port 8944
///
/// The same platform with the payment step removed. The door holds a policy —
/// a quorum among authorised identities and a time window — and decides for
/// itself, with no server in the loop, that the policy has been met. The two
/// taps do not have to be at the same moment; the first signature waits inside
/// the door until the second arrives or the window closes.
///
/// This node exists to make one point: *payment is one action on a node, not
/// what the platform is for.* Everything else here — identity, declaration,
/// local decision, audit — is unchanged.
///
/// NFC, because a gloved hand at a door is the case tapping was made for.
library;

import 'package:device_payment_nodes/node_kit.dart';

Future<void> main(List<String> args) => serve(
      const NodeDeclaration(
        identity: NodeIdentity(
          deviceId: 'gat-b2-plant',
          name: 'B2 plant room',
          operator: 'Facilities, Seorin building',
        ),
        seller: '',
        channel: 'NFC',
        summary: 'Two authorised people, within ten minutes of each other.',
        entryCode: 'SP-QR-GATB2PL',
        status: 'Locked · no signature held',
        offers: [
          Offer(
            id: 'gate-open',
            title: 'Open the door',
            detail: 'Needs a second authorised person within ten minutes. The '
                'door decides this itself and writes who took part into its '
                'own log — no network is involved at any point.',
            performance: Performance.quorum,
            price: 'no charge',
            duration: 'ten minute window',
          ),
          Offer(
            id: 'gate-inspection',
            title: 'Open for inspection',
            detail: 'Same door, stricter policy: one of the two must hold the '
                'inspector role, and the order is enforced.',
            performance: Performance.quorum,
            price: 'no charge',
            duration: 'ten minute window · role required',
          ),
        ],
      ),
      args,
    );
