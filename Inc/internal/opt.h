/** @file opt.h
 *  @brief Provides the default options for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INTERNAL_OPT_H_
#define INTERNAL_OPT_H_

//included first so any user settings take precedence
#include "sheaperdopts.h"

#ifndef SHEAPERD_CMSIS_2
#define SHEAPERD_CMSIS_2
	#include "cmsis_os2.h"
#endif

#ifndef SHEAPERD_SHEAP
	#define SHEAPERD_SHEAP 1
#endif

#ifndef SHEAPERD_STACK_GUARD
	#define SHEAPERD_STACK_GUARD 	0
	#define SHEAPERD_MPU_M0PLUS		0
	#define SHEAPERD_MPU_M3_M4_M7	0
	#define SHEAPERD_MPU_M23		0
	#define	SHEAPERD_MPU_M33_M35P	0
	#define SHEAPERD_MPU_REGIONS	0
#endif

#if SHEAPERD_MPU_M23 || SHEAPERD_MPU_M33_M35P
	#define SHEAPERD_ARMV8			1
#endif

#if SHEAPERD_MPU_M3_M4_M7
	#define SHEAPERD_ARMV7			1
#endif

#if SHEAPERD_MPU_M0PLUS
	#define SHEAPERD_ARMV6			1
#endif

#ifndef SHEAPERD_DISABLE_CORTEXM3_M4_WRITE_BUFFERING
	#define SHEAPERD_DISABLE_CORTEXM3_M4_WRITE_BUFFERING 	1
#endif

#ifndef SHEAP_MINIMUM_MALLOC_SIZE
	#define SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE 		4
#endif
#if SHEAP_MINIMUM_MALLOC_SIZE < 4
	#define SHEAPERD_SHEAP_MINIMUM_MALLOC_SIZE 		4
#endif

#ifndef SHEAPERD_USE_SNPRINTF_ASSERT
	#define SHEAPERD_USE_SNPRINTF_ASSERT	1
	#define SHEAPERD_ASSERT_BUFFER_SIZE 	256
#endif

#if SHEAPERD_STACK_GUARD
	/**
	 * For an explanation of the MPU registers see some documentation like:
	 * 	+ ARMv7-M Architecture Reference Manual: https://developer.arm.com/documentation/ddi0403/latest/
	 * 	+ https://www.st.com/resource/en/application_note/dm00272912-managing-memory-protection-unit-in-stm32-mcus-stmicroelectronics.pdf
	 * 	+ http://ww1.microchip.com/downloads/en/AppNotes/Atmel-42128-AT02346-Using-the-MPU-on-Atmel-Cortex-M3-M4-based-Microcontroller_Application-Note.pdf
	 */
	#ifndef STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB
		#define STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB 0b000110
	#endif
	#ifndef STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_ENABLED
		#define STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_ENABLED 0b000
	#endif
	#ifndef STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_DISABLED
		#define STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_DISABLED 0b011
	#endif
#endif

#define ASSERT_TYPE(TYPE, VALUE) ((TYPE){ 0 } = (VALUE))
#define SHEAPERD_ASSERT(msg, assert, assertionType) 	\
do { 													\
	if (!(assert)) { 									\
		SHEAPERD_PORT_ASSERT(msg, assertionType);		\
	}													\
} while(0)


#ifdef SHEAPERD_INERCEPT_SBRK
void* _sbrk(ptrdiff_t incr);
#endif

#ifndef SHEAPERD_SHEAP_PC_LOG_SIZE
	#define SHEAPERD_SHEAP_PC_LOG_SIZE 			20
#endif

#if SHEAPERD_SHEAP_PC_LOG_SIZE <= 0
	#define SHEAPERD_SHEAP_PC_LOG_SIZE 			20
#endif

#define SHEAPERD_SHEAP_FREE_CHECK_UNALIGNED_SIZE	1
#ifdef SHEAPERD_SHEAP_FREE_CHECK_UNALIGNED_SIZE
	#define SHEAPERD_SHEAP_OVERWRITE_ON_FREE			1
#endif
#ifndef SHEAPERD_SHEAP_OVERWRITE_VALUE
	#define SHEAPERD_SHEAP_OVERWRITE_VALUE				0xFF
#endif
#ifndef SHEAPERD_SHEAP_MUTEX_WAIT_TICKS
	#define SHEAPERD_SHEAP_MUTEX_WAIT_TICKS				100
#endif

// TODO: implement logic in sheap.c
#define SHEAPERD_SHEAP_MEMORY_ALLOCATION_FIRST_FIT			0
#ifndef SHEAPERD_SHEAP_MEMORY_ALLOCATION_STRATEGY
	#define SHEAPERD_SHEAP_MEMORY_ALLOCATION_STRATEGY  		SHEAPERD_SHEAP_MEMORY_ALLOCATION_FIRST_FIT
#endif

#ifndef SHEAPERD_SHEAP_USE_EXTENDED_HEADER
	#define SHEAPERD_SHEAP_USE_EXTENDED_HEADER				1
	#ifndef SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_PC
		#define SHEAPERD_SHEAP_AUTO_CREATED_BLOCK_PC		1
	#endif
#endif



//TODO: implement logic in sheap.c
#define SHEAPERD_SHEAP_CHECK_ALL_BLOCKS_ON_FREE		0
#define SHEAPERD_SHEAP_CHECK_ALL_BLOCKS_ON_MALLOC	0

#define SHEAPERD_CRC32_POLY				0x04C11DB7
#define SHEAPERD_CRC32_XOR_OUT			0xFFFFFFFF

#define SHEAPERD_CRC16_POLY				0x1021
#define SHEAPERD_CRC16_XOR_OUT			0x0000

#endif /* INTERNAL_OPT_H_ */
