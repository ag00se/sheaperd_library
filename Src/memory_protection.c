/** @file memory_protection.c
 *  @brief Provides the implementation for configuring memory regions and access permissions using the MPU
 *
 * For an explanation of the MPU registers see some documentation like:
 * 	+ ARMv7-M Architecture Reference Manual: https://developer.arm.com/documentation/ddi0403/latest/
 * 	+ https://www.st.com/resource/en/application_note/dm00272912-managing-memory-protection-unit-in-stm32-mcus-stmicroelectronics.pdf
 * 	+ http://ww1.microchip.com/downloads/en/AppNotes/Atmel-42128-AT02346-Using-the-MPU-on-Atmel-Cortex-M3-M4-based-Microcontroller_Application-Note.pdf
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/opt.h"

#if MEMORY_PROTECTION && SHEAPERD_ARMV7
#include "memory_protection.h"

#define ASSERT_RETURN_ERROR_ON_FAILURE(assert, error)	\
do{                                                     \
	if (assert) {										\
		return error;									\
	}													\
}while(0)

#define MPU_REGION_ADDRESS_32BIT_ALIGNMENT_MASK	0x1F

#define MPU_RASR_SIZE_Pos						1
#define MPU_RASR_TEX_SCB_Pos					16
#define MPU_RASR_AP_Pos							24
#define MPU_RASR_XN_Pos							28

#define MPU_RBAR_VALID_Pos						4
#define MPU_CTRL_PRIVDEFENA_Pos					2

#ifndef MPU_TYPE_DREGION_Pos
	#define MPU_TYPE_DREGION_Pos                8
#endif
#ifndef MPU_TYPE_DREGION_Msk
	#define MPU_TYPE_DREGION_Msk               (0xFFUL << MPU_TYPE_DREGION_Pos)
#endif

#ifndef SCS_BASE
	#define SCS_BASE          (0xE000E000UL)
#endif
#ifndef MPU_BASE
	#define MPU_BASE          (SCS_BASE +  0x0D90UL)
#endif
#ifndef MPU
	#define MPU               ((MPU_Type*)MPU_BASE)
#endif

#if SHEAPERD_MPU_M0PLUS || SHEAPERD_MPU_M3_M4_M7 || SHEAPERD_MPU_M23 || SHEAPERD_MPU_M33_M35P
typedef struct {
	uint32_t TYPE;
	uint32_t CTRL;
	uint32_t RNR;
	uint32_t RBAR;

#if SHEAPERD_ARMV6 || SHEAPERD_ARMV7
	uint32_t RASR;
	#if SHEAPERD_ARMV7
		uint32_t RBAR_A1;
		uint32_t RASR_A1;
		uint32_t RBAR_A2;
		uint32_t RASR_A2;
		uint32_t RBAR_A3;
		uint32_t RASR_A3;
	#endif
#elif SHEAPERD_ARMV8
	uint32_t RLAR;
	#if SHEAPERD_MPU_M23
		uint32_t RESERVED0[7U];
	#elif SHEAPERD_MPU_M33_M35P
		  uint32_t RBAR_A1;
		  uint32_t RLAR_A1;
		  uint32_t RBAR_A2;
		  uint32_t RLAR_A2;
		  uint32_t RBAR_A3;
		  uint32_t RLAR_A3;
		  uint32_t RESERVED0[1];
	#endif
	union {
		uint32_t MAIR[2];
		struct {
			uint32_t MAIR0;
			uint32_t MAIR1;
		};
	};
#endif
} MPU_Type;
#endif

#define MEMORY_PROTECTION_ALIGNMENT_TABLE_OFFSET	4
static uint32_t gAlignmentTable[28] = {
	0x1F,			0x3F,			0x7F,			0xFF,
	0x1FF,			0x3FF,			0x7FF,			0xFFF,
	0x1FFF,			0x3FFF,			0x7FFF,			0xFFFF,
	0x1FFFF,		0x3FFFF,		0x7FFFF,		0xFFFFF,
	0x1FFFFF,		0x3FFFFF,		0x7FFFFF,		0xFFFFFF,
	0x1FFFFFF,		0x3FFFFFF,		0x7FFFFFF,		0xFFFFFFF,
	0x1FFFFFFF,		0x3FFFFFFF,		0x7FFFFFFF,		0xFFFFFFFF,
};

static bool isAddressValid(uint32_t address);
static bool isAlignmentValid(mpu_region_t* region);

mpu_error_t memory_protection_configureRegion(mpu_region_t* region){
	ASSERT_RETURN_ERROR_ON_FAILURE(memory_protection_getNumberOfMPURegions() == 0, NO_MPU_AVAILABLE);
	memory_protection_disableMPU();
	if(!isAddressValid(region->address)){
		return INVALID_REGION_ADDRESS;
	}
#if SHEAPERD_ARMV7
	if(!isAlignmentValid(region)){
		return INVALID_REGION_ADDRESS_ALIGNMENT;
	}
#endif
	MPU->RBAR = region->address | (1 << MPU_RBAR_VALID_Pos) | region->number;
	uint8_t tex_scb = region->tex << 3 | region->shareable << 2 | region->cachable << 1 | region->bufferable;
	MPU->RASR = (region->ap << MPU_RASR_AP_Pos) | (region->xn << MPU_RASR_XN_Pos) | (tex_scb << MPU_RASR_TEX_SCB_Pos) | (region->size << MPU_RASR_SIZE_Pos) | region->enabled;
	memory_protection_enableMPU();
	return NO_ERROR;
}

uint8_t memory_protection_getNumberOfMPURegions(){
	return (uint8_t)(MPU->TYPE >> MPU_TYPE_DREGION_Pos);
}

bool memory_protection_isMPUEnabled(){
	return (MPU->CTRL & 0x1) != 0;
}

mpu_error_t memory_protection_enableMPU(){
	ASSERT_RETURN_ERROR_ON_FAILURE(memory_protection_getNumberOfMPURegions() == 0, NO_MPU_AVAILABLE);
	MPU->CTRL = (1 << MPU_CTRL_PRIVDEFENA_Pos) | 0x1;
	__asm volatile ("dsb 0xF":::"memory");
	__asm volatile ("isb 0xF":::"memory");
	return NO_ERROR;
}

mpu_error_t memory_protection_disableMPU(){
	ASSERT_RETURN_ERROR_ON_FAILURE(memory_protection_getNumberOfMPURegions() == 0, NO_MPU_AVAILABLE);
	__asm volatile ("dmb 0xF":::"memory");
	MPU->CTRL  = 0;
	return NO_ERROR;
}

static bool isAddressValid(uint32_t address){
	return (address & MPU_REGION_ADDRESS_32BIT_ALIGNMENT_MASK) == 0;
}

static bool isAlignmentValid(mpu_region_t* region){
	uint32_t mask = gAlignmentTable[region->size - MEMORY_PROTECTION_ALIGNMENT_TABLE_OFFSET];
	uint32_t result = (region->address & mask);
	return result == 0;
}

#endif
