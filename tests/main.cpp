#include "slmem.h"
#include <random>

typedef DefaultLeakDetectPolicy<65536> MyLeakDetectPolicy;
typedef DefaultAllocTagPolicy<65536> MyAllocTagPolicy;
// TODO add the following tests
// implement tests covering leak detect, tag alloc, fallback policy

static constexpr size_t leak_debug_data_size = MyLeakDetectPolicy::NeededSizeInBytes;
static constexpr size_t tracking_debug_data_size = MyAllocTagPolicy::NeededSizeInBytes;
char leak_debug_data[leak_debug_data_size];
char tracking_debug_data[tracking_debug_data_size];


int main(int argc, char *argv[]) {
	MyLeakDetectPolicy::SetData(leak_debug_data, leak_debug_data_size);
	MyAllocTagPolicy::SetData(tracking_debug_data, tracking_debug_data_size);

	char workBufferData[11];
	LinearAllocator<1> workBuffer(workBufferData, sizeof(workBufferData));

	*(char*)workBuffer.Alloc(1) = 'w';
	*(char*)workBuffer.Alloc(1) = 'o';
	*(char*)workBuffer.Alloc(1) = 'r';
	*(char*)workBuffer.Alloc(1) = 'k';
	*(char*)workBuffer.Alloc(1) = 'b';
	*(char*)workBuffer.Alloc(1) = 'u';
	*(char*)workBuffer.Alloc(1) = 'f';
	*(char*)workBuffer.Alloc(1) = 'f';
	*(char*)workBuffer.Alloc(1) = 'e';
	*(char*)workBuffer.Alloc(1) = 'r';
	*(char*)workBuffer.Alloc(1) = '\0';

	struct FatChar {
		union {
			char data;
			int64_t padding;
		};

		FatChar &operator=(char val) {
			data = val;
			return *this;
		}
	};

	static constexpr size_t PoolCapacity = 11;
	typedef PoolAllocatorBitArray<char, PoolCapacity, MyAllocTagPolicy, MyLeakDetectPolicy> TestPoolBit;
	typedef PoolAllocatorFreelist<FatChar, PoolCapacity, MyAllocTagPolicy, MyLeakDetectPolicy> TestPoolList;
	static constexpr size_t bitPoolNeededSize = TestPoolBit::NeededSizeInBytes;
	static constexpr size_t listPoolNeededSize = TestPoolList::NeededSizeInBytes;

	assert(bitPoolNeededSize != listPoolNeededSize);

	TestPoolBit  poolAllocBit;
	TestPoolList poolAllocList;

	char testPoolBitData[bitPoolNeededSize];
	FatChar testPoolListData[listPoolNeededSize];

	poolAllocBit.SetData(testPoolBitData, bitPoolNeededSize);
	poolAllocList.SetData(testPoolListData, listPoolNeededSize);

#define POOLALLOCBIT_TAG	"[BIT] "
#define POOLALLOCLIST_TAG	"[LIST] "

	auto bitPoolAllocFunc = [&]() -> void {
		*poolAllocBit.Get(POOLALLOCBIT_TAG "w") = 'w';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "o") = 'o';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "r") = 'r';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "k") = 'k';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "b") = 'b';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "u") = 'u';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "f") = 'f';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "f") = 'f';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "e") = 'e';
		*poolAllocBit.Get(POOLALLOCBIT_TAG "r") = 'r';
		*poolAllocBit.Get() = '\0';
	};

	auto listPoolAllocFunc = [&]() -> void {
		*poolAllocList.Get(POOLALLOCLIST_TAG "w") = 'w';
		*poolAllocList.Get(POOLALLOCLIST_TAG "o") = 'o';
		*poolAllocList.Get(POOLALLOCLIST_TAG "r") = 'r';
		*poolAllocList.Get(POOLALLOCLIST_TAG "k") = 'k';
		*poolAllocList.Get(POOLALLOCLIST_TAG "b") = 'b';
		*poolAllocList.Get(POOLALLOCLIST_TAG "u") = 'u';
		*poolAllocList.Get(POOLALLOCLIST_TAG "f") = 'f';
		*poolAllocList.Get(POOLALLOCLIST_TAG "f") = 'f';
		*poolAllocList.Get(POOLALLOCLIST_TAG "e") = 'e';
		*poolAllocList.Get(POOLALLOCLIST_TAG "r") = 'r';
		*poolAllocList.Get() = '\0';
	};

	bitPoolAllocFunc();
	listPoolAllocFunc();

	auto onlyFatCharFilter = [](const MyAllocTagPolicy::TagInfo &tag) -> bool {
		return tag.allocSize >= sizeof(FatChar);
	};

	size_t fatCharPrintCount = 0;
	auto DefaultPrintTestFilter = [&fatCharPrintCount](const MyAllocTagPolicy::TagInfo &tag) -> void {
		MyAllocTagPolicy::DefaultPrint(tag);
		fatCharPrintCount++;
	};

	MyAllocTagPolicy::Dump(DefaultPrintTestFilter, onlyFatCharFilter);
	assert(fatCharPrintCount == PoolCapacity);

	MyAllocTagPolicy::Dump();

	assert(poolAllocBit.GetCount() == PoolCapacity);
	assert(poolAllocList.GetCount() == PoolCapacity);

	for (int i = PoolCapacity - 1; i >= 0; i--) {
		poolAllocBit.Return(&testPoolBitData[i]);
		poolAllocList.Return(&testPoolListData[i]);
	}

	assert(poolAllocBit.GetCount() == 0);
	assert(poolAllocList.GetCount() == 0);

	for (int i = 0; i < PoolCapacity; i++) {
		char *bitAlloc = poolAllocBit.Get();
		FatChar *listAlloc = poolAllocList.Get();
		*bitAlloc = (char)i;
		*listAlloc = (char)i;
		poolAllocBit.Return(bitAlloc);
		poolAllocList.Return(listAlloc);
	}

	assert(poolAllocBit.GetCount() == 0);
	assert(poolAllocList.GetCount() == 0);

	int remainingBitAllocs = poolAllocBit.GetCount();
	int remainingListAllocs = poolAllocList.GetCount();

	enum RandOp : int {
		BitAlloc = 0,
		BitFree = 1,
		ListAlloc = 2,
		ListFree = 3,

		OpCount
	};

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> disOp(BitAlloc, ListFree);
	std::uniform_int_distribution<> disAllocs(0, PoolCapacity - 1);

	int remainingOps[OpCount];
	remainingOps[BitAlloc] = remainingOps[ListAlloc] = PoolCapacity;

	int remainingTotalOps = 100000;

	char *bitAllocs[PoolCapacity];
	FatChar *listAllocs[PoolCapacity];

	memset(bitAllocs, intptr_t(NULL), sizeof(bitAllocs));
	memset(listAllocs, intptr_t(NULL), sizeof(listAllocs));

	while (remainingTotalOps) {
		const RandOp op = RandOp(disOp(gen));
		switch (op) {
			case BitAlloc:
			{
				const size_t allocCount = poolAllocBit.GetCount();
				if (allocCount == PoolCapacity)
					continue;

				bitAllocs[allocCount] = poolAllocBit.Get();
				assert(bitAllocs[allocCount] >= testPoolBitData && bitAllocs[allocCount] < testPoolBitData + PoolCapacity);
			}
			break;

			case ListAlloc:
			{
				const size_t allocCount = poolAllocList.GetCount();
				if (allocCount == PoolCapacity)
					continue;

				listAllocs[allocCount] = poolAllocList.Get();
				assert(listAllocs[allocCount] >= testPoolListData && listAllocs[allocCount] < testPoolListData + PoolCapacity);
			}
			break;

			case BitFree:
			{
				const size_t allocCount = poolAllocBit.GetCount();
				if (allocCount == 0)
					continue;

				const size_t allocIdx = disAllocs(gen) % allocCount;
				poolAllocBit.Return(bitAllocs[allocIdx]);

				if (allocIdx + 1 < allocCount) {
					void **from = reinterpret_cast<void **>(&bitAllocs[allocIdx + 1]);
					void **to = reinterpret_cast<void **>(&bitAllocs[allocIdx]);
					const size_t numAllocs = allocCount - (allocIdx + 1);
					const size_t moveSize = numAllocs * sizeof(bitAllocs[0]);
					memmove(to, from, moveSize);
				}

				bitAllocs[allocCount - 1] = NULL;
			}

			break;

			case ListFree:
			{
				const size_t allocCount = poolAllocList.GetCount();
				if (allocCount == 0)
					continue;

				const size_t allocIdx = disAllocs(gen) % allocCount;
				poolAllocList.Return(listAllocs[allocIdx]);

				if (allocIdx + 1 < allocCount) {
					void *from = reinterpret_cast<void **>(&listAllocs[allocIdx + 1]);
					void *to = reinterpret_cast<void **>(&listAllocs[allocIdx]);
					const size_t numAllocs = allocCount - (allocIdx + 1);
					const size_t moveSize = numAllocs * sizeof(listAllocs[0]);
					memmove(to, from, moveSize);
				}

				listAllocs[allocCount - 1] = NULL;
			}
			break;

			default:
			break;
		}

		remainingTotalOps--;
	}

	MyLeakDetectPolicy::Dump();

	size_t numLeaks = 0;
	size_t leakedSize = 0;
	auto testLeakPolicyCountAllocs = [&poolAllocBit, &poolAllocList, &numLeaks, &leakedSize](const MyLeakDetectPolicy::LeakInfo &leak) -> void {
		numLeaks++;
		leakedSize += leak.size;
	};

	MyLeakDetectPolicy::EnumerateRemainingAllocs(testLeakPolicyCountAllocs);

	const size_t expectedNumLeaks = poolAllocBit.GetCount() + poolAllocList.GetCount();
	const size_t expectedLeakedSize = poolAllocBit.GetCount() * sizeof(char) + poolAllocList.GetCount() * sizeof(FatChar);
	assert(expectedNumLeaks == numLeaks);
	assert(expectedLeakedSize == leakedSize);

	return 0;
}

