/** @file sheap.h
 *  @brief Provides the api for the secure heap (sheap) implementation.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include <sheaperd.h>

typedef enum {
	SHEAP_OK,
	SHEAP_INVALID_POINTER,
	SHEAP_ERROR
} sheap_status_t;

typedef struct{
	uint8_t* 	heapMin;
	uint8_t* 	heapMax;
	uint32_t 	currentAllocations;
	uint32_t 	totalBytesAllocated;
	uint32_t	userDataAllocatedAlligned;
	uint32_t	userDataAllocated;
	size_t 		size;
} sheap_heapStat_t;

/**
 * The following function are implemented in assembler. See the ../asm folder
 */
void* sheap_malloc_lr(size_t size);
void* sheap_calloc_lr(size_t num, size_t size);
void* sheap_free_lr(void* p);


/**
 * Allocates memory of the requested size and provides a pointer to the memory
 * to the caller. For best error detection support one should use the provided
 * SHEAP_MALLOC(size, pVoid) macro.
 * If this function is called directly the parameter id can be set to 0. In
 * general it is intended to record some form of identification of the caller to
 * obtain additional information.
 *
 * @param size the size of memory to be allocated
 * @param id the value to identify the origin of the calling context
 */
void* sheap_malloc(size_t size, uint32_t id);
void* sheap_calloc(size_t num, size_t size, uint32_t id);
/**
 * Deallocates memory associated with the provided pointer.
 * For best error detection support one should use the provided
 * SHEAP_FREE(pVoid) macro.
 * If this function is called directly the parameter id can be set to 0. In
 * general it is intended to record some form of identification of the caller to
 * obtain additional information.
 *
 * @param ptr the pointer associated with the memory to be freed
 * @param id the value to identify the origin of the calling context
 */
void sheap_free(void* ptr, uint32_t id);
size_t sheap_getHeapSize();
size_t sheap_getAllocatedBytesAligned();
size_t sheap_getAllocatedBytes();
void sheap_getHeapStatistic(sheap_heapStat_t* heapStat);

#if SHEAPERD_SHEAP_USE_EXTENDED_HEADER == 1
sheap_status_t sheap_getAllocationID(void* allocatedPtr, uint32_t* id);
#endif

/**
 * Initializes the sheap allocator
 * ATTENTION: this function must be called before the scheduler is started
 */
void sheap_init(uint32_t* heapStart, size_t size);

/**
 * Align the the size to a multiple of a predefined size
 *
 * The define 'SHEAP_MINIMUM_MALLOC_SIZE' can be used to specify the size to align to.
 * This function align the provided size to 'SHEAP_MINIMUM_MALLOC_SIZE'
 *
 * Examples: n = 7 --> return 8; n = 11 --> return 12
 *
 * @param n		The size to be aligned
 *
 * @return		n aligned to 'SHEAP_MINIMUM_MALLOC_SIZE'
 */
size_t sheap_align(size_t n);

/**
 * Gets the latest program counter addresses that used SHEAP_MALLOC() or SHEAP_FREE()
 *
 * Fills the provided destination array with the last recorded program counter
 * addresses that used SHEAP_MALLOC() or SHEAP_FREE()
 *
 * @param destination 	Filled with the id
 * @param n			  	Size of the provided destination array
 *
 * @return			 	The numbers of ids written to the destination array
 */
uint32_t sheap_getLatestAllocationIDs(uint32_t destination[], uint32_t n);

#endif /* INC_SHEAP_H_ */
