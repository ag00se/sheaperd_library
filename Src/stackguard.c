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

typedef struct{
	int32_t taskId;
	uint32_t* sp;
	stackguard_mpu_regionSize_t size;
} stackguard_mpuRegion_t;

static uint8_t gNumberOfRegions = 0;
static uint8_t gNextUnusedRegion = 0;
static stackguard_mpuRegion_t gTasksRegions[SHEAPERD_MPU_MAX_REGIONS] = { 0 };

static int8_t getRegionForTask(uint32_t taskId, stackguard_mpuRegion_t* region);

static uint8_t getMPURegions(){
	return (uint8_t)(MPU->TYPE >> MPU_TYPE_DREGION_Pos);
}

sheaperd_MPUState_t stackguard_initMPU(){
	gNumberOfRegions = 0;
	for(int i = 0; i < SHEAPERD_MPU_MAX_REGIONS; i++){
		gTasksRegions[i].taskId = -1;
		gTasksRegions[i].sp = 0;
		gTasksRegions[i].size = 0;
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
	if(stackSize == STACKGUARD_MPU_INVALID_SIZE){
		return STACKGUARD_MPU_INVALID_REGION_SIZE;
	}
	gTasksRegions[gNextUnusedRegion].taskId = taskId;
	gTasksRegions[gNextUnusedRegion].sp = sp;
	gTasksRegions[gNextUnusedRegion].size = stackSize;
	MPU->RBAR = spAddress | (1 << 4) | gNextUnusedRegion;

	// 			 AP = Access permissions  					 				TEX SCB = Type extension mask (shareable, cacheable, bufferable)
	//			 (Default: no access)        				 				(Default: 0b000110)        	    			     (Region size)      (Enable)
	MPU->RASR = (STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_DISABLED << 24) | STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB << 16 | (stackSize << 1) | 0x1;

	while(gNextUnusedRegion < gNumberOfRegions && gTasksRegions[gNextUnusedRegion].taskId != -1){
		gNextUnusedRegion++;
	}
	return STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_enableMPU(){
	if(stackguard_isMPUEnabled()){
		return STACKGUARD_MPU_ALREADY_ENABLED;
	}
	MPU->CTRL = (1 << 2) | 0x1;
	__asm volatile ("dsb 0xF":::"memory");
	__asm volatile ("isb 0xF":::"memory");
	return STACKGUARD_NO_ERROR;
}

void stackguard_taskSwitchIn(uint32_t taskId, uint32_t* sp){
	if(!stackguard_isMPUEnabled()){
		return;
	}
	stackguard_mpuRegion_t region;
	int8_t regionIndex = getRegionForTask(taskId, &region);
	if(regionIndex == -1){
		return;
	}
	uint32_t spAddress = (uint32_t)sp;
	if((spAddress & 0x1F) != 0){
		return;
	}
	if(region.size == STACKGUARD_MPU_INVALID_SIZE){
		return;
	}

	//Make sure this works - atm hard fault on task switch task.c 1275
	MPU->RBAR = spAddress | (1 << 4) | regionIndex;
	MPU->RASR = (STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_DISABLED << 24) | STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB << 16 | (region.size << 1) | 0x0;
	__asm volatile ("dsb 0xF":::"memory");
	__asm volatile ("isb 0xF":::"memory");
}

static int8_t getRegionForTask(uint32_t taskId, stackguard_mpuRegion_t* region){
	region->taskId = -1;
	region->size = STACKGUARD_MPU_INVALID_SIZE;
	region->sp = 0;
	int8_t regionIndex = -1;
	for(int i = 0; i < gNumberOfRegions; i++){
		if(gTasksRegions[i].taskId == taskId){
			region->taskId = taskId;
			region->sp = gTasksRegions[i].sp;
			region->size = gTasksRegions[i].size;
			regionIndex = i;
		}
	}
	return regionIndex;
}

bool stackguard_isMPUEnabled(){
	return (MPU->CTRL & 0x1) != 0;
}

#endif
