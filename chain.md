# 사슬 — 코드에서 결제까지

## 용어 — 특허와 사업이 같은 것을 다르게 부른다

두 어휘를 섞어 쓰면 문서가 금방 못 읽게 된다. 대응은 이렇다.

| 특허 (`출원본_KR`) | 사업·코드 | 무엇 |
|---|---|---|
| 무인기기 | **노드** | 자기가 무엇을 할 수 있는지 UI 로 내놓는 쪽. 별도 물건이 아니라 MCP server 이고 따라서 앱이다 |
| 이동단말 | **호스트** (AppPlayer) | 그 UI 를 사람에게 제시하고 승인을 획득하는 쪽 |
| 제공 액션 명세 | **UI DSL 문서** | 노드가 선언하는 것 |
| 서명 증표 | (미구현) | 승인 결과를 선언된 액션에 묶은 것 |
| 근거리 무선 인터페이스 | **채널** (NFC·BLE·Wi-Fi·QR) | 제품 축이 아니라 `mcp_channel` 이 추상화한 전송. 같은 노드가 갈아끼운다 |

**결제는 상위 등급 노드의 한 동작**이지 별도 제품이 아니다 — 등급은 키가 정한다.

세 영역이 이어 붙는다. 각 홉에서 **무엇이 실물이고 무엇이 아닌지**를 적는다. 이 구분이
흐려지면 초록이 무슨 뜻인지 알 수 없게 된다.

```
  사람이 코드를 건넨다
        │
        │  ① 취득 — 링크 또는 맨 코드
        ▼
  세이프페이지 해석기          ← 실 배포 (safepage.app)
        │  EntryDecision (대상 종류 · ref · route · 발급자)
        ▼
  코어 진입 파이프라인          ← appplayer_core
        │  등록 → 개방
        ▼
  결제 노드 (MCP 서버)          ← 실 서버, HTTP 로 건다
        │  ui:// 문서를 보낸다
        ▼
  런타임 렌더                   ← flutter_mcp_ui_runtime
        │  문서의 payment 액션 버튼
        ▼
  티어 결제 포트                ← PayRunnerPaymentPort (Pro)
        │  플래너가 표면을 고른다
        ▼
  페이러너                      ← hostedPage | nativeSheet
        │
        ▼
  복귀 — 딥링크로 봉투가 돌아온다
```

## 홉별 실물/비실물

| 홉 | 실물인 것 | 실물이 아닌 것 |
|---|---|---|
| **취득** | 맨 코드·링크 파싱, claim 호스트 대조 | 폰 카메라 스캔(맥에서는 붙여넣기) |
| **세이프페이지 해석기** | **실 배포에 붙는다.** 발급 코드가 열리고 발급자가 실려 온다. AASA 라이브 | Android claim(`assetlinks`)은 **의도적 미게시** |
| **코어 진입** | 등록·개방이 코어 자기 opener 로 | — |
| **결제 노드** | **실 MCP 서버**를 저장소에서 띄워 HTTP 로 건다(루프백 고정) | — |
| **렌더** | 서버가 보낸 문서를 이 티어 런타임이 실제로 그린다 | — |
| **결제 포트** | 출고 경로 그대로. 라우터·SDK 플래너 실물 | — |
| **페이러너 표면** | 호스티드 실결제·앱 내 시트 실결제 **관통 이력 있음**(원장 대조까지) | 자동 테스트가 데스크톱에서 표면을 열지는 **못한다** — §정책 참조 |
| **복귀** | 스킴 등록·OS 해석·앱 전달까지 실 OS | — |

## 데스크톱에서 결제가 거절되는 것은 설계다

`PayRunnerPaymentPort` 는 호스트 정책으로 **시트만** 허용한다.

```dart
// os/appplayer/appplayer_pro/dart/lib/adapters/pay_runner_payment_port.dart:95
allow: const <PaymentSurface>{PaymentSurface.nativeSheet},
```

맥에는 시트가 없으니 플래너가 거절하고, 문서는 `code: PAYMENT_UNAVAILABLE` 을 받는다.
**그 거절이 문서에 닿는 것**이 데스크톱에서 검사할 수 있는 합류점이고, 아무것도 못 들은
문서가 실패다. 브라우저가 열린다고 단언하는 검사는 이 정책 이전의 것이다.

`PaymentSurface` 는 두 값뿐이다 — `hostedPage`(셀러 호스티드 페이지, 외부 브라우저) ·
`nativeSheet`(프로바이더 네이티브 시트, 카드가 SDK 를 안 떠남).
정의: `saas_app/pay_runner/sdk/pay_runner_sdk/lib/src/providers.dart:19`.

## 진입 대상은 다섯 가지다

`appplayer_core` 의 `EntryTargetKind` (`lib/src/entry/entry_target.dart:58`):

| 값 | 뜻 |
|---|---|
| `server` | 서빙되는 MCP 엔드포인트 |
| `localServer` | 역참조가 아니라 **이 망에서 발견된** 노드 |
| `bundle` | 설치된 번들 |
| `listing` | 마켓 리스팅 — 계정 게이트라 손님에게는 안 열린다 |
| `external` | `tel:` · `mailto:` · https 페이지 |

어느 종류가 실제로 검사되고 있는지는 [`coverage.md`](coverage.md) 에 있다.
