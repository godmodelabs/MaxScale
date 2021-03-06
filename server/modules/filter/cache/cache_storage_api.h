#ifndef _MAXSCALE_FILTER_CACHE_CACHE_H
#define _MAXSCALE_FILTER_CACHE_CACHE_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdbool.h>
#include <stdint.h>
#include <buffer.h>
#include <mysql_client_server_protocol.h>
#include <skygw_debug.h>

EXTERN_C_BLOCK_BEGIN

typedef enum cache_result
{
    CACHE_RESULT_OK,
    CACHE_RESULT_NOT_FOUND,
    CACHE_RESULT_OUT_OF_RESOURCES,
    CACHE_RESULT_ERROR
} cache_result_t;

typedef void* CACHE_STORAGE;

enum
{
    CACHE_KEY_MAXLEN = 128
};

typedef struct cache_storage_api
{
    /**
     * Called immediately after the storage module has been loaded.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    bool (*initialize)();

    /**
     * Creates an instance of cache storage. This function should, if necessary,
     * create the actual storage, initialize it and prepare to put and get
     * cache items.
     *
     * @param name  The name of the cache instance.
     * @param ttl   Time to live; number of seconds the value is valid.
     * @param argc  The number of elements in the argv array.
     * @param argv  Array of arguments, as passed in the `storage_args` parameter
     *              in the cache section in the MaxScale configuration file.
     * @return A new cache instance, or NULL if the instance could not be
     *         created.
     */
    CACHE_STORAGE* (*createInstance)(const char *name,
                                     uint32_t ttl,
                                     int argc, char* argv[]);

    /**
     * Frees an CACHE_STORAGE instance earlier created with createInstance.
     *
     * @param instance The CACHE_STORAGE instance to be freed.
     */
    void (*freeInstance)(CACHE_STORAGE* instance);

    /**
     * Create a key for a GWBUF.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param query      An SQL query. Must be one contiguous buffer.
     * @param key        Pointer to array of CACHE_KEY_MAXLEN size where
     *                   the key will be written.
     * @return CACHE_RESULT_OK if a key was created, otherwise some error code.
     */
    cache_result_t (*getKey)(CACHE_STORAGE* storage,
                             const GWBUF* query,
                             char* key);
    /**
     * Get a value from the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with getKey.
     * @param result     Pointer to variable that after a successful return will
     *                   point to a GWBUF.
     * @return CACHE_RESULT_OK if item was found,
     *         CACHE_RESULT_NOT_FOUND if item was not found (which may be because
     *         the ttl was reached), or some other error code.
     */
    cache_result_t (*getValue)(CACHE_STORAGE* storage,
                               const char* key,
                               GWBUF** result);

    /**
     * Put a value to the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with getKey.
     * @param value      Pointer to GWBUF containing the value to be stored.
     *                   Must be one contiguous buffer.
     * @return CACHE_RESULT_OK if item was successfully put,
     *         CACHE_RESULT_OUT_OF_RESOURCES if item could not be put, due to
     *         some resource having become exhausted, or some other error code.
     */
    cache_result_t (*putValue)(CACHE_STORAGE* storage,
                               const char* key,
                               const GWBUF* value);
} CACHE_STORAGE_API;

#define CACHE_STORAGE_ENTRY_POINT "CacheGetStorageAPI"
typedef CACHE_STORAGE_API* (*CacheGetStorageAPIFN)();

EXTERN_C_BLOCK_END

#endif
