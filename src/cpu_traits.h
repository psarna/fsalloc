#ifndef __FSALLOC_CPU_TRAITS_H
#define __FSALLOC_CPU_TRAITS_H

#include "fsalloc/fsalloc.h"
#include <csignal>
#include <sys/mman.h>

namespace fsalloc {

namespace x86_64 {

int get_mprotect_flags(void *ctx) {
	int flags;
	ucontext_t *context = (ucontext_t *)ctx;

	if (context->uc_mcontext.gregs[REG_ERR] & 0x2) {
		flags = PROT_READ | PROT_WRITE;
	} else {
		flags = PROT_READ;
	}

	return flags;
}

} // namespace x86_64

int get_mprotect_flags(void *ctx) {
#ifdef __x86_64__
	return x86_64::get_mprotect_flags(ctx);
#else
#error "Support for platforms other than x86_64 is not implemented"
#endif
}

}

#endif // __FSALLOC_CPU_TRAITS_H
