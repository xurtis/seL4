/*
 * Copyright 2019, DornerWorks
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_DORNERWORKS_BSD)
 */
#include <config.h>
#include <types.h>
#include <arch/machine/gic_v3.h>

#define IRQ_SET_ALL 0xffffffff

#define RDIST_BANK_SZ 0x00010000
/* One GICR region and one GICR_SGI region */
#define GICR_PER_CORE_SIZE  (0x20000)
/* Assume 8 cores */
#define GICR_SIZE           (0x100000)

#define GIC_DEADLINE_MS 2
#define GIC_REG_WIDTH   32

volatile struct gic_dist_map *const gic_dist = (volatile struct gic_dist_map *)(GICD_PPTR);
volatile void *const gicr_base = (volatile uint8_t *)(GICR_PPTR);

uint32_t active_irq[CONFIG_MAX_NUM_NODES] = {IRQ_NONE};
volatile struct gic_rdist_map *gic_rdist_map[CONFIG_MAX_NUM_NODES] = { 0 };
volatile struct gic_rdist_sgi_ppi_map *gic_rdist_sgi_ppi_map[CONFIG_MAX_NUM_NODES] = { 0 };

#ifdef CONFIG_ARCH_AARCH64
#define MPIDR_AFF0(x) (x & 0xff)
#define MPIDR_AFF1(x) ((x >> 8) & 0xff)
#define MPIDR_AFF2(x) ((x >> 16) & 0xff)
#define MPIDR_AFF3(x) ((x >> 32) & 0xff)
#else
#define MPIDR_AFF0(x) (x & 0xff)
#define MPIDR_AFF1(x) ((x >> 8) & 0xff)
#define MPIDR_AFF2(x) ((x >> 16) & 0xff)
#define MPIDR_AFF3(x) (0)
#endif
#define MPIDR_MT(x)   (x & BIT(24))

static word_t mpidr_map[CONFIG_MAX_NUM_NODES];

static inline word_t get_mpidr(word_t core_id)
{
    return mpidr_map[core_id];
}

static inline word_t get_current_mpidr(void)
{
    word_t core_id = CURRENT_CPU_INDEX();
    return get_mpidr(core_id);
}

static inline uint64_t mpidr_to_gic_affinity(void)
{
    word_t mpidr = get_current_mpidr();
    uint64_t affinity = 0;
    affinity = (uint64_t)MPIDR_AFF3(mpidr) << 32 | MPIDR_AFF2(mpidr) << 16 |
               MPIDR_AFF1(mpidr) << 8  | MPIDR_AFF0(mpidr);
    return affinity;
}

/* Wait for completion of a distributor change */
static uint32_t gicv3_do_wait_for_rwp(volatile uint32_t *ctlr_addr)
{
    uint32_t val;
    bool_t waiting = true;
    uint32_t ret = 0;

    uint32_t gpt_cnt_tval = 0;
    uint32_t deadline_ms =  GIC_DEADLINE_MS;
    uint32_t gpt_cnt_ciel;

    /* Check the value before reading the generic timer */
    val = *ctlr_addr;
    if (!(val & GICD_CTLR_RWP)) {
        return 0;
    }
    SYSTEM_READ_WORD(CNTFRQ, gpt_cnt_tval);
    gpt_cnt_ciel = gpt_cnt_tval + (deadline_ms * TICKS_PER_MS);

    while (waiting) {
        SYSTEM_READ_WORD(CNTFRQ, gpt_cnt_tval);
        val = *ctlr_addr;

        if (gpt_cnt_tval >= gpt_cnt_ciel) {
            printf("GICV3 RWP Timeout after %u ms\n", deadline_ms);
            ret = 1;
            waiting = false;

        } else if (!(val & GICD_CTLR_RWP)) {
            ret = 0;
            waiting = false;
        }
    }
    return ret;
}

static void gicv3_dist_wait_for_rwp(void)
{
    gicv3_do_wait_for_rwp(&gic_dist->ctlr);
}

static void gicv3_redist_wait_for_rwp(void)
{
    gicv3_do_wait_for_rwp(&gic_rdist_map[CURRENT_CPU_INDEX()]->ctlr);
}

static void gicv3_enable_sre(void)
{
    uint32_t val = 0;

    /* ICC_SRE_EL1 */
    SYSTEM_READ_WORD(ICC_SRE_EL1, val);
    val |= GICC_SRE_EL1_SRE;

    SYSTEM_WRITE_WORD(ICC_SRE_EL1, val);
    isb();
}


BOOT_CODE static void dist_init(void)
{
    word_t i;
    uint32_t type;
    unsigned int nr_lines;
    uint64_t affinity;
    uint32_t priority;

    /* Disable GIC Distributor */
    gic_dist->ctlr = 0;
    gicv3_dist_wait_for_rwp();

    type = gic_dist->typer;

    nr_lines = GIC_REG_WIDTH * ((type & GICD_TYPE_LINESNR) + 1);

    /* Assume level-triggered */
    for (i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 16) {
        gic_dist->icfgrn[(i / 16)] = 0;
    }

    /* Default priority for global interrupts */
    priority = (GIC_PRI_IRQ << 24 | GIC_PRI_IRQ << 16 | GIC_PRI_IRQ << 8 |
                GIC_PRI_IRQ);
    for (i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 4) {
        gic_dist->ipriorityrn[(i / 4)] = priority;
    }
    /* Disable and clear all global interrupts */
    for (i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 32) {
        gic_dist->icenablern[(i / 32)] = IRQ_SET_ALL;
        gic_dist->icpendrn[(i / 32)] = IRQ_SET_ALL;
    }

    /* Turn on the distributor */
    gic_dist->ctlr = GICD_CTL_ENABLE | GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1NS | GICD_CTLR_ENABLE_G0;
    gicv3_dist_wait_for_rwp();

    /* Route all global IRQs to this CPU */
    affinity = mpidr_to_gic_affinity();
    for (i = NR_GIC_LOCAL_IRQS; i < nr_lines; i++) {
        gic_dist->iroutern[i] = affinity;
    }
}

BOOT_CODE static void gicr_locate_interface(void)
{
    word_t offset;
    int core_id = SMP_TERNARY(getCurrentCPUIndex(), 0);
    word_t mpidr = get_current_mpidr();
    uint32_t val;

    /*
     * Iterate through all redistributor interfaces looking for one that matches
     * our mpidr.
     */
    for (offset = 0; offset < GICR_SIZE; offset += GICR_PER_CORE_SIZE) {

        uint64_t typer = ((struct gic_rdist_map *)((word_t)gicr_base + offset))->typer;
        if ((typer >> 32) == ((MPIDR_AFF3(mpidr) << 24) |
                              (MPIDR_AFF2(mpidr) << 16) |
                              (MPIDR_AFF1(mpidr) <<  8) |
                              MPIDR_AFF0(mpidr))) {

            word_t gicr = (word_t)gicr_base + offset;
            if (gic_rdist_map[core_id] != NULL || gic_rdist_sgi_ppi_map[core_id] != NULL) {
                printf("GICv3: %s[%d] %p is not null\n",
                       gic_rdist_map[core_id] == NULL ? "gic_rdist_map" : "gic_rdist_sgi_ppi_map",
                       core_id,
                       gic_rdist_map[core_id] == NULL ? (void *)gic_rdist_map[core_id] : (void *)gic_rdist_sgi_ppi_map[core_id]);
                halt();
            }
            gic_rdist_map[core_id] = (void *)gicr;
            gic_rdist_sgi_ppi_map[core_id] = (void *)(gicr + RDIST_BANK_SZ);

            /*
             * GICR_WAKER should be Read-all-zeros in Non-secure world
             * and we expect redistributors to be alread awoken by an earlier loader.
             * However if we get a value back then something is probably wrong.
             */
            val = gic_rdist_map[core_id]->waker;
            if (val & GICR_WAKER_ChildrenAsleep) {
                printf("GICv3: GICR_WAKER returned non-zero %x\n", val);
                halt();
            }

            break;
        }
    }
    if (offset >= GICR_SIZE) {
        printf("GICv3: GICR base for CPU %d %d %d %d (Logic ID %d) not found\n",
               (int)MPIDR_AFF3(mpidr), (int)MPIDR_AFF2(mpidr),
               (int)MPIDR_AFF1(mpidr), (int)MPIDR_AFF0(mpidr), core_id);
        halt();
    }


}

BOOT_CODE static void gicr_init(void)
{
    int i;
    uint32_t priority;

    /* Find redistributor for this core. */
    gicr_locate_interface();

    /* Deactivate SGIs/PPIs */
    gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->icactiver0 = ~0;

    /* Set priority on PPI and SGI interrupts */
    priority = (GIC_PRI_IRQ << 24 | GIC_PRI_IRQ << 16 | GIC_PRI_IRQ << 8 |
                GIC_PRI_IRQ);
    for (i = 0; i < NR_GIC_LOCAL_IRQS; i += 4) {
        gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->ipriorityrn[i / 4] = priority;
    }

    /*
     * Disable all PPI interrupts, ensure all SGI interrupts are
     * enabled.
     */
    gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->icenabler0 = 0xffff0000;
    gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->isenabler0 = 0x0000ffff;

    /* Set ICFGR1 for PPIs as level-triggered */
    gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->icfgr1 = 0x0;

    gicv3_redist_wait_for_rwp();
}

BOOT_CODE static void cpu_iface_init(void)
{
    uint32_t icc_ctlr = 0;

    /* Enable system registers */
    gicv3_enable_sre();

    /* No priority grouping: ICC_BPR1_EL1 */
    SYSTEM_WRITE_WORD(ICC_BPR1_EL1, 0);

    /* Set priority mask register: ICC_PMR_EL1 */
    SYSTEM_WRITE_WORD(ICC_PMR_EL1, DEFAULT_PMR_VALUE);

    /* EOI drops priority and deactivates the interrupt: ICC_CTLR_EL1 */
    SYSTEM_READ_WORD(ICC_CTLR_EL1, icc_ctlr);
    icc_ctlr &= ~BIT(GICC_CTLR_EL1_EOImode_drop);
    SYSTEM_WRITE_WORD(ICC_CTLR_EL1, icc_ctlr);

    /* Enable Group1 interrupts: ICC_IGRPEN1_EL1 */
    SYSTEM_WRITE_WORD(ICC_IGRPEN1_EL1, 1);

    /* Sync at once at the end of cpu interface configuration */
    isb();
}

void setIRQTrigger(irq_t irq, bool_t trigger)
{

    /* GICv3 has read-only GICR_ICFG0 for SGI with
     * default value 0xaaaaaaaa, and read-write GICR_ICFG1
     * for PPI with default 0x00000000.*/
    if (is_sgi(irq)) {
        return;
    }
    int word = irq >> 4;
    int bit = ((irq & 0xf) * 2);
    uint32_t icfgr = 0;
    if (is_ppi(irq)) {
        icfgr = gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->icfgr1;
    } else {
        icfgr = gic_dist->icfgrn[word];
    }

    if (trigger) {
        icfgr |= (0b10 << bit);
    } else {
        icfgr &= ~(0b11 << bit);
    }

    if (is_ppi(irq)) {
        gic_rdist_sgi_ppi_map[CURRENT_CPU_INDEX()]->icfgr1 = icfgr;
    } else {
        /* Update GICD_ICFGR<n>. Note that the interrupt should
         * be disabled before changing the field, and this function
         * assumes the caller has disabled the interrupt. */
        gic_dist->icfgrn[word] = icfgr;
    }

    return;
}

BOOT_CODE void initIRQController(void)
{
    dist_init();
}

BOOT_CODE void cpu_initLocalIRQController(void)
{
    word_t mpidr = 0;
    SYSTEM_READ_WORD(MPIDR, mpidr);

    mpidr_map[CURRENT_CPU_INDEX()] = mpidr;

    gicr_init();
    cpu_iface_init();
}

#ifdef ENABLE_SMP_SUPPORT
/*
* 25-24: target lister filter
* 0b00 - send the ipi to the CPU interfaces specified in the CPU target list
* 0b01 - send the ipi to all CPU interfaces except the cpu interface.
*        that requrested teh ipi
* 0b10 - send the ipi only to the CPU interface that requested the IPI.
* 0b11 - reserved
*.
* 23-16: CPU targets list
* each bit of CPU target list [7:0] refers to the corresponding CPU interface.
* 3-0:   SGIINTID
* software generated interrupt id, from 0 to 15...
*/
void ipiBroadcast(irq_t irq, bool_t includeSelfCPU)
{
    gic_dist->sgi_control = (!includeSelfCPU << GICD_SGIR_TARGETLISTFILTER_SHIFT) | (irq << GICD_SGIR_SGIINTID_SHIFT);
}

void ipi_send_target(irq_t irq, word_t cpuTargetList)
{
    gic_dist->sgi_control = (cpuTargetList << GICD_SGIR_CPUTARGETLIST_SHIFT) | (irq << GICD_SGIR_SGIINTID_SHIFT);
}
#endif /* ENABLE_SMP_SUPPORT */
