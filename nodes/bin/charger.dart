/// A kerbside EV charger.
///
///   dart run bin/charger.dart --http --port 8943
///
/// Metered by quantity rather than by time, and the quantity is the one the
/// machine measured — not an estimate made anywhere else. The excess over the
/// pre-authorised amount is settled at the closing event, and a session
/// abandoned without one is settled by the service on elapsed energy.
///
/// It is on Wi-Fi because a charge post usually has power and a network
/// already. That changes which channel is chosen and nothing else.
library;

import 'package:device_payment_nodes/node_kit.dart';

Future<void> main(List<String> args) => serve(
      const NodeDeclaration(
        identity: NodeIdentity(
          deviceId: 'chg-seogyo-04',
          name: 'Charge post 4',
          operator: 'Seogyo-dong kerbside charging',
        ),
        seller: 'euid_svLR9zNzvMlm4Db0tuEa',
        channel: 'Wi-Fi',
        summary: '7 kW AC, Type 2. Bay 4, nearest the corner.',
        entryCode: 'SP-QR-CHG04SG',
        status: 'Available · post 3 is in use, 22 minutes remaining',
        offers: [
          Offer(
            id: 'charge-metered',
            title: 'Charge and go',
            detail: 'Starts now and counts what it delivers. Unplug when you '
                'like — the reading at that moment is what is settled.',
            performance: Performance.metered,
            price: '340 KRW / kWh',
            meterUnit: 'kWh',
          ),
          Offer(
            id: 'charge-topup',
            title: 'Top up a set amount',
            detail: 'Type what you want to spend and it stops there. Useful '
                'when the car is not yours.',
            performance: Performance.metered,
            price: 'you choose',
            amountFromCustomer: true,
            meterUnit: 'kWh',
          ),
          Offer(
            id: 'charge-overnight',
            title: 'Overnight, flat',
            detail: 'From now until 07:00 for one price, however much it '
                'takes. The post stops charging when the interval ends.',
            performance: Performance.interval,
            price: '9,900 KRW',
            duration: 'until 07:00',
          ),
        ],
      ),
      args,
    );
