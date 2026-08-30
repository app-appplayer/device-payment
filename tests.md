# 테스트 목록

**최근 실측 = 2026-08-30**(전부 이 날 직접 실행). 숫자는 그날 실행에서 적은 것이고,
기억이나 이전 기록에서 옮긴 것이 아니다.

모두 `os/appplayer/appplayer_pro/dart` 에서 돈다.

각 검사가 특허의 어느 단계를 만지는지는 [`patent.md`](patent.md) §1 에 대응한다. 요약하면
**전부 선언·승인 두 단계 안에 있고, 결속·동작을 만지는 검사는 하나도 없다.**

| 검사 | 특허 단계 | 선언 주체 |
|---|---|---|
| `entry_to_payment_live` | 선언(호스트 쪽 절반) + 승인 | 노드가 아니라 **서버**(HTTP) |
| `payment_document_end_to_end` | 선언(호스트 쪽 절반) | 서버 |
| `payment_demo_app_live` | 선언 | 서버 · 번들 |
| `payment_return_routing_live` | 승인의 복귀 | — |
| `payment_sheet_door_live` | 승인의 표면 | — |
| `entry_resolver_live` | 선언 이전 — 코드가 무엇을 가리키는가 | 세이프페이지 |
| `payment_checkout_address_live` | 승인 | — |

## 합류 — 이 사슬의 본체

### `integration_test/entry_to_payment_live_test.dart` — **6 PASS**

다른 무엇도 덮지 않는 자리. 실 MCP 결제 노드를 HTTP(8931)로 걸고, 코어 opener 가 등록·개방하고,
서버가 보낸 문서를 이 티어 런타임이 그리고, 그 버튼이 티어 결제 포트에 닿아 답이 문서 state 로
돌아온다.

- 덮는 것: ①진입이 명시한 페이지로 열림 ②합류(버튼→포트→문서) + 정책(데스크톱은 브라우저로
  안 나감) ③없는 페이지는 고지 ④두 번 스캔해도 등록 1행 ⑤손님 방문은 기기에 아무것도 안 남김
- **안 덮는 것**: 해석기. 그 반쪽은 `entry_resolver_live.dart` 가 실 배포에 대고 잰다.
  둘 사이를 지나가는 것은 `EntryDecision` 하나다.
- 이 테스트가 코어 결함 하나를 잡았다 — 그전까지 `server` 대상을 **실제로 연 코드가 없었다.**

## 진입 쪽

### `test_live/entry_resolver_live.dart` — **8 PASS**

출고 경로(`buildProEntryController` + 실 dio)로 **실 배포**에 붙는다.

- 이 빌드가 파생하는 주소에서 해석기가 답한다 · 이 빌드가 claim 안 한 호스트는 해석 안 한다 ·
  발급 코드가 열리고 발급자가 실려 온다 · 계정 게이트는 「미지원」이 아니라 「신원 필요」로 고지 ·
  코드가 앱이 아니라 **페이지**를 지목할 수 있다 · AASA 가 이 빌드를 명시 ·
  Android claim 은 미게시이고 그렇다고 말한다 · 앱 없는 기기가 막다른 길이 아니다
- **안 덮는 것**: 해석 이후. 결정에서 멈춘다.

## 결제 쪽

| 파일 | 결과 | 덮는 것 |
|---|---|---|
| `integration_test/payment_document_end_to_end_test.dart` | **7 PASS** | 문서가 결제를 요청하는 것 자체 — 실제 `payment` 버튼을 이 티어 런타임에 올려 탭 → 능력 → 포트 |
| `integration_test/payment_return_routing_live_test.dart` | **4 PASS** | OS 가 소유한 절반 — 스킴 등록 · LaunchServices 해석 · 앱으로의 전달. 유닛으로는 볼 수 없는 자리 |
| `integration_test/payment_demo_app_live_test.dart` | **12 PASS** | 번들 소스와 서버 소스를 **같은 시나리오로** 돈다 — 소스가 페이지를 만드는 방식일 뿐 결제 계약과 무관함을 증명 |
| `integration_test/payment_sheet_door_live_test.dart` | **2 PASS** | 시트 문 |

## 빨간 것 하나

### `test_live/payment_checkout_address_live.dart` — **1 PASS · 2 FAIL**

원인 확정. flaky 아니다.

```
Expected: an object with length of <1>
  Actual: []
the port decided not to open
```

포트가 `allow: {nativeSheet}` 만 선언하고 맥에는 시트가 없어 **설계대로 거절**한다. 그러면
아무 주소도 안 열리고 `expect(opened, hasLength(1))` 이 깨진다. 정책 이전에 쓰인 「브라우저가
열린다」 단언이 **이 파일에만** 남아 있다 — 형제 셋은 이미 전환됐다.

이 파일의 주제(「이 호스트가 만드는 주소가 실제로 서빙되는가」)는 아직 값이 있으므로 버리지
않는다. 데스크톱에서 잴 수 있는 형태는 **플래너에 모바일 보드를 주어 주소만 만들고 그 주소를
fetch** 하는 것이다 — 포트 정책은 건드리지 않는다. 시트 유무는 이 파일의 주제가 아니다.

## 노드 샘플 (이 폴더가 소유)

| 검사 | 결과 | 덮는 것 |
|---|---|---|
| `device-payment/nodes/test/declaration_test.dart` | **22 PASS** | 서빙되는 문서의 불변식 — 상태 초기값·금액은 고객이 친 곳에서만·판매자/품목·오류 경로. 헬퍼가 아니라 **돌아가는 서버에서 읽어** 검사한다 |
| `appplayer_pro/dart/integration_test/node_samples_live_test.dart` | **35 PASS** | 다섯 노드가 **두 전송에서 실제로 렌더**된다 — streamable HTTP 와 **ndjson TCP(보드 와이어)**, 후자는 출고 커넥터 `extensionSchemeConnector` 로 건다. — 선언한 조건이 화면에 뜨고, 버튼이 눌리고, 미해결 바인딩이 없고, 결과 칸이 빈 채로 뜨지 않는다. `CAPTURE=1` 이면 촬영용 PNG 를 남긴다 |

렌더 검사가 존재하는 이유는 이 화면들이 **키노트·매거진·촬영에 나가기 때문**이다.

## 실기 보드 (하드웨어 필요)

### `appplayer_pro/dart/integration_test/board_serial_live_test.dart` — **4 PASS**

아래 §기기 신원 주장 앞의 항목과 같은 파일이다. 이 자리에 있던 설명은 보드가 `led`
레퍼런스를 돌던 때의 것이라 걷어냈다 — 그 도메인은 이 보드에 더 이상 없고, 남겨 두면
없는 검사를 있다고 적는 셈이다. 경위는 트랙에 있다.

## 임베디드 노드 — 증표 검증 (`embedded/locker_node/`)

### `host/check.py` — **25 checks, all passed**

같은 도메인 C 를 맥에서 돌려 설계 §8 을 그대로 검사한다. 보드 없이 돈다.

| # | 무엇 | 결과 |
|---|---|---|
| 2 · 2b | 시각 보정 전에는 열지도, 증표를 받지도 않는다 | 거절 |
| 10 | **뒤로 가는 시각 보정**은 거절 | 거절 |
| 3 · 4 | 유효 증표 수용 → 열림 | 통과 |
| 5 | **서명 1바이트 변조** | `signature does not verify` |
| 6 | 남의 기기 증표(서명은 유효) | `another device` |
| 7 | **노드가 선언한 적 없는 액션**(서명은 유효) | `never offered` |
| 7b | 남의 대상 | `another target` |
| 8 · 8b · 8c | 재생 · 과거 세션 값 | `already used` |
| 9 · 9b · 9c | 짧은 구간 → 열림 → **아무것도 안 보낸 채 만료** → 거절 | `authority expired` |

**통과 기준이 거절이라는 점이 요점이다.** 3·4 만 도는 구현은 검증하는 척하는 노드다.

**호스트 검사가 결함 하나를 잡았다**: 만료로 권한이 걷힌 뒤 `locker.open` 이 사유를
`no authority held` 로 덮어써서 **만료됐다는 사실이 화면에서 사라졌다.** 사람은 애초에 권한이
없었다는 말을 듣게 된다. 만료 사유를 따로 보존하도록 고쳤다.

### `host/board_check.py` — **21 checks, all passed ON THE BOARD** (2026-08-31)

스물하나이지 스물셋이 아니다. 냉부팅 전용 둘은 **시각을 이미 아는 보드에서 해당이 없고**,
조용히 사라지는 대신 그렇게 말한다.

같은 검사를 WeAct H723 실기에 대고 돌린다. **Ed25519 검증이 칩에서 실제로 돈다** — 변조 서명이
보드에서 막힌다. 만료도 실기에서 확인했다: 구간이 끝나는 동안 **보드에 아무것도 보내지 않았고**
그 뒤 `locker.open` 이 `authority expired` 로 거절했다.

**세션 창도 실기 확인**: 창이 닫히면 문은 거절하되 **대여는 그대로 살아 있다**
(`remaining=3576s`, 사유 = `session window closed — present again`). 1:1 기계라 승인해 놓고
떠난 사람이 기계를 붙잡지 못하게 하는 층이고, 산 권한과는 다른 층이다.

### `appplayer_pro/dart/integration_test/board_serial_live_test.dart` — **4 PASS**

앱플레이어가 `serial://` 로 그 보드를 열어 **선언된 조건을 렌더**한다 — `Locker B12` ·
`4 hours` · `One day` · `3,000 KRW` · `Open`. 이 문자열은 테스트에 쓴 것이 아니라 와이어에서 왔다.
화면이 스스로 「보안요소 없음 · 래치는 LED」라고 말하는 것도 고정했다.

`Open` 을 누르면 **거절 사유가 화면에 도달한다**(라이브 `state://locker`). 읽을 수 없는 거절은
호출이 유실된 것과 구분되지 않는다.

**플래시는 이제 버튼이 필요 없다** — `sys.dfu` 로 부트로더에 넣고 `dfu-util` 로 쓴다. 최초 1회만
BOOT0 였다.

## 갱신 갈래 — `host/renewing_check.py` **10 PASS**

특허 §4.1.1 이 **자기 구성**이라고 한 쪽. 주차 차단기 도메인으로 검사한다.

| 무엇 | 결과 |
|---|---|
| 첫 제시가 상태에 진입하고 **끝 시각을 약속하지 않는다** | `renewing until released` |
| **부재 중 누적** — 아무것도 안 보낸 채 경과 | `units=4` |
| 카운트다운이 아니다 | `remaining=0s` |
| **만료되지 않는다** | 시간이 지나도 거절 없음 |
| **같은 제스처가 해제하고 총액을 확정** | `released units=4` |
| 같은 기계의 **고정형 제안은 여전히 카운트다운** | `remaining=3600s` |

**갈래는 제안마다이지 노드마다가 아니다** — 한 차단기가 갱신형과 고정형을 같이 판다.

**표시에서 잡은 것 둘**: 갱신형인데 `remaining` 이 카운트다운을 보여 줬고(공개 접근자를 안 거치고
내부 함수를 읽고 있었다), 사유가 `until <시각>` 이라고 **없는 만료를 약속**했다. 둘 다 화면에
그대로 나갈 뻔했다.

## 정전 생존 — `host/power_cut_check.py` **10 PASS** · 보드 **10 PASS**

프로세스를 죽였다 살리는 것이 이 빌드의 정전이고, 보드에서는 실제 재부팅이다.

| 무엇 | 결과 |
|---|---|
| 대여와 재생 카운터가 **돌아온다** | `remaining=` 살아 있고 이전 세션 값은 여전히 거절 |
| **시계는 안 돌아온다** | `time=unknown` — 그리고 그렇게 말한다 |
| **래치도 안 돌아온다** | 정전 뒤 열린 문은 아무도 열기로 하지 않은 문 |
| **첫 보정의 하한이 여기서 일한다** | 대여 시작보다 앞선 보정 거절 — 온 부팅에서는 발동조차 못 하던 규칙 |

보드는 STM32 가 플래시 페이지에 흉내 내는 EEPROM 을 쓴다. 저장은 **타깃이 대고** 플랫폼은
어디에 두는지 모른다.

**밟다가 잡은 것 셋**

| | |
|---|---|
| **저장이 필드를 다 채우기 전에 돌았다** | 구간 0 을 기록해서, 되살아난 기계가 대여를 **즉시 만료로** 읽었다. RAM 은 맞고 디스크가 틀린 형태라 제일 안 보이는 종류다 |
| **성공에 사유를 안 남겼다** | 문이 열렸는데 화면은 직전 거절(`no time yet`)을 그대로 보여 줬다 — 자기모순 |
| **검사들이 저장소를 공유했다** | 기계가 기억하기 시작하자 앞 검사의 대여가 다음 검사의 첫 제시를 **해제로** 바꿨다. 검사마다 저장소를 갈랐다 |

## 기기 신원 주장 — `host/assertion_check.py` **7 PASS** · 보드 **10 PASS**

특허 §3.2, **반대 방향**. 기기가 `deviceId ‖ counter ‖ nonce ‖ H(선언)` 에 서명하고 서비스가
검증한 뒤에만 발행한다.

| 무엇 | 결과 |
|---|---|
| 칩이 자기 명세에 서명 | STM32H723 에서 Ed25519 서명 |
| 주장 재사용 | `assertion counter N was already used` |
| **안 서명한 명세 중계** | `the relayed declaration is not the one the device signed` |
| 변조된 주장 | `the device assertion does not verify` |
| 논스가 매번 다름 | H7 RNG 주변장치 |
| **사슬의 끝** | 그렇게 발행된 증표를 그 기계가 수용하고 문이 열린다 |

**보드를 멈추게 한 것**: `device.assert` 의 **RNG 무한 대기**. H7 의 RNG 는 커널 클럭(HSI48)이
따로 있고 기본 꺼짐인데 버스 클럭만 켰다. `DRDY` 가 영영 안 서고 서브 루프가 안 돌아온다 —
USB 는 인터럽트라 계속 열거되니 **겉보기엔 벽돌**이고 `sys.dfu` 조차 처리가 안 돼 소프트 복구도
막혔다. 클럭을 켜고 **대기를 유한하게** 했다: 못 뽑으면 거절하고 노드는 산다.

## 가상 노드 — 보드 없이 대수를 늘린다

### `appplayer_pro/dart/integration_test/virtual_nodes_live_test.dart` — **9 PASS**

같은 도메인 C 를 맥에서 TCP(보드 와이어)로 올리고 **출고 커넥터**로 연다. 세 기계가 각자
자기 조건과 **점유**를 화면에 말한다 — 보관함 `one person at a time`, 자판기
`several at a time`. 같은 플랫폼·같은 와이어에서 답이 갈리는 것이 요점이다.

### 자판기 도메인 호스트 검사 — **9 PASS**

검증 코드를 **한 줄도 안 쓰고** 플랫폼 보장 전량을 받는다: 서명 변조 · 남의 기기 ·
선언 안 한 액션 · 남의 슬롯 · 재생. `authority.c` 를 뽑은 것이 옳았다는 증거다.

**밟다가 잡은 것 둘**

| | |
|---|---|
| **자판기 페이지가 조용히 잘렸다** | 1536 버퍼에 1535 — JSON 이 깨져 화면이 아예 안 열린다. 호스트는 「uri 가 UI 정의로 안 풀린다」고만 말해 **엉뚱한 곳을 가리킨다.** 버퍼를 늘리고, 잘리면 **「이 기계는 조건을 보여 줄 수 없다」는 유효한 페이지**를 대신 서빙하게 했다 |
| **가상 노드가 틱을 안 돌렸다** | `read()` 에서 블록하니 말을 걸어야만 틱이 돈다 → 라이브 상태가 안 갱신되고 **만료가 스스로 일어나지 않는다.** 보드는 슈퍼루프라 계속 돈다. `poll()` 로 같게 만들었다 |

## 이웃 — 같은 트리에 있으나 이 사슬은 아님

`integration_test/location_port_live_test.dart` · `test_live/composed_origin_recovery_live.dart` ·
`test_live/kernel_redial_live.dart`. 진입·결제 사슬 밖이라 여기서 관리하지 않는다.

## 빌드된 앱에 대고 묻는 검사

`analyze 0` · 전량 PASS · dry-run 0 은 전부 **소스에 대한 진술**이다. 아래 둘만이 빌드된 앱을
연다. 실행법은 [`README.md`](README.md) §3.

| 검사 | 최근 실측 (2026-08-30, macOS 표준) |
|---|---|
| `capability_probe/verify.py` | `capability probe OK — 16 section checks, all reported (none) and drew` |
| `capability_probe/run_corpus.py` | `spec expression corpus — 24 pass · 0 FAIL` |
