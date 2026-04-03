#include "hub75_dma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

/* -----------------------------------------------------------------------
 * Mailbox interface (/dev/vcio)
 * ----------------------------------------------------------------------- */
#define IOCTL_MBOX_PROPERTY  _IOWR(100, 0, char*)

#define MBOX_TAG_MEM_ALLOC   0x3000Cu
#define MBOX_TAG_MEM_LOCK    0x3000Du
#define MBOX_TAG_MEM_UNLOCK  0x3000Eu
#define MBOX_TAG_MEM_FREE    0x3000Fu

/* MEM_FLAG_DIRECT|MEM_FLAG_COHERENT — uncached, L2-coherent memory.
 * Returns bus addresses with 0xC0000000 prefix on BCM2710. */
#define MBOX_MEM_FLAGS  0x0Cu

static int mbox_ioctl(int fd, uint32_t *buf) {
    return ioctl(fd, IOCTL_MBOX_PROPERTY, buf);
}

/* Allocate physically contiguous DMA-safe memory via /dev/vcio.
 * On success sets *handle and returns the bus address.
 * On failure returns 0. */
static uint32_t mbox_alloc(int vcio_fd, uint32_t size, uint32_t *handle) {
    uint32_t msg[9];
    msg[0] = sizeof(msg);
    msg[1] = 0;
    msg[2] = MBOX_TAG_MEM_ALLOC;
    msg[3] = 12;
    msg[4] = 0;
    msg[5] = size;
    msg[6] = 4096;        /* page-aligned */
    msg[7] = MBOX_MEM_FLAGS;
    msg[8] = 0;
    if (mbox_ioctl(vcio_fd, msg) < 0 || !(msg[1] & 0x80000000u)) {
        fprintf(stderr, "hub75_dma: mailbox alloc failed\n");
        return 0;
    }
    *handle = msg[5];

    /* Lock to get bus address */
    uint32_t lock_msg[7];
    lock_msg[0] = sizeof(lock_msg);
    lock_msg[1] = 0;
    lock_msg[2] = MBOX_TAG_MEM_LOCK;
    lock_msg[3] = 4;
    lock_msg[4] = 0;
    lock_msg[5] = *handle;
    lock_msg[6] = 0;
    if (mbox_ioctl(vcio_fd, lock_msg) < 0) {
        fprintf(stderr, "hub75_dma: mailbox lock failed\n");
        return 0;
    }
    return lock_msg[5]; /* bus address */
}

static void mbox_free(int vcio_fd, uint32_t handle) {
    uint32_t msg[7];
    msg[0] = sizeof(msg); msg[1] = 0;
    msg[3] = 4; msg[4] = 0; msg[5] = handle; msg[6] = 0;

    msg[2] = MBOX_TAG_MEM_UNLOCK;
    mbox_ioctl(vcio_fd, msg);

    msg[1] = 0; msg[4] = 0; msg[5] = handle;
    msg[2] = MBOX_TAG_MEM_FREE;
    mbox_ioctl(vcio_fd, msg);
}

/* -----------------------------------------------------------------------
 * GPIO helpers
 * ----------------------------------------------------------------------- */
static void gpio_set_output(volatile uint32_t *gpio, int pin) {
    int reg   = pin / 10;
    int shift = (pin % 10) * 3;
    uint32_t v = gpio[reg];
    v &= ~(7u << shift);
    v |=  (1u << shift); /* function 001 = output */
    gpio[reg] = v;
}

/* All GPIO pins used by HUB75 */
static const int HUB75_PINS[] = {
    PIN_OE, PIN_CLK, PIN_STR,
    PIN_ADDR_A, PIN_ADDR_B, PIN_ADDR_C, PIN_ADDR_D, PIN_ADDR_E,
    PIN_C0_R, PIN_C0_G, PIN_C0_B, PIN_C0_R2, PIN_C0_G2, PIN_C0_B2,
    PIN_C1_R, PIN_C1_G, PIN_C1_B, PIN_C1_R2, PIN_C1_G2, PIN_C1_B2,
};
#define NUM_HUB75_PINS  ((int)(sizeof(HUB75_PINS)/sizeof(HUB75_PINS[0])))

/* -----------------------------------------------------------------------
 * DMA CB chain construction
 *
 * Per-row sequence (518 CBs/row × 32 rows = 16576 CBs total):
 *
 *   For c = 0..127:
 *     CB_A: GPCLR0 ← all_data_clk  (clear all 12 data pins + CLK)
 *     CB_B: GPSET0 ← pixel_buf[r*128+c]  (set this pixel's data bits)
 *     CB_C: GPSET0 ← clk_set        (CLK rising edge)
 *     CB_D: GPCLR0 ← clk_clr        (CLK falling edge)
 *   CB_T1: GPSET0 ← oe_set          (blank display)
 *   CB_T2: GPCLR0 ← addr_all_clr    (clear address bits)
 *   CB_T3: GPSET0 ← addr_set[r]     (set address for this row)
 *   CB_T4: GPSET0 ← str_set         (STR rising edge — latch into row r)
 *   CB_T5: GPCLR0 ← str_clr         (STR falling edge)
 *   CB_T6: GPCLR0 ← oe_clr          (unblank — display row r)
 *
 * Row 31's CB_T6 nextconbk → CB 0 (infinite loop).
 * ----------------------------------------------------------------------- */
static void build_cb_chain(hub75_dma_state_t *s) {
    /* Populate constant control words */
    s->ctrl[CTRL_ALL_DATA_CLK] = MASK_ALL_DATA_CLK;
    s->ctrl[CTRL_CLK_SET]      = MASK_CLK;
    s->ctrl[CTRL_CLK_CLR]      = MASK_CLK;
    s->ctrl[CTRL_STR_SET]      = MASK_STR;
    s->ctrl[CTRL_STR_CLR]      = MASK_STR;
    s->ctrl[CTRL_OE_SET]       = MASK_OE;
    s->ctrl[CTRL_OE_CLR]       = MASK_OE;
    s->ctrl[CTRL_ADDR_ALL_CLR] = MASK_ADDR_ALL;

    /* addr_set[r] = GPSET0 word to set address lines for row r.
     * addr_set[32] wraps to row 0 (used by loopback). */
    for (int r = 0; r <= 32; r++) {
        int rr = r & 31;
        uint32_t bits = 0;
        if (rr &  1) bits |= (1u << PIN_ADDR_A);
        if (rr &  2) bits |= (1u << PIN_ADDR_B);
        if (rr &  4) bits |= (1u << PIN_ADDR_C);
        if (rr &  8) bits |= (1u << PIN_ADDR_D);
        if (rr & 16) bits |= (1u << PIN_ADDR_E);
        s->addr_set[r] = bits;
    }

    /* Zero pixel buffer — all LEDs off at startup */
    memset(s->pixel_buf, 0, PIXEL_BUF_BYTES);

    /* Bus-address helpers */
#define CB_BUS(i)     (s->bus_base + (uint32_t)((i) * 32u))
#define PX_BUS(r,c)   (s->bus_base + PIXEL_BUF_OFFSET + (uint32_t)(((r)*HUB75_COLS+(c))*4u))
#define CTRL_BUS(i)   (s->bus_base + CTRL_OFFSET     + (uint32_t)((i)*4u))
#define ADDR_BUS(r)   (s->bus_base + ADDR_SET_OFFSET + (uint32_t)((r)*4u))

    const uint32_t TI = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP;

    uint32_t idx = 0;

    for (int row = 0; row < HUB75_ROWS; row++) {
        /* Column data loop — 128 × 4 CBs */
        for (int col = 0; col < HUB75_COLS; col++) {
            /* CB_A: clear data + clock */
            s->cbs[idx] = (dma_cb_t){
                TI, CTRL_BUS(CTRL_ALL_DATA_CLK), GPCLR0_BUS, 4, 0,
                CB_BUS(idx+1), {0,0}
            };
            idx++;

            /* CB_B: set pixel data */
            s->cbs[idx] = (dma_cb_t){
                TI, PX_BUS(row, col), GPSET0_BUS, 4, 0,
                CB_BUS(idx+1), {0,0}
            };
            idx++;

            /* CB_C: CLK high */
            s->cbs[idx] = (dma_cb_t){
                TI, CTRL_BUS(CTRL_CLK_SET), GPSET0_BUS, 4, 0,
                CB_BUS(idx+1), {0,0}
            };
            idx++;

            /* CB_D: CLK low */
            s->cbs[idx] = (dma_cb_t){
                TI, CTRL_BUS(CTRL_CLK_CLR), GPCLR0_BUS, 4, 0,
                CB_BUS(idx+1), {0,0}
            };
            idx++;
        }

        /* Transition sequence — 6 CBs */

        /* CB_T1: blank */
        s->cbs[idx] = (dma_cb_t){
            TI, CTRL_BUS(CTRL_OE_SET), GPSET0_BUS, 4, 0,
            CB_BUS(idx+1), {0,0}
        };
        idx++;

        /* CB_T2: clear address */
        s->cbs[idx] = (dma_cb_t){
            TI, CTRL_BUS(CTRL_ADDR_ALL_CLR), GPCLR0_BUS, 4, 0,
            CB_BUS(idx+1), {0,0}
        };
        idx++;

        /* CB_T3: set address to current row */
        s->cbs[idx] = (dma_cb_t){
            TI, ADDR_BUS(row), GPSET0_BUS, 4, 0,
            CB_BUS(idx+1), {0,0}
        };
        idx++;

        /* CB_T4: STR high */
        s->cbs[idx] = (dma_cb_t){
            TI, CTRL_BUS(CTRL_STR_SET), GPSET0_BUS, 4, 0,
            CB_BUS(idx+1), {0,0}
        };
        idx++;

        /* CB_T5: STR low */
        s->cbs[idx] = (dma_cb_t){
            TI, CTRL_BUS(CTRL_STR_CLR), GPCLR0_BUS, 4, 0,
            CB_BUS(idx+1), {0,0}
        };
        idx++;

        /* CB_T6: unblank — last row loops back to CB 0 */
        uint32_t next = (row < HUB75_ROWS - 1) ? CB_BUS(idx+1) : CB_BUS(0);
        s->cbs[idx] = (dma_cb_t){
            TI, CTRL_BUS(CTRL_OE_CLR), GPCLR0_BUS, 4, 0,
            next, {0,0}
        };
        idx++;
    }

    /* Sanity check */
    if (idx != NUM_CBS) {
        fprintf(stderr, "hub75_dma: CB count mismatch: built %u, expected %u\n",
                idx, NUM_CBS);
    }

#undef CB_BUS
#undef PX_BUS
#undef CTRL_BUS
#undef ADDR_BUS
}

/* -----------------------------------------------------------------------
 * Public: hub75_dma_init
 * ----------------------------------------------------------------------- */
int hub75_dma_init(hub75_dma_state_t *s, int dma_channel) {
    memset(s, 0, sizeof(*s));
    s->dma_channel = dma_channel;
    s->mem_fd  = -1;
    s->vcio_fd = -1;

    /* Open /dev/mem for peripheral access */
    s->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (s->mem_fd < 0) {
        perror("hub75_dma: open /dev/mem");
        return -1;
    }

    /* Map GPIO registers */
    s->gpio = (volatile uint32_t*)mmap(NULL, 4096,
        PROT_READ | PROT_WRITE, MAP_SHARED, s->mem_fd, GPIO_PHYS_BASE);
    if (s->gpio == MAP_FAILED) {
        perror("hub75_dma: mmap GPIO");
        s->gpio = NULL;
        hub75_dma_shutdown(s);
        return -1;
    }

    /* Map DMA channel registers */
    /* Map the full DMA register page — channel offset (ch*0x100) is not page-aligned */
    volatile uint32_t *dma_base = (volatile uint32_t*)mmap(NULL, 4096,
        PROT_READ | PROT_WRITE, MAP_SHARED, s->mem_fd, DMA_PHYS_BASE);
    if (dma_base == MAP_FAILED) {
        perror("hub75_dma: mmap DMA");
        s->dma_ch = NULL;
        hub75_dma_shutdown(s);
        return -1;
    }
    s->dma_ch = dma_base + (uint32_t)dma_channel * (0x100u / 4u);

    /* Open /dev/vcio for mailbox */
    s->vcio_fd = open("/dev/vcio", O_RDWR);
    if (s->vcio_fd < 0) {
        perror("hub75_dma: open /dev/vcio");
        hub75_dma_shutdown(s);
        return -1;
    }

    /* Allocate 1 MB physically contiguous DMA buffer */
    s->bus_base = mbox_alloc(s->vcio_fd, DMA_BUF_ALLOC, &s->mb_handle);
    if (!s->bus_base) {
        hub75_dma_shutdown(s);
        return -1;
    }

    /* On BCM2710 with MEM_FLAG_DIRECT|MEM_FLAG_COHERENT, bus addr prefix is 0xC0...
     * Strip top 2 bits to get physical address. */
    uint32_t phys = s->bus_base & ~0xC0000000u;
    fprintf(stderr, "hub75_dma: DMA buffer bus=0x%08X phys=0x%08X\n",
            s->bus_base, phys);

    s->virt_base = mmap(NULL, DMA_BUF_ALLOC,
        PROT_READ | PROT_WRITE, MAP_SHARED, s->mem_fd, phys);
    if (s->virt_base == MAP_FAILED) {
        perror("hub75_dma: mmap DMA buffer");
        s->virt_base = NULL;
        hub75_dma_shutdown(s);
        return -1;
    }

    /* Set up sub-pointers */
    s->cbs       = (dma_cb_t*)((char*)s->virt_base + 0);
    s->pixel_buf = (uint32_t*)((char*)s->virt_base + PIXEL_BUF_OFFSET);
    s->addr_set  = (uint32_t*)((char*)s->virt_base + ADDR_SET_OFFSET);
    s->ctrl      = (uint32_t*)((char*)s->virt_base + CTRL_OFFSET);

    /* Configure all HUB75 GPIO pins as outputs */
    for (int i = 0; i < NUM_HUB75_PINS; i++)
        gpio_set_output(s->gpio, HUB75_PINS[i]);

    /* Safe initial state: blank display, all data + address low */
    s->gpio[GPSET0] = MASK_OE;          /* OE high = display disabled  */
    s->gpio[GPCLR0] = MASK_ALL_DATA_CLK | MASK_ADDR_ALL | MASK_STR;

    /* Build DMA control block chain */
    build_cb_chain(s);

    /* Start DMA channel */
    s->dma_ch[DMA_REG_CS] = DMA_CS_RESET;
    usleep(10);
    s->dma_ch[DMA_REG_DEBUG] = 7u;          /* clear error flags */
    s->dma_ch[DMA_REG_CS] = 0;
    s->dma_ch[DMA_REG_CONBLK_AD] = s->bus_base; /* first CB = start of buffer */
    s->dma_ch[DMA_REG_CS] = DMA_CS_ACTIVE;

    fprintf(stderr, "hub75_dma: DMA channel %d started\n", dma_channel);
    return 0;
}

/* -----------------------------------------------------------------------
 * Public: hub75_dma_shutdown
 * ----------------------------------------------------------------------- */
void hub75_dma_shutdown(hub75_dma_state_t *s) {
    /* Stop DMA */
    if (s->dma_ch) {
        s->dma_ch[DMA_REG_CS] = 0; /* clear ACTIVE */
        usleep(100);
    }

    /* Blank display */
    if (s->gpio)
        s->gpio[GPSET0] = MASK_OE;

    if (s->virt_base && s->virt_base != MAP_FAILED) {
        munmap(s->virt_base, DMA_BUF_ALLOC);
        s->virt_base = NULL;
    }
    if (s->mb_handle && s->vcio_fd >= 0) {
        mbox_free(s->vcio_fd, s->mb_handle);
        s->mb_handle = 0;
    }
    if (s->dma_ch && s->dma_ch != MAP_FAILED) {
        /* compute base of the mapped DMA page from stored channel offset */
        volatile uint32_t *dma_base2 = s->dma_ch - (uint32_t)s->dma_channel * (0x100u / 4u);
        munmap((void*)dma_base2, 4096);
        s->dma_ch = NULL;
    }
    if (s->gpio && s->gpio != MAP_FAILED) {
        munmap((void*)s->gpio, 4096);
        s->gpio = NULL;
    }
    if (s->vcio_fd >= 0) { close(s->vcio_fd); s->vcio_fd = -1; }
    if (s->mem_fd  >= 0) { close(s->mem_fd);  s->mem_fd  = -1; }
}

/* -----------------------------------------------------------------------
 * Public: hub75_dma_update_panels
 *
 * Recomputes all 32×128 GPSET0 words from two 128×64 pixel slices.
 * Writes go to coherent DMA memory — visible to the DMA engine immediately.
 * No DMA stop/restart needed; the engine picks up new values on its next pass.
 * ----------------------------------------------------------------------- */
void hub75_dma_update_panels(hub75_dma_state_t *s,
                              const hub75_rgb_t *slice0,
                              const hub75_rgb_t *slice1) {
    for (int row = 0; row < HUB75_ROWS; row++) {
        for (int col = 0; col < HUB75_COLS; col++) {
            /* Panel 0 — top half: row r, col c */
            const hub75_rgb_t p0t = slice0[row * HUB75_COLS + col];
            /* Panel 0 — bottom half: row r+32, col c */
            const hub75_rgb_t p0b = slice0[(row + HUB75_ROWS) * HUB75_COLS + col];
            /* Panel 1 — top and bottom halves */
            const hub75_rgb_t p1t = slice1[row * HUB75_COLS + col];
            const hub75_rgb_t p1b = slice1[(row + HUB75_ROWS) * HUB75_COLS + col];

            uint32_t gpset = 0;

            /* Color swap: pixel.r → G pin, pixel.g → B pin, pixel.b → R pin */
            if (p0t.r) gpset |= (1u << PIN_C0_R);
            if (p0t.g) gpset |= (1u << PIN_C0_G);
            if (p0t.b) gpset |= (1u << PIN_C0_B);

            if (p0b.r) gpset |= (1u << PIN_C0_R2);
            if (p0b.g) gpset |= (1u << PIN_C0_G2);
            if (p0b.b) gpset |= (1u << PIN_C0_B2);

            if (p1t.r) gpset |= (1u << PIN_C1_R);
            if (p1t.g) gpset |= (1u << PIN_C1_G);
            if (p1t.b) gpset |= (1u << PIN_C1_B);

            if (p1b.r) gpset |= (1u << PIN_C1_R2);
            if (p1b.g) gpset |= (1u << PIN_C1_G2);
            if (p1b.b) gpset |= (1u << PIN_C1_B2);

            s->pixel_buf[row * HUB75_COLS + col] = gpset;
        }
    }
    __sync_synchronize(); /* memory barrier — ensure all writes reach DMA memory */
}
