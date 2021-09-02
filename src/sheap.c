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
 *  This implementation provides malloc/free routines with added safety measures like:
 *  	+ checking double free
 *  	+ each allocated memory block has a checksum to recognize external altering / invalid access
 *  	+ each allocated memory block is suffixed with a boundary tag which can be checked on each free call to determine if a direct bound overflow happened
 *  	  (When free is called for a block, the header of the block can be checked. If it is not valid, the boundary can be checked. If the header is not valid, but the
 *  	  boundary is, it is an indicator that something went wrong and it could be because of an bound overflow)
 *  	+ TODO: rewrite! when using the provided SHEAP_MALLOC/SHEAP_FREE macros the program counter of each of these calls
 *  	  will be stored to support debugging if an error occurs (number of saved pc's can be configured
 *  	  using the SHEAPERD_DEFAULT_SHEAP_PC_LOG_SIZE define)
 *
 *	TODO: rewrite! Optional: The sheap allocator can be configured to use an extended memory block layout during execution.
 *	The extended layout contains an additional 4 bytes in the header which store the program counter of the calling function. The information from which line of code a memory block has been allocated or freed
 *	can prove useful during debugging. This feature increases the memory overhead from 16 byte to 24 byte.
 *	The pc is inserted after the aligned size/alloc flag and before the alignment offset and it is part of the CRC calculation.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/opt.h"
#include "sheap.h"

// don't build sheap if not enabled via options
#if SHEAPERD_SHEAP

#define PAYLOAD_BLOCK_TO_MEMORY_BLOCK(payload) 	((memory_blockInfo_t*)payload) - 1
#define GET_NEXT_MEMORY_BLOCK(block) 			(memory_blockInfo_t*)(((uint8_t*)block) + 2 * sizeof(memory_blockInfo_t) + block->size)
#define GET_PREV_MEMORY_BLOCK(block)			(memory_blockInfo_t*)(((uint8_t*)block) - 2 * sizeof(memory_blockInfo_t) - GET_SIZE_OF_PREV_BLOCK(block))
#define GET_BOUNDARY_TAG(block)					(memory_blockInfo_t*)(((uint8_t*)block) + sizeof(memory_blockInfo_t) + block->size)
#define GET_BLOCK_OVERHEAD_SIZE(size)			(size_t) (size +  (2 * sizeof(memory_blockInfo_t)))
#define GET_SIZE_OF_PREV_BLOCK(block)			(block - 1)->size

#define REPORT_ERROR_AND_RETURN(assertMsg, assertionType)  	\
do{                                                        	\
	SHEAPERD_ASSERT(assertMsg, false, assertionType);	    \
	return;                                            		\
}while(0)
#define REPORT_ERROR_AND_RETURN_NULL(assertMsg, assertionType)  	\
do{                                                             	\
	SHEAPERD_ASSERT(assertMsg, false, assertionType);	       		\
	return NULL;                                            		\
}while(0)
#define REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType)  	\
do{                                                             	\
	SHEAPERD_ASSERT(assertMsg, false, assertionType);	       		\
	sheap_releaseMutex();                                            		\
}while(0)
#define REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(assertMsg, assertionType)	\
do{                                                               						\
	freeBusy = false;																	\
	REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType);     	         			\
	return;                                                        						\
}while(0)
#define REPORT_ERROR_RELEASE_MUTEX_AND_RETURN_NULL(assertMsg, assertionType)    	\
do{                                                                         		\
	REPORT_ERROR_AND_RELEASE_MUTEX(assertMsg, assertionType);						\
	return NULL;                                                          			\
}while(0)

#define CLEAR_MALLOC_FLAG_AND_RETURN_NULL()	\
do{											\
	allocBusy = false;						\
	return NULL;							\
}while(0)

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	#pragma pack(1)
	typedef struct memory_blockInfo_t{
		uint32_t 			isAllocated : 1;
		uint32_t 			size 		: 31;
		uint32_t			id;
		uint16_t			alignmentOffset;
		uint16_t			crc;
	} memory_blockInfo_t;
	#pragma pack()
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
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

#if SHEAPERD_CMSIS_2 == 1
static osMutexId_t gMemMutex_id;
static const osMutexAttr_t memMutex_attr = {
	  "sheap_mutex",
	  osMutexRecursive,
	  NULL,
	  0U
};
#endif

#if SHEAPERD_CMSIS_1 == 1
osMutexDef(sheap_mutex);
osMutexId gMemMutex_id;
#endif

static memory_blockInfo_t* gStartBlock;
static sheap_heapStat_t gHeap;
static uint32_t gHeaderIds[SHEAP_HEADER_ID_LOG_SIZE];
static int16_t gCurrentIDIndex;

static void sheap_logAccess();
static memory_blockInfo_t* getNextFreeBlockOfSize(size_t size);
static bool isBlockValid(memory_blockInfo_t* block);
static bool isBlockCRCValid(memory_blockInfo_t* block);
static void clearMemory(uint8_t* ptr, size_t size);
static void updateBlockBoundary(memory_blockInfo_t* block);
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
	static void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated, uint32_t id);
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
static uint8_t* allocateBlock(size_t size, uint32_t id, bool initializePayload);
static void* sheap_alloc_impl(size_t size, uint32_t id, bool initializeData);

static void sheap_initMutex();
static bool sheap_acquireMutex();
static bool sheap_releaseMutex();

static bool allocBusy;
static bool freeBusy;

void sheap_init(uint32_t* heapStart, size_t size){
	if(size == 0){
		SHEAPERD_ASSERT("Sheap init failed due to invalid size.", size > 0, SHEAP_INIT_INVALID_SIZE);
		return;
	}
	for(int i = 0; i < SHEAP_HEADER_ID_LOG_SIZE; i++){
		gHeaderIds[i] = 0;
	}
	gCurrentIDIndex = -1;

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
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), 0, false, SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_ID);
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
	updateBlockHeader(gStartBlock, gHeap.size - 2 * sizeof(memory_blockInfo_t), 0, false);
#endif
	updateBlockBoundary(gStartBlock);
	sheap_initMutex();
}

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
sheap_status_t sheap_getAllocationID(void* ptr, uint32_t* id) {
	if(!sheap_acquireMutex()){
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
	*id = current->id;
	sheap_releaseMutex();
	return SHEAP_OK;
}
#endif

void sheap_logAccess(uint32_t id){
	gHeaderIds[(++gCurrentIDIndex) % SHEAP_HEADER_ID_LOG_SIZE] = (uint32_t) id;
}

size_t sheap_getHeapSize(){
	return gHeap.size;
}

size_t sheap_align(size_t n) {
	return (n + SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE - 1) & ~(SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE - 1);
}

memory_blockInfo_t* getNextFreeBlockOfSize(size_t size){
#if SHEAPERD_SHEAP_MEMORY_ALLOCATION_STRATEGY == SHEAPERD_SHEAP_MEMORY_ALLOCATION_FIRST_FIT
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

void* sheap_malloc(size_t size, uint32_t id) {
    return sheap_alloc_impl(size, id, false);
}

void* sheap_calloc(size_t num, size_t size, uint32_t id) {
    return sheap_alloc_impl(num * size, id, true);
}

void* sheap_alloc_impl(size_t size, uint32_t id, bool initializeData) {
#if SHEAPERD_NO_OS == 1
    if(allocBusy) {
        SHEAPERD_ASSERT("Overlapping call to allocation functions 'sheap_malloc/sheap_alloc' detected. Returning without allocation.", allocBusy == false, SHEAP_MALLOC_CALL_OVERLAP);
        return NULL;
    } else {
        allocBusy = true;
    }
#endif
    if(gHeap.heapMin == NULL){
        SHEAPERD_ASSERT("\"SHEAP_MALLOC\" must not be used before the initialization (\"sheap_init\").", gHeap.heapMin != NULL, SHEAP_NOT_INITIALIZED);
        CLEAR_MALLOC_FLAG_AND_RETURN_NULL();
    }
    if(!sheap_acquireMutex()){
        return NULL;
    }
    if(id != 0){
        sheap_logAccess(id);
    }
    if(size == 0){
        SHEAPERD_ASSERT("Cannot allocate size of 0. Is this call intentional?", size > 0, SHEAP_SIZE_ZERO_ALLOC);
        CLEAR_MALLOC_FLAG_AND_RETURN_NULL();
    }
    uint8_t* allocated = allocateBlock(size, id, initializeData);
    // allocated may be NULL here
#if SHEAPERD_NO_OS == 1
    allocBusy = false;
#endif
    sheap_releaseMutex();
    return allocated;
}

uint8_t* allocateBlock(size_t size, uint32_t id, bool initializePayload) {
    size_t sizeAligned = sheap_align(size);
    memory_blockInfo_t* allocate = getNextFreeBlockOfSize(sizeAligned);
    if(allocate == NULL) {
        return NULL;
    }
    uint32_t preAllocSize = allocate->size;
    if (preAllocSize < GET_BLOCK_OVERHEAD_SIZE(sizeAligned)
            + (SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE + (2 * sizeof(memory_blockInfo_t)))) {
        // No additional block of minimum size can be created after this block, therefore take all available memory to obtain heap structure
        sizeAligned = preAllocSize;
    }

    allocate->isAllocated = true;
    allocate->size = sizeAligned;
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
    allocate->id = id;
#endif
    allocate->alignmentOffset = sizeAligned - size;
    updateHeapStatistics(MEMORY_OP_ALLOC, 1, sizeAligned, size,
                         GET_BLOCK_OVERHEAD_SIZE(sizeAligned));

    updateCRC(allocate);
    updateBlockBoundary(allocate);

    uint8_t* payload = (uint8_t*) (allocate + 1);

    if (allocate->size < preAllocSize) {
        memory_blockInfo_t *remainingBlock = GET_NEXT_MEMORY_BLOCK(allocate);
#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
        updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(sizeAligned),
                          0, false, SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_ID);
#elif SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 0
        updateBlockHeader(remainingBlock, preAllocSize - GET_BLOCK_OVERHEAD_SIZE(sizeAligned), 0, false);
#endif
        updateBlockBoundary(remainingBlock);
    }

    if(initializePayload) {
        uint8_t* p = payload;
        while(p < (uint8_t*)GET_BOUNDARY_TAG(allocate)) {
            *p++ = SHEAPERD_SHEAP_CALLOC_VALUE;
        }
    }
    return payload;
}

void sheap_free(void* ptr, uint32_t id){
#if SHEAPERD_NO_OS == 1
	if(freeBusy) {
		SHEAPERD_ASSERT("Overlapping call to 'sheap_free' detected. Returning without freeing memory.", freeBusy == false, SHEAP_FREE_CALL_OVERLAP);
		return;
	} else {
		freeBusy = true;
	}
#endif
	if(!sheap_acquireMutex()){
		return;
	}
	if(id != 0){
		sheap_logAccess(id);
	}
	if(ptr == NULL){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"MEMORY ERROR: Free operation not valid for null pointer", SHEAP_ERROR_NULL_FREE);
	}
	if(ptr < (void*)gHeap.heapMin || ptr > (void*)gHeap.heapMax){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"Cannot free pointer outside of heap.", SHEAP_ERROR_FREE_PTR_NOT_IN_HEAP);
	}
	memory_blockInfo_t* current = PAYLOAD_BLOCK_TO_MEMORY_BLOCK(ptr);
	if(current == NULL){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"Cannot free the provided pointer", SHEAP_ERROR_FREE_INVALID_HEADER);
	}
	if(!isBlockHeaderCRCValid(current)){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"MEMORY ERROR: Free operation can not be performed as block header is not valid",
				SHEAP_ERROR_FREE_INVALID_HEADER);
	}else if(!isBlockBoundaryCRCValid(current)){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"MEMORY ERROR: Free operation can not be performed as block boundary is not valid. It may have been altered. Calling the error callback",
				SHEAP_ERROR_FREE_INVALID_BOUNDARY);
	}

#ifdef SHEAPERD_SHEAP_FREE_CHECK_UNALIGNED_SIZE
	bool illegalWrite = checkForIllegalWrite(current);
	if(illegalWrite){
		REPORT_ERROR_RELEASE_MUTEX_CLEAR_FREE_FLAG_AND_RETURN(
				"MEMORY ERROR: Out of bound write detected. Free operation aborted", SHEAP_ERROR_OUT_OF_BOUND_WRITE);
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
		current->id = id;
#endif
		updateCRC(current);
		updateBlockBoundary(current);
	}else{
		SHEAPERD_ASSERT("MEMORY ERROR: Double free detected.", false, SHEAP_ERROR_DOUBLE_FREE);
	}
	sheap_releaseMutex();
#if SHEAPERD_NO_OS == 1
	freeBusy = false;
#endif
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

uint32_t sheap_getLatestAllocationIDs(uint32_t destination[], uint32_t n){
	uint32_t index = gCurrentIDIndex;
	uint32_t count = 0;
	while (gHeaderIds[index % SHEAP_HEADER_ID_LOG_SIZE] != 0 && count < n && count < SHEAP_HEADER_ID_LOG_SIZE) {
		destination[count++] = gHeaderIds[(index--) % SHEAP_HEADER_ID_LOG_SIZE];
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
	return ((uint8_t*) next) < (gHeap.heapMax - GET_BLOCK_OVERHEAD_SIZE(SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE)) && !next->isAllocated;
}

void updateCRC(memory_blockInfo_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	block->crc = util_crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
}

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
void updateBlockHeader(memory_blockInfo_t* block, size_t sizeAligned, size_t sizeRequested, bool isAllocated, uint32_t id){
	block->isAllocated = isAllocated;
	block->size = sizeAligned;
	block->id = id;
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
	boundary->id = block->id;
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
	uint16_t headerCrc = util_crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	crcData = (const uint8_t*)boundary;
	uint16_t boundaryCrc = util_crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	return (block->crc == headerCrc) && (boundary->crc == boundaryCrc) && (headerCrc == boundaryCrc);
}

bool isBlockHeaderCRCValid(memory_blockInfo_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	uint16_t crc = util_crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
	return block->crc == crc;
}

bool isBlockBoundaryCRCValid(memory_blockInfo_t* block){
	memory_blockInfo_t* boundary = GET_BOUNDARY_TAG(block);
	const uint8_t* crcData = (const uint8_t*)boundary;
	uint16_t crc = util_crc16_sw_calculate(crcData, sizeof(memory_blockInfo_t) - sizeof(uint16_t));
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

void sheap_initMutex(){
#if SHEAPERD_NO_OS == 1
	util_error_t error = ERROR_NO_ERROR;
#endif
#if SHEAPERD_CMSIS_1 == 1
	util_error_t error = util_initMutex(osMutex(sheap_mutex), &gMemMutex_id);
#endif
#if SHEAPERD_CMSIS_2 == 1
	util_error_t error = util_initMutex(&gMemMutex_id, &memMutex_attr);
#endif
	if(error == ERROR_MUTEX_DELETION_FAILED){
		SHEAPERD_ASSERT("Mutex deletion failed.", false, SHEAPERD_ERROR_MUTEX_DELETION_FAILED);
	} else if (error == ERROR_MUTEX_CREATION_FAILED){
		SHEAPERD_ASSERT("Mutex creation failed.", false, SHEAPERD_ERROR_MUTEX_CREATION_FAILED);
	}
}

bool sheap_acquireMutex(){
	#if SHEAPERD_NO_OS == 1
		return true;
	#else
	util_error_t error = util_acquireMutex(gMemMutex_id, SHEAPERD_DEFAULT_MUTEX_WAIT_TICKS);
	switch (error){
		case ERROR_MUTEX_IS_NULL:
			SHEAPERD_ASSERT("No mutex available. Consider undefining 'SHEAPERD_CMSIS_2' if no mutex is needed.", false, SHEAPERD_ERROR_MUTEX_IS_NULL);
			return false;
		case ERROR_MUTEX_ACQUIRE_FAILED:
			SHEAPERD_ASSERT("Could not acquire mutex.", false, SHEAPERD_ERROR_MUTEX_ACQUIRE_FAILED);
			return false;
		default:
			return true;
	}
	#endif
}

bool sheap_releaseMutex(){
	#if SHEAPERD_NO_OS == 1
		return true;
	#else
	util_error_t error = util_releaseMutex(gMemMutex_id);
	switch(error){
		case ERROR_MUTEX_IS_NULL:
			SHEAPERD_ASSERT("No mutex available. Consider removing the 'SHEAPERD_CMSIS_2' define if no mutex is needed.", false, SHEAPERD_ERROR_MUTEX_IS_NULL);
			return false;
		case ERROR_MUTEX_RELEASE_FAILED:
			SHEAPERD_ASSERT("Could not release mutex.", false, SHEAPERD_ERROR_MUTEX_RELEASE_FAILED);
			return false;
		default:
			return true;
	}
#endif
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
