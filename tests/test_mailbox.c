/* test_mailbox.c — Verify /dev/vcio mailbox DMA memory allocation works.
 * Run with: sudo ./build/test_mailbox
 * Expected: bus address with 0xC0000000 prefix, read-back of written pattern. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#define IOCTL_MBOX_PROPERTY  _IOWR(100, 0, char*)
#define MBOX_TAG_MEM_ALLOC   0x3000Cu
#define MBOX_TAG_MEM_LOCK    0x3000Du
#define MBOX_TAG_MEM_UNLOCK  0x3000Eu
#define MBOX_TAG_MEM_FREE    0x3000Fu
#define MEM_FLAGS  0x0Cu   /* DIRECT | COHERENT */

int main(void) {
    int vcio = open("/dev/vcio", O_RDWR);
    if (vcio < 0) { perror("open /dev/vcio"); return 1; }

    /* Allocate 4096 bytes */
    uint32_t msg[9];
    msg[0] = sizeof(msg); msg[1] = 0;
    msg[2] = MBOX_TAG_MEM_ALLOC; msg[3] = 12; msg[4] = 0;
    msg[5] = 4096; msg[6] = 4096; msg[7] = MEM_FLAGS; msg[8] = 0;
    if (ioctl(vcio, IOCTL_MBOX_PROPERTY, msg) < 0 || !(msg[1] & 0x80000000u)) {
        fprintf(stderr, "FAIL: mem_alloc\n"); return 1;
    }
    uint32_t handle = msg[5];
    printf("Allocated handle: 0x%08X\n", handle);

    /* Lock to get bus address */
    uint32_t lmsg[7];
    lmsg[0] = sizeof(lmsg); lmsg[1] = 0;
    lmsg[2] = MBOX_TAG_MEM_LOCK; lmsg[3] = 4; lmsg[4] = 0;
    lmsg[5] = handle; lmsg[6] = 0;
    if (ioctl(vcio, IOCTL_MBOX_PROPERTY, lmsg) < 0) {
        fprintf(stderr, "FAIL: mem_lock\n"); return 1;
    }
    uint32_t bus_addr = lmsg[5];
    printf("Bus address:  0x%08X\n", bus_addr);
    printf("Prefix check: 0x%02X (expect 0xC0)\n", bus_addr >> 24);

    /* Map to virtual address */
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) { perror("open /dev/mem"); return 1; }
    uint32_t phys = bus_addr & ~0xC0000000u;
    printf("Physical addr: 0x%08X\n", phys);

    void *virt = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, phys);
    if (virt == MAP_FAILED) { perror("mmap"); return 1; }
    printf("Virtual addr:  %p\n", virt);

    /* Write pattern and read back */
    uint32_t *p = (uint32_t*)virt;
    p[0] = 0xDEADBEEFu;
    p[1] = 0x12345678u;
    __sync_synchronize();
    printf("Write: 0x%08X 0x%08X\n", p[0], p[1]);
    printf("Read:  0x%08X 0x%08X\n", p[0], p[1]);
    if (p[0] != 0xDEADBEEFu || p[1] != 0x12345678u)
        printf("FAIL: readback mismatch!\n");
    else
        printf("PASS: readback OK\n");

    /* Cleanup */
    munmap(virt, 4096);
    close(memfd);

    uint32_t fmsg[7];
    fmsg[0] = sizeof(fmsg); fmsg[1] = 0;
    fmsg[2] = MBOX_TAG_MEM_UNLOCK; fmsg[3] = 4; fmsg[4] = 0;
    fmsg[5] = handle; fmsg[6] = 0;
    ioctl(vcio, IOCTL_MBOX_PROPERTY, fmsg);
    fmsg[1] = 0; fmsg[4] = 0; fmsg[5] = handle;
    fmsg[2] = MBOX_TAG_MEM_FREE;
    ioctl(vcio, IOCTL_MBOX_PROPERTY, fmsg);

    close(vcio);
    printf("Freed. Done.\n");
    return 0;
}
