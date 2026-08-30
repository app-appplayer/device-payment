/// A barrier at a small surface car park.
///
///   dart run bin/parking.dart --http --port 8942
///
/// The metered shape. The barrier lifts on the way in, counts, and settles the
/// excess at the closing proximity event — the tap on the way out. If that tap
/// never comes, the service settles periodically on elapsed time with neither
/// the barrier nor any phone involved.
///
/// This is also the node that shows why the channel is not the product: a
/// driver does not lower a window to scan a printed code, so this one is
/// reached over BLE while the machine is otherwise identical to the laundry.
library;

import 'package:device_payment_nodes/node_kit.dart';

Future<void> main(List<String> args) => serve(
      const NodeDeclaration(
        identity: NodeIdentity(
          deviceId: 'prk-mangwon-gate1',
          name: 'Mangwon car park — gate 1',
          operator: 'Mangwon public parking',
        ),
        seller: 'euid_svLR9zNzvMlm4Db0tuEa',
        channel: 'BLE',
        summary: '32 bays, open air. Gate 1 is the only way in and out.',
        entryCode: 'SP-QR-PRKG1M9',
        status: 'Open · 11 bays free',
        offers: [
          Offer(
            id: 'parking-hourly',
            title: 'Park now',
            detail: 'The barrier lifts and starts counting. Tap again on the '
                'way out and only the time you used is settled.',
            performance: Performance.metered,
            price: '1,200 KRW / 30 min',
            meterUnit: 'half hours',
          ),
          Offer(
            id: 'parking-flat-day',
            title: 'All day, flat',
            detail: 'One price until midnight. Cheaper than the meter past '
                'about five hours, and it never counts past that.',
            performance: Performance.interval,
            price: '12,000 KRW',
            duration: 'until midnight',
          ),
          Offer(
            id: 'parking-resident',
            title: 'Resident pass',
            detail: 'Renews monthly until released. The gate recognises the '
                'pass without a network — it checks the voucher it already '
                'holds.',
            performance: Performance.renewing,
            price: '90,000 KRW / month',
            duration: 'each month',
          ),
        ],
      ),
      args,
    );
