# nodes — 노드 샘플

**노드는 별도 물건이 아니다. MCP server 이고, 따라서 앱이다.** 여기 다섯은 그 문장을 코드로
세운 것이다. 각각 실제로 뜨고, 호스트가 열면 화면이 나오고, 그 화면을 촬영할 수 있다.

샘플이 하는 일은 **선언**이다 — 자기가 무엇을 어떤 조건에 제공하는지 진술한다. 위젯을 쓰지
않는다. 조건을 쓰면 킷(`lib/node_kit.dart`)이 그것을 UI DSL 로 편다. 특허가 첫 단계라고
부르는 것이 바로 이 모양이다.

## 다섯

| 노드 | 무엇 | 수행 유형 | 채널 | 포트 |
|---|---|---|---|---|
| `laundry` | 무인 빨래방 세탁기 | **1회** 수행 후 종료 | QR | 8940 |
| `locker` | 역 물품보관함 | **구간**(4시간·1일) + **갱신**(월) | QR | 8941 |
| `parking` | 주차장 차단기 | **계량** → 출차 시 정산 + 구간 + 갱신 | BLE | 8942 |
| `charger` | 노변 EV 충전기 | **계량**(kWh) + 고객 입력 금액 + 구간 | Wi-Fi | 8943 |
| `gate` | 기계실 출입문 | **정족수 · 결제 없음** | NFC | 8944 |

다섯이 **수행 유형을 하나씩 맡는다.** 같은 화면을 다섯 벌 만든 것이 아니라, 특허가 열거한
수행의 형태들을 각각 현실 기계 하나로 앉힌 것이다.

`gate` 가 특히 그렇다 — 결제를 뺀 같은 플랫폼이다. 신원·선언·국소 판정·감사가 그대로 있고
돈만 없다. **결제는 상위 등급 노드의 한 동작이지 플랫폼의 목적이 아니라는 것**을 이 노드
하나가 말한다.

## 돌리는 법

```sh
cd device-payment/nodes
dart pub get

dart run bin/locker.dart --http --port 8941    # streamable HTTP
dart run bin/locker.dart --tcp  --port 8961    # ndjson over TCP — 보드 와이어
dart run bin/locker.dart                        # stdio
```

`--tcp` 는 임베디드 보드가 쓰는 그 계약이다(`specs/platform/17-device-discovery.md` §3,
`proto=ndjson`: UTF-8 · `\n` 종단 · 한 줄이 한 메시지). 호스트는 `tcp://127.0.0.1:8961` 로
걸고, 그 다이얼은 `ble://`·`serial://` 보드가 지나가는 것과 **같은 확장 스킴 커넥터**다.

호스트에서 열려면 진입 대상을 `server` 로 두고 `http://127.0.0.1:8941/mcp` 를 가리키면 된다.
`integration_test/node_samples_live_test.dart`(Pro) 가 정확히 그 경로로 다섯을 전부 연다.

**포트를 겹치지 마라.** 겹치면 오류가 아니라 *조용히* 어긋난다 — 두 번째 bind 가 실패하고
측정하는 쪽은 먼저 잡은 노드를 잰다. 실제로 한 번 당했고, 그래서 노드마다 포트가 다르다.

## 검증

| 무엇 | 어디 | 최근 |
|---|---|---|
| 문서 불변식(상태 초기값·금액 출처·판매자/품목·오류 경로) | `test/declaration_test.dart` | **22 PASS** |
| 실제 렌더(HTTP) + **보드 와이어(tcp://) 재확인** | `appplayer_pro/dart/integration_test/node_samples_live_test.dart` | **35 PASS** |

렌더 검사가 있는 이유는 이 화면들이 **키노트·매거진·촬영에 나가기 때문**이다. 문서가 검증에
걸리면 화면이 곱게 나빠지지 않는다 — 아예 안 열린다. 리허설에서 알게 될 일이 아니다.

## 촬영용 캡처

```sh
cd os/appplayer/appplayer_pro/dart
CAPTURE=1 flutter test integration_test/node_samples_live_test.dart -d macos
```

`shots/<node>.png` 다섯 장이 나온다(1180×2200, @2x). 기본값은 꺼져 있다 — 매번 파일을 쓰면
측정하는 트리를 측정이 바꾼다.

## 정직하게 적어 두는 것

- **어느 노드도 키를 갖고 있지 않다.** 화면의 신원 줄이 `declared, not signed` 라고 말한다.
  서명된 것처럼 보이는 데모가 가장 비싼 종류의 데모다.
- **`channel` 필드는 라벨이다.** 화면에 찍힐 뿐 전송을 고르지 않는다. 실제로 밟은 전송은
  **둘** — streamable HTTP 와 **ndjson over TCP(보드 와이어)** 다. 나머지(BLE·serial·NFC)는
  하드웨어가 있어야 밟는다.

  선언을 한 줄도 고치지 않고 두 전송에서 같은 화면이 나왔다. 그것이 「채널은 제품 축이 아니라
  전송」의 실물 형태다.
- **결제 표면은 여기서 열리지 않는다.** 데스크톱에서 티어는 표면이 없어 설계대로 거절한다.
  그 거절이 문서에 닿는 것까지가 여기서 볼 수 있는 것이다.
