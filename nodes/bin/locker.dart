/// A left-luggage locker at a station.
///
///   dart run bin/locker.dart --http --port 8941
///
/// Two performances in one machine, and the difference between them is the
/// whole reason the platform talks about *intervals* rather than moments:
///
///   - by the hour or by the day — the authority runs out and the locker stops
///     opening, with nothing sent to it to make that happen;
///   - monthly — the authority renews each period on its own until someone
///     releases it, with no further act by the renter.
///
/// The locker decides both locally. It has no clock of its own beyond the time
/// a phone hands it at each tap.
library;

import 'package:device_payment_nodes/node_kit.dart';

Future<void> main(List<String> args) => serve(
      const NodeDeclaration(
        identity: NodeIdentity(
          deviceId: 'lkr-hongdae-b12',
          name: 'Locker B12',
          operator: 'Hongdae Station storage',
        ),
        seller: 'euid_svLR9zNzvMlm4Db0tuEa',
        channel: 'QR',
        summary: 'Medium locker, 45 × 40 × 60 cm. Exit 3 concourse.',
        entryCode: 'SP-QR-LKRB12C4',
        status: 'Free · 6 of 24 lockers in this bank are taken',
        offers: [
          Offer(
            id: 'locker-4h',
            title: '4 hours',
            detail: 'Opens as often as you like until it runs out. It stops '
                'opening on its own — nothing is sent to the locker to end it.',
            performance: Performance.interval,
            price: '3,000 KRW',
            duration: 'until 4 hours from now',
          ),
          Offer(
            id: 'locker-24h',
            title: 'One day',
            detail: 'Same locker, until this time tomorrow. Left longer than '
                'that and the overstay is settled when you come back.',
            performance: Performance.interval,
            price: '6,000 KRW',
            duration: 'until this time tomorrow',
          ),
          Offer(
            id: 'locker-monthly',
            title: 'Monthly',
            detail: 'Keeps renewing each month until you release it. You do '
                'nothing in between, and releasing is never conditioned on '
                'what you owe.',
            performance: Performance.renewing,
            price: '45,000 KRW / month',
            duration: 'each month',
          ),
        ],
      ),
      args,
    );
