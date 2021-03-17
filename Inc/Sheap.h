/** @file sheap.h
 *  @brief Provides the default options for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include "sheaperd.h"

#define SHEAP_MINIMUM_MALLOC_SIZE 		4

#ifndef SHEAPERD_SHEAP_USE_MEM_TAGGING
#define SHEAPERD_SHEAP_USE_MEM_TAGGING 		0
#else
#define SHEAPERD_SHEAP_MEM_TAG_SIZE_BYTES	4
#define SHEAPERD_SHEAP_MEM_TAG_VALUE		0xDEADBEEF
#endif

/** \def SHEAP_MALLOC(size, pVoid)
*    \brief Allocates the size \size on the heap and returns the pointer in pVoid \pVoid.
*
*    Size \size specifies the amount of data to allocate and pVoid \pVoid is a void* to assign
*    the result to.
*    This macro records the pc of the caller for debugging purpose.
*/
#define SHEAP_MALLOC(size, pVoid)																\
do {																							\
	if(ASSERT_TYPE(size_t, size)){																\
		size_t s = size;																		\
		__asm volatile("mov r0, %0\n\t" : "=r" (s));	/* Store the size in r0				*/	\
		__asm volatile(																			\
				"mov r1, pc				\n" 			/* Store the pc in r1				*/	\
				"bl sheap_malloc_impl"					/* Branch to malloc implementation	*/	\
		);																						\
		register int* pAlloc asm ("r0");														\
		pVoid = pAlloc;																			\
	}																							\
} while(0)


#define SHEAP_FREE(pVoid)																			\
do {																								\
	__asm volatile("mov r0, %0\n\t" : "=r" (pVoid));	/* Store the pointer to free in r0		*/	\
	__asm volatile(																					\
				"mov r1, pc				\n" 			/* Store the pc in r1					*/	\
				"bl sheap_free_impl"					/* Branch to malloc implementation		*/	\
		);																							\
} while(0)

void sheap_init(uint32_t* heapStart, size_t size);
void* sheap_malloc_impl();
void sheap_free_impl();
void* malloc(size_t size);
void free(void* ptr);

#endif /* INC_SHEAP_H_ */
