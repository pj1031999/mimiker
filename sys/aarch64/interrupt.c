#include <sys/klog.h>
#include <sys/mimiker.h>
#include <sys/context.h>
#include <aarch64/interrupt.h>
#include <aarch64/armreg.h>

#define _DAIF(x) ((DAIF_##x##_MASKED) >> 6)

void cpu_intr_disable(void) {
  __asm __volatile("msr daifset, %0" ::"i"(_DAIF(I)));
}

void cpu_intr_enable(void) {
  __asm __volatile("msr daifclr, %0" ::"i"(_DAIF(I)));
}

bool cpu_intr_disabled(void) {
  uint32_t daif = READ_SPECIALREG(daif);
  return (daif & DAIF_I_MASKED) != 0;
}

extern uint32_t mmio_read(uint32_t reg);
extern void mmio_write(uint32_t reg, uint32_t data);

#define BCM2836
#include <dev/bcm2835reg.h>

/* BCM2836 interrupt handling (core private interrupts) */

#define TIMER_IRQ_CTRL_N(x)                                                    \
  (BCM2836_ARM_LOCAL_BASE + BCM2836_LOCAL_TIMER_IRQ_CONTROLN(x))
#define MAILBOX_IRQ_CTRL_N(x)                                                  \
  (BCM2836_ARM_LOCAL_BASE + BCM2836_LOCAL_MAILBOX_IRQ_CONTROLN(x))
#define INTC_IRQPENDING_N(x)                                                   \
  (BCM2836_ARM_LOCAL_BASE + BCM2836_LOCAL_INTC_IRQPENDINGN(x))

#define BCM2836_NCPUS 4
#define BCM2836_NIRQPERCPU 32

#define BCM2836_INT_LOCALBASE 0
#define BCM2836_INT_BASECPUN(n)                                                \
  (BCM2836_INT_LOCALBASE + ((n)*BCM2836_NIRQPERCPU))
#define BCM2836_NIRQ (BCM2836_NIRQPERCPU * BCM2836_NCPUS)

/* BCM2835 interrupt handling (peripherals) */

#define BCM2835_ARMICU(x)                                                      \
  BCM2835_PERIPHERALS_BUS_TO_PHYS(BCM2835_ARMICU_BASE + (x))

#define BCM2835_INT_BASE BCM2836_NIRQ

static void bcm2835_intr_init(void) {
  mmio_write(BCM2835_ARMICU(BCM2835_INTC_IRQBDISABLE), -1);
  mmio_write(BCM2835_ARMICU(BCM2835_INTC_IRQ1DISABLE), -1);
  mmio_write(BCM2835_ARMICU(BCM2835_INTC_IRQ2DISABLE), -1);
}

static void bcm2836_local_intr_init(void) {
  mmio_write(TIMER_IRQ_CTRL_N(0), 0);
  mmio_write(MAILBOX_IRQ_CTRL_N(0), 0);
}

static void bcm2836_intr_dispatch(uint32_t reg, unsigned irq_base) {
  uint32_t pending = mmio_read(reg);
  unsigned irq;

  /* __builtin_ffs returns one plus the index of the least significant
   * 1-bit of x, or if x is zero, returns zero. */
  while ((irq = ffs(pending))) {
    irq -= 1;

    unsigned _irq = irq_base + irq;

    klog("Spurious interrupt %d!\n", _irq);

    pending &= ~(1 << irq);
  }
}

void cpu_intr_init(void) {
  bcm2835_intr_init();
  bcm2836_local_intr_init();
}

void cpu_intr_handler(void) {
  bcm2836_intr_dispatch(INTC_IRQPENDING_N(0), BCM2836_INT_BASECPUN(0));
  bcm2836_intr_dispatch(BCM2835_ARMICU(BCM2835_INTC_IRQBPENDING),
               BCM2835_INT_BASICBASE);
  bcm2836_intr_dispatch(BCM2835_ARMICU(BCM2835_INTC_IRQ1PENDING),
                        BCM2835_INT_GPU0BASE);
  bcm2836_intr_dispatch(BCM2835_ARMICU(BCM2835_INTC_IRQ2PENDING),
                        BCM2835_INT_GPU1BASE);
}
