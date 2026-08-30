/// A washing machine in an unstaffed laundromat.
///
///   dart run bin/laundry.dart --http --port 8940
///
/// The simplest performance there is: one run, then done. Nothing renews and
/// nothing is counted, so nothing has to be settled afterwards — which is why
/// this is the node to start from when explaining the platform.
///
/// A machine like this has no screen, no card reader and no wide-area link.
/// It has a printed code on the lid.
library;

import 'package:device_payment_nodes/node_kit.dart';

Future<void> main(List<String> args) => serve(
      const NodeDeclaration(
        identity: NodeIdentity(
          deviceId: 'wsh-2f-07',
          name: 'Washer 7',
          operator: 'Haneul Laundry, Yeonhui-dong',
        ),
        seller: 'euid_svLR9zNzvMlm4Db0tuEa',
        channel: 'QR',
        summary: 'Second floor, by the window. Drum 15 kg.',
        entryCode: 'SP-QR-WSH2F07A1',
        status: 'Idle · last cycle finished 22 minutes ago',
        offers: [
          Offer(
            id: 'laundry-standard',
            title: 'Standard wash',
            detail: 'Warm water, 42 minutes. The door unlocks when it ends.',
            performance: Performance.once,
            price: '4,000 KRW',
          ),
          Offer(
            id: 'laundry-bedding',
            title: 'Bedding wash',
            detail: 'Longer cycle with an extra rinse, 68 minutes. Duvets up '
                'to 10 kg.',
            performance: Performance.once,
            price: '7,000 KRW',
          ),
          Offer(
            id: 'laundry-dry-30',
            title: 'Dryer, 30 minutes',
            detail: 'Runs the drum next to this one. Add more time before it '
                'stops and the heat is never interrupted.',
            performance: Performance.once,
            price: '3,000 KRW',
          ),
        ],
      ),
      args,
    );
