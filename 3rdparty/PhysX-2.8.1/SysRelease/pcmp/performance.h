/*----------------------------------------------------------------------
    This Software and Related Documentation are Proprietary to Ageia
    Technologies, Inc.

    Copyright 2003 Ageia Technologies, Inc. St. Louis, MO
    Unpublished -
    All Rights Reserved Under the Copyright Laws of the United States.

    Restricted Rights Legend:  Use, Duplication, or Disclosure by
    the Government is Subject to Restrictions as Set Forth in
    Paragraph (c)(1)(ii) of the Rights in Technical Data and
    Computer Software Clause at DFARS 252.227-7013.  Ageia
    Technologies Inc.
-----------------------------------------------------------------------*/

#ifndef __PCMP_PERFORMANCE_H__
#define __PCMP_PERFORMANCE_H__ 1

typedef enum {
    PDU_EVENT_CYCLES = 0,
    PDU_EVENT_DESC_FIFO_FULL,
    PDU_EVENT_ISM_RD,
    PDU_EVENT_ISM_WR,
    PDU_EVENT_EMU_MIPS_RD,
    PDU_EVENT_EMU_MIPS_WR,
    PDU_EVENT_EMU_HOST_RD,
    PDU_EVENT_EMU_HOST_WR,
    PDU_EVENT_MBOX_RD,
    PDU_EVENT_MBOX_WR,
    PDU_EVENT_HIU_MASTER_RD,
    PDU_EVENT_HIU_MASTER_WR,
    PDU_EVENT_HIU_SLAVE_RD,
    PDU_EVENT_HIU_SLAVE_WR
}pdu_event_group;

typedef enum {
    VCU_EVENT_INSTRUCTION_ISSUED = 0,
    VCU_EVENT_DUAL_INSTR_EXECUTED,
    VCU_EVENT_SYNC_WAIT,
    VCU_EVENT_SYNC_ENTERED,
    VCU_EVENT_STALL_OBSERVED,
    VCU_EVENT_STALLB_CONFLICT_A,
    VCU_EVENT_VDU_DMA_RD,
    VCU_EVENT_VDU_DMA_WR,
    VCU_EVENT_DMA_INSTRUCTION,
    VCU_EVENT_ISM0_RD,
    VCU_EVENT_ISM0_WR,
    VCU_EVENT_ISM1_RD,
    VCU_EVENT_ISM1_WR
}vcu_event_group;

typedef enum {
    VPU_EVENT_SLOT1_INSTRUCTION_ISSUED = 0,
    VPU_EVENT_SLOT2_INSTRUCTION_ISSUED,
    VPU_EVENT_SYNC_WAIT,
    VPU_EVENT_STALL_OBSERVED,
    VPU_EVENT_STALL_DIV_SQRT_CONTENTION,
    VPU_EVENT_STALLB_CONFLICT_A,
    VPU_EVENT_VDU_DMA_RD,
    VPU_EVENT_VDU_DMA_WR
}vpu_event_group;

typedef enum {
    VDU_EVENT_DESC_FIFO_FULL = 0,
    VDU_EVENT_ISM0_RD,
    VDU_EVENT_ISM0_WR,
    VDU_EVENT_ISM1_RD,
    VDU_EVENT_ISM1_WR,
    VDU_EVENT_EMU_RD,
    VDU_EVENT_EMU_WR,
    VDU_VDU_MBOX_RD,
    VDU_VDU_MBOX_WR
}vdu_event_group;


typedef enum {
    VPE_PERF_THREAD_A = 0,
    VPE_PERF_THEAD_B
}vpe_perf_thread_t;

typedef enum {
    VPE_PERF_VCU = 0,
    VPE_PERF_VDU,
    VPE_PERF_COUNT_CYCLES,
    VPE_PERF_VPU0 = 4,
    VPE_PERF_VPU1,
    VPE_PERF_VPU2,
    VPE_PERF_VPU3,
}vpe_perf_unit_t;

typedef enum {
    PERF_START = 0,
    PERF_STOP,
    PERF_RESET,
    PERF_RESET_ALL,
    PERF_STOP_ALL,
    PERF_START_ALL,
}perf_action_t;

#define NUM_VPE_COUNTERS                8
#define NUM_PDU_COUNTERS                2

typedef enum {
    MIPS_EVENT0_CYCLES = 0,
    MIPS_EVENT0_INSTR_FETCHED,
    MIPS_EVENT0_LOAD_CACHE_OPS,
    MIPS_EVENT0_ALL_STORES,
    MIPS_EVENT0_COND_STORES,
    MIPS_EVENT0_FAILED_COND_STORES,
    MIPS_EVENT0_BRANCHES,
    MIPS_EVENT0_ITLB_MISS,
    MIPS_EVENT0_DTLB_MISS,
    MIPS_EVENT0_ICACHE_MISS,
    MIPS_EVENT0_INSTR_SCHEDULED,
    MIPS_EVENT0_RESERVED0,
    MIPS_EVENT0_RESERVED1,
    MIPS_EVENT0_RESERVED2,
    MIPS_EVENT0_DUAL_ISSUED_EXEC,
    MIPS_EVENT0_INSTR_EXEC
}mips_event0_group;

typedef enum {
    MIPS_EVENT1_CYCLES = 0,
    MIPS_EVENT1_INSTR_EXEC,
    MIPS_EVENT1_LOAD_CACHE_OPS,
    MIPS_EVENT1_ALL_STORES,
    MIPS_EVENT1_COND_STORES,
    MIPS_EVENT1_FPU_INSTR_EXEC,
    MIPS_EVENT1_DCACHE_LINES_EVIC,
    MIPS_EVENT1_TLB_MISS_EXP,
    MIPS_EVENT1_BRANCH_MISPREDICT,
    MIPS_EVENT1_DCACHE_MISS,
    MIPS_EVENT1_INSTR_STALL_SCHED_CONFL,
    MIPS_EVENT1_RESERVED0,
    MIPS_EVENT1_RESERVED1,
    MIPS_EVENT1_RESERVED2,
    MIPS_EVENT1_RESERVED,
    MIPS_EVENT1_COP2_INSTR_EXEC
}mips_event1_group;

#endif
