/** @file stackguard.c
 *  @brief Provides the implementation of the stack protection using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/opt.h"

// don't include stackguard if not enabled via options
#if SHEAPERD_STACK_GUARD

#include "stackguard.h"

#define STACKGUARD_ALIGNMENT_TABLE_OFFSET		4

#define MPU_RASR_SIZE_Pos						1
#define MPU_RASR_TEX_SCB_Pos					16
#define MPU_RASR_AP_Pos							24

#define MPU_RBAR_VALID_Pos						4

#define MPU_CTRL_PRIVDEFENA						2

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

typedef struct{
	int32_t taskId;
	uint32_t* sp;
	stackguard_mpu_regionSize_t size;
} stackguard_mpuRegion_t;

static uint32_t gAlignmentTable[28] = {
	0x1F,			0x3F,			0x7F,			0xFF,
	0x1FF,			0x3FF,			0x7FF,			0xFFF,
	0x1FFF,			0x3FFF,			0x7FFF,			0xFFFF,
	0x1FFFF,		0x3FFFF,		0x7FFFF,		0xFFFFF,
	0x1FFFFF,		0x3FFFFF,		0x7FFFFF,		0xFFFFFF,
	0x1FFFFFF,		0x3FFFFFF,		0x7FFFFFF,		0xFFFFFFF,
	0x1FFFFFFF,		0x3FFFFFFF,		0x7FFFFFFF,		0xFFFFFFFF,
};

static uint8_t gNumberOfRegions = 0;
static uint8_t gNextUnusedRegion = 0;
static stackguard_mpuRegion_t gTasksRegions[STACKGUARD_NUMBER_OF_MPU_REGIONS] = { 0 };

static stackguard_mpuRegion_t* getRegionForTask(uint32_t taskId);
static bool isAlignmentValid(uint32_t* sp, stackguard_mpu_regionSize_t size);
static uint32_t getAlignmentMask(stackguard_mpu_regionSize_t size);

static uint8_t getMPURegions(){
	return (uint8_t)(MPU->TYPE >> MPU_TYPE_DREGION_Pos);
}

stackguard_error_t stackguard_init(){
	stackguard_disableMPU();
	gNumberOfRegions = 0;
	for(int i = 0; i < STACKGUARD_NUMBER_OF_MPU_REGIONS; i++){
		gTasksRegions[i].taskId = -1;
		gTasksRegions[i].sp = 0;
		gTasksRegions[i].size = 0;
	}
	gNumberOfRegions = getMPURegions();
	MPU->CTRL = 0 | 1 << MPU_CTRL_PRIVDEFENA;
	return gNumberOfRegions == 0 ? STACKGUARD_NO_MPU_AVAILABLE : STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, stackguard_mpu_regionSize_t stackSize){
	uint32_t spAddress = (uint32_t)sp;
	if((spAddress & 0x1F) != 0){
		return STACKGUARD_INVALID_MPU_ADDRESS;
	}
	if(stackSize == STACKGUARD_MPU_INVALID_SIZE){
		return STACKGUARD_MPU_INVALID_REGION_SIZE;
	}
#if SHEAPERD_ARMV7
	if(!isAlignmentValid(sp, stackSize)){
		return STACKGUARD_INVALID_STACK_ALIGNMENT;
	}
#endif
	if(gNextUnusedRegion >= gNumberOfRegions){
		return STACKGUARD_NO_MPU_REGION_LEFT;
	}
	gTasksRegions[gNextUnusedRegion].taskId = taskId;
	gTasksRegions[gNextUnusedRegion].sp = sp;
	gTasksRegions[gNextUnusedRegion].size = stackSize;
	MPU->RBAR = spAddress | (1 << MPU_RBAR_VALID_Pos) | gNextUnusedRegion;

	// 			 					AP = Access permissions  					 			TEX SCB = Type extension mask (shareable, cacheable, bufferable)
	//			 					(Default: no access)        				 			(Default: 0b000110)        	    			     							(Region size)    		(Enable)
	MPU->RASR = (STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_NO_ACCESS << MPU_RASR_AP_Pos) | STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB << MPU_RASR_TEX_SCB_Pos | (stackSize << MPU_RASR_SIZE_Pos) | 0x1;

	while(gNextUnusedRegion < gNumberOfRegions && gTasksRegions[gNextUnusedRegion].taskId != -1){
		gNextUnusedRegion++;
	}
	return STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_removeTask(uint32_t taskId){
	stackguard_mpuRegion_t* region = getRegionForTask(taskId);
	if(region == NULL){
		return STACKGUARD_TASK_NOT_FOUND;
	}
	region->taskId = -1;
	region->sp = 0;
	region->size = 0;
	return STACKGUARD_NO_ERROR;
}

void stackguard_enableMPU(){
	MPU->CTRL = (1 << MPU_CTRL_PRIVDEFENA) | 0x1;
	__asm volatile ("dsb 0xF":::"memory");
	__asm volatile ("isb 0xF":::"memory");
}

void stackguard_disableMPU(){
	__asm volatile ("dmb 0xF":::"memory");
	MPU->CTRL  = 0;
}

bool stackguard_isMPUEnabled(){
	return (MPU->CTRL & 0x1) != 0;
}

void stackguard_taskSwitchIn(uint32_t taskId){
	if(!stackguard_isMPUEnabled()){
		return;
	}
	stackguard_disableMPU();
	for(int i = 0; i < gNumberOfRegions; i++){
		stackguard_mpuRegion_t region = gTasksRegions[i];
		if(region.taskId != -1){
			uint32_t ap = STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_NO_ACCESS;
			if(region.taskId == taskId){
				ap = STACKGUARD_MPU_MEMORY_ATTRIBUTES_AP_REGION_FULL_ACCESS;
			}
			MPU->RBAR = (uint32_t)region.sp | (1 << MPU_RBAR_VALID_Pos) | i;
			MPU->RASR = (ap << MPU_RASR_AP_Pos)
					| STACKGUARD_MPU_MEMORY_ATTRIBUTES_TEX_SCB << MPU_RASR_TEX_SCB_Pos | (region.size << MPU_RASR_SIZE_Pos) | 0x1;
		}
	}
	stackguard_enableMPU();
}

static stackguard_mpuRegion_t* getRegionForTask(uint32_t taskId){
	for(int i = 0; i < gNumberOfRegions; i++){
		if(gTasksRegions[i].taskId == taskId){
			return &gTasksRegions[i];
		}
	}
	return NULL;
}

static bool isAlignmentValid(uint32_t* sp, stackguard_mpu_regionSize_t size){
	uint32_t mask = getAlignmentMask(size);
	uint32_t result = ((uint32_t)sp & mask);
	return result == 0;
}

static uint32_t getAlignmentMask(stackguard_mpu_regionSize_t size){
	if(size == STACKGUARD_MPU_INVALID_SIZE){
		return size;
	}
	return gAlignmentTable[size - STACKGUARD_ALIGNMENT_TABLE_OFFSET];
}

#endif
