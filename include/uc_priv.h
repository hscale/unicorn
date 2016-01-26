/* Unicorn Emulator Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2015 */

#ifndef UC_PRIV_H
#define UC_PRIV_H

#include <stdint.h>
#include <stdio.h>

#include "qemu.h"
#include "unicorn/unicorn.h"
#include "list.h"

// These are masks of supported modes for each cpu/arch.
// They should be updated when changes are made to the uc_mode enum typedef.
#define UC_MODE_ARM_MASK    (UC_MODE_ARM|UC_MODE_THUMB|UC_MODE_LITTLE_ENDIAN)
#define UC_MODE_MIPS_MASK   (UC_MODE_MIPS32|UC_MODE_MIPS64|UC_MODE_LITTLE_ENDIAN|UC_MODE_BIG_ENDIAN)
#define UC_MODE_X86_MASK    (UC_MODE_16|UC_MODE_32|UC_MODE_64|UC_MODE_LITTLE_ENDIAN)
#define UC_MODE_PPC_MASK    (UC_MODE_PPC64|UC_MODE_BIG_ENDIAN)
#define UC_MODE_SPARC_MASK  (UC_MODE_SPARC32|UC_MODE_SPARC64|UC_MODE_BIG_ENDIAN)
#define UC_MODE_M68K_MASK   (UC_MODE_BIG_ENDIAN)

#define ARR_SIZE(a) (sizeof(a)/sizeof(a[0]))

QTAILQ_HEAD(CPUTailQ, CPUState);

typedef struct ModuleEntry {
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
    module_init_type type;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

typedef uc_err (*query_t)(struct uc_struct *uc, uc_query_type type, size_t *result);

// return 0 on success, -1 on failure
typedef int (*reg_read_t)(struct uc_struct *uc, unsigned int regid, void *value);
typedef int (*reg_write_t)(struct uc_struct *uc, unsigned int regid, const void *value);

typedef void (*reg_reset_t)(struct uc_struct *uc);

typedef bool (*uc_write_mem_t)(AddressSpace *as, hwaddr addr, const uint8_t *buf, int len);

typedef bool (*uc_read_mem_t)(AddressSpace *as, hwaddr addr, uint8_t *buf, int len);

typedef void (*uc_args_void_t)(void*);

typedef void (*uc_args_uc_t)(struct uc_struct*);
typedef int (*uc_args_int_uc_t)(struct uc_struct*);

typedef bool (*uc_args_tcg_enable_t)(struct uc_struct*);

typedef void (*uc_minit_t)(struct uc_struct*, ram_addr_t);

typedef void (*uc_args_uc_long_t)(struct uc_struct*, unsigned long);

typedef void (*uc_args_uc_u64_t)(struct uc_struct *, uint64_t addr);

typedef MemoryRegion* (*uc_args_uc_ram_size_t)(struct uc_struct*,  ram_addr_t begin, size_t size, uint32_t perms);

typedef MemoryRegion* (*uc_args_uc_ram_size_ptr_t)(struct uc_struct*,  ram_addr_t begin, size_t size, uint32_t perms, void *ptr);

typedef void (*uc_mem_unmap_t)(struct uc_struct*, MemoryRegion *mr);

typedef void (*uc_readonly_mem_t)(MemoryRegion *mr, bool readonly);

// which interrupt should make emulation stop?
typedef bool (*uc_args_int_t)(int intno);

// some architecture redirect virtual memory to physical memory like Mips
typedef uint64_t (*uc_mem_redirect_t)(uint64_t address);

struct hook {
    int type;            // UC_HOOK_*
    int insn;            // instruction for HOOK_INSN
    int refs;            // reference count to free hook stored in multiple lists
    uint64_t begin, end; // only trigger if PC or memory access is in this address (depends on hook type)
    void *callback;      // a uc_cb_* type
    void *user_data;
};

// hook list offsets
// mirrors the order of uc_hook_type from include/unicorn/unicorn.h
enum uc_hook_idx {
    UC_HOOK_INTR_IDX,
    UC_HOOK_INSN_IDX,
    UC_HOOK_CODE_IDX,
    UC_HOOK_BLOCK_IDX,
    UC_HOOK_MEM_READ_UNMAPPED_IDX,
    UC_HOOK_MEM_WRITE_UNMAPPED_IDX,
    UC_HOOK_MEM_FETCH_UNMAPPED_IDX,
    UC_HOOK_MEM_READ_PROT_IDX,
    UC_HOOK_MEM_WRITE_PROT_IDX,
    UC_HOOK_MEM_FETCH_PROT_IDX,
    UC_HOOK_MEM_READ_IDX,
    UC_HOOK_MEM_WRITE_IDX,
    UC_HOOK_MEM_FETCH_IDX,

    UC_HOOK_MAX,
};

// for loop macro to loop over hook lists
#define HOOK_FOREACH(uc, hh, idx)                         \
    struct list_item *cur;                                \
    for (                                                 \
        cur = (uc)->hook[idx##_IDX].head;                 \
        cur != NULL && ((hh) = (struct hook *)cur->data)  \
            /* stop excuting callbacks on stop request */ \
            && !uc->stop_request;                         \
        cur = cur->next)

// if statement to check hook bounds
#define HOOK_BOUND_CHECK(hh, addr)                  \
    ((((addr) >= (hh)->begin && (addr) <= (hh)->end) \
         || (hh)->begin > (hh)->end))

#define HOOK_EXISTS(uc, idx) ((uc)->hook[idx##_IDX].head != NULL)
#define HOOK_EXISTS_BOUNDED(uc, idx, addr) _hook_exists_bounded((uc)->hook[idx##_IDX].head, addr)

static inline bool _hook_exists_bounded(struct list_item *cur, uint64_t addr)
{
    while (cur != NULL) {
        if (HOOK_BOUND_CHECK((struct hook *)cur->data, addr))
            return true;
        cur = cur->next;
    }
    return false;
}

//relloc increment, KEEP THIS A POWER OF 2!
#define MEM_BLOCK_INCR 32

struct uc_struct {
    uc_arch arch;
    uc_mode mode;
    QemuMutex qemu_global_mutex; // qemu/cpus.c
    QemuCond qemu_cpu_cond; // qemu/cpus.c
    QemuThread *tcg_cpu_thread; // qemu/cpus.c
    QemuCond *tcg_halt_cond; // qemu/cpus.c
    struct CPUTailQ cpus;   // qemu/cpu-exec.c
    uc_err errnum;  // qemu/cpu-exec.c
    AddressSpace as;
    query_t query;
    reg_read_t reg_read;
    reg_write_t reg_write;
    reg_reset_t reg_reset;

    uc_write_mem_t write_mem;
    uc_read_mem_t read_mem;
    uc_args_void_t release;     // release resource when uc_close()
    uc_args_uc_u64_t set_pc;  // set PC for tracecode
    uc_args_int_t stop_interrupt;   // check if the interrupt should stop emulation

    uc_args_uc_t init_arch, pause_all_vcpus, cpu_exec_init_all;
    uc_args_int_uc_t vm_start;
    uc_args_tcg_enable_t tcg_enabled;
    uc_args_uc_long_t tcg_exec_init;
    uc_args_uc_ram_size_t memory_map;
    uc_args_uc_ram_size_ptr_t memory_map_ptr;
    uc_mem_unmap_t memory_unmap;
    uc_readonly_mem_t readonly_mem;
    uc_mem_redirect_t mem_redirect;
    // list of cpu
    void* cpu;

    MemoryRegion *system_memory;    // qemu/exec.c
    MemoryRegion io_mem_rom;    // qemu/exec.c
    MemoryRegion io_mem_notdirty;   // qemu/exec.c
    MemoryRegion io_mem_unassigned; // qemu/exec.c
    MemoryRegion io_mem_watch;  // qemu/exec.c
    RAMList ram_list;   // qemu/exec.c
    CPUState *next_cpu; // qemu/cpus.c
    BounceBuffer bounce;    // qemu/cpu-exec.c
    volatile sig_atomic_t exit_request; // qemu/cpu-exec.c
    spinlock_t x86_global_cpu_lock; // for X86 arch only
    bool global_dirty_log;  // qemu/memory.c
    /* This is a multi-level map on the virtual address space.
       The bottom level has pointers to PageDesc.  */
    void **l1_map;  // qemu/translate-all.c
    size_t l1_map_size;
    /* code generation context */
    void *tcg_ctx;  // for "TCGContext tcg_ctx" in qemu/translate-all.c
    /* memory.c */
    unsigned memory_region_transaction_depth;
    bool memory_region_update_pending;
    bool ioeventfd_update_pending;
    QemuMutex flat_view_mutex;
    QTAILQ_HEAD(memory_listeners, MemoryListener) memory_listeners;
    QTAILQ_HEAD(, AddressSpace) address_spaces;
    // qom/object.c
    GHashTable *type_table;
    Type type_interface;
    Object *root;
    bool enumerating_types;
    // util/module.c
    ModuleTypeList init_type_list[MODULE_INIT_MAX];
    // hw/intc/apic_common.c
    DeviceState *vapic;
    int apic_no;
    bool mmio_registered;
    bool apic_report_tpr_access;
    CPUState *current_cpu;

    // linked lists containing hooks per type
    struct list hook[UC_HOOK_MAX];

    // hook to count number of instructions for uc_emu_start()
    uc_hook count_hook;

    size_t emu_counter; // current counter of uc_emu_start()
    size_t emu_count; // save counter of uc_emu_start()

    uint64_t block_addr;    // save the last block address we hooked

    bool init_tcg;      // already initialized local TCGv variables?
    bool stop_request;  // request to immediately stop emulation - for uc_emu_stop()
    bool emulation_done;  // emulation is done by uc_emu_start()
    QemuThread timer;   // timer for emulation timeout
    uint64_t timeout;   // timeout for uc_emu_start()

    uint64_t invalid_addr;  // invalid address to be accessed
    int invalid_error;  // invalid memory code: 1 = READ, 2 = WRITE, 3 = CODE

    uint64_t addr_end;  // address where emulation stops (@end param of uc_emu_start())

    int thumb;  // thumb mode for ARM
    // full TCG cache leads to middle-block break in the last translation?
    bool block_full;
    MemoryRegion **mapped_blocks;
    uint32_t mapped_block_count;
    uint32_t mapped_block_cache_index;
    void *qemu_thread_data; // to support cross compile to Windows (qemu-thread-win32.c)
    uint32_t target_page_size;
    uint32_t target_page_align;
    uint64_t next_pc;   // save next PC for some special cases
};

#include "qemu_macro.h"

// check if this address is mapped in (via uc_mem_map())
MemoryRegion *memory_mapping(struct uc_struct* uc, uint64_t address);

#endif
