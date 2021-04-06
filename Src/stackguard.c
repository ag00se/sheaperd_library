/** @file stackguard.c
 *  @brief Provides the implementation of the stack protection using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */


#include "internal/opt.h"

// don't build stackguard if not enabled via options
#if SHEAPERD_STACK_GUARD

#include "stackguard.h"

#ifndef MPU_TYPE_DREGION_Pos
#define MPU_TYPE_DREGION_Pos                8U
#endif
#ifndef MPU_TYPE_DREGION_Msk
#define MPU_TYPE_DREGION_Msk               (0xFFUL << MPU_TYPE_DREGION_Pos)
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

#ifndef SCS_BASE
	#define SCS_BASE          (0xE000E000UL)
#endif
#ifndef MPU_BASE
	#define MPU_BASE          (SCS_BASE +  0x0D90UL)
#endif
#ifndef MPU
	#define MPU               ((MPU_Type*)MPU_BASE)
#endif

/**
 * For an explanation of the MPU registers see some documentation like:
 * https://www.st.com/resource/en/application_note/dm00272912-managing-memory-protection-unit-in-stm32-mcus-stmicroelectronics.pdf
 * http://ww1.microchip.com/downloads/en/AppNotes/Atmel-42128-AT02346-Using-the-MPU-on-Atmel-Cortex-M3-M4-based-Microcontroller_Application-Note.pdf
 */
#define INTERNAL_SRAM_TEX_SCB (0b000110 << 16)

static uint8_t gNumberOfRegions = 0;
static uint8_t gNextUnusedRegion = 0;
static int32_t gTaskIds[SHEAPERD_MPU_MAX_REGIONS];

static stackguard_synchronization_barrier_cb gDSB;
static stackguard_synchronization_barrier_cb gISB;

static uint8_t getMPURegions(){
	return (uint8_t)(MPU->TYPE >> MPU_TYPE_DREGION_Pos);
}

Sheaperd_MPUState_t stackguard_initMPU(stackguard_synchronization_barrier_cb dsb, stackguard_synchronization_barrier_cb isb){
	gDSB = dsb;
	gISB = isb;
	gNumberOfRegions = 0;
	for(int i = 0; i < SHEAPERD_MPU_MAX_REGIONS; i++){
		gTaskIds[i] = -1;
	}
	gNumberOfRegions = getMPURegions();
	MPU->CTRL = 0 | 1 << 2;
	return gNumberOfRegions == 0 ? SHEAPERD_MPU_NOT_AVAILABLE : SHEAPERD_MPU_INITIALIZED;
}

stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, stackguard_mpu_regionSize_t stackSize){
	uint32_t spAddress = (uint32_t)sp;
	if((spAddress & 0x1F) != 0){
		return STACKGUARD_INVALID_MPU_ADDRESS;
	}
	if(gNextUnusedRegion >= gNumberOfRegions){
		return STACKGUARD_NO_MPU_REGION_LEFT;
	}
	gTaskIds[gNextUnusedRegion] = taskId;
	MPU->RBAR = spAddress | (1 << 4) | gNextUnusedRegion;

	// AP = Access permissions, TEX SCB = Type extension mask (shareable, cacheable, bufferable)
	//			AP (No Access)        (TEX SCB)        	  (Region size)   (Enable)
	MPU->RASR = (0b000 << 24) | INTERNAL_SRAM_TEX_SCB | (stackSize << 1) | 0x1;
	while(gNextUnusedRegion < gNumberOfRegions && gTaskIds[gNextUnusedRegion] != -1){
		gNextUnusedRegion++;
	}
	return STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_enableMPU(){
	if(gDSB == NULL || gISB == NULL){
		return STACKGUARD_NO_SYNCHRONIZATION_BARRIER_CALLBACKS_PROVIDED;
	}
	MPU->CTRL = (1 << 2) | 0x1;
	gDSB();
	gISB();
	return STACKGUARD_NO_ERROR;
}

#endif
