/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <arch/machine/registerset.h>

const register_t msgRegisters[] = {
    R2, R3, R4, R5
};
compile_assert(
    consistent_message_registers,
    sizeof(msgRegisters) / sizeof(msgRegisters[0]) == n_msgRegisters
);

const register_t frameRegisters[] = {
    FaultIP, SP, CPSR,
    R0, R1, R8, R9, R10, R11, R12
};
compile_assert(
    consistent_frame_registers,
    sizeof(frameRegisters) / sizeof(frameRegisters[0]) == n_frameRegisters
);

const register_t gpRegisters[] = {
    R2, R3, R4, R5, R6, R7, R14,
    TPIDRURW,
};
compile_assert(
    consistent_gp_registers,
    sizeof(gpRegisters) / sizeof(gpRegisters[0]) == n_gpRegisters
);

