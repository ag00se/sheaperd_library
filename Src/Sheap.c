/*
 * Sheap.c
 *
 *  Created on: 02.03.2021
 *      Author: JK
 */


#include "Sheap.h"

static uint32_t gPC;

static void sheap_coalesceBlocks();

void sheap_init(uint8_t* heapStart, size_t size){

}

void* sheap_malloc_impl(uint32_t* stack){
	register int* size asm ("r0");
	register int* pc asm ("r1");
	size_t s = (size_t)size;
	gPC = (uint32_t)pc;


	return 0x12345678;
}

__attribute__(( naked )) void sheap_free(void* pData);
void sheap_free(void* pData){

}
