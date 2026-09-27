/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
#include "berryos.h"

/* Kernel copy helper (no libc available in the kernel build). */
static void kmemcpy(void* dst, const void* src, size_t n){
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
}

/* ---- Part B: stack high-water watermark (stack-overflow verification) ----
 * After allocating a stack we poison it with 0xCC.  As the task uses the
 * stack (growing downward from the top) those bytes get overwritten.  The
 * lowest non-0xCC byte therefore marks the deepest stack usage seen so far. */
static void stack_fill_sentinel(uint64_t base, uint32_t size){
    unsigned char* p = (unsigned char*)base;
    uint32_t i;
    for (i = 0; i < size; i++) p[i] = 0xCC;
}
static uint32_t stack_bytes_used(uint64_t base, uint32_t size){
    const unsigned char* p = (const unsigned char*)base;
    uint32_t i;
    for (i = 0; i < size; i++) if (p[i] != 0xCC) break;
    return i;   /* bytes from the bottom that have been touched */
}

/* First-run entry for forked children (defined in switch.S). */
extern void child_resume_stub(void);

/* Page-table / scheduler forward declarations already in berryos.h. */

#define ISTACK_SIZE USER_ISTACK_SIZE

/* =====================================================================
 * M:N scheduler (M1 stage-3).
 *
 * Two ready queues implement a hybrid policy:
 *   - realtime queue  : round-robin, higher priority than normal
 *   - normal queue    : round-robin with a time slice (quantum)
 * Preemption happens on the PIT tick; a task may also yield voluntarily.
 * Tasks are kernel threads running on private 16 KiB stacks allocated
 * from the slab/buddy allocator.
 * =================================================================== */

#define TASK_MAX        32
#define TASK_STACK_SIZE (32 * 1024)
#define QUANTUM_DEFAULT 5            /* ticks (50 ms @100 Hz) */

/* States */
#define TASK_READY    0
#define TASK_RUNNING  1
#define TASK_BLOCKED  2
#define TASK_EXITED   3
#define TASK_ZOMBIE   4   /* exited, awaiting reaping by parent (M2) */

/* Priorities (lower = higher priority) */
#define PRIO_REALTIME 0
#define PRIO_NORMAL   1

struct task {
    uint32_t        id;
    int             state;
    int             prio;
    uint32_t        quantum;      /* ticks per slice */
    uint32_t        ticks_left;   /* remaining in current slice */
    uint64_t        rsp;          /* saved stack pointer (context) */
    void          (*entry)(void*);/* first-run entry */
    void*           arg;
    uint64_t        stack;        /* task stack base (for freeing) */
    uint64_t        istack;       /* ring-0 trap stack (user tasks) */
    uint64_t        istack_base;  /* watermark base (0 = no istack) */
    uint32_t        istack_size;
    uint32_t        max_stack;    /* peak kernel-task-stack usage (bytes) */
    uint32_t        max_istack;   /* peak istack usage (bytes) */
    uint64_t        cr3;          /* per-process page-table base (phys) */
    int             parent;       /* parent task id (0 = none) */
    int             exit_code;    /* exit code when state == TASK_ZOMBIE */
    int             wait_child;   /* -2 idle, -1 wait any, >0 wait pid */
    struct task*    next;
};

static struct task  pool[TASK_MAX];
static struct task* rt_queue;      /* realtime ready queue (ring) */
static struct task* norm_queue;    /* normal ready queue (ring) */
static struct task* current;
static struct task* idle;          /* boot flow, never queued */
static uint32_t     next_id = 1;

extern void switch_to(uint64_t* old_rsp, uint64_t new_rsp);
extern void sched_entry_stub(void);

/* ---- ring queue ops (q points at tail; q->next is head) ---- */
static void enqueue(struct task** q, struct task* t){
    t->next = NULL;
    if (*q == NULL){
        *q = t;
        t->next = t;
    } else {
        t->next = (*q)->next;
        (*q)->next = t;
        *q = t;
    }
}

static struct task* dequeue(struct task** q){
    struct task* head;
    if (*q == NULL) return NULL;
    head = (*q)->next;
    if (head == *q){
        *q = NULL;
    } else {
        (*q)->next = head->next;
    }
    head->next = NULL;
    return head;
}

static struct task* pick_next(void){
    struct task* t;
    t = dequeue(&rt_queue);
    if (t) return t;
    t = dequeue(&norm_queue);
    if (t) return t;
    return idle;                 /* nothing runnable -> idle */
}

/* Stacks of exited tasks are freed only after we resume on a safe stack
 * (i.e. not on the stack being freed). */
static uint64_t deferred_free[TASK_MAX];
static int      deferred_free_count;

static void deferred_push(uint64_t a){
    if (a && deferred_free_count < TASK_MAX)
        deferred_free[deferred_free_count++] = a;
}

static void deferred_release(void){
    while (deferred_free_count){
        uint64_t a = deferred_free[--deferred_free_count];
        kfree((void*)a);
    }
}

static void schedule(void){
    struct task* prev = current;
    struct task* next;

    /* Put prev back on its ready queue first so that a lone task can be
     * picked again instead of falling through to idle. */
    if (prev != idle && prev->state == TASK_RUNNING){
        prev->state = TASK_READY;
        if (prev->prio == PRIO_REALTIME) enqueue(&rt_queue, prev);
        else                             enqueue(&norm_queue, prev);
    }

    next = pick_next();
    if (!next){
        if (prev != idle) prev->state = TASK_RUNNING;
        return;
    }
    if (next == prev){
        prev->state = TASK_RUNNING;      /* got itself back: refill slice */
        prev->ticks_left = prev->quantum;
        return;
    }

    current = next;
    next->state = TASK_RUNNING;
    next->ticks_left = next->quantum;
    if (prev->cr3 != next->cr3) pgdir_activate(next->cr3);
    if (next->istack) tss_set_rsp0(next->istack + ISTACK_SIZE);
    switch_to(&prev->rsp, next->rsp);   /* returns only when prev resumes */

    /* Resumed on our own stack: safely free any exited task's stacks. */
    deferred_release();
}

/* ---- public API ---- */

void sched_init(void){
    int i;
    for (i = 0; i < TASK_MAX; i++){
        pool[i].id     = 0;
        pool[i].state  = TASK_EXITED;
        pool[i].next   = NULL;
    }
    rt_queue  = NULL;
    norm_queue = NULL;
    idle = &pool[0];
    idle->state = TASK_RUNNING;
    idle->prio  = PRIO_NORMAL;
    current = idle;
}

int sched_create(void (*entry)(void*), void* arg, int prio, uint32_t quantum){
    struct task* t = NULL;
    uint64_t stack;
    uint64_t* sp;
    int i;

    if (prio != PRIO_REALTIME) prio = PRIO_NORMAL;
    if (quantum == 0) quantum = QUANTUM_DEFAULT;

    cli();
    for (i = 1; i < TASK_MAX; i++){
        if (pool[i].state == TASK_EXITED){ t = &pool[i]; break; }
    }
    if (!t){
        sti();
        return -1;
    }
    stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!stack){
        sti();
        return -1;
    }

    t->id         = next_id++;
    t->state      = TASK_READY;
    t->prio       = prio;
    t->quantum    = quantum;
    t->ticks_left = quantum;
    t->entry      = entry;
    t->arg        = arg;
    t->stack      = stack;
    stack_fill_sentinel(stack, TASK_STACK_SIZE);
    t->istack     = 0;
    t->istack_base = 0;
    t->istack_size = 0;
    t->max_stack  = 0;
    t->max_istack = 0;
    t->cr3        = paging_kernel_cr3();
    t->parent     = 0;
    t->exit_code  = 0;
    t->wait_child = -2;

    /* Craft the initial stack frame so the first switch_to lands in
     * sched_entry_stub.  Layout (top of stack -> down), matching the
     * pop order in switch_to: rbp, rbx(entry), r12, r13, r14, r15(arg)
     * followed by the return address sched_entry_stub. */
    sp = (uint64_t*)(stack + TASK_STACK_SIZE);
    *--sp = (uint64_t)sched_entry_stub;      /* ret addr */
    *--sp = 0;                                /* rbp   */
    *--sp = (uint64_t)entry;                  /* rbx   */
    *--sp = 0;                                /* r12   */
    *--sp = 0;                                /* r13   */
    *--sp = 0;                                /* r14   */
    *--sp = (uint64_t)arg;                    /* r15   */
    t->rsp = (uint64_t)sp;

    if (prio == PRIO_REALTIME) enqueue(&rt_queue, t);
    else                       enqueue(&norm_queue, t);
    sti();
    return (int)t->id;
}

void sched_start(void){
    cli();
    schedule();
    sti();
}

/* Called from the PIT interrupt (isr.c): preemption driver. */
void scheduler_tick(void){
    if (!current || current == idle) return;
    /* Part B: sample the running task's peak stack usage at this instant. */
    if (current->stack){
        uint32_t u = stack_bytes_used(current->stack, TASK_STACK_SIZE);
        if (u > current->max_stack) current->max_stack = u;
    }
    if (current->istack_base){
        uint32_t u = stack_bytes_used(current->istack_base, current->istack_size);
        if (u > current->max_istack) current->max_istack = u;
    }
    if (current->ticks_left > 0){
        current->ticks_left--;
        if (current->ticks_left > 0) return;
    }
    /* slice exhausted -> reschedule */
    schedule();
}

/* Part B: dump peak stack usage for every task (stack-overflow verification).
 * Call periodically from the idle loop to watch how much of each stack is
 * actually consumed after the size increase. */
void sched_report_usage(void){
    int i;
    serial_puts("[stack-wm] totals: kstack=");
    serial_hex((uint64_t)TASK_STACK_SIZE);
    serial_puts(" istack=");
    serial_hex((uint64_t)ISTACK_SIZE);
    serial_puts("\r\n");
    for (i = 0; i < TASK_MAX; i++){
        if (pool[i].state == TASK_EXITED && pool[i].id == 0) continue;
        serial_puts("  id=");    serial_hex(pool[i].id);
        serial_puts(" st=");     serial_hex(pool[i].state);
        serial_puts(" kStack="); serial_hex(pool[i].max_stack);
        serial_puts(" iStack="); serial_hex(pool[i].max_istack);
        serial_puts("\r\n");
    }
}

void sched_yield(void){
    cli();
    if (current && current != idle)
        schedule();
    sti();
}

/* A task's entry returned: terminate it and switch to the next task.
 * Called from sched_entry_stub; never returns.  The exiting task's stack
 * is not freed here (we are still running on it); it is handed to the
 * scheduler, which frees it after the context switch on a safe stack. */
void sched_task_exit(void){
    struct task* prev = current;
    struct task* next;

    cli();
    prev->state = TASK_EXITED;
    deferred_push(prev->stack);
    deferred_push(prev->istack);

    next = pick_next();
    if (!next){
        serial_puts("[M1] all tasks exited; halting.\r\n");
        for (;;) hlt();
    }
    current = next;
    next->state = TASK_RUNNING;
    next->ticks_left = next->quantum;
    if (prev->cr3 != next->cr3) pgdir_activate(next->cr3);
    if (next->istack) tss_set_rsp0(next->istack + ISTACK_SIZE);
    switch_to(&prev->rsp, next->rsp);
    for (;;) hlt();   /* unreachable */
}

/* =====================================================================
 * M2 multiprocess: fork / wait / exit + page-table teardown.
 * =================================================================== */

/* Find a live task by id (NULL if none). */
static struct task* task_by_id(int id){
    int i;
    for (i = 0; i < TASK_MAX; i++)
        if (pool[i].id == (uint32_t)id) return &pool[i];
    return NULL;
}

static uint64_t read_cr3(void){
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* Free every user page and the page-table nodes owned by THIS directory
 * (PML4[0].pdpt[1..]), keeping the PML4, pdpt and shared kernel mappings.
 * Used by exec() to discard the old image. */
void free_user_space(uint64_t cr3){
    uint64_t* pml4 = (uint64_t*)(cr3 & PTE_ADDR);
    int p, d, t;
    if (!(pml4[0] & PTE_P)) return;
    {
        uint64_t* pdpt = (uint64_t*)(pml4[0] & PTE_ADDR);
        for (p = 1; p < 512; p++){
            uint64_t pdpt_ent;
            uint64_t* pd;
            if (!(pdpt[p] & PTE_P)) continue;
            pdpt_ent = pdpt[p];
            pd = (uint64_t*)(pdpt_ent & PTE_ADDR);
            for (d = 0; d < 512; d++){
                uint64_t pd_ent = pd[d];
                uint64_t* pt;
                if (!(pd_ent & PTE_P)) continue;
                if (pd_ent & PTE_PS) continue;   /* 2 MiB not used for user */
                pt = (uint64_t*)(pd_ent & PTE_ADDR);
                for (t = 0; t < 512; t++)
                    if (pt[t] & PTE_P) pmm_free_page(pt[t] & PTE_ADDR);
                pmm_free_page(pd_ent & PTE_ADDR);
            }
            pmm_free_page(pdpt_ent & PTE_ADDR);
            pdpt[p] = 0;
        }
    }
}

/* Free an entire process page directory (user pages + its private tables).
 * Does not touch shared kernel mappings.  Used when reaping a zombie child. */
void pgdir_free(uint64_t cr3){
    free_user_space(cr3);
    {
        uint64_t* pml4 = (uint64_t*)(cr3 & PTE_ADDR);
        if (pml4[0] & PTE_P)
            pmm_free_page(pml4[0] & PTE_ADDR);   /* this dir's private pdpt */
        pmm_free_page(cr3 & PTE_ADDR);           /* the PML4 page itself */
    }
}

/* Deep-copy the user address space from src_cr3 into dst_cr3, allocating
 * fresh physical pages for every user page.  Returns 0 on success, -1 on OOM. */
static int copy_user_space(uint64_t src_cr3, uint64_t dst_cr3){
    uint64_t* spml4 = (uint64_t*)(src_cr3 & PTE_ADDR);
    uint64_t saved;
    int p, d, t;
    if (!(spml4[0] & PTE_P)) return 0;
    {
        uint64_t* spdpt = (uint64_t*)(spml4[0] & PTE_ADDR);
        for (p = 1; p < 512; p++){
            uint64_t* spd;
            uint64_t* spt;
            if (!(spdpt[p] & PTE_P)) continue;
            spd = (uint64_t*)(spdpt[p] & PTE_ADDR);
            for (d = 0; d < 512; d++){
                uint64_t pd_ent = spd[d];
                if (!(pd_ent & PTE_P)) continue;
                if (pd_ent & PTE_PS) continue;
                spt = (uint64_t*)(pd_ent & PTE_ADDR);
                for (t = 0; t < 512; t++){
                    uint64_t pte = spt[t];
                    uint64_t phys, new_phys, vaddr;
                    uint64_t* src;
                    uint64_t* dst;
                    int k;
                    if (!(pte & PTE_P)) continue;
                    phys = pte & PTE_ADDR;
                    new_phys = pmm_alloc_page();
                    if (!new_phys) return -1;
                    src = (uint64_t*)phys;
                    dst = (uint64_t*)new_phys;
                    for (k = 0; k < 512; k++) dst[k] = src[k];
                    vaddr = ((uint64_t)p << 30) | ((uint64_t)d << 21) | ((uint64_t)t << 12);
                    saved = read_cr3();
                    pgdir_activate(dst_cr3);
                    map_page(vaddr, new_phys, 6);   /* P|W|US */
                    pgdir_activate(saved);
                }
            }
        }
    }
    return 0;
}

/* Create a child process that is a copy of the caller.  On return in the
 * parent, r->rax holds the child pid; in the child it is 0. */
int sched_fork(struct regs* r){
    struct task* child;
    uint64_t stack, istack, cr3;
    int i;
    struct regs* child_r;

    cli();
    child = NULL;
    for (i = 1; i < TASK_MAX; i++){
        if (pool[i].state == TASK_EXITED){ child = &pool[i]; break; }
    }
    if (!child){ sti(); return -1; }

    stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!stack){ sti(); return -1; }
    stack_fill_sentinel(stack, TASK_STACK_SIZE);
    istack = (uint64_t)kmalloc(ISTACK_SIZE);
    if (!istack){ kfree((void*)stack); sti(); return -1; }
    stack_fill_sentinel(istack, ISTACK_SIZE);
    cr3 = pgdir_new();
    if (!cr3){ kfree((void*)stack); kfree((void*)istack); sti(); return -1; }

    if (copy_user_space(current->cr3, cr3) != 0){
        kfree((void*)stack); kfree((void*)istack);
        /* On OOM we leak cr3's pages; acceptable for a demo. */
        sti(); return -1;
    }

    /* Copy the parent's trap stack verbatim; this becomes the child's own
     * ISTAK frame.  The child resumes in user mode at the parent's fork-return
     * point, so we zero its return register (rax = 0 => "I am the child"). */
    /* Poison the child istack, then copy ONLY the active trap frame (the iret
     * context) so the child resumes in ring 3.  The rest stays sentinel -- the
     * watermark depends on it. */
    {
        uint64_t off = (uint64_t)r - current->istack;   /* frame offset in istack */
        uint64_t len = ISTACK_SIZE - off;               /* frame size to copy */
        kmemcpy((void*)(istack + off), (const void*)(uint64_t)r, len);
    }

    /* Craft the child's FIRST kernel-stack frame.  When the scheduler first
     * dispatches the child, switch_to() returns into child_resume_stub, which
     * loads the child's ISTAK frame (child_r) and iret's back to ring 3. */
    child_r = (struct regs*)((uint64_t)istack +
                             ((uint64_t)r - (uint64_t)current->istack));
    child_r->rax = 0;
    {
        uint64_t* top = (uint64_t*)(stack + TASK_STACK_SIZE);
        *--top = (uint64_t)child_r;             /* popped into rsp by stub */
        *--top = (uint64_t)child_resume_stub;   /* switch_to return address */
        *--top = 0; *--top = 0; *--top = 0;     /* rbp, rbx, r12 */
        *--top = 0; *--top = 0; *--top = 0;     /* r13, r14, r15 */
        child->rsp = (uint64_t)top;
    }
    child->stack  = stack;
    child->istack = istack;
    child->istack_base = istack;
    child->istack_size = ISTACK_SIZE;
    child->max_stack  = 0;
    child->max_istack = 0;
    child->cr3    = cr3;

    child->id         = next_id++;
    child->state      = TASK_READY;
    child->prio       = current->prio;
    child->quantum    = current->quantum;
    child->ticks_left = current->quantum;
    child->entry      = current->entry;
    child->arg        = current->arg;
    child->parent     = (int)current->id;
    child->exit_code  = 0;
    child->wait_child = -2;
    child->next       = NULL;

    if (child->prio == PRIO_REALTIME) enqueue(&rt_queue, child);
    else                              enqueue(&norm_queue, child);

    r->rax = (uint64_t)child->id;   /* parent gets child pid */
    sti();
    return (int)child->id;
}

/* Wake a blocking parent that is waiting for `child` (called on child exit). */
static void sched_wake_parent(struct task* child){
    struct task* p = task_by_id(child->parent);
    if (p && p->state == TASK_BLOCKED && p->wait_child == (int)child->id){
        p->state = TASK_READY;
        p->wait_child = -2;
        if (p->prio == PRIO_REALTIME) enqueue(&rt_queue, p);
        else                          enqueue(&norm_queue, p);
    }
}

/* Block until child `pid` exits, then reap it and return its exit code.
 * Returns -1 if pid is not our child. */
int sched_wait(int pid){
    struct task* child;
    for (;;){
        child = task_by_id(pid);
        if (!child || child->parent != (int)current->id) return -1;
        if (child->state == TASK_ZOMBIE){
            int code = child->exit_code;
            deferred_push(child->stack);
            deferred_push(child->istack);
            pgdir_free(child->cr3);
            child->state = TASK_EXITED;
            child->id = 0;
            return code;
        }
        current->state = TASK_BLOCKED;
        current->wait_child = pid;
        sched_yield();
        /* resumed: re-check the child's state */
    }
}

/* Terminate the current process as a zombie, wake its parent and switch
 * away.  Resources are reaped by the parent's sched_wait().  Never returns. */
void sched_exit(int code){
    struct task* prev = current;
    struct task* next;
    cli();
    prev->exit_code = code;
    prev->state = TASK_ZOMBIE;
    sched_wake_parent(prev);
    next = pick_next();
    if (!next){
        serial_puts("[M2] all processes gone; halting.\r\n");
        for (;;) hlt();
    }
    current = next;
    next->state = TASK_RUNNING;
    next->ticks_left = next->quantum;
    if (prev->cr3 != next->cr3) pgdir_activate(next->cr3);
    if (next->istack) tss_set_rsp0(next->istack + ISTACK_SIZE);
    switch_to(&prev->rsp, next->rsp);
    for (;;) hlt();
}

uint32_t sched_self_id(void){
    return current ? current->id : 0;
}

/* M3: mark the calling task blocked (used while waiting in sys_read). */
void sched_self_block(void){
    if (current) current->state = TASK_BLOCKED;
}

/* M3: wake a blocked task by id (called from the keyboard ISR when a line
 * is ready).  Mirrors sched_wake_parent for the wait path. */
void sched_wake_task(int id){
    struct task* t = task_by_id(id);
    if (t && t->state == TASK_BLOCKED){
        t->state = TASK_READY;
        if (t->prio == PRIO_REALTIME) enqueue(&rt_queue, t);
        else                          enqueue(&norm_queue, t);
    }
}

void sched_set_istack(uint64_t istack){
    if (current){
        current->istack = istack;
        current->istack_base = istack;
        current->istack_size = ISTACK_SIZE;
        current->max_istack = 0;
    }
}

void sched_set_my_cr3(uint64_t cr3){
    if (current) current->cr3 = cr3;
}

uint64_t sched_current_cr3(void){
    return current ? current->cr3 : paging_kernel_cr3();
}

/* ---- introspection (for demo / future ps) ---- */
uint32_t sched_task_count(void){
    uint32_t n = 0;
    int i;
    for (i = 0; i < TASK_MAX; i++)
        if (pool[i].state != TASK_EXITED && pool[i].id != 0) n++;
    return n;
}

/* ---- task table as text (shell `grove`) ----
 * Buffered, bounds-checked emitters so a short caller buffer can never be
 * overrun; same "returns bytes written, NUL-terminates" contract as bfs_ls(). */
struct tk_out { char* buf; unsigned long n; unsigned long pos; };

static void tk_str(struct tk_out* o, const char* s){
    while (*s && o->pos + 1 < o->n) o->buf[o->pos++] = *s++;
}

/* Left-aligned unsigned decimal in a fixed field. */
static void tk_u(struct tk_out* o, uint32_t v, int width){
    char b[12];
    int i = 11, len;
    b[i--] = 0;
    if (v == 0) b[i--] = '0';
    while (v){ b[i--] = (char)('0' + (v % 10)); v /= 10; }
    len = 10 - i;                       /* digits written */
    tk_str(o, &b[i + 1]);
    while (len < width){ tk_str(o, " "); len++; }
}

static void tk_pad(struct tk_out* o, const char* s, int width){
    int len = 0;
    const char* p = s;
    while (*p++) len++;
    tk_str(o, s);
    while (len < width){ tk_str(o, " "); len++; }
}

long sched_tasks(char* buf, unsigned long n){
    static const char* const st_names[] = {
        "ready", "running", "blocked", "exited", "zombie"
    };
    struct tk_out o;
    int i;

    if (!buf || n == 0) return -1;
    o.buf = buf; o.n = n; o.pos = 0;

    tk_pad(&o, "id", 5);
    tk_pad(&o, "prio", 7);
    tk_pad(&o, "state", 10);
    tk_pad(&o, "parent", 8);
    tk_str(&o, "space\r\n");

    for (i = 0; i < TASK_MAX; i++){
        struct task* t = &pool[i];
        const char* st;
        if (t->id == 0 || t->state == TASK_EXITED) continue;
        st = (t->state >= 0 && t->state <= 4) ? st_names[t->state] : "?";
        tk_u(&o, t->id, 5);
        tk_pad(&o, t->prio == PRIO_REALTIME ? "rt" : "norm", 7);
        tk_pad(&o, st, 10);
        tk_u(&o, (uint32_t)t->parent, 8);
        tk_str(&o, (t->cr3 == paging_kernel_cr3()) ? "kernel" : "user");
        tk_str(&o, "\r\n");
    }
    if (o.pos < n) buf[o.pos] = 0;
    return (long)o.pos;
}
