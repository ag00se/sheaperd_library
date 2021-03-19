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

#define PAYLOAD_BLOCK_TO_MEMORY_BLOCK(payload) (memory_block_t*)(((uint8_t*)payload)-sizeof(memory_block_t))
#define MEMORY_BLOCK_TO_MEMORY_TAG(block) (uint32_t*)(((uint32_t*)block) + BYTE_TO_WORD(sizeof(memory_block_t)) + (BYTE_TO_WORD(block->size)))
#define GET_R1(r1_val)  asm ("mov %0, r1" : "=r" (r1_val))

typedef struct{
	uint8_t* 	heapMin;
	uint8_t* 	heapMax;
	uint32_t 	currentAllocations;
	uint32_t 	totalBytesAllocated;
	uint32_t	userDataAllocated;
	size_t 		size;
} heap_t;


#pragma pack(1)
typedef struct memory_block_t memory_block_t;
typedef struct memory_block_t{
	//Allocations will only be performed in 4 byte steps. Therefore the lowest bit can be used as a flag
	uint32_t 			isAllocated : 1;
	uint32_t 			size 		: 31;
	memory_block_t* 	next;
	uint32_t			crc;
	//payload added here - no datatype for convenient pointer arithmetics
} memory_block_t ;
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

static memory_block_t* gStartBlock;
static heap_t gHeap;
static uint32_t gProgramCounters[SHEAPERD_SHEAP_PC_LOG_SIZE];
static uint32_t gCurrentPCIndex;

static void sheap_logAccess();
static size_t align(size_t n);
static memory_block_t* getNextFreeBlockOfSize(size_t size);
static bool isBlockValid(memory_block_t* block);
static bool isBlockCRCValid(memory_block_t* block);
static bool isBlockMemoryTagValid(memory_block_t* block);
static void clearMemoryTag(memory_block_t* block);


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
	for(int i = 0; i < gHeap.size; i++){
		gHeap.heapMin[i] = 0;
	}
	gStartBlock = (memory_block_t*) gHeap.heapMin;
	gStartBlock->size = size;
	gStartBlock->isAllocated = false;
	gStartBlock->next = NULL;
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

//Aligns the requested size to a multiple of 4 byte
size_t align(size_t n) {
	return (n + SHEAP_MINIMUM_MALLOC_SIZE - 1) & ~(SHEAP_MINIMUM_MALLOC_SIZE - 1);
}

memory_block_t* getNextFreeBlockOfSize(size_t size){
	size_t overheadSize = size + sizeof(memory_block_t);
	if(SHEAPERD_SHEAP_USE_MEM_TAGGING){
		overheadSize += SHEAPERD_SHEAP_MEM_TAG_SIZE_BYTES;
	}
	memory_block_t* current = gStartBlock;
	while(current->isAllocated == true){
		current = current->next;
	}
	if(current == NULL){
		//no more memory left
		return NULL;
	}
	uint8_t* test = (((uint8_t*)current) + overheadSize);
	if(test > gHeap.heapMax){
		//no more memory left
		return NULL;
	}
	return current;
}

//static void sheap_coalesceBlocks();

void* malloc(size_t size){
	if(size == 0){
		SHEAPERD_ASSERT("Cannot allocate size of 0. Is this call intentional?", size > 0);
		return NULL;
	}
	size = align(size);
	memory_block_t* freeBlock = getNextFreeBlockOfSize(size);

	freeBlock->isAllocated = true;
	freeBlock->size = size;
	gHeap.currentAllocations += 1;
	//TODO: make statistics - own funciton
	//	gHeap.totalBytesAllocated += ;
	gHeap.userDataAllocated += size;

	//pointer arithmetic used to jump over the
	uint8_t* payload = (uint8_t*)(freeBlock + 1);
	if(SHEAPERD_SHEAP_USE_MEM_TAGGING){
		uint32_t* pMemTag = (uint32_t*)(payload + size);
		*pMemTag = SHEAPERD_SHEAP_MEM_TAG_VALUE;
		freeBlock->next = (memory_block_t*)(payload + size + SHEAPERD_SHEAP_MEM_TAG_SIZE_BYTES);
	}else{
		freeBlock->next = (memory_block_t*)(payload + size);
	}

	//TODO: check if enough memory available
	memory_block_t* next = freeBlock->next;
	next->isAllocated = false;
	next->next = NULL;
	next->crc = 0;
	next->size = 0;

	const uint8_t* crcData = (const uint8_t*)freeBlock;
	uint32_t crc = crc32_sw_calculate(crcData, 8);
	freeBlock->crc = crc;
    return payload;
}

void free(void* ptr){
	memory_block_t* current = PAYLOAD_BLOCK_TO_MEMORY_BLOCK(ptr);
	if(current == NULL){
		//Cannot free pointer - error
		SHEAPERD_ASSERT("Cannot free the provided pointer", current != NULL);
		return;
	}
	bool blockValid = isBlockValid(current);
	if(!blockValid){
		SHEAPERD_ASSERT("MEMORY ERROR: Free operation can not be performed as block has been altered", blockValid);
		//Maybe hardfault? Error callback? Configrable!!!!!
		return;
	}

	if(current->isAllocated){
		current->isAllocated = false;

		gHeap.currentAllocations -= 1;
		gHeap.userDataAllocated -= current->size;
		//TODO: stats
//		gHeap.totalBytesAllocated -=
		//Maybe consolidate
	}
}

bool isBlockValid(memory_block_t* block){
	if(!isBlockCRCValid(block)){
		return false;
	}
	if(!isBlockMemoryTagValid(block)){
		return false;
	}
	return true;
}

bool isBlockCRCValid(memory_block_t* block){
	const uint8_t* crcData = (const uint8_t*)block;
	return block->crc == crc32_sw_calculate(crcData, 8);
}

bool isBlockMemoryTagValid(memory_block_t* block){
	return *(MEMORY_BLOCK_TO_MEMORY_TAG(block)) == SHEAPERD_SHEAP_MEM_TAG_VALUE;
}

void clearMemoryTag(memory_block_t* block){
	*(MEMORY_BLOCK_TO_MEMORY_TAG(block)) = 0;
}
