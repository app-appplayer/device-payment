# 샘플 — 어디 있고 무엇이 그것을 모는가

옮기지 않았다. 각 샘플은 자기 자리에서 자기 소비자에게 쓰인다 — 여기 사본을 두면
**갈라지고, 갈라진 사본이 초록이면 아무 말도 하지 않는다.**

| 샘플 | 위치 | 무엇인가 | 이것을 모는 테스트 |
|---|---|---|---|
| 결제 데모 서버 | `apps/payment_demo_server` | **결제 노드.** `payment` 액션이 있는 `ui://` 문서를 서빙하는 실 MCP 서버. `--http --port` 로 루프백 바인딩 | `entry_to_payment_live` (8931) · `payment_demo_app_live` |
| 결제 데모 번들 | `apps/payment_demo.mbd` | 같은 화면을 **번들 소스**로 담은 것 | `payment_demo_app_live` — 서버 소스와 같은 시나리오를 돌려 「소스 방식은 결제 계약과 무관」을 증명 |
| capability probe 번들 | `os/core/appplayer/dart/tool/capability_probe/` 가 **생성** | 선언한 능력을 전부 한 페이지에서 실행. 자산(WAV·PDF·Lottie)은 체크인 안 하고 생성 | `verify.py` |
| 스펙 식 코퍼스 | `capability_probe/spec_expression_corpus.json` | 스펙이 자기 본문에 적어 둔 식들 | `run_corpus.py` + 런타임 유닛 짝(`test/spec/spec_expressions_test.dart`) |
| 데모 쇼케이스 | `os/appplayer/appplayer/dart/example/demo_showcase.mbd` | 위젯 전시 번들. 이름 정본 = **`UI Showcase`** | 번들 메타데이터 통합 테스트(Pro) |

## 이 폴더가 직접 갖는 것 — 노드 샘플

위 표는 *다른 곳에 있는* 것을 가리킨다. 아래는 이 폴더가 소유한다. 옮겨 온 것이 아니라
여기서 만든 것이라 사본 문제가 없다.

| 노드 | 수행 유형 | 채널(선언) | 포트 |
|---|---|---|---|
| `nodes/bin/laundry.dart` | 1회 | QR | 8940 |
| `nodes/bin/locker.dart` | 구간 · 갱신 | QR | 8941 |
| `nodes/bin/parking.dart` | 계량 · 구간 · 갱신 | BLE | 8942 |
| `nodes/bin/charger.dart` | 계량(kWh) · 고객 입력 금액 · 구간 | Wi-Fi | 8943 |
| `nodes/bin/gate.dart` | 정족수 · **결제 없음** | NFC | 8944 |

상세 = [`nodes/README.md`](nodes/README.md). 촬영용 캡처 = `nodes/shots/*.png`.

## 왜 결제 데모 서버가 루프백에만 붙는가

인증이 없는 서버다. 라우팅 주소에 올리면 **남의 화면이 근처 네트워크에 올라간다.**
`--http` 는 가산으로 넣은 것이고 바인딩은 루프백으로 고정돼 있다.

## 스펙 예제는 여기서 관리하지 않는다

`specs/` 안의 예제는 **계약의 일부**다. 그것을 이 폴더로 옮기거나 사본을 두면 계약이 두 곳에
서게 된다. 이 폴더는 그 예제가 실물에서 서는지를 **어디서 확인하는가**만 가리킨다 —
스펙 식 코퍼스(위 표)가 정확히 그 다리다.
