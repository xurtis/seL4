#
# Copyright 2019, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the GNU General Public License version 2. Note that NO WARRANTY is provided.
# See "LICENSE_GPLv2.txt" for details.
#
# @TAG(DATA61_GPL)
#

#
# Architecture selection
#
set(configure_string "")
set(c_sources "")
set(asm_sources "")
set(bf_declarations "")

include(${CMAKE_CURRENT_LIST_DIR}/../tools/internal.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../tools/helpers.cmake)
# Create and set all of the Kernel config options that can
# be derived from the seL4 arch which is one of the following:
# aarch32, aarch64, arm_hyp, riscv32, riscv64, x86_64, ia32
# This macro is intended to be called from within a platform config.
macro(declare_seL4_arch sel4_arch)
    set(KernelSel4Arch "${sel4_arch}" CACHE STRING "")
    config_choice(
        KernelSel4Arch
        SEL4_ARCH
        "Architecture mode for building the kernel"
        "aarch32;KernelSel4ArchAarch32;ARCH_AARCH32"
        "aarch64;KernelSel4ArchAarch64;ARCH_AARCH64"
        "arm_hyp;KernelSel4ArchArmHyp;ARCH_ARM_HYP"
        "riscv32;KernelSel4ArchRiscV32;ARCH_RISCV32"
        "riscv64;KernelSel4ArchRiscV64;ARCH_RISCV64"
        "x86_64;KernelSel4ArchX86_64;ARCH_X86_64"
        "ia32;KernelSel4ArchIA32;ARCH_IA32"
    )

    config_choice(
        KernelArch
        ARCH
        "Architecture to use when building the kernel"
        "arm;KernelArchARM;ARCH_ARM;KernelSel4ArchAarch32 OR KernelSel4ArchAarch64 OR KernelSel4ArchArmHyp"
        "riscv;KernelArchRiscV;ARCH_RISCV;KernelSel4ArchRiscV32 OR KernelSel4ArchRiscV64"
        "x86;KernelArchX86;ARCH_X86;KernelSel4ArchX86_64 OR KernelSel4ArchIA32"
    )

    # The following config options are legacy and can be removed if they
    # aren't used anywhere anymore.
    if(KernelArchARM)
        config_set(KernelArmSel4Arch ARM_SEL4_ARCH "${KernelSel4Arch}")
    elseif(KernelArchRiscV)
        config_set(KernelRiscVSel4Arch RISCV_SEL4_ARCH "${KernelSel4Arch}")
    elseif(KernelArchX86)
        config_set(KernelX86Sel4Arch X86_SEL4_ARCH "${KernelSel4Arch}")
    endif()

    # arm-hyp masquerades as an aarch32 build
    if(KernelSel4ArchArmHyp)
        config_set(KernelSel4ArmHypAarch32 ARCH_AARCH32 ON)
        set(KernelSel4ArchAarch32 ON CACHE INTERNAL "" FORCE)
    else()
        config_set(KernelSel4ArmHypAarch32 ARCH_AARCH32 OFF)
    endif()

    # Set kernel mode options
    if(KernelSel4ArchAarch32 OR KernelSel4ArchArmHyp OR KernelSel4ArchRiscV32 OR KernelSel4ArchIA32)
        set_kernel_32()
    elseif(KernelSel4ArchAarch64 OR KernelSel4ArchRiscV64 OR KernelSel4ArchX86_64)
        set_kernel_64()
    endif()

endmacro()

# Register a platform's config options to be set if it is selected.
# Additionally, the kernel_platforms variable can be used as a record of all
# platforms that can be built once the platform configuration files have been
# processed.
# name: name of the platform, KernelPlatform will be set to this.
# config1: the CMake configuration name. Such as KernelPlatImx7.
# config2: the C header file config name without CONFIG_. Such as PLAT_IMX7_SABRE.
# enable_test: A CMake boolean formula that allows the option to be selected.
#     e.g. "KernelSel4ArchAarch32", or "KernelSel4ArchX86_64 OR KernelSel4ArchIA32"
macro(declare_platform name config1 config2 enable_test)
    list(APPEND kernel_platforms "${name}\;${config1}\;${config2}\;${enable_test}")
    if("${KernelPlatform}" STREQUAL ${name})
        set(${config1} ON CACHE INTERNAL "" FORCE)
        # Write KernelPlatform into the cache in case it is only a local variable
        set(KernelPlatform ${KernelPlatform} CACHE STRING "")
    else()
        set(${config1} OFF CACHE INTERNAL "" FORCE)
    endif()
endmacro()

unset(CONFIGURE_PLIC_MAX_NUM_INT CACHE)
unset(CONFIGURE_TIMER_FREQUENCY CACHE)
unset(CONFIGURE_MAX_IRQ CACHE)
unset(CONFIGURE_NUM_PPI CACHE)
unset(CONFIGURE_INTERRUPT_CONTROLLER CACHE)
unset(CONFIGURE_TIMER CACHE)
unset(CONFIGURE_SMMU CACHE)
function(declare_default_headers)
    cmake_parse_arguments(
        PARSE_ARGV
        0
        CONFIGURE
        ""
        "TIMER_FREQUENCY;MAX_IRQ;NUM_PPI;PLIC_MAX_NUM_INT;INTERRUPT_CONTROLLER;TIMER;SMMU"
        ""
    )
    set(CONFIGURE_TIMER_FREQUENCY "${CONFIGURE_TIMER_FREQUENCY}" CACHE INTERNAL "")
    set(CONFIGURE_MAX_IRQ "${CONFIGURE_MAX_IRQ}" CACHE INTERNAL "")
    set(CONFIGURE_NUM_PPI "${CONFIGURE_NUM_PPI}" CACHE INTERNAL "")
    set(CONFIGURE_PLIC_MAX_NUM_INT "${CONFIGURE_PLIC_MAX_NUM_INT}" CACHE INTERNAL "")
    set(CONFIGURE_INTERRUPT_CONTROLLER "${CONFIGURE_INTERRUPT_CONTROLLER}" CACHE INTERNAL "")
    set(CONFIGURE_TIMER "${CONFIGURE_TIMER}" CACHE INTERNAL "")
    set(CONFIGURE_SMMU "${CONFIGURE_SMMU}" CACHE INTERNAL "")
endfunction()

# For all of the common variables we set a default value here if they haven't
# been set by a platform.
foreach(
    var
    IN
    ITEMS
    KernelArmCortexA7
    KernelArmCortexA8
    KernelArmCortexA9
    KernelArmCortexA15
    KernelArmCortexA53
    KernelArmCortexA57
    KernelArm1136JF_S
    KernelArchArmV6
    KernelArchArmV7a
    KernelArchArmV7ve
    KernelArchArmV8a
    KernelArmPASizeBits40
    KernelArmPASizeBits44
)
    unset(${var} CACHE)
    set(${var} OFF)
endforeach()
unset(KernelArmMach CACHE)
unset(KernelArmMachFeatureModifiers CACHE)
unset(KernelArmCPU CACHE)
unset(KernelArmArmV CACHE)

file(GLOB result ${CMAKE_CURRENT_LIST_DIR}/../src/plat/*/config.cmake)
list(SORT result)

foreach(file ${result})
    include("${file}")
endforeach()

config_choice(KernelPlatform PLAT "Select the platform" ${kernel_platforms})

# Now enshrine all the common variables in the config
config_set(KernelArmCortexA7 ARM_CORTEX_A7 "${KernelArmCortexA7}")
config_set(KernelArmCortexA8 ARM_CORTEX_A8 "${KernelArmCortexA8}")
config_set(KernelArmCortexA9 ARM_CORTEX_A9 "${KernelArmCortexA9}")
config_set(KernelArmCortexA15 ARM_CORTEX_A15 "${KernelArmCortexA15}")
config_set(KernelArmCortexA53 ARM_CORTEX_A53 "${KernelArmCortexA53}")
config_set(KernelArmCortexA57 ARM_CORTEX_A57 "${KernelArmCortexA57}")
config_set(KernelArm1136JF_S ARM1136JF_S "${KernelArm1136JF_S}")
config_set(KernelArchArmV6 ARCH_ARM_V6 "${KernelArchArmV6}")
config_set(KernelArchArmV7a ARCH_ARM_V7A "${KernelArchArmV7a}")
config_set(KernelArchArmV7ve ARCH_ARM_V7VE "${KernelArchArmV7ve}")
config_set(KernelArchArmV8a ARCH_ARM_V8A "${KernelArchArmV8a}")
config_set(KernelArmPASizeBits40 ARM_PA_SIZE_BITS_40 "${KernelArmPASizeBits40}")
config_set(KernelArmPASizeBits44 ARM_PA_SIZE_BITS_44 "${KernelArmPASizeBits44}")

# Check for v7ve before v7a as v7ve is a superset and we want to set the
# actual armv to that, but leave armv7a config enabled for anything that
# checks directly against it
if(KernelArchArmV7ve)
    set(KernelArmArmV "armv7ve" CACHE INTERNAL "")
elseif(KernelArchArmV7a)
    set(KernelArmArmV "armv7-a" CACHE INTERNAL "")
elseif(KernelArchArmV8a)
    set(KernelArmArmV "armv8-a" CACHE INTERNAL "")
elseif(KernelArchArmV6)
    set(KernelArmArmV "armv6" CACHE INTERNAL "")
endif()
if(KernelArmCortexA7)
    set(KernelArmCPU "cortex-a7" CACHE INTERNAL "")
elseif(KernelArmCortexA8)
    set(KernelArmCPU "cortex-a8" CACHE INTERNAL "")
elseif(KernelArmCortexA9)
    set(KernelArmCPU "cortex-a9" CACHE INTERNAL "")
elseif(KernelArmCortexA15)
    set(KernelArmCPU "cortex-a15" CACHE INTERNAL "")
elseif(KernelArmCortexA53)
    set(KernelArmCPU "cortex-a53" CACHE INTERNAL "")
elseif(KernelArmCortexA57)
    set(KernelArmCPU "cortex-a57" CACHE INTERNAL "")
elseif(KernelArm1136JF_S)
    set(KernelArmCPU "arm1136jf-s" CACHE INTERNAL "")
endif()
if(KernelArchARM)
    config_set(KernelArmMach ARM_MACH "${KernelArmMach}")
endif()

set(config_configure_string ${configure_string} CACHE INTERNAL "")
set(config_c_sources "")
set(config_asm_sources "")
set(config_bf_declarations "")
foreach(file IN LISTS c_sources)
    string(
        REPLACE
            "${CMAKE_CURRENT_SOURCE_DIR}/"
            ""
            file
            "${file}"
    )
    list(APPEND config_c_sources "${file}")
endforeach()
foreach(file IN LISTS asm_sources)
    string(
        REPLACE
            "${CMAKE_CURRENT_SOURCE_DIR}/"
            ""
            file
            "${file}"
    )
    list(APPEND config_asm_sources "${file}")
endforeach()
foreach(file IN LISTS bf_declarations)
    string(
        REPLACE
            "${CMAKE_CURRENT_SOURCE_DIR}/"
            ""
            file
            "${file}"
    )
    list(APPEND config_bf_declarations "${file}")
endforeach()

set(config_c_sources ${config_c_sources} CACHE INTERNAL "")
set(config_asm_sources ${config_asm_sources} CACHE INTERNAL "")
set(config_bf_declarations ${config_bf_declarations} CACHE INTERNAL "")
set(config_KernelDTSList ${KernelDTSList} CACHE INTERNAL "")
