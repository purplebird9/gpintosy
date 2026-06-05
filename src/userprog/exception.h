#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

#include <stdbool.h>

/** Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /**< 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /**< 0: read, 1: write. */
#define PF_U 0x4    /**< 0: kernel, 1: user process. */

void exception_init (void);
void exception_print_stats (void);

/** LAB3B: Stack Growth */
#ifdef VM
#define STACK_MAX_BYTES (8 * 1024 * 1024) /**< Absolute stack limit: 8MB. */
bool seems_like_stack_access (const void *fault_addr, const void *esp);
bool grow_stack (void *upage);
#endif


#endif /**< userprog/exception.h */
