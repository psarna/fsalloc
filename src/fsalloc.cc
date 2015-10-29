#include "fsalloc/fsalloc.h"
#include "fsalloc/cpu_traits.h"

#include <malloc.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace fsalloc;

const db::handle_t Info::invalid_handle = {
			std::numeric_limits<decltype(rid.pgno)>::max(),
			std::numeric_limits<decltype(rid.indx)>::max()
	};

/*
 * gAllocations		 - map of allocated regions
 * gRegionCache		 - queue of regions cached in RAM
 * gRegionCacheCapacity - max capacity of region cache
 * gStats			   - usage statistics
 * default_sigsegv	  - default handler for SIGSEGV signal
 */
namespace {
	AllocMap gAllocations;
	RegionCache gRegionCache;
	uint32_t gRegionCacheCapacity;
	Stats gStats;

	struct sigaction default_sigsegv;
}

/*! \brief Inserts region to cache, performing a writeback if limit is reached */
static void cacheRegion(void *addr) {
	gRegionCache.push_back(addr);

	if (gRegionCache.size() > gRegionCacheCapacity) {
		writeback();
	}
}

/*! \brief Verifies if requested address is within allocated region */
static bool verify(void *region, void *addr, uint32_t size) {
	int dist = std::distance(reinterpret_cast<char *>(region), reinterpret_cast<char *>(addr));
	return dist >= 0 && static_cast<uint32_t>(dist) <= size;
}

/*! \brief Performs mprotect() call, protecting a page from reading/writing */
static void protect(void *region, size_t size, int flags) {
	int err = mprotect(region, sizealign(size), flags);
	if (err) {
		throw std::runtime_error("mprotect failed");
	}
}

/*! \brief Performs a madvise(MADV_DONTNEED) call, effectively removing page from RAM */
static void forget(void *region, size_t size) {
	int err = madvise(region, sizealign(size), MADV_DONTNEED);
	if (err) {
		throw std::runtime_error("madvise failed");
	}
	protect(region, size, PROT_NONE);
}

void fsalloc::writeback() {
	assert(gRegionCache.size() > 0);

	//Remove page from cache
	void *addr = gRegionCache.front();
	Info &info = gAllocations[addr];
	gRegionCache.pop_front();
	info.cached = false;

	if (!info.dirty) {
		forget(addr, info.size);
		gStats.cache_hits++;
		return;
	}

	// Unprotect page in order to read its data
	protect(addr, info.size, PROT_READ);

	// Write page to db
	if (info.valid()) {
		db::put(addr, info.size, info.rid);
	} else {
		info.rid = db::put(addr, info.size);
	}
	info.dirty = false;

	// Advise as not needed - effectively freeing the page frame on Linux
	// and reprotect
	forget(addr, info.size);

	gStats.writebacks++;
}

AllocMap::iterator fsalloc::allocated(void *addr) {
	return gAllocations.find(addr);
}

void *fsalloc::fsalloc(uint32_t size) {
	void *addr;

	addr = mmap(nullptr, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr == MAP_FAILED) {
		throw std::runtime_error("fsalloc: mmap failed");
	}

	gAllocations[addr] = Info::emptyInfo(size);
	cacheRegion(addr);

	gStats.allocs++;
	return addr;
}

void fsalloc::fsfree(void *addr) {
	int ret;
	auto it = allocated(addr);
	if (it != gAllocations.end()) {
		Info &info = it->second;

		db::del(info.rid);

		ret = munmap(addr, sizealign(info.size));
		if (ret < 0) {
			throw std::runtime_error("fsalloc: munmap failed");
		}

		gAllocations.erase(it);
	}

	gStats.frees++;
}

/*! \brief SIGSEGV signal handler */
static void handler(int sig, siginfo_t *si, void *ctx) {
	void *region;
	char *tmp;
	int mprotect_flags;

	region = pagealign(si->si_addr);
	auto it = allocated(region);
	if (it != gAllocations.end()) {
		Info &info = it->second;
		if (!verify(region, si->si_addr, info.size)) {
			default_sigsegv.sa_handler(sig);
		}
		mprotect_flags = get_mprotect_flags(ctx);
		if (mprotect_flags & PROT_WRITE) {
			info.dirty = true;
			if (info.cached) {
				protect(region, info.size, mprotect_flags);
				return;
			}
		}

		// Filling with contents from db (or extracting a never-used-page)
		if (info.valid()) {
			// Page needs to be read and written to be filled with data
			protect(region, info.size, PROT_READ | PROT_WRITE);
			tmp = db::get(info.rid);
			memcpy(region, tmp, info.size);
		}

		info.cached = true;
		cacheRegion(region);

		// Region is now protected according to its access type
		protect(region, info.size, mprotect_flags);
	} else {
		default_sigsegv.sa_handler(sig);
	}
}

void fsalloc::init(const std::string &path, uint32_t capacity) {
	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	if (sigaction(SIGSEGV, &sa, &default_sigsegv) == -1) {
		throw std::runtime_error("fsalloc: sigaction failed");
	}

	gRegionCacheCapacity = capacity;
	gStats = Stats();

	db::init(path, kPagesize, 1024, 1);
}

void fsalloc::term() {
	db::term();
}

const Stats &stats() {
	return gStats;
}
