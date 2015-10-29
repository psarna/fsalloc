#include "fsalloc/fsalloc.h"

#include <cstring>
#include <stdexcept>

using namespace fsalloc;
using namespace fsalloc::db;

namespace {
	database_t *gDatabase;
	const uint32_t kGigabyte = 1024 * 1024 * 1024;
}

void fsalloc::db::init(const std::string &path, uint32_t pagesize, uint64_t cachesize, int ncache) {
	int err;

	err = db_create(&gDatabase, nullptr, 0);
	if (err) {
		throw std::runtime_error("Could not create database");
	}

	err = gDatabase->set_pagesize(gDatabase, pagesize);
	if (err) {
		throw std::runtime_error("Could not set pagesize for database");
	}

	err = gDatabase->set_cachesize(gDatabase, cachesize / kGigabyte, cachesize % kGigabyte , ncache);
	if (err) {
		throw std::runtime_error("Could not set cachesize for database");
	}

	err = gDatabase->open(gDatabase, nullptr, path.c_str(), nullptr, DB_HEAP, DB_CREATE | DB_TRUNCATE, 0);
	if (err) {
		throw std::runtime_error("Could not open database");
	}
}

void fsalloc::db::term() {
	gDatabase->close(gDatabase, DB_NOSYNC);
}

char *fsalloc::db::get(handle_t rid) {
	int err;
	entry_t key, data;

	memset(&key, 0, sizeof(entry_t));
	memset(&data, 0, sizeof(entry_t));

	key.data = &rid;
	key.size = sizeof(rid);
	key.ulen = sizeof(rid);
	key.flags = DB_DBT_USERMEM;

	err = gDatabase->get(gDatabase, 0, &key, &data, 0);

	if (err) {
		throw std::runtime_error("Getting from database failed");
	}
	return reinterpret_cast<char *>(data.data);
}

handle_t fsalloc::db::put(void *element, uint32_t size) {
	int err;
	entry_t key, data;
	handle_t rid;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.data = &rid;
	key.size = sizeof(rid);
	key.ulen = sizeof(rid);
	key.flags = DB_DBT_USERMEM;

	data.size = size;
	data.data = element;
	data.flags = DB_DBT_USERMEM;

	err = gDatabase->put(gDatabase, nullptr, &key, &data, DB_APPEND);
	if (err) {
		throw std::runtime_error("Putting to database failed");
	}

	return rid;
}

void fsalloc::db::put(void *element, uint32_t size, handle_t rid) {
	int err;
	cursor_t *cursor;
	entry_t key, data;

	err = gDatabase->cursor(gDatabase, nullptr, &cursor, 0);
	if (err) {
		throw std::runtime_error("Creating cursor for database failed");
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.data = &rid;
	key.size = sizeof(rid);
	key.ulen = sizeof(rid);
	key.flags = DB_DBT_USERMEM;

	err = cursor->c_get(cursor, &key, &data, DB_SET); // Set - search by key
	if (err) {
		throw std::runtime_error("Getting cursor from database failed");
	}

	data.size = size;
	data.data = element;
	data.flags = DB_DBT_USERMEM;

	err = cursor->c_put(cursor, &key, &data, DB_CURRENT);
	if (err) {
		throw std::runtime_error("Commiting changes to database entry failed");
	}

	cursor->c_close(cursor);
}

void fsalloc::db::del(handle_t rid) {
	int err;
	entry_t key;

	memset(&key, 0, sizeof(entry_t));

	key.data = &rid;
	key.size = sizeof(rid);
	key.ulen = sizeof(rid);
	key.flags = DB_DBT_USERMEM;

	err = gDatabase->del(gDatabase, 0, &key, 0);
	if (err && err != DB_NOTFOUND) {
		throw std::runtime_error("Getting from database failed");
	}
}
