/*
 * fsalloc - memory allocation on file system
 *
 * This module allows applications to overload memory management
 * for specified classes with file system allocation:
 * - memory is stored in a database (BerkeleyDB) in a single file
 * - only most frequently used pages exist in RAM
 * - simple user space implementation based on custom handler for SIGSEGV
 *
 * Usage:
 * 1. init() should be called before any overloaded allocation happens
 *
 * 2. Direct calls:
 *   void *x = fsalloc(2 * sizeof(int));
 *   void *y = fsalloc<int>();
 *   Class z = fsnew<Class>(custom, constructor, parameters);
 *   fsfree(x);
 *   fsfree(y);
 *   fsdelete(z);
 *
 * 3. Overloading memory management for specified classes:
 *   class Foo : public fsalloc::managed {
 *    HugeStructure huge_structure;
 *     void *memory_hog[1024];
 *   }
 *
 *   Foo *foo = new Foo(1, 2, 3);
 *   delete foo;
 *
 * 4. term() should be called before exit
 *
 * Considered storage for database:
 *        +======+=======+=================+
 *        | fast | cheap | no RAM overhead |
 * +======+======+=======+=================+
 * | HDD  |  -   |   X   |		X		|
 * +------+------+-------+-----------------+
 * | SSD  |  X   |   -   |		X		|
 * +------+------+-------+-----------------+
 * | zram |  X   |   X   |		-		|
 * +------+------+-------+-----------------+
 *
 ************************************************
 * Copyright 2015 Piotr Sarna <p.sarna@tlen.pl> *
 ************************************************
 */

#ifndef __FSALLOC_H
#define __FSALLOC_H

#include "fsalloc/db_wrapper.h"
#include <cstdarg>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>

namespace fsalloc {

/*! \brief Represents information on single allocation */
struct Info {
	db::handle_t rid; /*!< key for BerkeleyDB heap database entry */
	uint32_t size;	/*!< size of allocated region */
	bool dirty : 1;   /*!< true iff region is dirty (its current state is different than in database) */
	bool cached : 1;  /*!< true iff region is cached in RAM */

	static Info emptyInfo(uint32_t s) {
		// emptyInfo is cached, because each new allocations goes to cache first
		return {invalid_handle, s, false, true};
	}

	bool valid() {
		return rid.pgno != invalid_handle.pgno || rid.indx != invalid_handle.indx;
	}

	static const db::handle_t invalid_handle;
};

/*! \brief Represents global fsalloc statistics */
struct Stats {
	unsigned long long allocs;
	unsigned long long frees;
	unsigned long long cache_hits;
	unsigned long long writebacks;
};

/* \brief keeps information about every allocated region */
typedef std::unordered_map<void *, Info> AllocMap;
/* \brief keeps a queue of regions active in RAM */
typedef std::deque<void *> RegionCache;

static const int kPagesize = getpagesize();
static const int kDefaultCapacity = 0x100000;

inline void debug(const char* format, ...) {
#ifndef NDEBUG
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
#else
	(void)format;
#endif
}

/* \brief Aligns address to pagesize granularity */
inline void *pagealign(void *ptr) {
	static const int pageshift = ffs(kPagesize) - 1;

	return reinterpret_cast<void *>(((reinterpret_cast<intptr_t>((ptr)) >> pageshift) << pageshift));
}

/*! \brief Aligns size to be a multiple of pagesize */
inline uint32_t sizealign(uint32_t size) {
	return ((size + kPagesize - 1) / kPagesize) * kPagesize;
}

/*! \brief Returns an iterator to allocated region or AllocMap::end if not found */
AllocMap::iterator allocated(void *addr);

/*! \brief Allocates 'size' bytes */
void *fsalloc(uint32_t size);

/*! \brief Explicitly frees allocated region */
void fsfree(void *addr);

/*! \brief Performs a writeback to database */
void writeback();

/*! \brief Allocates new T object */
template<typename T>
T *fsalloc() {
	return static_cast<T *>(fsalloc(sizeof(T)));
}

/*! \brief Frees T object allocated with fsalloc */
template<typename T>
void fsfree(T *addr) {
	return fsfree(reinterpret_cast<void *>(addr));
}

template<typename T, typename... Args>
T *fsnew(Args&&... args) {
	T *addr = fsalloc::fsalloc<T>();
	return new (addr) T(args...);
}

template<typename T, typename... Args>
void fsdelete(T *obj) {
	obj->~T();
	return fsfree<T>(obj);
}

/*! \brief Performs initialization steps for fsalloc module */
void init(const std::string &path, uint32_t capacity = kDefaultCapacity);


/*! \brief Terminates fsalloc module */
void term();

/*! \brief Returns structure with global fsalloc statistics */
const Stats &stats();

/*! \brief Base structure for classes managed by fsalloc
 * Inheriting from this structure overloads memory management
 * of a class to fsalloc.
 */
struct managed {
	void *operator new(std::size_t size) {
		return fsalloc::fsalloc(size);
	}

	void operator delete(void *obj) noexcept {
		fsalloc::fsfree(obj);
	}

	// Operators not (yet) supported by fsalloc
	void* operator new(std::size_t size, const std::nothrow_t& nothrow_value) noexcept = delete;
	void* operator new (std::size_t size, void* ptr) noexcept = delete;
	void* operator new[] (std::size_t size) = delete;
	void* operator new[] (std::size_t size, const std::nothrow_t& nothrow_value) noexcept = delete;
	void* operator new[] (std::size_t size, void* ptr) noexcept = delete;
	void operator delete(void* ptr, const std::nothrow_t& nothrow_constant) noexcept = delete;
	void operator delete(void* ptr, void* voidptr2) noexcept = delete;
	void operator delete[] (void* ptr) noexcept = delete;
	void operator delete[] (void* ptr, const std::nothrow_t& nothrow_constant) noexcept = delete;
	void operator delete[] (void* ptr, void* voidptr2) noexcept = delete;
};

} // namespace fsalloc

#endif //__FSALLOC_H
