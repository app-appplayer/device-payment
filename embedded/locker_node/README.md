# device nodes — 증표를 검증하는 임베디드 플랫폼과 그 도메인들

**설계**: [`../../design/locker-node-2026-08-30.md`](../../design/locker-node-2026-08-30.md)
**보드**: WeAct MiniH723VGTX (STM32H723), USB CDC

레퍼런스 `led` 도메인이 보이는 것은 선언 → 렌더 → 도구 호출 → 물리 동작까지다. 그건 원격
제어다. 이 노드가 더하는 것이 플랫폼의 본체다 — **서명된 증표를 기기에서, 통신 없이,
자기가 선언한 액션에 대해 검증하고, 권한 구간을 스스로 판정한다.**

공유 레퍼런스 트리(`embedded/mcp_node/`)는 건드리지 않았다. 이 노드는 기기결제 축의 물건이라
그 증거들과 같은 자리에서 관리한다.

## 플랫폼과 도메인

증표·서명·재생·구간·세션 창은 **어느 기계나 같다.** 기계마다 다시 쓰면 구멍도 기계마다
생긴다. 그래서 한 번만 쓰고(`authority.c`), 도메인은 **자기가 무엇인지만** 말한다.

```
src/authority.h/.c       플랫폼 — 증표 검증 7단계 · 재생 거부 · 권한 구간 ·
                                  시각 추정 · 세션 창 · 점유 선언 · 잘림 가드
src/payment_locker.c     도메인 — 보관함: 대여 3종 · exclusive · 래치
src/vending.c            도메인 — 자판기: 상품 3종 · shared · 오거
src/ed25519_shim.cpp     C ↔ Arduino Crypto 경계 (검증 전용)
src/crypto/              Ed25519 검증에 필요한 부분만 vendoring
src/service_pubkey.inc   서비스 공개키 — 생성물, 손으로 고치지 않는다
locker_node.ino          보드 타깃 — 신원 주입 + 슈퍼루프
tools/mint_voucher/      증표 발급자 (원격 서비스 대역) — 서명 키는 여기에만 있다
host/                    같은 도메인을 맥에서: stdio 검사 + `--tcp` 가상 노드
```

**도메인은 서명도 세션 값도 시계도 보지 않는다.** 보관함은 289줄이고 자판기는 그보다 짧다.

### 도메인이 답하는 것

| | 보관함 | 자판기 |
|---|---|---|
| 무엇을 파는가 | 4시간 · 하루 | 물 · 커피 |
| **점유하는가** | **exclusive** — 문이 몇 시간 잡힌다 | **shared** — 배출은 몇 초다 |
| 무엇을 구동하는가 | 래치 | 오거 |

점유는 **수행 유형에서 읽어 낼 수 없다.** 세탁은 `once` 인데 드럼을 잡고 배출도 `once` 인데
아무것도 안 잡는다. 그래서 플랫폼이 추론하지 않고 도메인에게 묻는다.

## 가상 노드 — 보드 없이 기계를 늘린다

같은 C 를 맥에서 TCP(보드 와이어)로 올린다. 세차장 베이도 빨래방 드럼도 이 책상에 없지만,
검증되는 것은 문이 아니라 **권한**이고 그 C 는 칩에서나 노트북에서나 같다.

```sh
NODE_NAME='Locker B12' ./host/payment_locker_host --tcp 9111 &
NODE_NAME='Vending A3' ./host/vending_host        --tcp 9112 &
```

호스트는 `tcp://127.0.0.1:9111` 로 건다. **보드 검사가 칩을 증명하고, 가상 노드가 대수를
증명한다.**

## 왜 비대칭인가

발급자가 서명하고 기기는 **공개키로 검증만** 한다. 공유 비밀(HMAC)로 하면 구현은 훨씬 싸지만
**기기가 자기 권한을 스스로 발행할 수 있게 된다** — 보관함이 절대 할 수 없어야 하는 일이다.
그래서 `src/crypto/` 에 서명 코드는 들어오지 않았고, 키 생성이 참조하는 `RNG` 는 **정의 없이
선언만** 두었다. 누군가 그 길로 가면 링크가 심볼 이름을 대고 깨진다. 0 으로 채운 키를 조용히
내주는 스텁을 두지 않았다.

## 돌려 보기 (보드 없이)

```sh
cd tools/mint_voucher && dart run bin/mint.dart keygen   # 최초 1회
cd ../.. && make -C host && python3 host/check.py
```

보드가 있으면 `python3 host/board_check.py` 로 **같은 검사를 실기에** 돌린다 (**16건 통과**,
2026-08-31).

`check.py` 가 설계 §8 의 검증 계획을 그대로 돈다 — 위조 서명 · 남의 기기 · 선언 안 한 액션 ·
남의 대상 · 재생 · 시계 되돌리기 · **아무것도 안 보낸 채로 만료**. 통과 기준은 거절이 나는
것이지 열리는 것이 아니다.

## 보드에 올리기

**최초 1회만 손이 필요하다.** 그다음부터는 `sys.dfu` 로 넣고 쓴다:

```sh
python3 host/dfu_jump.py /dev/cu.usbmodem*   # 부트로더로
dfu-util -a 0 -s 0x08000000:leave -D build/locker_node.ino.bin
```

```sh
FQBN='STMicroelectronics:stm32:GenH7:pnum=WeActMiniH723VGTX,usb=CDCgen,xusb=HSFS'
INC="-I$PWD/src -I$PWD/src/crypto -I$PWD/src/crypto/utility"
arduino-cli compile --fqbn "$FQBN" --build-path build \
  --build-property "compiler.c.extra_flags=$INC" \
  --build-property "compiler.cpp.extra_flags=$INC" .

# 보드를 DFU 로: BOOT0 누른 채 NRST 짧게 → BOOT0 놓기
dfu-util -a 0 -s 0x08000000:leave -D build/locker_node.ino.bin
```

`compiler.*.extra_flags` 가 필요한 이유: 아두이노는 스케치 폴더만 include 경로에 넣어서
`src/` 의 헤더를 못 찾는다.

## 이 노드가 아닌 것

- **보안요소가 없다.** 검증 공개키는 플래시 상수다. 화면도 그렇게 말한다.
- **래치가 없다.** 열림은 온보드 LED 다. 실물 잠금장치가 있는 척하지 않는다.
- **기기가 서명하지 않는다.** 특허 §3.2 의 기기 주장(기기 개인키로 명세 해시에 서명)은
  다음 컷이다. 지금은 서비스 → 기기 방향만 선다.
- **근거리 무선이 아니다.** USB CDC 유선이다. 같은 도메인을 BLE 보드에 올리는 것은 전송을
  바꾸는 일이지 이 코드를 바꾸는 일이 아니다.
