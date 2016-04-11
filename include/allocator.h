#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <new>


#define SL_STRINGIFY_MACRO(x)	#x
#define SL_CONCAT_MACRO(a, b, sep)	a sep SL_STRINGIFY_MACRO(b)
#define SL_CURRENT_FILE_LINENUM	SL_CONCAT_MACRO(__FILE__, __LINE__, ":")

#define SLMEM_NOALLOC_TAG_POLICY_INVALID_ID	"NULL"

class NoAllocTagPolicy {
public:
	static void Tag(const void * /*addr*/, const char * /*id*/, size_t /*size*/) { }
	static void Untag(const void * /*addr*/) { }
};

class NoLeakDetectPolicy {
public:
	static void Assign(const void * /*addr*/, size_t /*size*/) {}
	static void Unassign(const void * /*addr*/ ) {}
};

class NoFallbackPolicy {
public:
	static void OnAlloc(const void * /*addr*/, size_t /*size*/) {}
};

template <typename T>
class AllocatorTraits {
public:
	~AllocatorTraits() {
		T::Free(T::Alloc(0));
	}

	static void SetData(T &allocator, void *preAllocatedData, size_t size) {
		allocator.SetData(preAllocatedData, size);
	}

	static void* Alloc(T &allocator, size_t size, const char *allocId = SLMEM_NOALLOC_TAG_POLICY_INVALID_ID) {
		return allocator.Alloc(size, allocId);
	}

	static void Free(T &allocator, void *addr) {
		allocator.Free(addr);
	}

	static size_t GetCount(T &allocator) {
		return allocator.GetCount();
	}
};

// TODO enforce LeakDetectPolicy to always be NoLeakDetectPolicy as there's no real freeing of allocations in a LinearAllocator
template<size_t Alignment = 4, typename AllocTagPolicy = NoAllocTagPolicy, typename LeakDetectPolicy = NoLeakDetectPolicy, typename FallbackPolicy = NoFallbackPolicy>
class LinearAllocator {
public:
	LinearAllocator(void *preAllocatedData, size_t size)
		: data(preAllocatedData)
		, currentPtr(static_cast<unsigned char*>(data))
		, dataSize(size) {
	}

	~LinearAllocator() {
	}

	void *Alloc(size_t size, const char *allocId = SLMEM_NOALLOC_TAG_POLICY_INVALID_ID) {
		assert(data && "Allocator preallocated data not set.");

		// TODO add a SLMISC_ALIGNUP or something like that and use it here
		const size_t alignedSize = (size + Alignment - 1) & ~(Alignment - 1);

		void *ret = nullptr;
		if (currentPtr + alignedSize <= static_cast<unsigned char*>(data) + dataSize) {
			ret = currentPtr;
			currentPtr += alignedSize;
			count++;

			AllocTagPolicy::Tag(ret, allocId, alignedSize);
			LeakDetectPolicy::Assign(ret, alignedSize);
		}
		else {
			FallbackPolicy::OnAlloc(nullptr, alignedSize);
		}

		return ret;
	}

	void Free(void *addr) {}

	void Reset() {
		currentPtr = data;
		count = 0;
	}

	size_t GetCount() {
		return count;
	}

private:
	void *data = nullptr;
	unsigned char *currentPtr = nullptr;
	size_t dataSize = 0;
	size_t count = 0;
};

template<typename ElemType, size_t Capacity, typename AllocTagPolicy = NoAllocTagPolicy, typename LeakDetectPolicy = NoLeakDetectPolicy, typename FallbackPolicy = NoFallbackPolicy>
class PoolAllocatorBitArray {
public:
	PoolAllocatorBitArray(void *preAllocatedData, size_t size) :
		dataAsVoid(preAllocatedData),
		freeElem(dataAsElemType) {

		SetData(preAllocatedData, size);
	}

	PoolAllocatorBitArray() :
		dataAsVoid(nullptr) {
		memset(elemsUsage, 0, sizeof(elemsUsage));
	}

	~PoolAllocatorBitArray() {
	}

	void SetData(void *preAllocatedData, size_t size) {
		assert(NeededSizeInBytes <= size && "Pre-allocated data is smaller than the allocator required size.");

		dataAsVoid = preAllocatedData;
		freeElem = dataAsElemType;

		memset(elemsUsage, 0, sizeof(elemsUsage));
	}

	bool HasData() const {
		return dataAsVoid != nullptr;
	}

	// TODO AllocatorTrait requires Alloc/Free methods which is not compatible with this, add a PoolType = true for the pool allocator classes which would enable
	// it to be validated accordingly by the AllocatorTrait?
	template<bool ShouldConstruct = false>
	ElemType *Get(const char *allocId = SLMEM_NOALLOC_TAG_POLICY_INVALID_ID) {
		assert(dataAsVoid && "Preallocated data not set.");

		if (count == Capacity) {
			FallbackPolicy::OnAlloc(nullptr, sizeof(ElemType));
			return nullptr;
		}

		int freeIdx = -1;
		int freeBit = -1;
		if (!freeElem) {
			findFree(freeIdx, freeBit);

			assert(freeIdx != -1 && freeBit != -1 && "Internal error. No free element but element count != Capacity.");
		}
		else {
			const size_t poolIndex = freeElem - dataAsElemType;
			freeIdx = poolIndex >> 5;
			freeBit = poolIndex % 32;
		}

		assert((elemsUsage[freeIdx] & (1 << freeBit)) == 0 && "Internal error. Free element pointer doesn't point to a free slot.");
		elemsUsage[freeIdx] |= (1 << freeBit);
		count++;

		// try to set freeElem to the next element
		freeElem = nullptr;
		if (count < Capacity) {
			if (freeBit < 31) {
				const int nextBit = (freeBit + 1) % Capacity;
				freeElem = isFree(freeIdx, nextBit) ? &dataAsElemType[poolIndexFromUsageIndexAndBit(freeIdx, nextBit)] : nullptr;
			}
			else {
				freeElem = isFree(freeIdx + 1, 0) ? &dataAsElemType[poolIndexFromUsageIndexAndBit(freeIdx + 1, 0)] : nullptr;
			}
		}

		ElemType *ret = &dataAsElemType[poolIndexFromUsageIndexAndBit(freeIdx, freeBit)];
		assert(ret >= dataAsElemType && ret < dataAsElemType + Capacity);

		AllocTagPolicy::Tag(ret, allocId, sizeof(ElemType));
		LeakDetectPolicy::Assign(ret, sizeof(ElemType));

		if (ShouldConstruct)
			new (ret) ElemType;

		return ret;
	}

	template<bool ShouldDestroy = false>
	void Return(ElemType *elem) {
		assert((elem >= dataAsElemType && elem < dataAsElemType + Capacity) && "The element is not within this pool range.");

		const size_t poolIndex = elem - dataAsElemType;
		const int index = poolIndex >> 5;
		const int bit = poolIndex % 32;

		assert((elemsUsage[index] & (1 << bit)) && "Element already freed.");
		elemsUsage[index] &= ~(1 << bit);

		assert(count && "Internal error. Freeing an element while count is already at 0.");
		count--;

		ElemType *ret = &dataAsElemType[poolIndexFromUsageIndexAndBit(index, bit)];
		freeElem = ret;

		LeakDetectPolicy::Unassign(ret);
		AllocTagPolicy::Untag(ret);

		if (ShouldDestroy)
			ret->~ElemType();

	}

	size_t GetCount() const {
		return count;
	}

	static constexpr size_t NeededSizeInBytes = sizeof(ElemType) * Capacity;

private:
	static constexpr size_t ElemTypeSize = sizeof(ElemType);
	static constexpr size_t ElemsUsageCount = (Capacity + 31) / 32;

	void findFree(int &index, int &bit) {
		for (int local_index = 0; local_index < ElemsUsageCount; local_index++) {
			for (int local_bit = 0; local_bit < 32; local_bit++) {
				if (isFree(local_index, local_bit)) {
					index = local_index;
					bit = local_bit;
					break;
				}
			}
		}
	}

	bool isFree(int index, int bit) const {
		return (elemsUsage[index] & (1 << bit)) == 0;
	}

	size_t poolIndexFromUsageIndexAndBit(int index, int bit) const {
		size_t ret = (index << 5) + bit;
		assert(ret < Capacity);

		return ret;
	}

	union {
		void *dataAsVoid;
		ElemType *dataAsElemType;
	};

	ElemType *freeElem = nullptr;

	int elemsUsage[ElemsUsageCount];
	size_t count = 0;
};

template<typename ElemType, size_t Capacity, typename AllocTagPolicy = NoAllocTagPolicy, typename LeakDetectPolicy = NoLeakDetectPolicy, typename FallbackPolicy = NoFallbackPolicy>
class PoolAllocatorFreelist {
public:
	PoolAllocatorFreelist(void *preAllocatedData, size_t size) {
		SetData(preAllocatedData, size);
	}

	PoolAllocatorFreelist() {
	}

	~PoolAllocatorFreelist() {
	}

	void SetData(void *preAllocatedData, size_t size) {
		static_assert(sizeof(ElemType) >= sizeof(void*), "Pool element size must be greater than a pointer size.");
		assert(NeededSizeInBytes <= size && "Pre-allocated data is smaller than the allocator required size.");

		data = preAllocatedData;
		freeElemHead = (FreelistNode*)data;

		for (size_t i = 0; i < Capacity - 1; i++) {
			FreelistNode *curr = (FreelistNode *)(&((ElemType*)data)[i]);
			FreelistNode *next = (FreelistNode *)(&((ElemType*)data)[i + 1]);
			curr->next =  next;
		}
		((FreelistNode *)(&((ElemType*)data)[Capacity - 1]))->next = nullptr;
	}

	bool HasData() const {
		return data != nullptr;
	}

	// TODO AllocatorTrait requires Alloc/Free methods which is not compatible with this, add a PoolType = true for the pool allocator classes which would enable
	// it to be validated accordingly by the AllocatorTrait?
	template<bool ShouldConstruct = false>
	ElemType *Get(const char *allocId = SLMEM_NOALLOC_TAG_POLICY_INVALID_ID) {
		if (!freeElemHead) {
			FallbackPolicy::OnAlloc(nullptr, sizeof(ElemType));
			return nullptr;
		}

		ElemType *ret = (ElemType*)freeElemHead;
		AllocTagPolicy::Tag(ret, allocId, sizeof(ElemType));
		LeakDetectPolicy::Assign(ret, sizeof(ElemType));

		if (ShouldConstruct)
			new (ret) ElemType;

		freeElemHead = freeElemHead->next;

		assert(count < Capacity && "Internal error. There's a free element while the current count is already at Capacity.");
		count++;
		return ret;
	}

	template<bool ShouldDestroy = false>
	void Return(ElemType *elem) {
		assert((elem >= (ElemType*)data && elem < (ElemType*)data + Capacity) && "The element is not within this pool range.");

		LeakDetectPolicy::Unassign(elem);
		AllocTagPolicy::Untag(elem);

		if (ShouldDestroy)
			elem->~ElemType();

		FreelistNode *prevFree = freeElemHead;
		freeElemHead = (FreelistNode*)elem;
		freeElemHead->next = prevFree;

		assert(count && "Internal error. Freeing an element while count is already at 0.");
		count--;
	}

	size_t GetCount() const {
		return count;
	}

	static constexpr size_t NeededSizeInBytes = sizeof(ElemType) * Capacity;

private:
	static constexpr size_t ElemTypeSize = sizeof(ElemType);

	struct FreelistNode {
		FreelistNode *next = nullptr;
	};

	void *data = nullptr;

	FreelistNode *freeElemHead = nullptr;

	size_t count = 0;
};

