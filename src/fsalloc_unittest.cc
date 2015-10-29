#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "fsalloc/fsalloc.h"

TEST(Fsalloc, MultiAlloc) {
	fsalloc::init("/tmp/fsalloc.bdb", 2);

	std::array<int *, 1024> arr;
	for (unsigned i = 0; i < arr.size(); ++i) {
		arr[i] = fsalloc::fsalloc<int>();
		*arr[i] = i * 2;
	}

	for (unsigned i = 0; i < arr.size(); ++i) {
		EXPECT_EQ(2 * i, *arr[i]);
	}
}

TEST(Fsalloc, BasicAlloc) {
	char *p; char a;
	char *buffer;
	char *dummy;
	std::vector<int> *vec, *vec2;

	fsalloc::init("/tmp/fsalloc.bdb", 4);

	dummy = reinterpret_cast<char *>(fsalloc::fsalloc(7));
	buffer = fsalloc::fsalloc<char>();
	vec = fsalloc::fsalloc<std::vector<int>>();
	memcpy(dummy, "hello!", 7);

	p = buffer;
	// Access the data (write mode)
	*p = 'y';
	// Force writeback
	*fsalloc::fsalloc<char>() = 'x';
	// Access the data (read mode)
	a = *p;
	EXPECT_EQ(a, 'y');

	*vec = std::vector<int>();
	vec->push_back(4);
	vec->push_back(6);
	vec2 = fsalloc::fsalloc<std::vector<int>>();

	*vec2 = *vec;
	vec->push_back(1);
	vec2->push_back(7);
	vec2->at(1) = 2;

	*fsalloc::fsalloc<char *>() = nullptr;
	*fsalloc::fsalloc<char>() = 'b';

	vec2->push_back(1);

	EXPECT_EQ(*vec, (std::vector<int>{4, 6, 1}));
	EXPECT_EQ(*vec2, (std::vector<int>{4, 2, 7, 1}));

	EXPECT_EQ(0, strcmp(dummy, "hello!"));
	dummy[5] = '?';
	EXPECT_EQ(0, strcmp(dummy, "hello?"));

	fsalloc::fsfree<char>(dummy);
	fsalloc::fsfree<char>(buffer);
}
