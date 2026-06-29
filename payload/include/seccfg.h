#pragma once

#include <types.h>

typedef struct {
    u32 magic;
    u32 version;
    u32 size;
    u32 lock_state;
    u32 dm_verity;
    u32 sboot_runtime;
    u32 end_magic;
    u8 hash[32];
    u8 reserved[0x200 - 0x3C];
} SecCfgV4;

#define SECCFG_SIZE        (0x3C)
#define SECCFG_START_MAGIC 0x4D4D4D4D
#define SECCFG_END_MAGIC   0x45454545

typedef enum {
    LKS_DEFAULT = 1,
    LKS_MP_DEFAULT,
    LKS_UNLOCK,
    LKS_LOCK,
    LKS_VERIFIED,
    LKS_CUSTOM,
} LockState;

typedef enum {
   DM_VERITY_OK        = 0,
   DM_VERITY_ERR       = 1
} DmVerityState;
