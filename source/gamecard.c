/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>

#include "gamecard.h"
#include "service_guard.h"
#include "utils.h"

#define GAMECARD_READ_BUFFER_SIZE               0x800000                /* 8 MiB */

#define GAMECARD_ACCESS_WAIT_TIME               3                       /* Seconds */

#define GAMECARD_UPDATE_TID                     (u64)0x0100000000000816

#define GAMECARD_ECC_BLOCK_SIZE                 0x200
#define GAMECARD_ECC_DATA_SIZE                  0x24

#define GAMECARD_STORAGE_AREA_NAME(x)           ((x) == GameCardStorageArea_Normal ? "normal" : ((x) == GameCardStorageArea_Secure ? "secure" : "none"))

#define GAMECARD_CAPACITY_1GiB                  (u64)0x40000000
#define GAMECARD_CAPACITY_2GiB                  (u64)0x80000000
#define GAMECARD_CAPACITY_4GiB                  (u64)0x100000000
#define GAMECARD_CAPACITY_8GiB                  (u64)0x200000000
#define GAMECARD_CAPACITY_16GiB                 (u64)0x400000000
#define GAMECARD_CAPACITY_32GiB                 (u64)0x800000000

/* Type definitions. */

typedef enum {
    GameCardStorageArea_None   = 0,
    GameCardStorageArea_Normal = 1,
    GameCardStorageArea_Secure = 2
} GameCardStorageArea;

typedef struct {
    u64 offset;         ///< Relative to the start of the gamecard header.
    u64 size;           ///< Whole partition size.
    u64 header_size;    ///< Full header size.
    u8 *header;         ///< GameCardHashFileSystemHeader + GameCardHashFileSystemEntry + Name Table.
} GameCardHashFileSystemPartitionInfo;

/* Global variables. */

static FsDeviceOperator g_deviceOperator = {0};
static FsEventNotifier g_gameCardEventNotifier = {0};
static Event g_gameCardKernelEvent = {0};
static bool g_openDeviceOperator = false, g_openEventNotifier = false, g_loadKernelEvent = false;

static thrd_t g_gameCardDetectionThread;
static UEvent g_gameCardDetectionThreadExitEvent = {0};
static mtx_t g_gameCardSharedDataMutex;
static bool g_gameCardDetectionThreadCreated = false, g_gameCardInserted = false, g_gameCardInfoLoaded = false;

static FsGameCardHandle g_gameCardHandle = {0};
static FsStorage g_gameCardStorage = {0};
static u8 g_gameCardStorageCurrentArea = GameCardStorageArea_None;
static u8 *g_gameCardReadBuf = NULL;

static GameCardHeader g_gameCardHeader = {0};
static u64 g_gameCardStorageNormalAreaSize = 0, g_gameCardStorageSecureAreaSize = 0;
static u64 g_gameCardCapacity = 0;

static u8 *g_gameCardHfsRootHeader = NULL;   /// GameCardHashFileSystemHeader + GameCardHashFileSystemEntry + Name Table.
static GameCardHashFileSystemPartitionInfo *g_gameCardHfsPartitions = NULL;

/* Function prototypes. */

static bool gamecardCreateDetectionThread(void);
static void gamecardDestroyDetectionThread(void);
static int gamecardDetectionThreadFunc(void *arg);

static inline bool gamecardIsInserted(void);

static void gamecardLoadInfo(void);
static void gamecardFreeInfo(void);

static bool gamecardGetHandle(void);
static inline void gamecardCloseHandle(void);

static bool gamecardOpenStorageArea(u8 area);
static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset, bool lock);
static void gamecardCloseStorageArea(void);

static bool gamecardGetStorageAreasSizes(void);
static inline u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size);

static bool gamecardGetHashFileSystemPartitionIndexByType(u8 type, u32 *out);
static inline GameCardHashFileSystemEntry *gamecardGetHashFileSystemEntryByIndex(void *hfs_header, u32 idx);
static inline char *gamecardGetHashFileSystemEntryName(void *hfs_header, u32 name_offset);

/* Service guard used to generate thread-safe initialize + exit functions. */
/* I'm using this here even though this actually isn't a real service but who cares, it gets the job done. */
NX_GENERATE_SERVICE_GUARD(gamecard);

bool gamecardIsReady(void)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    ret = (g_gameCardInserted && g_gameCardInfoLoaded);
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardRead(void *out, u64 read_size, u64 offset)
{
    return gamecardReadStorageArea(out, read_size, offset, true);
}

bool gamecardGetHeader(GameCardHeader *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        memcpy(out, &g_gameCardHeader, sizeof(GameCardHeader));
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetTotalSize(u64 *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        *out = (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize);
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetTrimmedSize(u64 *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        *out = (sizeof(GameCardHeader) + ((u64)g_gameCardHeader.valid_data_end_address * GAMECARD_MEDIA_UNIT_SIZE));
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetRomCapacity(u64 *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        *out = g_gameCardCapacity;
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetCertificate(FsGameCardCertificate *out)
{
    Result rc = 0;
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorGetGameCardDeviceCertificate(&g_deviceOperator, &g_gameCardHandle, out);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorGetGameCardDeviceCertificate failed! (0x%08X)", rc);
        ret = R_SUCCEEDED(rc);
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetBundledFirmwareUpdateVersion(u32 *out)
{
    Result rc = 0;
    u64 update_id = 0;
    u32 update_version = 0;
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorUpdatePartitionInfo(&g_deviceOperator, &g_gameCardHandle, &update_version, &update_id);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorUpdatePartitionInfo failed! (0x%08X)", rc);
        ret = (R_SUCCEEDED(rc) && update_id == GAMECARD_UPDATE_TID);
        if (ret) *out = update_version;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetOffsetAndSizeFromHashFileSystemPartitionEntryByName(u8 hfs_partition_type, const char *name, u64 *out_offset, u64 *out_size)
{
    bool ret = false;
    char *entry_name = NULL;
    size_t name_len = 0;
    u32 hfs_partition_idx = 0;
    GameCardHashFileSystemHeader *fs_header = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    
    if (!g_gameCardInserted || !g_gameCardInfoLoaded || !name || !*name || (!out_offset && !out_size) || !gamecardGetHashFileSystemPartitionIndexByType(hfs_partition_type, &hfs_partition_idx))
    {
        LOGFILE("Invalid parameters!");
        goto out;
    }
    
    name_len = strlen(name);
    fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsPartitions[hfs_partition_idx].header;
    
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        fs_entry = gamecardGetHashFileSystemEntryByIndex(fs_header, i);
        if (!fs_entry) continue;
        
        entry_name = gamecardGetHashFileSystemEntryName(fs_header, fs_entry->name_offset);
        if (!entry_name) continue;
        
        if (!strncasecmp(entry_name, name, name_len))
        {
            if (out_offset) *out_offset = (g_gameCardHfsPartitions[hfs_partition_idx].offset + g_gameCardHfsPartitions[hfs_partition_idx].header_size + fs_entry->offset);
            if (out_size) *out_size = fs_entry->size;
            ret = true;
            break;
        }
    }
    
out:
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}














NX_INLINE Result _gamecardInitialize(void)
{
    Result rc = 0;
    
    /* Allocate memory for the gamecard read buffer */
    g_gameCardReadBuf = malloc(GAMECARD_READ_BUFFER_SIZE);
    if (!g_gameCardReadBuf)
    {
        LOGFILE("Unable to allocate memory for the gamecard read buffer!");
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto out;
    }
    
    /* Open device operator */
    rc = fsOpenDeviceOperator(&g_deviceOperator);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenDeviceOperator failed! (0x%08X)", rc);
        goto out;
    }
    
    g_openDeviceOperator = true;
    
    /* Open gamecard detection event notifier */
    rc = fsOpenGameCardDetectionEventNotifier(&g_gameCardEventNotifier);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardDetectionEventNotifier failed! (0x%08X)", rc);
        goto out;
    }
    
    g_openEventNotifier = true;
    
    /* Retrieve gamecard detection kernel event */
    rc = fsEventNotifierGetEventHandle(&g_gameCardEventNotifier, &g_gameCardKernelEvent, true);
    if (R_FAILED(rc))
    {
        LOGFILE("fsEventNotifierGetEventHandle failed! (0x%08X)", rc);
        goto out;
    }
    
    g_loadKernelEvent = true;
    
    /* Create usermode exit event */
    ueventCreate(&g_gameCardDetectionThreadExitEvent, false);
    
    /* Create gamecard detection thread */
    g_gameCardDetectionThreadCreated = gamecardCreateDetectionThread();
    if (!g_gameCardDetectionThreadCreated)
    {
        LOGFILE("Failed to create gamecard detection thread!");
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    
out:
    return rc;
}

static void _gamecardCleanup(void)
{
    /* Destroy gamecard detection thread */
    if (g_gameCardDetectionThreadCreated)
    {
        gamecardDestroyDetectionThread();
        g_gameCardDetectionThreadCreated = false;
    }
    
    /* Close gamecard detection kernel event */
    if (g_loadKernelEvent)
    {
        eventClose(&g_gameCardKernelEvent);
        g_loadKernelEvent = false;
    }
    
    /* Close gamecard detection event notifier */
    if (g_openEventNotifier)
    {
        fsEventNotifierClose(&g_gameCardEventNotifier);
        g_openEventNotifier = false;
    }
    
    /* Close device operator */
    if (g_openDeviceOperator)
    {
        fsDeviceOperatorClose(&g_deviceOperator);
        g_openDeviceOperator = false;
    }
    
    /* Free gamecard read buffer */
    if (g_gameCardReadBuf)
    {
        free(g_gameCardReadBuf);
        g_gameCardReadBuf = NULL;
    }
}

static bool gamecardCreateDetectionThread(void)
{
    if (mtx_init(&g_gameCardSharedDataMutex, mtx_plain) != thrd_success)
    {
        LOGFILE("Failed to initialize gamecard shared data mutex!");
        return false;
    }
    
    if (thrd_create(&g_gameCardDetectionThread, gamecardDetectionThreadFunc, NULL) != thrd_success)
    {
        LOGFILE("Failed to create gamecard detection thread!");
        mtx_destroy(&g_gameCardSharedDataMutex);
        return false;
    }
    
    return true;
}

static void gamecardDestroyDetectionThread(void)
{
    /* Signal the exit event to terminate the gamecard detection thread */
    ueventSignal(&g_gameCardDetectionThreadExitEvent);
    
    /* Wait for the gamecard detection thread to exit */
    thrd_join(g_gameCardDetectionThread, NULL);
    
    /* Destroy mutex */
    mtx_destroy(&g_gameCardSharedDataMutex);
}

static int gamecardDetectionThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    bool prev_status = false;
    
    Waiter gamecard_event_waiter = waiterForEvent(&g_gameCardKernelEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_gameCardDetectionThreadExitEvent);
    
    mtx_lock(&g_gameCardSharedDataMutex);
    
    /* Retrieve initial gamecard insertion status */
    g_gameCardInserted = prev_status = gamecardIsInserted();
    
    /* Load gamecard info right away if a gamecard is inserted */
    if (g_gameCardInserted) gamecardLoadInfo();
    
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    while(true)
    {
        /* Wait until an event is triggered */
        rc = waitMulti(&idx, -1, gamecard_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Exit event triggered */
        if (idx == 1) break;
        
        /* Retrieve current gamecard insertion status */
        /* Only proceed if we're dealing with a status change */
        mtx_lock(&g_gameCardSharedDataMutex);
        
        g_gameCardInserted = gamecardIsInserted();
        
        if (!prev_status && g_gameCardInserted)
        {
            /* Don't access the gamecard immediately to avoid conflicts with HOS / sysmodules */
            utilsSleep(GAMECARD_ACCESS_WAIT_TIME);
            
            /* Load gamecard info */
            gamecardLoadInfo();
        } else {
            /* Free gamecard info */
            gamecardFreeInfo();
        }
        
        prev_status = g_gameCardInserted;
        
        mtx_unlock(&g_gameCardSharedDataMutex);
    }
    
    /* Free gamecard info and close gamecard handle */
    mtx_lock(&g_gameCardSharedDataMutex);
    gamecardFreeInfo();
    g_gameCardInserted = false;
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return 0;
}

static inline bool gamecardIsInserted(void)
{
    bool inserted = false;
    Result rc = fsDeviceOperatorIsGameCardInserted(&g_deviceOperator, &inserted);
    if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorIsGameCardInserted failed! (0x%08X)", rc);
    return (R_SUCCEEDED(rc) && inserted);
}

static void gamecardLoadInfo(void)
{
    if (g_gameCardInfoLoaded) return;
    
    GameCardHashFileSystemHeader *fs_header = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    
    /* Retrieve gamecard storage area sizes */
    /* gamecardReadStorageArea() actually checks if the storage area sizes are greater than zero, so we must first perform this step */
    if (!gamecardGetStorageAreasSizes())
    {
        LOGFILE("Failed to retrieve gamecard storage area sizes!");
        goto out;
    }
    
    /* Read gamecard header */
    if (!gamecardReadStorageArea(&g_gameCardHeader, sizeof(GameCardHeader), 0, false))
    {
        LOGFILE("Failed to read gamecard header!");
        goto out;
    }
    
    /* Check magic word from gamecard header */
    if (__builtin_bswap32(g_gameCardHeader.magic) != GAMECARD_HEAD_MAGIC)
    {
        LOGFILE("Invalid gamecard header magic word! (0x%08X)", __builtin_bswap32(g_gameCardHeader.magic));
        goto out;
    }
    
    /* Get gamecard capacity */
    g_gameCardCapacity = gamecardGetCapacityFromRomSizeValue(g_gameCardHeader.rom_size);
    if (!g_gameCardCapacity)
    {
        LOGFILE("Invalid gamecard capacity value! (0x%02X)", g_gameCardHeader.rom_size);
        goto out;
    }
    
    if (utilsGetCustomFirmwareType() == UtilsCustomFirmwareType_SXOS)
    {
        /* The total size for the secure storage area is maxed out under SX OS */
        /* Let's try to calculate it manually */
        g_gameCardStorageSecureAreaSize = ((g_gameCardCapacity - ((g_gameCardCapacity / GAMECARD_ECC_BLOCK_SIZE) * GAMECARD_ECC_DATA_SIZE)) - g_gameCardStorageNormalAreaSize);
    }
    
    /* Allocate memory for the root hash FS header */
    g_gameCardHfsRootHeader = calloc(g_gameCardHeader.partition_fs_header_size, sizeof(u8));
    if (!g_gameCardHfsRootHeader)
    {
        LOGFILE("Unable to allocate memory for the root hash FS header!");
        goto out;
    }
    
    /* Read root hash FS header */
    if (!gamecardReadStorageArea(g_gameCardHfsRootHeader, g_gameCardHeader.partition_fs_header_size, g_gameCardHeader.partition_fs_header_address, false))
    {
        LOGFILE("Failed to read root hash FS header from offset 0x%lX!", g_gameCardHeader.partition_fs_header_address);
        goto out;
    }
    
    fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
    
    if (__builtin_bswap32(fs_header->magic) != GAMECARD_HFS0_MAGIC)
    {
        LOGFILE("Invalid magic word in root hash FS header! (0x%08X)", __builtin_bswap32(fs_header->magic));
        goto out;
    }
    
    if (!fs_header->entry_count || !fs_header->name_table_size || \
        (sizeof(GameCardHashFileSystemHeader) + (fs_header->entry_count * sizeof(GameCardHashFileSystemEntry)) + fs_header->name_table_size) > g_gameCardHeader.partition_fs_header_size)
    {
        LOGFILE("Invalid file count and/or name table size in root hash FS header!");
        goto out;
    }
    
    /* Allocate memory for the hash FS partitions info */
    g_gameCardHfsPartitions = calloc(fs_header->entry_count, sizeof(GameCardHashFileSystemEntry));
    if (!g_gameCardHfsPartitions)
    {
        LOGFILE("Unable to allocate memory for the hash FS partitions info!");
        goto out;
    }
    
    /* Read hash FS partitions */
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        fs_entry = gamecardGetHashFileSystemEntryByIndex(g_gameCardHfsRootHeader, i);
        if (!fs_entry || !fs_entry->size)
        {
            LOGFILE("Invalid hash FS partition entry!");
            goto out;
        }
        
        g_gameCardHfsPartitions[i].offset = (g_gameCardHeader.partition_fs_header_address + g_gameCardHeader.partition_fs_header_size + fs_entry->offset);
        g_gameCardHfsPartitions[i].size = fs_entry->size;
        
        /* Partially read the current hash FS partition header */
        GameCardHashFileSystemHeader partition_header = {0};
        if (!gamecardReadStorageArea(&partition_header, sizeof(GameCardHashFileSystemHeader), g_gameCardHfsPartitions[i].offset, false))
        {
            LOGFILE("Failed to partially read hash FS partition #%u header from offset 0x%lX!", i, g_gameCardHfsPartitions[i].offset);
            goto out;
        }
        
        if (__builtin_bswap32(partition_header.magic) != GAMECARD_HFS0_MAGIC)
        {
            LOGFILE("Invalid magic word in hash FS partition #%u header! (0x%08X)", i, __builtin_bswap32(partition_header.magic));
            goto out;
        }
        
        if (!partition_header.name_table_size)
        {
            LOGFILE("Invalid name table size in hash FS partition #%u header!", i);
            goto out;
        }
        
        /* Calculate the full header size for the current hash FS partition and round it to a GAMECARD_MEDIA_UNIT_SIZE bytes boundary */
        g_gameCardHfsPartitions[i].header_size = (sizeof(GameCardHashFileSystemHeader) + (partition_header.entry_count * sizeof(GameCardHashFileSystemEntry)) + partition_header.name_table_size);
        g_gameCardHfsPartitions[i].header_size = ROUND_UP(g_gameCardHfsPartitions[i].header_size, GAMECARD_MEDIA_UNIT_SIZE);
        
        /* Allocate memory for the hash FS partition header */
        g_gameCardHfsPartitions[i].header = calloc(g_gameCardHfsPartitions[i].header_size, sizeof(u8));
        if (!g_gameCardHfsPartitions[i].header)
        {
            LOGFILE("Unable to allocate memory for the hash FS partition #%u header!", i);
            goto out;
        }
        
        /* Finally, read the full hash FS partition header */
        if (!gamecardReadStorageArea(g_gameCardHfsPartitions[i].header, g_gameCardHfsPartitions[i].header_size, g_gameCardHfsPartitions[i].offset, false))
        {
            LOGFILE("Failed to read full hash FS partition #%u header from offset 0x%lX!", i, g_gameCardHfsPartitions[i].offset);
            goto out;
        }
    }
    
    g_gameCardInfoLoaded = true;
    
out:
    if (!g_gameCardInfoLoaded) gamecardFreeInfo();
}

static void gamecardFreeInfo(void)
{
    memset(&g_gameCardHeader, 0, sizeof(GameCardHeader));
    
    g_gameCardStorageNormalAreaSize = 0;
    g_gameCardStorageSecureAreaSize = 0;
    
    g_gameCardCapacity = 0;
    
    if (g_gameCardHfsRootHeader)
    {
        if (g_gameCardHfsPartitions)
        {
            GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
            
            for(u32 i = 0; i < fs_header->entry_count; i++)
            {
                if (g_gameCardHfsPartitions[i].header) free(g_gameCardHfsPartitions[i].header);
            }
        }
        
        free(g_gameCardHfsRootHeader);
        g_gameCardHfsRootHeader = NULL;
    }
    
    if (g_gameCardHfsPartitions)
    {
        free(g_gameCardHfsPartitions);
        g_gameCardHfsPartitions = NULL;
    }
    
    gamecardCloseStorageArea();
    
    g_gameCardInfoLoaded = false;
}

static bool gamecardGetHandle(void)
{
    if (!g_gameCardInserted)
    {
        LOGFILE("Gamecard not inserted!");
        return false;
    }
    
    Result rc1 = 0, rc2 = 0;
    FsStorage tmp_storage = {0};
    
    /* 10 tries */
    for(u8 i = 0; i < 10; i++)
    {
        /* First try to open a gamecard storage area using the current gamecard handle */
        rc1 = fsOpenGameCardStorage(&tmp_storage, &g_gameCardHandle, 0);
        if (R_SUCCEEDED(rc1))
        {
            fsStorageClose(&tmp_storage);
            break;
        }
        
        /* If the previous call failed, we may have an invalid handle, so let's close the current one and try to retrieve a new one */
        gamecardCloseHandle();
        rc2 = fsDeviceOperatorGetGameCardHandle(&g_deviceOperator, &g_gameCardHandle);
    }
    
    if (R_FAILED(rc1) || R_FAILED(rc2))
    {
        /* Close leftover gamecard handle */
        gamecardCloseHandle();
        
        if (R_FAILED(rc1)) LOGFILE("fsOpenGameCardStorage failed! (0x%08X)", rc1);
        if (R_FAILED(rc2)) LOGFILE("fsDeviceOperatorGetGameCardHandle failed! (0x%08X)", rc2);
        
        return false;
    }
    
    return true;
}

static inline void gamecardCloseHandle(void)
{
    svcCloseHandle(g_gameCardHandle.value);
    g_gameCardHandle.value = 0;
}

static bool gamecardOpenStorageArea(u8 area)
{
    if (!g_gameCardInserted || (area != GameCardStorageArea_Normal && area != GameCardStorageArea_Secure))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    if (g_gameCardHandle.value && serviceIsActive(&(g_gameCardStorage.s)) && g_gameCardStorageCurrentArea == area) return true;
    
    gamecardCloseStorageArea();
    
    Result rc = 0;
    u32 partition = (area - 1); /* Zero-based index */
    
    /* Retrieve a new gamecard handle */
    if (!gamecardGetHandle())
    {
        LOGFILE("Failed to retrieve gamecard handle!");
        return false;
    }
    
    /* Open storage area */
    rc = fsOpenGameCardStorage(&g_gameCardStorage, &g_gameCardHandle, partition);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardStorage failed to open %s storage area! (0x%08X)", GAMECARD_STORAGE_AREA_NAME(area), rc);
        gamecardCloseHandle();
        return false;
    }
    
    g_gameCardStorageCurrentArea = area;
    
    return true;
}

static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset, bool lock)
{
    if (lock) mtx_lock(&g_gameCardSharedDataMutex);
    
    bool success = false;
    
    if (!g_gameCardInserted || !g_gameCardStorageNormalAreaSize || !g_gameCardStorageSecureAreaSize || !out || !read_size || \
        offset >= (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize) || (offset + read_size) > (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize))
    {
        LOGFILE("Invalid parameters!");
        goto out;
    }
    
    Result rc = 0;
    u8 *out_u8 = (u8*)out;
    u8 area = (offset < g_gameCardStorageNormalAreaSize ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);
    
    /* Handle reads that span both the normal and secure gamecard storage areas */
    if (area == GameCardStorageArea_Normal && (offset + read_size) > g_gameCardStorageNormalAreaSize)
    {
        /* Calculate normal storage area size difference */
        u64 diff_size = (g_gameCardStorageNormalAreaSize - offset);
        
        if (!gamecardReadStorageArea(out_u8, diff_size, offset, false)) goto out;
        
        /* Adjust variables to read right from the start of the secure storage area */
        read_size -= diff_size;
        offset = g_gameCardStorageNormalAreaSize;
        out_u8 += diff_size;
        area = GameCardStorageArea_Secure;
    }
    
    /* Open a storage area if needed */
    /* If the right storage area has already been opened, this will return true */
    if (!gamecardOpenStorageArea(area))
    {
        LOGFILE("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
        goto out;
    }
    
    /* Calculate appropiate storage area offset and retrieve the right storage area pointer */
    u64 base_offset = (area == GameCardStorageArea_Normal ? offset : (offset - g_gameCardStorageNormalAreaSize));
    
    if (!(base_offset % GAMECARD_MEDIA_UNIT_SIZE) && !(read_size % GAMECARD_MEDIA_UNIT_SIZE))
    {
        /* Optimization for reads that are already aligned to GAMECARD_MEDIA_UNIT_SIZE bytes */
        rc = fsStorageRead(&g_gameCardStorage, base_offset, out_u8, read_size);
        if (R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (aligned)", read_size, base_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto out;
        }
        
        success = true;
    } else {
        /* Fix offset and/or size to avoid unaligned reads */
        u64 block_start_offset = (base_offset - (base_offset % GAMECARD_MEDIA_UNIT_SIZE));
        u64 block_end_offset = ROUND_UP(base_offset + read_size, GAMECARD_MEDIA_UNIT_SIZE);
        u64 block_size = (block_end_offset - block_start_offset);
        
        u64 chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? GAMECARD_READ_BUFFER_SIZE : block_size);
        u64 out_chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? (GAMECARD_READ_BUFFER_SIZE - (base_offset - block_start_offset)) : read_size);
        
        rc = fsStorageRead(&g_gameCardStorage, block_start_offset, g_gameCardReadBuf, chunk_size);
        if (R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (unaligned)", chunk_size, block_start_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto out;
        }
        
        memcpy(out_u8, g_gameCardReadBuf + (base_offset - block_start_offset), out_chunk_size);
        
        success = (block_size > GAMECARD_READ_BUFFER_SIZE ? gamecardReadStorageArea(out_u8 + out_chunk_size, read_size - out_chunk_size, base_offset + out_chunk_size, false) : true);
    }
    
out:
    if (lock) mtx_unlock(&g_gameCardSharedDataMutex);
    
    return success;
}

static void gamecardCloseStorageArea(void)
{
    if (serviceIsActive(&(g_gameCardStorage.s)))
    {
        fsStorageClose(&g_gameCardStorage);
        memset(&g_gameCardStorage, 0, sizeof(FsStorage));
    }
    
    gamecardCloseHandle();
    
    g_gameCardStorageCurrentArea = GameCardStorageArea_None;
}

static bool gamecardGetStorageAreasSizes(void)
{
    if (!g_gameCardInserted)
    {
        LOGFILE("Gamecard not inserted!");
        return false;
    }
    
    for(u8 i = 0; i < 2; i++)
    {
        Result rc = 0;
        u64 area_size = 0;
        u8 area = (i == 0 ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);
        
        if (!gamecardOpenStorageArea(area))
        {
            LOGFILE("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
            return false;
        }
        
        rc = fsStorageGetSize(&g_gameCardStorage, (s64*)&area_size);
        
        gamecardCloseStorageArea();
        
        if (R_FAILED(rc) || !area_size)
        {
            LOGFILE("fsStorageGetSize failed to retrieve %s storage area size! (0x%08X)", GAMECARD_STORAGE_AREA_NAME(area), rc);
            g_gameCardStorageNormalAreaSize = g_gameCardStorageSecureAreaSize = 0;
            return false;
        }
        
        if (area == GameCardStorageArea_Normal)
        {
            g_gameCardStorageNormalAreaSize = area_size;
        } else {
            g_gameCardStorageSecureAreaSize = area_size;
        }
    }
    
    return true;
}

static inline u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size)
{
    u64 capacity = 0;
    
    switch(rom_size)
    {
        case GameCardRomSize_1GiB:
            capacity = GAMECARD_CAPACITY_1GiB;
            break;
        case GameCardRomSize_2GiB:
            capacity = GAMECARD_CAPACITY_2GiB;
            break;
        case GameCardRomSize_4GiB:
            capacity = GAMECARD_CAPACITY_4GiB;
            break;
        case GameCardRomSize_8GiB:
            capacity = GAMECARD_CAPACITY_8GiB;
            break;
        case GameCardRomSize_16GiB:
            capacity = GAMECARD_CAPACITY_16GiB;
            break;
        case GameCardRomSize_32GiB:
            capacity = GAMECARD_CAPACITY_32GiB;
            break;
        default:
            break;
    }
    
    return capacity;
}

static bool gamecardGetHashFileSystemPartitionIndexByType(u8 type, u32 *out)
{
    if (type > GameCardHashFileSystemPartitionType_Secure || !out) return false;
    
    char *entry_name = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
    
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        fs_entry = gamecardGetHashFileSystemEntryByIndex(fs_header, i);
        if (!fs_entry) continue;
        
        entry_name = gamecardGetHashFileSystemEntryName(fs_header, fs_entry->name_offset);
        if (!entry_name) continue;
        
        if (!strcasecmp(entry_name, GAMECARD_HFS_PARTITION_NAME(type)))
        {
            *out = i;
            return true;
        }
    }
    
    return false;
}

static inline GameCardHashFileSystemEntry *gamecardGetHashFileSystemEntryByIndex(void *hfs_header, u32 idx)
{
    if (!hfs_header || idx >= ((GameCardHashFileSystemHeader*)hfs_header)->entry_count) return NULL;
    return (GameCardHashFileSystemEntry*)((u8*)hfs_header + sizeof(GameCardHashFileSystemHeader) + (idx * sizeof(GameCardHashFileSystemEntry)));
}

static inline char *gamecardGetHashFileSystemEntryName(void *hfs_header, u32 name_offset)
{
    if (!hfs_header) return NULL;
    
    GameCardHashFileSystemHeader *header = (GameCardHashFileSystemHeader*)hfs_header;
    if (!header->entry_count || name_offset >= header->name_table_size) return NULL;
    
    return ((char*)hfs_header + sizeof(GameCardHashFileSystemHeader) + (header->entry_count * sizeof(GameCardHashFileSystemEntry)) + name_offset);
}