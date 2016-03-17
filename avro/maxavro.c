/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2016
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "maxavro.h"
#include <log_manager.h>
#include <errno.h>

#define MAX_INTEGER_SIZE 10

#define avro_decode(n) ((n >> 1) ^ -(n & 1))
#define encode_long(n) ((n << 1) ^ (n >> 63))
#define more_bytes(b) (b & 0x80)

/**
 * @brief Read an Avro integer
 *
 * The integer lengths are all variable and the last bit in a byte indicates
 * if more bytes belong to the integer value. The real value of the integer is
 * the concatenation of the lowest seven bits of eacy byte. This value is encoded
 * in a zigzag patten i.e. first value is -1, second 1, third -2 and so on.
 * @param file The source FILE handle
 * @param dest Destination where the read value is written
 * @return True if value was read successfully
 */
bool maxavro_read_integer(maxavro_file_t* file, uint64_t *dest)
{
    uint64_t rval = 0;
    uint8_t nread = 0;
    uint8_t byte;
    size_t rdsz;
    do
    {
        if (nread >= MAX_INTEGER_SIZE)
        {
            file->last_error = MAXAVRO_ERR_VALUE_OVERFLOW;
            return false;
        }
        if ((rdsz = fread(&byte, sizeof(byte), 1, file->file)) != sizeof(byte))
        {
            // TODO: Integrate log_manager
            if (rdsz != 0)
            {
                file->last_error = MAXAVRO_ERR_IO;
            }
            return false;
        }
        rval |= (uint64_t)(byte & 0x7f) << (nread++ * 7);
    }
    while (more_bytes(byte));

    if (dest)
    {
        *dest = avro_decode(rval);
    }
    return true;
}

uint64_t maxavro_encode_integer(uint8_t* buffer, uint64_t val)
{
    uint64_t encval = encode_long(val);
    uint8_t nbytes = 0;

    while (more_bytes(encval))
    {
        buffer[nbytes++] = 0x80 | (0x7f & encval);
        encval >>= 7;
    }

    buffer[nbytes++] = encval;
    return nbytes;
}

/**
 * @brief Calculate the length of an Avro integer
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_integer(uint64_t val)
{
    uint64_t encval = encode_long(val);
    uint8_t nbytes = 0;

    while (more_bytes(encval))
    {
        nbytes++;
        encval >>= 7;
    }

    return nbytes;
}

bool maxavro_write_integer(FILE *file, uint64_t val)
{
    uint8_t buffer[MAX_INTEGER_SIZE];
    uint8_t nbytes = maxavro_encode_integer(buffer, val);
    return fwrite(buffer, 1, nbytes, file) == nbytes;
}

/**
 * @brief Read an Avro string
 *
 * The strings are encoded as one Avro integer followed by that many bytes of
 * data.
 * @param file File to read from
 * @return Pointer to newly allocated string or NULL if an error occurred
 *
 * @see maxavro_get_error
 */
char* maxavro_read_string(maxavro_file_t* file)
{
    char *key = NULL;
    uint64_t len;

    if (maxavro_read_integer(file, &len))
    {
        key = malloc(len + 1);
        if (key)
        {
            size_t nread = fread(key, 1, len, file->file);
            if (nread == len)
            {
                key[len] = '\0';
            }
            else
            {
                if (nread != 0)
                {
                    file->last_error = MAXAVRO_ERR_IO;
                }
                free(key);
                key = NULL;
            }
        }
        else
        {
            file->last_error = MAXAVRO_ERR_MEMORY;
        }
    }
    return key;
}

bool maxavro_skip_string(maxavro_file_t* file)
{
    uint64_t len;

    if (maxavro_read_integer(file, &len))
    {
        if (fseek(file->file, len, SEEK_CUR) != 0)
        {
            file->last_error = MAXAVRO_ERR_IO;
        }
        else
        {
            return true;
        }
    }

    return false;
}

uint64_t maxavro_encode_string(uint8_t* dest, const char* str)
{
    uint64_t slen = strlen(str);
    uint64_t ilen = maxavro_encode_integer(dest, slen);
    memcpy(dest, str, slen);
    return slen + ilen;
}

/**
 * @brief Calculate the length of an Avro string
 * @param val Vale to calculate
 * @return Length of the string in bytes
 */
uint64_t avro_length_string(const char* str)
{
    uint64_t slen = strlen(str);
    uint64_t ilen = avro_length_integer(slen);
    return slen + ilen;
}

bool maxavro_write_string(FILE *file, const char* str)
{
    uint64_t len = strlen(str);
    return maxavro_write_integer(file, len) && fwrite(str, 1, len, file) == len;
}

/**
 * @brief Read an Avro float
 *
 * The float is encoded as a 4 byte floating point value
 * @param file File to read from
 * @param dest Destination where the read value is stored
 * @return True if value was read successfully, false if an error occurred
 *
 * @see maxavro_get_error
 */
bool maxavro_read_float(maxavro_file_t* file, float *dest)
{
    size_t nread = fread(dest, 1, sizeof(*dest), file->file);
    if (nread != sizeof(*dest) && nread != 0)
    {
        file->last_error = MAXAVRO_ERR_IO;
        return false;
    }
    return nread == sizeof(*dest);
}

uint64_t maxavro_encode_float(uint8_t* dest, float val)
{
    memcpy(dest, &val, sizeof(val));
    return sizeof(val);
}

/**
 * @brief Calculate the length of a float value
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_float(float val)
{
    return sizeof(val);
}

bool maxavro_write_float(FILE *file, float val)
{
    return fwrite(&val, 1, sizeof(val), file) == sizeof(val);
}

/**
 * @brief Read an Avro double
 *
 * The float is encoded as a 8 byte floating point value
 * @param file File to read from
 * @param dest Destination where the read value is stored
 * @return True if value was read successfully, false if an error occurred
 *
 * @see maxavro_get_error
 */
bool maxavro_read_double(maxavro_file_t* file, double *dest)
{
    size_t nread = fread(dest, 1, sizeof(*dest), file->file);
    if (nread != sizeof(*dest) && nread != 0)
    {
        file->last_error = MAXAVRO_ERR_IO;
        return false;
    }
    return nread == sizeof(*dest);
}

uint64_t maxavro_encode_double(uint8_t* dest, double val)
{
    memcpy(dest, &val, sizeof(val));
    return sizeof(val);
}

/**
 * @brief Calculate the length of a double value
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_double(double val)
{
    return sizeof(val);
}

bool maxavro_write_double(FILE *file, double val)
{
    return fwrite(&val, 1, sizeof(val), file) == sizeof(val);
}

/**
 * @brief Read an Avro map
 *
 * A map is encoded as a series of blocks. Each block is encoded as an Avro
 * integer followed by that many key-value pairs of Avro strings. The last
 * block in the map will be a zero length block signaling its end.
 * @param file File to read from
 * @return A read map or NULL if an error occurred. The return value needs to be
 * freed with maxavro_map_free().
 */
maxavro_map_t* maxavro_map_read(maxavro_file_t *file)
{

    maxavro_map_t* rval = NULL;
    uint64_t blocks;

    if (!maxavro_read_integer(file, &blocks))
    {
        return NULL;
    }

    while (blocks > 0)
    {
        for (long i = 0; i < blocks; i++)
        {
            maxavro_map_t* val = calloc(1, sizeof(maxavro_map_t));
            if (val && (val->key = maxavro_read_string(file)) && (val->value = maxavro_read_string(file)))
            {
                val->next = rval;
                rval = val;
            }
            else
            {
                if (val == NULL)
                {
                    file->last_error = MAXAVRO_ERR_MEMORY;
                }
                maxavro_map_free(val);
                maxavro_map_free(rval);
                return NULL;
            }
        }
        if (!maxavro_read_integer(file, &blocks))
        {
            maxavro_map_free(rval);
            return NULL;
        }
    }

    return rval;
}

/**
 * @brief Free an Avro map
 *
 * @param value Map to free
 */
void maxavro_map_free(maxavro_map_t *value)
{
    while (value)
    {
        maxavro_map_t* tmp = value;
        value = value->next;
        free(tmp->key);
        free(tmp->value);
        free(tmp);
    }
}

maxavro_map_t* avro_map_start()
{
    return (maxavro_map_t*)calloc(1, sizeof(maxavro_map_t));
}

uint64_t avro_map_encode(uint8_t *dest, maxavro_map_t* map)
{
    uint64_t len = maxavro_encode_integer(dest, map->blocks);

    while (map)
    {
        len += maxavro_encode_string(dest, map->key);
        len += maxavro_encode_string(dest, map->value);
        map = map->next;
    }

    /** Maps end with an empty block i.e. a zero integer value */
    len += maxavro_encode_integer(dest, 0);
    return len;
}

/**
 * @brief Calculate the length of an Avro map
 * @param map Map to measure
 * @return Length of the map in bytes
 */
uint64_t avro_map_length(maxavro_map_t* map)
{
    uint64_t len = avro_length_integer(map->blocks);

    while (map)
    {
        len += avro_length_string(map->key);
        len += avro_length_string(map->value);
        map = map->next;
    }

    len += avro_length_integer(0);
    return len;
}
