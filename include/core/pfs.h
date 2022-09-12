/*
 * pfs.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __PFS_H__
#define __PFS_H__

#include "nca_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PFS0_MAGIC  0x50465330  /* "PFS0". */

typedef struct {
    u32 magic;              ///< "PFS0".
    u32 entry_count;
    u32 name_table_size;
    u8 reserved[0x4];
} PartitionFileSystemHeader;

NXDT_ASSERT(PartitionFileSystemHeader, 0x10);

typedef struct {
    u64 offset;
    u64 size;
    u32 name_offset;
    u8 reserved[0x4];
} PartitionFileSystemEntry;

NXDT_ASSERT(PartitionFileSystemEntry, 0x18);

/// Used with Partition FS sections from NCAs.
typedef struct {
    NcaStorageContext storage_ctx;      ///< Used to read NCA FS section data.
    NcaFsSectionContext *nca_fs_ctx;    ///< Same as storage_ctx.nca_fs_ctx. Placed here for convenience.
    u64 offset;                         ///< Partition offset (relative to the start of the NCA FS section).
    u64 size;                           ///< Partition size.
    bool is_exefs;                      ///< ExeFS flag.
    u64 header_size;                    ///< Full header size.
    u8 *header;                         ///< PartitionFileSystemHeader + (PartitionFileSystemEntry * entry_count) + Name Table.
} PartitionFileSystemContext;

/// Used with Partition FS images (e.g. NSPs).
typedef struct {
    PartitionFileSystemHeader header;   ///< Partition FS header. Holds the entry count and name table size.
    PartitionFileSystemEntry *entries;  ///< Partition FS entries.
    char *name_table;                   ///< Name table.
    u64 fs_size;                        ///< Partition FS data size. Updated each time a new entry is added.
} PartitionFileSystemFileContext;

/// Initializes a Partition FS context.
bool pfsInitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx);

/// Reads raw partition data using a Partition FS context.
/// Input offset must be relative to the start of the Partition FS.
bool pfsReadPartitionData(PartitionFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved PartitionFileSystemEntry using a Partition FS context.
/// Input offset must be relative to the start of the Partition FS entry.
bool pfsReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset);

/// Retrieves a Partition FS entry index by its name.
bool pfsGetEntryIndexByName(PartitionFileSystemContext *ctx, const char *name, u32 *out_idx);

/// Calculates the extracted Partition FS size.
bool pfsGetTotalDataSize(PartitionFileSystemContext *ctx, u64 *out_size);

/// Generates HierarchicalSha256 FS section patch data using a Partition FS context + entry, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the Partition FS entry data.
/// This function shares the same limitations as ncaGenerateHierarchicalSha256Patch().
/// Use the pfsWriteEntryPatchToMemoryBuffer() wrapper to write patch data generated by this function.
bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out);

/// Adds a new Partition FS entry to an existing PartitionFileSystemFileContext, using the provided entry name and size.
/// If 'out_entry_idx' is a valid pointer, the index to the new Partition FS entry will be saved to it.
bool pfsAddEntryInformationToFileContext(PartitionFileSystemFileContext *ctx, const char *entry_name, u64 entry_size, u32 *out_entry_idx);

/// Updates the name from a Partition FS entry in an existing PartitionFileSystemFileContext, using an entry index and the new entry name.
bool pfsUpdateEntryNameFromFileContext(PartitionFileSystemFileContext *ctx, u32 entry_idx, const char *new_entry_name);

/// Generates a full Partition FS header from an existing PartitionFileSystemFileContext and writes it to the provided memory buffer.
bool pfsWriteFileContextHeaderToMemoryBuffer(PartitionFileSystemFileContext *ctx, void *buf, u64 buf_size, u64 *out_header_size);

/// Miscellaneous functions.

NX_INLINE void pfsFreeContext(PartitionFileSystemContext *ctx)
{
    if (!ctx) return;
    ncaStorageFreeContext(&(ctx->storage_ctx));
    if (ctx->header) free(ctx->header);
    memset(ctx, 0, sizeof(PartitionFileSystemContext));
}

NX_INLINE u32 pfsGetEntryCount(PartitionFileSystemContext *ctx)
{
    if (!ctx || !ctx->header_size || !ctx->header) return 0;
    return ((PartitionFileSystemHeader*)ctx->header)->entry_count;
}

NX_INLINE PartitionFileSystemEntry *pfsGetEntryByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    if (idx >= pfsGetEntryCount(ctx)) return NULL;
    return (PartitionFileSystemEntry*)(ctx->header + sizeof(PartitionFileSystemHeader) + (idx * sizeof(PartitionFileSystemEntry)));
}

NX_INLINE char *pfsGetNameTable(PartitionFileSystemContext *ctx)
{
    u32 entry_count = pfsGetEntryCount(ctx);
    if (!entry_count) return NULL;
    return (char*)(ctx->header + sizeof(PartitionFileSystemHeader) + (entry_count * sizeof(PartitionFileSystemEntry)));
}

NX_INLINE char *pfsGetEntryName(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry)
{
    char *name_table = pfsGetNameTable(ctx);
    if (!name_table || !fs_entry || fs_entry->name_offset >= ((PartitionFileSystemHeader*)ctx->header)->name_table_size || !name_table[fs_entry->name_offset]) return NULL;
    return (name_table + fs_entry->name_offset);
}

NX_INLINE char *pfsGetEntryNameByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    PartitionFileSystemEntry *fs_entry = pfsGetEntryByIndex(ctx, idx);
    char *name_table = pfsGetNameTable(ctx);
    if (!fs_entry || !name_table) return NULL;
    return (name_table + fs_entry->name_offset);
}

NX_INLINE PartitionFileSystemEntry *pfsGetEntryByName(PartitionFileSystemContext *ctx, const char *name)
{
    u32 idx = 0;
    if (!pfsGetEntryIndexByName(ctx, name, &idx)) return NULL;
    return pfsGetEntryByIndex(ctx, idx);
}

NX_INLINE void pfsWriteEntryPatchToMemoryBuffer(PartitionFileSystemContext *ctx, NcaHierarchicalSha256Patch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!ctx || !ncaStorageIsValidContext(&(ctx->storage_ctx)) || ctx->nca_fs_ctx != ctx->storage_ctx.nca_fs_ctx || \
        ctx->storage_ctx.base_storage_type != NcaStorageBaseStorageType_Regular) return;
    ncaWriteHierarchicalSha256PatchToMemoryBuffer(ctx->nca_fs_ctx->nca_ctx, patch, buf, buf_size, buf_offset);
}

NX_INLINE void pfsFreeEntryPatch(NcaHierarchicalSha256Patch *patch)
{
    ncaFreeHierarchicalSha256Patch(patch);
}

NX_INLINE void pfsFreeFileContext(PartitionFileSystemFileContext *ctx)
{
    if (!ctx) return;
    if (ctx->entries) free(ctx->entries);
    if (ctx->name_table) free(ctx->name_table);
    memset(ctx, 0, sizeof(PartitionFileSystemFileContext));
}

NX_INLINE void pfsInitializeFileContext(PartitionFileSystemFileContext *ctx)
{
    if (!ctx) return;
    pfsFreeFileContext(ctx);
    ctx->header.magic = __builtin_bswap32(PFS0_MAGIC);
}

NX_INLINE u32 pfsGetEntryCountFromFileContext(PartitionFileSystemFileContext *ctx)
{
    return (ctx ? ctx->header.entry_count : 0);
}

NX_INLINE PartitionFileSystemEntry *pfsGetEntryByIndexFromFileContext(PartitionFileSystemFileContext *ctx, u32 idx)
{
    if (idx >= pfsGetEntryCountFromFileContext(ctx)) return NULL;
    return &(ctx->entries[idx]);
}

NX_INLINE char *pfsGetEntryNameByIndexFromFileContext(PartitionFileSystemFileContext *ctx, u32 idx)
{
    PartitionFileSystemEntry *fs_entry = pfsGetEntryByIndexFromFileContext(ctx, idx);
    if (!fs_entry || !ctx->name_table) return NULL;
    return (ctx->name_table + fs_entry->name_offset);
}

#ifdef __cplusplus
}
#endif

#endif /* __PFS_H__ */
