/** @file sheap.c
 *  @brief Provides a secure heap (sheap) implementations.
 *
 *  Memory block layout:
 *  +------------------------+------------+------------+---------------------------------------------+------------------------+------------+------------+
 *  |                        |            |            |                                             |                        |            |            |
 *  |      aligned size      |  alignment |   CRC16    |                  PAYLOAD                    |      aligned size      |  alignment |   CRC16    |
 *  |       alloc flag       |   offset   |            |                 USER DATA                   |       alloc flag       |   offset   |            |
 *  |                        |            |            |                                             |                        |            |            |
 *  +------------------------+------------+------------+---------------------------------------------+------------------------+-------------------------+
 *  ^-- 4 bytes              ^-- 2 bytes  ^-- 2 bytes  ^-- aligned size bytes                        ^-- 4bytes               ^-- 2 bytes  ^-- 2 bytes
 *
 *
 *	The memory block info stores the size of the allocated user data (payload size). As the size is aligned to at least 4 due to 'SHEAP_MINIMUM_MALLOC_SIZE'
 *	the lowest bit can be used as a flag to mark if a block currently is allocated or not.
 *	The CRC is calculated over the size/alloc-flag and the alignment offset. It is intended to detect bound overflow or altered blocks in general.
 *	The boundary (end tag) with size, alignment and CRC information is used for coalescing of blocks and can also be checked to recognize if a block was altered
 *
 *	Why is the aligned and the alignment offset stored?
 *	This library should help to detect as many memory related errors as possible. If only the aligned size is stored a user would allocate 5 bytes and a aligned size of 8 bytes is reserved and a block is created accordingly.
 *	When freeing a block and checking if a write out of bound occurred it could only be recognized if it altered the next block. When storing the alignment offset, the user requested size can be calculated and
 *	the block can be checked if for example 5 bytes have been requested from the user, and 7 bytes have been written. In that case an error can be reported when storing the additional alignment offset.
 *
 *  This implementation provides malloc/free routines with added safty measures like:
 *  	+ checking double free
 *  	+ each allocated memory block has a checksum to recognize external altering / invalid access
 *  	+ each allocated memory block is suffixed with a boundary tag which can be checked on each free call to determine if a direct bound overflow happened
 *  	  (When free is called for a block, the header of the block can be checked. If it is not valid, the boundary can be checked. If the header is not valid, but the
 *  	  boundary is, it is an indicator that something went wrong and it could be because of an bound overflow)
 *  	+ when using the provided SHEAP_MALLOC/SHEAP_FREE macros the program counter of each of these calls
 *  	  will be stored to support debugging if an error occurs (number of saved pc's can be configured
 *  	  using the SHEAPERD_DEFAULT_SHEAP_PC_LOG_SIZE define)
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/opt.h"

// don't build sheap if not enabled via options
#if SHEAPERD_SHEAP

#include "sheap.h"

#define PAYLOAD_BLOCK_TO_MEMORY_BLOCK(payload) 	((memory_blockInfo_t*)payload) - 1
#define GET_NEXT_MEMORY_BLOCK(block) 			(memory_blockInfo_t*)	(((uint8_t*)block) + 2 * sizeof(memory_blockInfo_t) + block->size)
#define GET_PREV_MEMORY_BLOCK(block)			(memory_blockInfo_t*)	(((uint8_t*)block) - 2 * sizeof(memory_blockInfo_t) - GET_SIZE_OF_PREV_BLOCK(block))
#define GET_BOUNDARY_TAG(block)					(memory_blockInfo_t*)  	(((uint8_t*)block) + sizeof(memory_blockInfo_t) + block->size)
#define GET_BLOCK_OVERHEAD_SIZE(size)			(size_t) (size +  (2 * sizeof(memory_blockInfo_t)))
#define GET_SIZE_OF_PREV_BLOCK(block)			(block - 1)->size

#define GET_R1(r1_val)							asm ("mov %0, r1" : "=r" (r1_val))

#define REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType)  	\
do{                                                             	\
	SHEAPERD_ASSERT(assertMsg, false, assertionType);	       		\
	/*errorCallback(errorType);*/                                  	\
	releaseMutex();                                            		\
}while(0)
#define REPORT_ERROR_RELEASE_MUTEX_AND_RETURN(assertMsg, assertionType)			\
do{                                                               				\
	REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType);     	         	\
	return;                                                        				\
}while(0)
#define REPORT_ERROR_RELEASE_MUTEX_AND_RETURN_NULL(assertMsg, assertionType)    	\
do{                                                                         		\
	REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType);						\
	return NULL;                                                          			\
}while(0)

#pragma pack(1)
typedef struct memory_blockInfo_t{
	uint32_t 			isAllocated : 1;
	uint32_t 			size 		: 31;
	uint16_t			alignmentOffset;
	uint16_t			crc;
} memory_blockInfo_t;
#pragma pack()

typedef enum{
	MEMORY_OP_ALLOC,
	MEMORY_OP_FREE
} memory_operation_t;

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
static sheap_heap_t gHeap;
static uint32_t gProgramCounters[SHEAPERD_SHEAP_PC_LOG_SIZE];
static uint32_t gCurrentPCIndex;

static void sheap_logAccess();
static memory_blockInfo_t* getNextFreeBlockOfSize(size_t size);
static bool isBlockValid(memory_blockInfo_t* block);
static bool isBlockCRCValid(memory_blockInfo_t* block);
static void clearMemory(uint8_t* ptr, size_t size);
static void updateBlockBoundary(memory_blockInfo_t* block);
static void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated);
static void updateCRC(memory_blockInfo_t* block);
static bool isPreviousBlockFree(memory_blockInfo_t* block);
static bool isNextBlockFree(memory_blockInfo_t* block);
static void coalesce(memory_blockInfo_t** block);
static void clearBlockMeta(memory_blockInfo_t* block);
static void clearBlockHeader(memory_blockInfo_t* block);
static void clearBlockBoundary(memory_blockInfo_t* block);
static bool isBlockHeaderCRCValid(memory_blockInfo_t* block);
static bool isBlockBoundaryCRCValid(memory_blockInfo_t* block);
static bool	checkForIllegalWrite(memory_blockInfo_t* block);
static void updateHeapStatistics(memory_operation_t op, uint32_t allocations, uint32_t sizeAligned, uint32_t size, uint32_t blockSize);

static void initMutex();
static bool acquireMutex();
static bool releaseMutex();

void sheap_init(uint32_t* heapStart, size_t size){
	if(size == 0){
		SHEAPERD_ASSERT("Sheap init failed due to invalid size.", size > 0, SHEAP_INIT_INVALID_SIZE);
		return;
	}
	for(int i = 0; i < SHEAPERD_SHEAP_PC_LOG_SIZE; i++){
		gProgramCounters[i] = 0;
	}
	gCurrentPCIndex = -1;

	gHeap.heapMin = (uint8_t*) heapStart;
	gHeap.size = size;
	gHeap.heapMax = gHeap.heapMin + gHeap.size;
	gHeap.userDataAllocatedAlligned = 0;
	gHeap.userDataAllocated = 0;
	gHeap.totalBytesAllocated = 0;
	gHeap.currentAllocations = 0;
	clearMemory(gHeap.heapMin, size);

	gStartBlock = (memory_blockInfo_t*) gHeap.heapMin;
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), 0, false);
	updateBlockBoundary(gStartBlock);
	initMutex();
}

void* sheap_malloc_impl(){
	// Save the size from r0 before doing any other operation
	//--------------------------------
	register int* size asm ("r0");
	size_t s = (size_t)size;
	//--------------------------------
	if(gHeap.heapMin == NULL){
		SHEAPERD_ASSERT("\"SHEAP_MALLOC\" must not be used before the initialization (\"sheap_init\").", gHeap.heapMin != NULL, SHEAP_NOT_INITIALIZED);
		return NULL;
	}
	uint32_t pc;
	GET_R1(pc);
	if(!acquireMutex()){
		return NULL;
	}
	sheap_logAccess(pc);
	return (void*)malloc(s);
}

void sheap_free_impl(){
	// Get the pointer to free from r0
	//---------------------------------
	register void* ptr asm ("r0");
	void* pFree = ptr;
	//---------------------------------
	uint32_t pc;
	GET_R1(pc);
	if(!acquireMutex()){
		return;
	}
	sheap_logAccess(pc);
	free((void*) pFree);
}

void sheap_logAccess(uint32_t pc){
	gProgramCounters[(++gCurrentPCIndex) % SHEAPERD_SHEAP_PC_LOG_SIZE] = (uint32_t)pc;
}

size_t sheap_getHeapSize(){
	return gHeap.size;
}

size_t sheap_align(size_t n) {
	return (n + SHEAP_MINIMUM_MALLOC_SIZE - 1) & ~(SHEAP_MINIMUM_MALLOC_SIZE - 1);
}

memory_blockInfo_t* getNextFreeBlockOfSize(size_t size){
	memory_blockInfo_t* current = gStartBlock;
	while((current->isAllocated == true || current->size < size) && (((uint8_t*)current) < gHeap.heapMax)){
		current = GET_NEXT_MEMORY_BLOCK(current);
	}
	if(current == NULL || (((uint8_t*)current) >= gHeap.heapMax)){
		SHEAPERD_ASSERT("MEMORY Information: No memory available.", false, SHEAP_OUT_OF_MEMORY);
		return NULL;
	}

	if(!isBlockValid(current)){
		SHEAPERD_ASSERT("MEMORY ERROR: Found invalid block. It may have been altered.", false, SHEAP_ERROR_INVALID_BLOCK);
		return NULL;
	}
	return current;
}

void* malloc(size_t size){
	if(size == 0){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN_NULL("Cannot allocate size of 0. Is this call intentional?", SHEAP_SIZE_ZERO_ALLOC);
	}
	size_t sizeAligned = sheap_align(size);
	memory_blockInfo_t* allocate = getNextFreeBlockOfSize(sizeAligned);

	if(allocate == NULL){
		releaseMutex();
		return NULL;
	}

	uint32_t preAllocSize = allocate->size;
	allocate->isAllocated = true;
	allocate->size = sizeAligned;
	allocate->alignmentOffset = sizeAligned - size;
	updateHeapStatistics(MEMORY_OP_ALLOC, 1, sizeAligned, size, GET_BLOCK_OVERHEAD_SIZE(sizeAligned));

	updateCRC(allocate);
	updateBlockBoundary(allocate);

	uint8_t* payload = (uint8_t*)(allocate + 1);

	if(preAllocSize > GET_BLOCK_OVERHEAD_SIZE(sizeAligned)){
		memory_blockInfo_t* remainingBlock = GET_NEXT_MEMORY_BLOCK(allocate);
		updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(sizeAligned), 0, false);
		updateBlockBoundary(remainingBlock);
	}
	releaseMutex();
    return payload;
}

void free(void* ptr){
	if(ptr == NULL){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("MEMORY ERROR: Free operation not valid for null pointer", SHEAP_ERROR_NULL_FREE);
	}
	if(ptr < (void*)gHeap.heapMin || ptr > (void*)gHeap.heapMax){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("Cannot free pointer outside of heap.", SHEAP_ERROR_FREE_PTR_NOT_IN_HEAP);
	}
	memory_blockInfo_t* current = PAYLOAD_BLOCK_TO_MEMORY_BLOCK(ptr);
	if(current == NULL){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("Cannot free the provided pointer", SHEAP_ERROR_FREE_INVALID_HEADER);
	}
	if(!isBlockHeaderCRCValid(current)){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("MEMORY ERROR: Free operation can not be performed as block header is not valid",
				SHEAP_ERROR_FREE_INVALID_HEADER);
	}else if(!isBlockBoundaryCRCValid(current)){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("MEMORY ERROR: Free operation can not be performed as block boundary is not valid. It may have been altered. Calling the error callback",
				SHEAP_ERROR_FREE_INVALID_BOUNDARY_POSSIBLE_OUT_OF_BOUND_WRITE);
	}

#ifdef SHEAPERD_SHEAP_FREE_CHECK_UNALIGNED_SIZE
	bool illegalWrite = checkForIllegalWrite(current);
	if(illegalWrite){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("MEMORY ERROR: Out of bound write detected. Free operation aborted", SHEAP_ERROR_OUT_OF_BOUND_WRITE);
	}
#endif

	if(current->isAllocated){
		current->isAllocated = false;
		updateHeapStatistics(MEMORY_OP_FREE, 1, current->size, current->size - current->alignmentOffset, GET_BLOCK_OVERHEAD_SIZE(current->size));

#ifdef SHEAPERD_SHEAP_OVERWRITE_ON_FREE
		clearMemory((uint8_t*)ptr, current->size);
#endif
		coalesce(&current);
		updateCRC(current);
		updateBlockBoundary(current);
	}else{
		SHEAPERD_ASSERT("MEMORY ERROR: Double free detected.", false, SHEAP_ERROR_DOUBLE_FREE);
	}
	releaseMutex();
}

void coalesce(memory_blockInfo_t** block){
	size_t size = (*block)->size;
	if (isNextBlockFree((*block))) {
		memory_blockInfo_t* next = GET_NEXT_MEMORY_BLOCK((*block));
		bool isValid = isBlockValid(next);
		SHEAPERD_ASSERT("MEMORY ERROR: Free cannot coalesce with next block as it is not valid.", isValid, SHEAP_ERROR_COALESCING_NEXT_BLOCK_ALTERED_INVALID_CRC);
		if (isValid) {
			size += next->size + (2 * sizeof(memory_blockInfo_t));
			clearBlockHeader(next);
			clearBlockBoundary(*block);
		}
	}
	if (isPreviousBlockFree((*block))) {
		memory_blockInfo_t* prev = GET_PREV_MEMORY_BLOCK((*block));
		bool isValid = isBlockValid(prev);
		SHEAPERD_ASSERT("MEMORY ERROR: Free cannot coalesce with previous block as it is not valid.", isValid, SHEAP_ERROR_COALESCING_PREV_BLOCK_ALTERED_INVALID_CRC);
		if (isValid) {
			size += prev->size + (2 * sizeof(memory_blockInfo_t));
			clearBlockHeader(*block);
			(*block) = prev;
			clearBlockBoundary(prev);
		}
	}
	(*block)->size = size;
	(*block)->alignmentOffset = 0;
}

uint32_t sheap_getLatestAllocationPCs(uint32_t destination[], uint32_t n){
	uint32_t index = gCurrentPCIndex;
	uint32_t count = 0;
	while(gProgramCounters[index % SHEAPERD_SHEAP_PC_LOG_SIZE] != 0 && count < n && count < SHEAPERD_SHEAP_PC_LOG_SIZE){
		destination[count++] = gProgramCounters[(index--) % SHEAPERD_SHEAP_PC_LOG_SIZE];
	}
	return count;
}

bool checkForIllegalWrite(memory_blockInfo_t* block){
	size_t requestedSize = block->size - block->alignmentOffset;
	uint8_t* pAfterPayload = ((uint8_t*)(block + 1)) + requestedSize;
	for(int i = 0; i < block->alignmentOffset; i++){
		if(pAfterPayload[i] != SHEAPERD_SHEAP_OVERWRITE_VALUE){
			return true;
		}
	}
	return false;
}

void clearBlockMeta(memory_blockInfo_t* block){
#ifdef SHEAPERD_SHEAP_OVERWRITE_ON_FREE
	clearBlockBoundary(block);
	clearBlockHeader(block);
#endif
}

void clearBlockHeader(memory_blockInfo_t* block){
	clearMemory((uint8_t*) block, sizeof(memory_blockInfo_t));
}

void clearBlockBoundary(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	clearMemory((uint8_t*) boundary, sizeof(memory_blockInfo_t));
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
	block->crc = crc16_sw_calculate(crcData, sizeof(uint32_t) + sizeof(uint16_t));
}

void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated){
	block->isAllocated = isAllocated;
	block->size = sizeAligned;
	block->alignmentOffset = sizeRequested == 0 ? 0 : sizeAligned - sizeRequested;
	updateCRC(block);
}


void updateBlockBoundary(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	boundary->isAllocated = block->isAllocated;
	boundary->size = block->size;
	boundary->alignmentOffset = block->alignmentOffset;
	boundary->crc = block->crc;
}

bool isBlockValid(memory_blockInfo_t* block){
	return isBlockCRCValid(block);
}

bool isBlockCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)block;
	uint16_t headerCrc = crc16_sw_calculate(crcData, sizeof(uint32_t) + sizeof(uint16_t));
	crcData = (const uint8_t*)boundary;
	uint16_t boundaryCrc = crc16_sw_calculate(crcData, sizeof(uint32_t) + sizeof(uint16_t));
	return (block->crc == headerCrc) && (boundary->crc == boundaryCrc) && (headerCrc == boundaryCrc);
}

bool isBlockHeaderCRCValid(memory_blockInfo_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	uint16_t crc = crc16_sw_calculate(crcData, sizeof(uint32_t) + sizeof(uint16_t));
	return block->crc == crc;
}

bool isBlockBoundaryCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)boundary;
	uint16_t crc = crc16_sw_calculate(crcData, sizeof(uint32_t) + sizeof(uint16_t));
	return block->crc == crc;
}

void clearMemory(uint8_t* ptr, size_t size){
	for(int i = 0; i < size; i++){
		ptr[i] = SHEAPERD_SHEAP_OVERWRITE_VALUE;
	}
}

void updateHeapStatistics(memory_operation_t op, uint32_t allocations, uint32_t sizeAligned, uint32_t size, uint32_t blockSize){
	switch(op){
		case MEMORY_OP_ALLOC:
			gHeap.currentAllocations += allocations;
			gHeap.userDataAllocatedAlligned += sizeAligned;
			gHeap.userDataAllocated += size;
			gHeap.totalBytesAllocated += blockSize;
			break;
		case MEMORY_OP_FREE:
			gHeap.currentAllocations -= allocations;
			gHeap.userDataAllocatedAlligned -= sizeAligned;
			gHeap.userDataAllocated -= size;
			gHeap.totalBytesAllocated -= blockSize;
			break;
	}
}

void initMutex(){
#ifdef SHEAPERD_CMSIS_2
	if(gMemMutex_id != NULL){
		osStatus_t status = osMutexDelete(gMemMutex_id);
		if(status != osOK){
			SHEAPERD_ASSERT("Mutex deletion failed.", gMemMutex_id != NULL, SHEAP_ERROR_MUTEX_CREATION_FAILED);
		}
	}
	gMemMutex_id = osMutexNew(&memMutex_attr);
	SHEAPERD_ASSERT("Mutex creation failed.", gMemMutex_id != NULL, SHEAP_ERROR_MUTEX_CREATION_FAILED);
	if(gMemMutex_id == NULL){
	}
#endif
}

bool acquireMutex(){
#ifdef SHEAPERD_CMSIS_2
	if(gMemMutex_id == NULL){
    	SHEAPERD_ASSERT("MEMORY Information: No mutex available. Consider undefining 'SHEAPERD_CMSIS_2' if no mutex is needed.", false, SHEAP_ERROR_MUTEX_IS_NULL);
    	return false;
	}
	osStatus_t status = osMutexAcquire(gMemMutex_id, SHEAPERD_SHEAP_MUTEX_WAIT_TICKS);
    if (status != osOK)  {
    	SHEAPERD_ASSERT("MEMORY Information: Could not acquire mutex.", false, SHEAP_ERROR_MUTEX_ACQUIRE_FAILED);
    	return false;
    }
#endif
    return true;
}

bool releaseMutex(){
#ifdef SHEAPERD_CMSIS_2
	if (gMemMutex_id == NULL) {
    	SHEAPERD_ASSERT("MEMORY Information: No mutex available. Consider undefining 'SHEAPERD_CMSIS_2' if no mutex is needed.", false, SHEAP_ERROR_MUTEX_IS_NULL);
    	return false;
	}
	osStatus_t status = osMutexRelease(gMemMutex_id);
	if (status != osOK) {
		SHEAPERD_ASSERT("MEMORY Information: Could not release mutex.", false, SHEAP_ERROR_MUTEX_RELEASE_FAILED);
    	return false;
	}
#endif
	return true;
}

void sheap_getHeapStatistic(sheap_heap_t* heap){
	if(heap != NULL){
		heap->currentAllocations = gHeap.currentAllocations;
		heap->heapMax = gHeap.heapMax;
		heap->heapMin = gHeap.heapMin;
		heap->size = gHeap.size;
		heap->totalBytesAllocated = gHeap.totalBytesAllocated;
		heap->userDataAllocated = gHeap.userDataAllocated;
		heap->userDataAllocatedAlligned = gHeap.userDataAllocatedAlligned;
	}
}

size_t sheap_getAllocatedBytesAligned(){
	return gHeap.userDataAllocatedAlligned;
}

size_t sheap_getAllocatedBytes(){
	return gHeap.userDataAllocated;
}

#endif
