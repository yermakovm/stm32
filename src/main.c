#include <stdint.h>
/* Core clock after reset = HSI = 16 MHz (no PLL is configured in this project). */
#define CORE_HZ         16000000UL

#define LED_RCC_BIT     2U            /* RCC_AHB1ENR.IOPCEN */
#define LED_PORT_BASE   0x40020800UL  /* GPIOC */
#define LED_PIN         13U

#define BTN_RCC_BIT     0U            /* RCC_AHB1ENR.IOPAEN */
#define BTN_PORT_BASE   0x40020000UL  /* GPIOA */
#define BTN_PIN         0U            /* PA0 (Discovery/Nucleo B1 convention) */
#define BTN_ACTIVE_HIGH 1U            /* 1 = pressed when pin reads 1 (pull-down wired) */

/*
 * "Period" = toggle interval: the LED stays HIGH for period ms and LOW for
 * period ms (equal halves). A button press picks one of the 5 modes below.
 * Initial mode = 500 ms -> 0.5 s on / 0.5 s off.
 */
enum {
    MODE_100MS = 0,   /* 0.1 s */
    MODE_500MS,       /* 0.5 s */
    MODE_1S,          /* 1.0 s */
    MODE_2S,          /* 2.0 s */
    MODE_3S,          /* 3.0 s */
    NUM_MODES
};

static const uint32_t blink_modes[NUM_MODES] = {100UL, 500UL, 1000UL, 2000UL, 3000UL};

#define INITIAL_MODE  MODE_500MS
#define DEBOUNCE_MS   20U             /* samples are taken at 1 kHz */

/* ---------------- Registers ---------------- */

#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))

#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b)  (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_PUPDR(b)   (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_IDR(b)     (*(volatile uint32_t *)((b) + 0x10))
#define GPIO_ODR(b)     (*(volatile uint32_t *)((b) + 0x14))

#define SYSTICK_CSR     (*(volatile uint32_t *)0xE000E010UL)
#define SYSTICK_LOAD    (*(volatile uint32_t *)0xE000E014UL)
#define SYSTICK_VAL     (*(volatile uint32_t *)0xE000E018UL)

#define DEMCR           (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004UL)

/* SysTick reload is 24-bit: 1 ms must fit. */
_Static_assert(CORE_HZ / 1000UL <= 0x00FFFFFFUL, "SysTick 1 ms reload exceeds 24 bits");

/* ---------------- Setup ---------------- */

static void peripheral_clock_enable(uint32_t bit)
{
    RCC_AHB1ENR |= (1U << bit);
    (void)RCC_AHB1ENR;    /* dummy reads: let the newly enabled clock settle */
    (void)RCC_AHB1ENR;
}

static void led_init(void)
{
    peripheral_clock_enable(LED_RCC_BIT);

    GPIO_MODER(LED_PORT_BASE) &= ~(3U << (LED_PIN * 2U));
    GPIO_MODER(LED_PORT_BASE) |=  (1U << (LED_PIN * 2U));   /* 01 = general purpose output */
    GPIO_OTYPER(LED_PORT_BASE) &= ~(1U << LED_PIN);         /* push-pull */
    GPIO_ODR(LED_PORT_BASE) &= ~(1U << LED_PIN);            /* start with LED off */
}

static void button_init(void)
{
    peripheral_clock_enable(BTN_RCC_BIT);

    GPIO_MODER(BTN_PORT_BASE) &= ~(3U << (BTN_PIN * 2U));   /* 00 = input */
    GPIO_PUPDR(BTN_PORT_BASE) &= ~(3U << (BTN_PIN * 2U));
    GPIO_PUPDR(BTN_PORT_BASE) |=
        (BTN_ACTIVE_HIGH ? 1U : 2U) << (BTN_PIN * 2U);      /* 01 = pull-down, 10 = pull-up */
}

/* 1 ms free-running tick, polled (no TICKINT: the startup file has no handlers). */
static void systick_init(void)
{
    SYSTICK_LOAD = (CORE_HZ / 1000UL) - 1UL;
    SYSTICK_VAL  = 0UL;
    SYSTICK_CSR  = (1UL << 2) | (1UL << 0);                 /* CLKSOURCE = core clock, ENABLE */
}

/* Cycle counter: used as an entropy source together with press timing. */
static void dwt_init(void)
{
    DEMCR    |= (1UL << 24);   /* TRCENA: power the DWT block */
    DWT_CTRL |= 1UL;           /* CYCCNTENA */
}

/* ---------------- Runtime ---------------- */

static int button_pressed(void)
{
    uint32_t raw = (GPIO_IDR(BTN_PORT_BASE) >> BTN_PIN) & 1U;
    return raw == BTN_ACTIVE_HIGH;
}

static uint32_t xorshift32(uint32_t s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/*
 * Entropy: DWT cycle count + SysTick phase at the instant of the press, i.e.
 * human reaction-time jitter. Each press re-rolls the seed and picks a mode.
 */
static uint32_t random_mode(uint32_t *seed)
{
    uint32_t s = *seed ^ DWT_CYCCNT ^ SYSTICK_VAL;
    if (s == 0UL)
        s = 0x9E3779B9UL;      /* xorshift must never be 0 */
    s = xorshift32(s);
    *seed = s;
    return blink_modes[s % NUM_MODES];
}

int main(void)
{
    /* All state is local: the startup code only does `bl main`, so .data/.bss
       initialisers are not guaranteed to be applied. */
    uint32_t seed      = 0x12345678UL;
    uint32_t period    = blink_modes[INITIAL_MODE];
    uint32_t countdown = period;
    uint32_t high_ms   = 0;
    uint32_t low_ms    = 0;
    int      pressed   = 0;

    led_init();
    button_init();
    dwt_init();
    systick_init();

    for (;;) {
        /* Wait 1 ms. COUNTFLAG (bit 16) is cleared by this read of CSR. */
        while (!(SYSTICK_CSR & (1UL << 16)))
            ;

        /* Debounce: the level must be stable for DEBOUNCE_MS. */
        if (button_pressed()) {
            low_ms = 0;
            if (high_ms < DEBOUNCE_MS)
                high_ms++;
        } else {
            high_ms = 0;
            if (low_ms < DEBOUNCE_MS)
                low_ms++;
        }

        int now = (high_ms >= DEBOUNCE_MS);
        if (now && !pressed) {                 /* rising edge = one press */
            period    = random_mode(&seed);
            countdown = period;                /* new period applies from now */
        }
        pressed = now;

        if (--countdown == 0UL) {
            GPIO_ODR(LED_PORT_BASE) ^= (1U << LED_PIN);
            countdown = period;
        }
    }
}
