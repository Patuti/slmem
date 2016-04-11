#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <functional>

#include "allocator.h"


#define SL_STRINGIFY_MACRO(x)	#x
#define SL_CONCAT_MACRO(a, b, sep)	a sep SL_STRINGIFY_MACRO(b)
#define SL_CURRENT_FILE_LINENUM	SL_CONCAT_MACRO(__FILE__, __LINE__, ":")


template <size_t Capacity>
class DefaultAllocTagPolicy {
public:
	struct TagInfo {
		const char *id;
		void *addr;
		size_t allocSize;
	};

	static void Tag(void *addr, const char* id, size_t size) {
		assert(tagCount < Capacity);

		TagInfo &tag = tags[tagCount++];
		tag.id = id;
		tag.addr = addr;
		tag.allocSize = size;
	}

	static void Untag(void *addr) {
		size_t remainingTags = tagCount;
		for (int i = 0; i < Capacity; i++) {
			if (!remainingTags) {
				break;
			}

			if (tags[i].addr == addr)  {
				tags[i].id = nullptr;
				tags[i].addr = nullptr;
				tags[i].allocSize = 0;
				tagCount--;
				break;
			}
			remainingTags--;
		}
	}

	static void SetData(void *user_data, size_t size) {
		assert(NeededSizeInBytes >= size);
		tags = static_cast<TagInfo*>(user_data);
		memset(tags, 0, NeededSizeInBytes);
	}

	static void Dump(std::function<void(const TagInfo&)> print = DefaultPrint, std::function<bool(const TagInfo&)> filter = nullptr) {
		for (size_t i = 0; i < tagCount; i++) {
			if (!filter || filter(tags[i])) {
				print(tags[i]);
			}
		}
	}

	static void DefaultPrint(const TagInfo &info) {
		printf("%-32s - %6zu\n", info.id, info.allocSize);
	}

	static constexpr size_t NeededSizeInBytes = sizeof(TagInfo) * Capacity;

private:
	static TagInfo *tags;
	static size_t tagCount;
};

// Leak detection
// unsafe for multithreading

#define DEFAULT_LEAK_DETECT_STORE_ALLOC_SIZE

template <size_t Capacity>
class DefaultLeakDetectPolicy {
public:
	struct LeakInfo {
		uintptr_t addr;
#if defined(DEFAULT_LEAK_DETECT_STORE_ALLOC_SIZE)
		size_t size;
#endif // #if defined(DEFAULT_LEAK_DETECT_STORE_ALLOC_SIZE)
	};

	static void Assign(void *addr, size_t size) {
		assert(leakCount < Capacity);
		if (leakCount == Capacity) {
			return;
		}

		for (size_t i = 0; i < Capacity; i++) {
			LeakInfo &leak = leaks[i];
			if (leak.addr == uintptr_t(NULL)) {
				leak.addr = uintptr_t(addr);
#if defined(DEFAULT_LEAK_DETECT_STORE_ALLOC_SIZE)
				leak.size = size;
#endif // #if defined(DEFAULT_LEAK_DETECT_STORE_ALLOC_SIZE)

				leakCount++;
				break;
			}
		}
	}

	static void Unassign(void *addr) {
		size_t leakRemaining = leakCount;
		for (int i = 0; i < Capacity; i++) {
			if (!leakRemaining)
				break;

			if (leaks[i].addr == uintptr_t(NULL))
				continue;

			if (leaks[i].addr == uintptr_t(addr)) {
				leaks[i].addr = uintptr_t(NULL);
				leaks[i].size = 0;
				leakCount--;
				break;
			}

			leakRemaining--;
		}
	}

	static void SetData(void *data, size_t size) {
		assert(NeededSizeInBytes >= size);

		leaks = static_cast<LeakInfo*>(data);
		memset(leaks, 0, NeededSizeInBytes);
	}

	static void EnumerateRemainingAllocs(std::function<void(const LeakInfo&)> func) {
		for (size_t i = 0; i < Capacity; i++) {
			const LeakInfo &leak = leaks[i];
			if (leak.addr != uintptr_t(NULL))
				func(leaks[i]);
		}
	}

	static void Dump() {
		auto printAllocs = [](const LeakInfo &leak) -> void{
			printf("Leak: 0x%-8lX - %6zu\n", leak.addr, leak.size);
		};
		EnumerateRemainingAllocs(printAllocs);
	}

	static constexpr size_t NeededSizeInBytes = Capacity * sizeof(LeakInfo);

private:
	static LeakInfo *leaks;
	static size_t leakCount;
};

template<size_t Capacity> typename DefaultAllocTagPolicy<Capacity>::TagInfo *DefaultAllocTagPolicy<Capacity>::tags = nullptr;
template<size_t Capacity> size_t DefaultAllocTagPolicy<Capacity>::tagCount = 0;
template<size_t Capacity> typename DefaultLeakDetectPolicy<Capacity>::LeakInfo *DefaultLeakDetectPolicy<Capacity>::leaks = nullptr;
template<size_t Capacity> size_t DefaultLeakDetectPolicy<Capacity>::leakCount = 0;

