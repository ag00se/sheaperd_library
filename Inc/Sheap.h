/*
 * Sheap.h
 *
 *  Created on: 02.03.2021
 *      Author: JK
 */

/** @file */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include <stddef.h>
#include <inttypes.h>


/** \def SHEAP_MALLOC(size, pVoid)
*    \brief Allocates the size \size on the heap and returns the pointer in pVoid \pVoid.
*
*    Size \size specifies the amount of data to allocate and pVoid \pVoid is a void* to assign
*    the result to.
*    This macro records the pc of the caller for debugging purpose.
*/
#define SHEAP_MALLOC(size, pVoid)														\
do {																					\
	__asm volatile(																		\
			"mov r1, pc				\n" /* Store the pc in r1					*/		\
			"bl sheap_malloc_impl"		/* Branch to malloc implementation		*/		\
	);																					\
	register int* pAlloc asm ("r0");													\
	pVoid = pAlloc;																		\
} while(0)


void* sheap_malloc_impl(uint32_t* stack);

void sheap_init(uint8_t* heapStart, size_t size);
__attribute__(( naked )) void sheap_free(void* pData);

#endif /* INC_SHEAP_H_ */
