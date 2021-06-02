/** @file sheap.c
 *  @brief Provides a secure heap (sheap) implementations.
 *
 *	//TODO: Add PC from malloc and free to header/boundary optional
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
 *  This implementation provides malloc/free routines with added safety measures like:
 *  	+ checking double free
 *  	+ each allocated memory block has a checksum to recognize external altering / invalid access
 *  	+ each allocated memory block is suffixed with a boundary tag which can be checked on each free call to determine if a direct bound overflow happened
 *  	  (When free is called for a block, the header of the block can be checked. If it is not valid, the boundary can be checked. If the header is not valid, but the
 *  	  boundary is, it is an indicator that something went wrong and it could be because of an bound overflow)
 *  	+ when using the provided SHEAP_MALLOC/SHEAP_FREE macros the program counter of each of these calls
 *  	  will be stored to support debugging if an error occurs (number of saved pc's can be configured
 *  	  using the SHEAPERD_DEFAULT_SHEAP_PC_LOG_SIZE define)
 *
 *	Optional: The sheap allocator can be configured to use an extended memory block layout during execution.
 *	The extended layout contains an additional 4 bytes in the header which store the program counter of the calling function. The information from which line of code a memory block has been allocated or freed
 *	can prove useful during debugging. This feature increases the memory overhead from 16 byte to 24 byte.
 *	The pc is inserted after the aligned size/alloc flag and before the alignment offset and it is part of the CRC calculation.
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

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	#pragma pack(1)
	typedef struct memory_blockInfo_t{
		uint32_t 			isAllocated : 1;
		uint32_t 			size 		: 31;
		uint32_t			pc;
		uint16_t			alignmentOffset;
		uint16_t			crc;
	} memory_blockInfo_t;
	#pragma pack()
#elif
#pragma pack(1)
	typedef struct memory_blockInfo_t{
		uint32_t 			isAllocated : 1;
		uint32_t 			size 		: 31;
		uint16_t			alignmentOffset;
		uint16_t			crc;
	} memory_blockInfo_t;
	#pragma pack()
#endif

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
static sheap_heapStat_t gHeap;
static uint32_t gProgramCounters[SHEAPERD_SHEAP_PC_LOG_SIZE];
static uint32_t gCurrentPCIndex;

static void sheap_logAccess();
static memory_blockInfo_t* getNextFreeBlockOfSize(size_t size);
static bool isBlockValid(memory_blockInfo_t* block);
static bool isBlockCRCValid(memory_blockInfo_t* block);
static void clearMemory(uint8_t* ptr, size_t size);
static void updateBlockBoundary(memory_blockInfo_t* block);
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	static void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated, uint32_t pc);
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
	static void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated);
#endif
static void updateCRC(memory_blockInfo_t* block);
static bool isPreviousBlockFree(memory_blockInfo_t* block);
static bool isNextBlockFree(memory_blockInfo_t* block);
static memory_blockInfo_t* coalesce(memory_blockInfo_t** block);
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
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), 0, false, SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_PC);
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), 0, false);
#endif
	updateBlockBoundary(gStartBlock);
	initMutex();
}

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
sheap_status_t sheap_getAllocationPC(void* ptr, uint32_t* pc) {
	if(!acquireMutex()){
		return SHEAP_ERROR;
	}
	if(ptr == NULL){
		return SHEAP_INVALID_POINTER;
	}
	if(ptr < (void*)gHeap.heapMin || ptr > (void*)gHeap.heapMax){
		return SHEAP_INVALID_POINTER;
	}
	memory_blockInfo_t* current = PAYLOAD_BLOCK_TO_MEMORY_BLOCK(ptr);
	if(current == NULL){
		return SHEAP_INVALID_POINTER;
	}
	if(!isBlockHeaderCRCValid(current)){
		return SHEAP_INVALID_POINTER;
	}else if(!isBlockBoundaryCRCValid(current)){
		return SHEAP_INVALID_POINTER;
	}
	*pc = current->pc;
	releaseMutex();
	return SHEAP_OK;
}
#endif

void sheap_logAccess(uint32_t pc){
	gProgramCounters[(++gCurrentPCIndex) % SHEAPERD_SHEAP_PC_LOG_SIZE] = (uint32_t)pc;
}

size_t sheap_getHeapSize(){
	return gHeap.size;
}

size_t sheap_align(size_t n) {
	return (n + SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE - 1) & ~(SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE - 1);
}

memory_blockInfo_t* getNextFreeBlockOfSize(size_t size){
#if SHEAPERD_SHEAP_MEMORY_ALLOCATION_STRATEGY == SHEAPERD_SHEAP_MEMORY_ALLOCATION
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
#else
	SHEAPERD_ASSERT("MEMORY ERROR: No memory allocation strategy found", false, SHEAP_CONFIG_ERROR_INVALID_ALLOCATION_STRATEGY);
	return NULL;
#endif
}

void* sheap_malloc(size_t size, uint32_t pc){
	if(gHeap.heapMin == NULL){
		SHEAPERD_ASSERT("\"SHEAP_MALLOC\" must not be used before the initialization (\"sheap_init\").", gHeap.heapMin != NULL, SHEAP_NOT_INITIALIZED);
		return NULL;
	}
	if(!acquireMutex()){
		return NULL;
	}
	if(pc != 0){
		sheap_logAccess(pc);
	}
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
	if(preAllocSize < GET_BLOCK_OVERHEAD_SIZE(sizeAligned) + (SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE + (2 * sizeof(memory_blockInfo_t)))){
		// No additional block of minimum size can be created after this block, therefore take all available memory to obtain heap structure
		sizeAligned = preAllocSize;
	}

	allocate->isAllocated = true;
	allocate->size = sizeAligned;
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	allocate->pc = pc;
#endif
	allocate->alignmentOffset = sizeAligned - size;
	updateHeapStatistics(MEMORY_OP_ALLOC, 1, sizeAligned, size, GET_BLOCK_OVERHEAD_SIZE(sizeAligned));

	updateCRC(allocate);
	updateBlockBoundary(allocate);

	uint8_t* payload = (uint8_t*)(allocate + 1);

	if(allocate->size < preAllocSize){
		memory_blockInfo_t* remainingBlock = GET_NEXT_MEMORY_BLOCK(allocate);
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
		updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(sizeAligned), 0, false, SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_PC);
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
		updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(sizeAligned), 0, false);
#endif
		updateBlockBoundary(remainingBlock);
	}
	releaseMutex();
    return payload;
}

void sheap_free(void* ptr, uint32_t pc){
	if(!acquireMutex()){
		return;
	}
	if(pc != 0){
		sheap_logAccess(pc);
	}
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
				SHEAP_ERROR_FREE_INVALID_BOUNDARY);
	}

#ifdef SHEAPERD_SHEAP_FREE_CHECK_UNALIGNED_SIZE
	bool illegalWrite = checkForIllegalWrite(current);
	if(illegalWrite){
		REPORT_ERROR_RELEASE_MUTEX_AND_RETURN("MEMORY ERROR: Out of bound write detected. Free operation aborted", SHEAP_ERROR_OUT_OF_BOUND_WRITE);
	}
#endif

	if(current->isAllocated) {
		current->isAllocated = false;
		updateHeapStatistics(MEMORY_OP_FREE, 1, current->size, current->size - current->alignmentOffset, GET_BLOCK_OVERHEAD_SIZE(current->size));

#ifdef SHEAPERD_SHEAP_OVERWRITE_ON_FREE
		clearMemory((uint8_t*)ptr, current->size);
#endif
		current = coalesce(&current);
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
		current->pc = pc;
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
		current->pc = SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_PC;
#endif
		updateCRC(current);
		updateBlockBoundary(current);
	}else{
		SHEAPERD_ASSERT("MEMORY ERROR: Double free detected.", false, SHEAP_ERROR_DOUBLE_FREE);
	}
	releaseMutex();
}

memory_blockInfo_t* coalesce(memory_blockInfo_t** block){
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
	return *block;
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
	block->crc = crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
}

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated, uint32_t pc){
	block->isAllocated = isAllocated;
	block->size = sizeAligned;
	block->pc = pc;
	block->alignmentOffset = sizeRequested == 0 ? 0 : sizeAligned - sizeRequested;
	updateCRC(block);
}
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated){
	block->isAllocated = isAllocated;
	block->size = sizeAligned;
	block->alignmentOffset = sizeRequested == 0 ? 0 : sizeAligned - sizeRequested;
	updateCRC(block);
}
#endif


void updateBlockBoundary(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	boundary->isAllocated = block->isAllocated;
	boundary->size = block->size;
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	boundary->pc = block->pc;
#endif
	boundary->alignmentOffset = block->alignmentOffset;
	boundary->crc = block->crc;
}

bool isBlockValid(memory_blockInfo_t* block){
	return isBlockCRCValid(block);
}

bool isBlockCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)block;
	uint16_t headerCrc = crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	crcData = (const uint8_t*)boundary;
	uint16_t boundaryCrc = crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	return (block->crc == headerCrc) && (boundary->crc == boundaryCrc) && (headerCrc == boundaryCrc);
}

bool isBlockHeaderCRCValid(memory_blockInfo_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	uint16_t crc = crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	return block->crc == crc;
}

bool isBlockBoundaryCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)boundary;
	uint16_t crc = crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
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
    	SHEAPERD_ASSERT("MEMORY Information: No mutex available. Consider removing the 'SHEAPERD_CMSIS_2' define if no mutex is needed.", false, SHEAP_ERROR_MUTEX_IS_NULL);
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

void sheap_getHeapStatistic(sheap_heapStat_t* heap){
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
