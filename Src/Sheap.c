/** @file sheap.c
 *  @brief Provides a secure heap (sheap) implementations.
 *
 *  This implementation provides malloc/free routines with added safty measures like:
 *  	+ checking double free
 *  	+ each allocated memory block has a checksum to recognize external altering / invalid access
 *  	+ each allocated memory block is suffixed with a memory tag which is checked on each free call to determine if a direct bound overlow happened
 *  		*-------------------------------------------*--------------*
 *  		|			Allocated data					|      CRC     |
 *  		*-------------------------------------------*--------------*
 *
 *  	+ when using the provided SHEAP_MALLOC/SHEAP_FREE macros the program counter of each of these calls
 *  	  will be stored to support debugging if an error occurs (Number of saved pc's can be configured
 *  	  using the SHEAPERD_DEFAULT_SHEAP_PC_LOG_SIZE define)
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include <sheap.h>

#define BYTE_TO_WORD(size) (size / sizeof(uint32_t))

#define PAYLOAD_BLOCK_TO_MEMORY_BLOCK(payload) 	((memory_blockInfo_t*)payload) - 1
#define GET_NEXT_MEMORY_BLOCK(block) 			(memory_blockInfo_t*)	(((uint8_t*)block) + 2 * sizeof(memory_blockInfo_t) + block->size)
#define GET_PREV_MEMORY_BLOCK(block)			(memory_blockInfo_t*)	(((uint8_t*)block) - 2 * sizeof(memory_blockInfo_t) - GET_SIZE_OF_PREV_BLOCK(block))
#define GET_BOUNDARY_TAG(block)					(memory_blockInfo_t*)  	(((uint8_t*)block) + sizeof(memory_blockInfo_t) + block->size)
#define GET_BLOCK_OVERHEAD_SIZE(size)			(size_t) (size +  (2 * sizeof(memory_blockInfo_t)))
#define GET_SIZE_OF_PREV_BLOCK(block)			(block - 1)->size

#define GET_R1(r1_val)							asm ("mov %0, r1" : "=r" (r1_val))

typedef struct{
	uint8_t* 	heapMin;
	uint8_t* 	heapMax;
	uint32_t 	currentAllocations;
	uint32_t 	totalBytesAllocated;
	uint32_t	userDataAllocated;
	size_t 		size;
} heap_t;


#pragma pack(1)
typedef struct memory_blockInfo_t memory_blockInfo_t;
typedef struct memory_blockInfo_t{
	//Allocations will only be performed in 4 byte steps. Therefore the lowest bit can be used as a flag
	uint32_t 			isAllocated : 1;
	uint32_t 			size 		: 31;
	uint32_t			crc;
	//payload added here - no datatype for convenient pointer arithmetics
} memory_blockInfo_t;
#pragma pack()

#ifdef SHEAPERD_CMSIS_2
	static osMutexId_t gMemMutex_id;
	static const osMutexAttr_t memMutex_attr = {
		  "sheap_mutex",
		  osMutexRecursive,
		  NULL,
		  0U
	};
#endif

static memory_blockInfo_t* gStartBlock;
static heap_t gHeap;
static uint32_t gProgramCounters[SHEAPERD_SHEAP_PC_LOG_SIZE];
static uint32_t gCurrentPCIndex;

static void sheap_logAccess();
static size_t align(size_t n);
static memory_blockInfo_t* getNextFreeBlockOfSize(size_t size);
static bool isBlockValid(memory_blockInfo_t* block);
static bool isBlockCRCValid(memory_blockInfo_t* block);
static void clearMemory(uint8_t* ptr, size_t size);
static void updateBlockBoundary(memory_blockInfo_t* block);
static void updateBlockHeader(memory_blockInfo_t* block, size_t size, bool isAllocated);
static void updateCRC(memory_blockInfo_t* block);
static bool isPreviousBlockFree(memory_blockInfo_t* block);
static bool isNextBlockFree(memory_blockInfo_t* block);

void sheap_init(uint32_t* heapStart, size_t size){
	if(size == 0){
		SHEAPERD_ASSERT("Sheap init failed due to invalid size.", size > 0);
		return;
	}
	for(int i = 0; i < SHEAPERD_SHEAP_PC_LOG_SIZE; i++){
		gProgramCounters[i] = 0;
	}
	gCurrentPCIndex = -1;

	gHeap.heapMin = (uint8_t*) heapStart;
	gHeap.size = size;
	gHeap.heapMax = gHeap.heapMin + gHeap.size;
	clearMemory(gHeap.heapMin, size);

	gStartBlock = (memory_blockInfo_t*) gHeap.heapMin;
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), false);
	updateBlockBoundary(gStartBlock);
#ifdef SHEAPERD_CMSIS_2
	gMemMutex_id = osMutexNew(&memMutex_attr);
	SHEAPERD_ASSERT("Mutex creation for sheap init failed.", gMemMutex_id != NULL);
#endif
}

void* sheap_malloc_impl(){
	// Save the size from r0 before doing any other operation
	//--------------------------------
	register int* size asm ("r0");
	size_t s = (size_t)size;
	//--------------------------------
	if(gHeap.heapMin == NULL){
		SHEAPERD_ASSERT("\"SHEAP_MALLOC\" must not be used before the initialization (\"sheap_init\").", gHeap.heapMin != NULL);
		return NULL;
	}
	uint32_t pc;
	GET_R1(pc);
	sheap_logAccess(pc);
	return (void*)malloc(s);
}

void sheap_free_impl(){
	sheap_logAccess();
}

void sheap_logAccess(uint32_t pc){
	gProgramCounters[(gCurrentPCIndex + 1) % SHEAPERD_SHEAP_PC_LOG_SIZE] = (uint32_t)pc;
}

size_t sheap_getHeapSize(){
	return gHeap.size;
}

//Aligns the requested size to a multiple of 4 byte
size_t align(size_t n) {
	return (n + SHEAP_MINIMUM_MALLOC_SIZE - 1) & ~(SHEAP_MINIMUM_MALLOC_SIZE - 1);
}

memory_blockInfo_t* getNextFreeBlockOfSize(size_t size){
	memory_blockInfo_t* current = gStartBlock;
	while((current->isAllocated == true || current->size < size) && (((uint8_t*)current) < gHeap.heapMax)){
		current = GET_NEXT_MEMORY_BLOCK(current);
	}
	if(current == NULL || (((uint8_t*)current) >= gHeap.heapMax)){
		//no more memory left
		return NULL;
	}

	if(!isBlockValid(current)){
		//block has most likely been altered -- fault
		return NULL;
	}
	return current;
}

//static void sheap_coalesceBlocks();

void* malloc(size_t size){
	if(size == 0){
		SHEAPERD_ASSERT("Cannot allocate size of 0. Is this call intentional?", false);
		return NULL;
	}
	size = align(size);
	memory_blockInfo_t* allocate = getNextFreeBlockOfSize(size);

	if(allocate == NULL){
		return NULL;
	}

	uint32_t preAllocSize = allocate->size;
	allocate->isAllocated = true;
	allocate->size = size;
	gHeap.currentAllocations += 1;
	//TODO: make statistics - own funciton
	//	gHeap.totalBytesAllocated += ;
	gHeap.userDataAllocated += size;

	updateCRC(allocate);
	updateBlockBoundary(allocate);

	//pointer arithmetic used to jump over the
	uint8_t* payload = (uint8_t*)(allocate + 1);

	if(preAllocSize > GET_BLOCK_OVERHEAD_SIZE(size)){
		memory_blockInfo_t* remainingBlock = GET_NEXT_MEMORY_BLOCK(allocate);
		updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(size), false);
		updateBlockBoundary(remainingBlock);
	}
    return payload;
}

void free(void* ptr){
	if(ptr == NULL){
		SHEAPERD_ASSERT("MEMORY ERROR: Free operation not valid for null pointer", false);
		return;
	}
	memory_blockInfo_t* current = PAYLOAD_BLOCK_TO_MEMORY_BLOCK(ptr);
	if(current == NULL){
		//Cannot free pointer - error
		SHEAPERD_ASSERT("Cannot free the provided pointer", current != NULL);
		return;
	}
	bool blockValid = isBlockValid(current);
	if(!blockValid){
		SHEAPERD_ASSERT("MEMORY ERROR: Free operation can not be performed as block has been altered", blockValid);
		//Maybe hardfault? Error callback? Configurable!!!!!
		return;
	}

	if(current->isAllocated){
		current->isAllocated = false;
		gHeap.currentAllocations -= 1;
		gHeap.userDataAllocated -= current->size;
		#ifdef SHEAPERD_SHEAP_OVERWRITE_ON_FREE
			clearMemory((uint8_t*)ptr, current->size);
		#endif

		size_t size = current->size;
		if(isNextBlockFree(current)){
			memory_blockInfo_t* next = GET_NEXT_MEMORY_BLOCK(current);
			bool isValid = isBlockValid(next);
			SHEAPERD_ASSERT("MEMORY ERROR: Free cannot coalesce with next block as it is not valid.", isValid);
			if(isValid){
				size += next->size + (2 * sizeof(memory_blockInfo_t));
			}
		}
		if(isPreviousBlockFree(current)){
			memory_blockInfo_t* prev = GET_PREV_MEMORY_BLOCK(current);
			bool isValid = isBlockValid(prev);
			SHEAPERD_ASSERT("MEMORY ERROR: Free cannot coalesce with previous block as it is not valid.", isValid);
			if(isValid){
				size += prev->size + (2 * sizeof(memory_blockInfo_t));
				current = prev;
			}
		}
		current->size = size;
		updateCRC(current);
		updateBlockBoundary(current);
		//TODO: stats
//		gHeap.totalBytesAllocated -=
		//Maybe consolidate
	}else{
		//Cannot free not allocated block -- should not be here?
		SHEAPERD_ASSERT("Sheaperd free block valid, but not allocated, should not happen.", false);
	}
}

bool isPreviousBlockFree(memory_blockInfo_t* block){
	memory_blockInfo_t* prevBoundary = block - 1;
	return ((uint8_t*) prevBoundary) >= gHeap.heapMin && !prevBoundary->isAllocated;
}

bool isNextBlockFree(memory_blockInfo_t* block){
	memory_blockInfo_t* next = GET_NEXT_MEMORY_BLOCK(block);
	return ((uint8_t*) next) <= gHeap.heapMax && !next->isAllocated;
}

void updateCRC(memory_blockInfo_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	block->crc = crc32_sw_calculate(crcData, 4);
}

void updateBlockHeader(memory_blockInfo_t* block, size_t size, bool isAllocated){
	block->isAllocated = isAllocated;
	block->size = size;
	updateCRC(block);
}


void updateBlockBoundary(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	boundary->isAllocated = block->isAllocated;
	boundary->size = block->size;
	boundary->crc = block->crc;
}

bool isBlockValid(memory_blockInfo_t* block){
	return isBlockCRCValid(block);
}

bool isBlockCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)block;
	uint32_t crc = crc32_sw_calculate(crcData, 4);
	return (block->crc == crc) && (boundary->crc == crc);
}

void clearMemory(uint8_t* ptr, size_t size){
	for(int i = 0; i < size; i++){
		ptr[i] = SHEAPERD_SHEAP_OVERWRITE_VALUE;
	}
}

size_t sheap_getAllocatedBytes(){
	return gHeap.userDataAllocated;
}
