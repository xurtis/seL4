#
# Copyright 2017, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the GNU General Public License version 2. Note that NO WARRANTY is provided.
# See "LICENSE_GPLv2.txt" for details.
#
# @TAG(DATA61_GPL)
#

cmake_minimum_required(VERSION 3.7.2)

declare_platform(zynq7000 KernelPlatformZynq7000 PLAT_ZYNQ7000 KernelSel4ArchAarch32)

if(KernelPlatformZynq7000)
    declare_seL4_arch(aarch32)
    set(KernelArmCortexA9 ON)
    set(KernelArchArmV7a ON)
    config_set(KernelARMPlatform ARM_PLAT zynq7000)
    config_set(KernelArmMach MACH "zynq")
    list(APPEND KernelDTSList "tools/dts/zynq7000.dts")
    declare_default_headers(
        TIMER_FREQUENCY 400000000llu
        MAX_IRQ 92
        NUM_PPI 32
        TIMER drivers/timer/arm_priv.h
        INTERRUPT_CONTROLLER arch/machine/gic_v2.h
    )
endif()

add_sources(
    DEP "KernelPlatformZynq7000"
    CFILES src/arch/arm/machine/l2c_310.c src/arch/arm/machine/gic_v2.c
)
