/* Arduino framework board binding. The domain's LED runs on this board's PE3
 * (silkscreen "E3"). STM32duino's digitalWrite(PE3) did not drive it on this
 * variant, but bare-metal register access does — so PE3 is driven straight
 * through the STM32H7 GPIOE registers here (the board HAL is inherently
 * platform-specific). millis() is still the Arduino core's. */
#include <Arduino.h>

extern "C" {
void board_arduino_init(void);
void board_led_set(int on);
int board_led_get(void);
unsigned long board_uptime_ms(void);
void board_enter_dfu(void);
}

/* STM32H7 register map (bare addresses). */
#define REG(a) (*(volatile uint32_t*)(a))
#define RCC_AHB4ENR REG(0x58024400u + 0xE0u) /* GPIO port clock enables */
#define GPIOE_MODER REG(0x58021000u + 0x00u)
#define GPIOE_BSRR  REG(0x58021000u + 0x18u) /* atomic set/reset */
#define LED_PIN 3u                           /* PE3 */

static int s_led = 0;

extern "C" void board_arduino_init(void) {
    RCC_AHB4ENR |= (1u << 4);                /* GPIOE clock enable */
    GPIOE_MODER &= ~(3u << (LED_PIN * 2));
    GPIOE_MODER |= (1u << (LED_PIN * 2));     /* PE3 = output */
    GPIOE_BSRR = (1u << (LED_PIN + 16));      /* off at boot */
}

extern "C" void board_led_set(int on) {
    s_led = on ? 1 : 0;
    /* HIGH = lit (active-high). If the LED lights on "off" instead, swap these. */
    GPIOE_BSRR = on ? (1u << LED_PIN) : (1u << (LED_PIN + 16));
}

extern "C" int board_led_get(void) {
    return s_led;
}

extern "C" unsigned long board_uptime_ms(void) {
    return millis();
}

/* Jump to the chip's own USB bootloader, so a reflash needs no button.
 *
 * The first flash of this firmware is by hand — BOOT0 held while NRST is
 * tapped — because nothing on the board can be asked to do it yet. From then
 * on `sys.dfu` calls this and the next flash is a command, which matters when
 * the thing being iterated on is verification logic.
 *
 * System memory on STM32H72x/H73x is 0x1FF09800 (AN2606). USB is detached
 * first: a host that still sees the CDC device will not enumerate the DFU one.
 * Nothing returns from here.
 */
#define SYSTEM_MEMORY_BASE 0x1FF09800u

extern "C" void board_enter_dfu(void) {
    /* Peripherals down first, USB with them: a host that still sees the CDC
     * device will not enumerate the DFU one. */
    HAL_DeInit();
    delay(50);

    /* Caches OFF before leaving, and this is not optional on an H7.
     *
     * Measured: with them on, the jump lands in the bootloader and the device
     * enumerates as DFU — and then every erase fails with `ERASE_PAGE
     * get_status`. It looks like a broken bootloader and is actually the
     * application's leftovers. Clean before disabling or the dirty lines are
     * simply dropped. */
    SCB_CleanDCache();
    SCB_DisableDCache();
    SCB_DisableICache();

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    HAL_RCC_DeInit();
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    const uint32_t* const boot = (const uint32_t*)SYSTEM_MEMORY_BASE;
    void (*entry)(void) = (void (*)(void))boot[1];
    SCB->VTOR = SYSTEM_MEMORY_BASE;
    __set_MSP(boot[0]);
    __enable_irq();
    entry();
    while (1) { }
}
