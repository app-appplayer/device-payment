# device-payment — 기기 결제 플랫폼 실증 대장

이 축이 **주장하는 것**은 `strategy/device-payment-platform/` 에 있다 — 설계 정본과 특허.
이 폴더는 그 주장이 **실물로 서 있는가**를 관리한다. 같은 축이고 하는 일이 다르다.

> 화면 없는 기기가 결제·신원 액션을 서빙한다. 노드가 자기가 무엇을 할 수 있는지 UI 로
> 내놓고, 호스트가 그것을 사람에게 제시한다. 결제는 상위 등급 노드의 한 동작이다.

문서와 **노드 샘플**이 있다. 기존 테스트·데모는 옮기지 않았다 — §2 가 그 이유다.

- [`patent.md`](patent.md) — **특허 청구항 ↔ 증거.** 어느 항에 증거가 있고 어느 항이 비어 있나
- [`chain.md`](chain.md) — 사슬. 각 홉에서 무엇이 실물이고 무엇이 아닌가
- [`tests.md`](tests.md) — 검사 목록. 덮는 것 · **안 덮는 것** · 실행법 · 최근 실측
- [`coverage.md`](coverage.md) — 채널 × 루트 행렬과 **빈칸**
- [`samples.md`](samples.md) — 데모·샘플이 어디 있고 어느 검사가 그것을 모는가
- [`nodes/`](nodes/README.md) — **노드 샘플 다섯.** 실제로 뜨고 열리고 촬영되는 기계들
- [`design/`](design/) — **설계 문서.** 착수 전에 여기부터 선다. 지금: [무인보관함 노드](design/locker-node-2026-08-30.md)
- [`embedded/locker_node/`](embedded/locker_node/README.md) — **증표를 검증하는 임베디드 노드.** 설계 §8 검증 13건 호스트 통과 · 보드 플래시 대기
- [`content/`](content/README.md) — **기사·키노트·영상.** 증거로 만드는 것이지 증거가 아니다 (콘피)

스펙은 여기로 옮기지 않는다. 계약의 정본은 `specs/` 이고 이 폴더는 **그 계약이 실물에서
서는지를 어디서 어떻게 확인하는가**만 관리한다.

## 1. 한 줄 요약

> 코드를 스캔하면 결제할 수 있는 페이지에 도착한다.

이 주장 하나가 세 영역에 걸쳐 있어서, 오랫동안 **만난 적 없는 반쪽들**로만 증명돼 있었다.
결제 쪽 검사는 이미 열려 있는 문서에서 시작했고, 진입 쪽 검사는 결정에서 멈췄다. 그 사이가
비어 있었다.

## 2. 왜 코드를 여기로 모으지 않았나 — 측정된 제약

**통합 테스트는 소유 패키지의 `integration_test/` 를 떠나면 돌지 않는다.** 밖에 두고 부르면
Flutter 가 이렇게 답한다:

```
Warning: integration_test plugin was not detected.
PlatformException(channel-error, Unable to establish connection on channel:
  "…FirebaseCoreHostApi.initializeCore")
```

플러그인이 안 잡히니 플랫폼 채널부터 죽는다. 실제로 파일 하나를 밖으로 옮겨 확인했다
(`test_live/` 의 평범한 테스트는 밖에서도 돌지만, 가족을 두 집에 나눠 두는 것이 한쪽에 두는
것보다 나쁘다).

사본을 두는 길도 있었으나 **사본은 갈라진다** — 갈라진 사본이 초록이면 그것은 아무 말도 하지
않는다. 그래서 이 폴더는 코드를 갖지 않고, **어디에 무엇이 있고 무엇이 아직 안 덮였는지**를
갖는다.

## 3. 전부 돌리는 법

세 묶음이고, 각각 자기 패키지에서 돈다.

```sh
# ① Pro 통합 테스트 — 실 MCP 노드 · 실 렌더 · 실 포트 (macOS)
cd os/appplayer/appplayer_pro/dart
flutter test integration_test/entry_to_payment_live_test.dart      -d macos
flutter test integration_test/payment_document_end_to_end_test.dart -d macos
flutter test integration_test/payment_return_routing_live_test.dart -d macos
flutter test integration_test/payment_demo_app_live_test.dart       -d macos
flutter test integration_test/payment_sheet_door_live_test.dart     -d macos

# ② 라이브 대상 검사 — 실 배포·네트워크가 필요하다
flutter test test_live/entry_resolver_live.dart          -d macos
flutter test test_live/payment_checkout_address_live.dart -d macos

# ③ 빌드된 앱에 대고 묻는 검사 — 소스 게이트가 못 보는 자리
cd os/core/appplayer/dart/tool/capability_probe
python3 build_probe.py "$HOME/Library/Application Support/com.makemind.appplayer/bundles"
# 앱을 띄우고 디버그 MCP 를 켠 뒤 (표준=7931 · Pro=7930)
python3 verify.py --port 7931
python3 run_corpus.py --port 7931
```

②는 네트워크와 상시 테스트 셀러가 필요하고, 돈은 쓰지 않는다 — 셀러 자격은 **테스트 모드**다.

## 4. 이 폴더를 갱신하는 규칙

- 테스트를 늘리거나 고치면 [`tests.md`](tests.md) 의 그 줄을 같이 고친다. **결과 숫자는
  기억이 아니라 그날 실행에서 적는다.**
- 빈칸이 메워지면 [`coverage.md`](coverage.md) 에서 지운다. 빈칸을 지우는 유일한 근거는
  그것을 덮는 테스트가 초록으로 돈 것이다.
- 「고쳤다」를 여기 적지 않는다. 경위는 트랙(`personas/cherry/tracks/payment-action-spec-*.md`)
  에 있고, 이 폴더는 **지금 무엇이 서 있는가**만 적는다.
