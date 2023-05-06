/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "elf.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

ssize_t sys_user_print_backtrace(uint64 level) {
  // sprint(">>> print_backtrace begin\n");

  assert(current);
  // sprint("sp = %p\n", (uint64*)current->trapframe->regs.sp);
  // sprint("fp = %p\n", (uint64*)current->trapframe->regs.s0);
  // for (int i=0;i<25;i++) {
  //   sprint("%p\t%p\n", (uint64*)current->trapframe->regs.sp+i, *((uint64*)current->trapframe->regs.sp+i));
  // }

  uint64 fp = *((uint64*)current->trapframe->regs.s0 - 1);
  int fnn;
  function_names fn[256];
  load_function_names_from_host_elf(current, &fnn, fn);

  for (int i=0;i<level;i++) {
    // sprint("fp = %p\n", fp);
    uint64 ra = *((uint64*)fp - 1);
    if (!ra) break;
    
    // sprint("ra = %p\n", ra);
    char * name = "not found";
    uint64 tmp = -1;
    for (int j=0;j<fnn;j++) {
      if (fn[j].addr < ra && ra-fn[j].addr < tmp) {
        tmp = ra-fn[j].addr;
        name = fn[j].name;
      }
    }
    sprint("%s\n", name);

    fp = *((uint64*)fp - 2);
  }

  // sprint(">>> print_backtrace end\n");
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
