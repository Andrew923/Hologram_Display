#pragma once
#ifndef HUB75_DMA_H
#define HUB75_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * BCM2710 (Pi Zero 2 W / Pi 3) peripheral physical addresses
 * ----------------------------------------------------------------------- */
#define GPIO_PHYS_BASE   0x3F200000u
#define DMA_PHYS_BASE    0x3F007000u

/* GPIO register word-offsets (index into volatile uint32_t*) */
#define GPSET0  7u
#define GPCLR0  10u

/* GPIO bus addresses for DMA dest_ad */
#define GPSET0_BUS  0x7E20001Cu
#define GPCLR0_BUS  0x7E200028u

/* DMA channel register word-offsets */
#define DMA_REG_CS        0u
#define DMA_REG_CONBLK_AD 1u
#define DMA_REG_DEBUG     8u

#define DMA_CS_RESET   (1u << 31)
#define DMA_CS_ACTIVE  (1u << 0)
#define DMA_CS_END     (1u << 1)
#define DMA_CS_ERROR   (1u << 8)

/* DMA Transfer Information flags */
#define DMA_TI_NO_WIDE_BURSTS (1u << 26)
#define DMA_TI_WAIT_RESP      (1u << 3)

/* -----------------------------------------------------------------------
 * GPIO pin assignments — "regular" mapping on Adafruit Triple Bonnet
 * Hardware color swap verified: pixel.r drives G pins, .g drives B, .b drives R
 * ----------------------------------------------------------------------- */
#define PIN_OE     18   /* Output Enable, active LOW */
#define PIN_CLK    17   /* Pixel shift clock         */
#define PIN_STR     4   /* Strobe / Latch            */

#define PIN_ADDR_A  22
#define PIN_ADDR_B  23
#define PIN_ADDR_C  24
#define PIN_ADDR_D  25
#define PIN_ADDR_E  15

/* Chain 0 (Port 1 / panel 0) — p0_g1→Red, p0_b1→Green, p0_r1→Blue */
#define PIN_C0_R   27   /* p0_g1 → panel-0 RED    (top half)    */
#define PIN_C0_G    7   /* p0_b1 → panel-0 GREEN  (top half)    */
#define PIN_C0_B   11   /* p0_r1 → panel-0 BLUE   (top half)    */
#define PIN_C0_R2   9   /* p0_g2 → panel-0 RED    (bottom half) */
#define PIN_C0_G2  10   /* p0_b2 → panel-0 GREEN  (bottom half) */
#define PIN_C0_B2   8   /* p0_r2 → panel-0 BLUE   (bottom half) */

/* Chain 1 (Port 2 / panel 1) */
#define PIN_C1_R    5   /* p1_g1 → panel-1 RED    (top half)    */
#define PIN_C1_G    6   /* p1_b1 → panel-1 GREEN  (top half)    */
#define PIN_C1_B   12   /* p1_r1 → panel-1 BLUE   (top half)    */
#define PIN_C1_R2  13   /* p1_g2 → panel-1 RED    (bottom half) */
#define PIN_C1_G2  20   /* p1_b2 → panel-1 GREEN  (bottom half) */
#define PIN_C1_B2  19   /* p1_r2 → panel-1 BLUE   (bottom half) */

/* Precomputed GPIO bitmasks */
#define MASK_OE      (1u << PIN_OE)
#define MASK_CLK     (1u << PIN_CLK)
#define MASK_STR     (1u << PIN_STR)
#define MASK_ADDR_ALL \
    ((1u<<PIN_ADDR_A)|(1u<<PIN_ADDR_B)|(1u<<PIN_ADDR_C)| \
     (1u<<PIN_ADDR_D)|(1u<<PIN_ADDR_E))

/* All 12 data pins + CLK — written to GPCLR0 at the start of each pixel */
#define MASK_ALL_DATA_CLK \
    ((1u<<PIN_C0_R) |(1u<<PIN_C0_G) |(1u<<PIN_C0_B) | \
     (1u<<PIN_C0_R2)|(1u<<PIN_C0_G2)|(1u<<PIN_C0_B2)| \
     (1u<<PIN_C1_R) |(1u<<PIN_C1_G) |(1u<<PIN_C1_B) | \
     (1u<<PIN_C1_R2)|(1u<<PIN_C1_G2)|(1u<<PIN_C1_B2)| \
     MASK_CLK)

/* -----------------------------------------------------------------------
 * Panel geometry
 * ----------------------------------------------------------------------- */
#define HUB75_ROWS       32    /* row addresses (1/32 scan on 64-row panels) */
#define HUB75_COLS      128    /* columns per panel                          */

/* -----------------------------------------------------------------------
 * DMA control block — BCM2835 spec, must be 32-byte aligned
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t ti;
    uint32_t source_ad;
    uint32_t dest_ad;
    uint32_t txfr_len;
    uint32_t stride;
    uint32_t nextconbk;
    uint32_t reserved[2];
} __attribute__((packed, aligned(32))) dma_cb_t;

/* -----------------------------------------------------------------------
 * DMA buffer layout within the 1 MB mailbox allocation
 *
 *  [0 .. CB_ARRAY_BYTES)         DMA control blocks  (16576 × 32 = 530432 B)
 *  [PIXEL_BUF_OFFSET ..)         pixel_buf[32][128]  (16384 B) ← CPU updates this
 *  [ADDR_SET_OFFSET ..)          addr_set[33]        (132 B)
 *  [CTRL_OFFSET ..)              ctrl[8]             (32 B)
 * ----------------------------------------------------------------------- */
#define NUM_CBS            16576u
#define CB_ARRAY_BYTES     (NUM_CBS * 32u)
#define PIXEL_BUF_OFFSET   CB_ARRAY_BYTES
#define PIXEL_BUF_BYTES    (HUB75_ROWS * HUB75_COLS * 4u)
#define ADDR_SET_OFFSET    (PIXEL_BUF_OFFSET + PIXEL_BUF_BYTES)
#define ADDR_SET_BYTES     (33u * 4u)
#define CTRL_OFFSET        (ADDR_SET_OFFSET + ADDR_SET_BYTES)
#define CTRL_BYTES         (8u * 4u)
#define DMA_BUF_TOTAL      (CTRL_OFFSET + CTRL_BYTES)  /* ~547 KB */
#define DMA_BUF_ALLOC      (1u << 20)                  /* 1 MB allocation */

/* Indices into ctrl[] */
#define CTRL_ALL_DATA_CLK  0u
#define CTRL_CLK_SET       1u
#define CTRL_CLK_CLR       2u
#define CTRL_STR_SET       3u
#define CTRL_STR_CLR       4u
#define CTRL_OE_SET        5u
#define CTRL_OE_CLR        6u
#define CTRL_ADDR_ALL_CLR  7u

/* -----------------------------------------------------------------------
 * Runtime state
 * ----------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t *gpio;    /* mmap'd GPIO registers            */
    volatile uint32_t *dma_ch;  /* mmap'd DMA channel registers     */
    int  mem_fd;                /* /dev/mem fd                      */
    int  vcio_fd;               /* /dev/vcio fd                     */
    uint32_t mb_handle;         /* mailbox allocation handle        */
    void    *virt_base;         /* CPU virtual address of DMA buf   */
    uint32_t bus_base;          /* DMA bus address of DMA buf       */

    /* Sub-pointers within the DMA buffer (CPU virtual addresses) */
    dma_cb_t *cbs;
    uint32_t *pixel_buf;   /* [row * HUB75_COLS + col] GPSET0 words */
    uint32_t *addr_set;    /* [0..32], row 32 wraps to row 0        */
    uint32_t *ctrl;        /* 8 constant control words              */

    int dma_channel;
} hub75_dma_state_t;

/* Pixel type (layout-compatible with Protocol.h RGB) */
typedef struct { uint8_t r, g, b; } hub75_rgb_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Initialise: map peripherals, allocate DMA buffer, build CB chain, start DMA.
 * Returns 0 on success, -1 on error (message printed to stderr). */
int  hub75_dma_init(hub75_dma_state_t *s, int dma_channel);

/* Stop DMA and release all resources. Safe to call even on partial init. */
void hub75_dma_shutdown(hub75_dma_state_t *s);

/* Update the pixel buffer for both panels simultaneously from two 128×64 slices.
 * slice0: pixels for panel 0 (Port 1), indexed [y*128+x], y=0..63, x=0..127.
 * slice1: pixels for panel 1 (Port 2), same layout.
 * The DMA engine reads the updated words on its next pass — no stop/restart needed. */
void hub75_dma_update_panels(hub75_dma_state_t *s,
                              const hub75_rgb_t *slice0,
                              const hub75_rgb_t *slice1);

#ifdef __cplusplus
}
#endif

#endif /* HUB75_DMA_H */
