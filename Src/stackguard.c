/** @file stackguard.c
 *  @brief Provides the implementation of the stack protection using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/opt.h"

// don't include stackguard if not enabled via options
#if SHEAPERD_STACK_GUARD
#ifndef MEMORY_PROTECTION
	#define MEMORY_PROTECTION 1
#endif
#include "stackguard.h"

#ifdef SHEAPERD_CMSIS_2
static osMutexId_t gStackMutex_id;
static const osMutexAttr_t stackMutex_attr = {
	  "stackguard_mutex",
	  osMutexRecursive,
	  NULL,
	  0U
};
#endif

typedef struct{
	int32_t taskId;
	mpu_region_t mpuRegion;
} stackguard_mpuRegion_t;

static uint8_t gNumberOfRegions = 0;
static uint8_t gNextUnusedRegion = 0;
static stackguard_mpuRegion_t gTasksRegions[STACKGUARD_NUMBER_OF_MPU_REGIONS] = { 0 };

static mpu_region_t createDefaultRegion(uint32_t number);
static stackguard_error_t removeRegion(uint32_t taskId);
static void fillRegionDefaults(mpu_region_t* region);

static bool acquireMutex();
static bool releaseMutex();

stackguard_error_t stackguard_init(){
	memory_protection_disableMPU();
	gNumberOfRegions = 0;
	for(uint32_t i = 0; i < STACKGUARD_NUMBER_OF_MPU_REGIONS; i++){
		gTasksRegions[i].taskId = -1;
		gTasksRegions[i].mpuRegion = createDefaultRegion(i);
	}
	gNumberOfRegions = memory_protection_getNumberOfMPURegions();
	memory_protection_disableMPU();
	util_error_t error = util_initMutex(&gStackMutex_id, &stackMutex_attr);
	SHEAPERD_ASSERT("Mutex creation failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_CREATION_FAILED);
	return gNumberOfRegions == 0 ? STACKGUARD_NO_MPU_AVAILABLE : STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, mpu_regionSize_t stackSize){
	if(gNextUnusedRegion >= gNumberOfRegions){
		return STACKGUARD_NO_MPU_REGION_LEFT;
	}
	if(!acquireMutex()){
		return STACKGUARD_MUTEX_ACQUIRE_FAILED;
	}
	stackguard_mpuRegion_t region;
	region.taskId = taskId;
	region.mpuRegion.address = (uint32_t)sp;
	region.mpuRegion.number = gNextUnusedRegion;
	region.mpuRegion.size = stackSize;
	fillRegionDefaults(&region.mpuRegion);

	mpu_error_t error = memory_protection_configureRegion(&region.mpuRegion);
	switch (error){
		case INVALID_REGION_ADDRESS:
			return STACKGUARD_INVALID_MPU_ADDRESS;
		case INVALID_REGION_ADDRESS_ALIGNMENT:
			return STACKGUARD_INVALID_STACK_ALIGNMENT;
		default:
			break;
	}
	gTasksRegions[gNextUnusedRegion] = region;
	while(gNextUnusedRegion < gNumberOfRegions && gTasksRegions[gNextUnusedRegion].taskId != -1){
		gNextUnusedRegion++;
	}
	releaseMutex();
	return STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_removeTask(uint32_t taskId){
	if(!acquireMutex()){
		return STACKGUARD_MUTEX_ACQUIRE_FAILED;
	}
	stackguard_error_t error = removeRegion(taskId);
	releaseMutex();
	return error;
}

void stackguard_taskSwitchIn(uint32_t taskId){
	if(!memory_protection_isMPUEnabled){
		SHEAPERD_ASSERT("Stackguard task switch in: MPU is not enabled.", false, STACKGUARD_MPU_NOT_ENABLED);
		return;
	}
	memory_protection_disableMPU();
	for(int i = 0; i < gNumberOfRegions; i++){
		stackguard_mpuRegion_t region = gTasksRegions[i];
		if(region.taskId != -1){
			region.mpuRegion.ap = region.taskId == taskId ? MPU_REGION_ALL_ACCESS_ALLOWED : MPU_REGION_ALL_ACCESS_DENIED;
			region.mpuRegion.number = i;
			memory_protection_configureRegion(&region.mpuRegion);
		}
	}
	memory_protection_enableMPU();
}

stackguard_error_t stackguard_guard(){
	return memory_protection_enableMPU() == NO_MPU_AVAILABLE ? STACKGUARD_NO_MPU_AVAILABLE : STACKGUARD_NO_ERROR;
}

static stackguard_error_t removeRegion(uint32_t taskId){
	for(uint32_t i = 0; i < gNumberOfRegions; i++){
		if(gTasksRegions[i].taskId == taskId){
			gTasksRegions[i].taskId = -1;
			gTasksRegions[i].mpuRegion = createDefaultRegion(0);
			if(i < gNextUnusedRegion){
				gNextUnusedRegion = i;
			}
			return STACKGUARD_NO_ERROR;
		}
	}
	return STACKGUARD_TASK_NOT_FOUND;
}

static void fillRegionDefaults(mpu_region_t* region){
	region->ap = MPU_REGION_ALL_ACCESS_DENIED;
	region->enabled = true;
	region->cachable = true;
	region->bufferable = false;
	region->shareable = true;
	region->tex = MPU_DEFAULT_TEX;
	region->xn = false;
}

static mpu_region_t createDefaultRegion(uint32_t number){
	mpu_region_t region = {
			.address = 0,
			.enabled = false,
			.number = number,
			.size = 0,
			.ap = MPU_REGION_ALL_ACCESS_DENIED,
			.cachable = true,
			.bufferable = false,
			.shareable = true,
			.tex = MPU_DEFAULT_TEX,
			.xn = false
	};
	return region;
}

static bool acquireMutex(){
	util_error_t error = util_acquireMutex(gStackMutex_id, SHEAPERD_DEFAULT_MUTEX_WAIT_TICKS);
	SHEAPERD_ASSERT("Mutex acquire failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_ACQUIRE_FAILED);
	return error == ERROR_NO_ERROR;
}

static bool releaseMutex(){
	util_error_t error = util_releaseMutex(gStackMutex_id);
	SHEAPERD_ASSERT("Mutex release failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_RELEASE_FAILED);
	return error == ERROR_NO_ERROR;
}

#endif
