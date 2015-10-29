#ifndef __FSALLOC_DB_WRAPPER_H
#define __FSALLOC_DB_WRAPPER_H

#include <db.h>
#include <string>

namespace fsalloc { namespace db {

typedef DB_HEAP_RID handle_t;
typedef DBC cursor_t;
typedef DBT entry_t;
typedef DB database_t;

void init(const std::string &path, uint32_t pagesize, uint64_t cachesize, int ncache);

void term();

char *get(handle_t rid);

handle_t put(void *element, uint32_t size);

void put(void *element, uint32_t size, handle_t rid);

void del(handle_t rid);

} }

#endif // __FSALLOC_DB_WRAPPER_H
