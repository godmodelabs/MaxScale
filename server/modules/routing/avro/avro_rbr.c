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


#include <mysql_utils.h>
#include <jansson.h>
#include <maxscale/alloc.h>
#include <avrorouter.h>
#include <strings.h>

#define WRITE_EVENT         0
#define UPDATE_EVENT        1
#define UPDATE_EVENT_AFTER  2
#define DELETE_EVENT        3

static bool warn_decimal = false; /**< Remove when support for DECIMAL is added */
static bool warn_bit = false; /**< Remove when support for BIT is added */
static bool warn_large_enumset = false; /**< Remove when support for ENUM/SET values
                                         * larger than 255 is added */

uint8_t* process_row_event_data(TABLE_MAP *map, TABLE_CREATE *create,
                                avro_value_t *record, uint8_t *ptr,
                                uint8_t *columns_present);
void notify_all_clients(AVRO_INSTANCE *router);
void add_used_table(AVRO_INSTANCE* router, const char* table);

/**
 * @brief Get row event name
 * @param event Event type
 * @return String representation of the event
 */
static int get_event_type(uint8_t event)
{
    switch (event)
    {

        case WRITE_ROWS_EVENTv0:
        case WRITE_ROWS_EVENTv1:
        case WRITE_ROWS_EVENTv2:
            return WRITE_EVENT;

        case UPDATE_ROWS_EVENTv0:
        case UPDATE_ROWS_EVENTv1:
        case UPDATE_ROWS_EVENTv2:
            return UPDATE_EVENT;

        case DELETE_ROWS_EVENTv0:
        case DELETE_ROWS_EVENTv1:
        case DELETE_ROWS_EVENTv2:
            return DELETE_EVENT;

        default:
            MXS_ERROR("Unexpected event type: %d (%0x)", event, event);
            return -1;
    }
}

/**
 * @brief Handle a table map event
 *
 * This converts a table map events into table meta data that will be used when
 * converting binlogs to Avro format.
 * @param router Avro router instance
 * @param hdr Replication header
 * @param ptr Pointer to event payload
 */
bool handle_table_map_event(AVRO_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr)
{
    bool rval = false;
    uint64_t id;
    char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
    int ev_len = router->event_type_hdr_lens[hdr->event_type];

    read_table_info(ptr, ev_len, &id, table_ident, sizeof(table_ident));
    TABLE_CREATE* create = hashtable_fetch(router->created_tables, table_ident);

    if (create)
    {
        ss_dassert(create->columns > 0);
        TABLE_MAP *old = hashtable_fetch(router->table_maps, table_ident);

        if (old == NULL || old->version != create->version)
        {
            TABLE_MAP *map = table_map_alloc(ptr, ev_len, create);

            if (map)
            {
                char* json_schema = json_new_schema_from_table(map);

                if (json_schema)
                {
                    char filepath[PATH_MAX + 1];
                    snprintf(filepath, sizeof(filepath), "%s/%s.%06d.avro",
                             router->avrodir, table_ident, map->version);

                    /** Close the file and open a new one */
                    hashtable_delete(router->open_tables, table_ident);
                    AVRO_TABLE *avro_table = avro_table_alloc(filepath, json_schema);

                    if (avro_table)
                    {
                        bool notify = old != NULL;

                        if (old)
                        {
                            router->active_maps[old->id % sizeof(router->active_maps)] = NULL;
                        }
                        hashtable_delete(router->table_maps, table_ident);
                        hashtable_add(router->table_maps, (void*) table_ident, map);
                        hashtable_add(router->open_tables, table_ident, avro_table);
                        save_avro_schema(router->avrodir, json_schema, map);
                        router->active_maps[map->id % sizeof(router->active_maps)] = map;
                        MXS_DEBUG("Table %s mapped to %lu", table_ident, map->id);
                        rval = true;

                        if (notify)
                        {
                            notify_all_clients(router);
                        }
                    }
                    else
                    {
                        MXS_ERROR("Failed to open new Avro file for writing.");
                    }
                    MXS_FREE(json_schema);
                }
                else
                {
                    MXS_ERROR("Failed to create JSON schema.");
                }
            }
            else
            {
                MXS_ERROR("Failed to allocate new table map.");
            }
        }
        else
        {
            ss_dassert(router->active_maps[old->id % sizeof(router->active_maps)] == old);
            router->active_maps[old->id % sizeof(router->active_maps)] = NULL;
            table_map_remap(ptr, ev_len, old);
            router->active_maps[old->id % sizeof(router->active_maps)] = old;
            MXS_DEBUG("Table %s re-mapped to %lu", table_ident, old->id);
            /** No changes in the schema */
            rval = true;
        }
    }
    else
    {
        MXS_WARNING("Table map event for table '%s' read before the DDL statement "
                    "for that table  was read. Data will not be processed for this "
                    "table until a DDL statement for it is read.", table_ident);
    }

    return rval;
}

/**
 * @brief Set common field values and update the GTID subsequence counter
 *
 * This sets the domain, server ID, sequence and event position fields of
 * the GTID. It also sets the event timestamp and event type fields.
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param event_type Event type
 * @param record Record to prepare
 */
static void prepare_record(AVRO_INSTANCE *router, REP_HEADER *hdr,
                           int event_type, avro_value_t *record)
{
    avro_value_t field;
    avro_value_get_by_name(record, avro_domain, &field, NULL);
    avro_value_set_int(&field, router->gtid.domain);

    avro_value_get_by_name(record, avro_server_id, &field, NULL);
    avro_value_set_int(&field, router->gtid.server_id);

    avro_value_get_by_name(record, avro_sequence, &field, NULL);
    avro_value_set_int(&field, router->gtid.seq);

    router->gtid.event_num++;
    avro_value_get_by_name(record, avro_event_number, &field, NULL);
    avro_value_set_int(&field, router->gtid.event_num);

    avro_value_get_by_name(record, avro_timestamp, &field, NULL);
    avro_value_set_int(&field, hdr->timestamp);

    avro_value_get_by_name(record, avro_event_type, &field, NULL);
    avro_value_set_enum(&field, event_type);
}

/**
 * @brief Handle a single RBR row event
 *
 * These events contain the changes in the data. This function assumes that full
 * row image is sent in every row event.
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param ptr Pointer to the start of the event
 * @return True on succcess, false on error
 */
bool handle_row_event(AVRO_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr)
{
    bool rval = false;
    uint8_t *start = ptr;
    uint8_t table_id_size = router->event_type_hdr_lens[hdr->event_type] == 6 ? 4 : 6;
    uint64_t table_id = 0;

    /** The first value is the ID where the table was mapped. This should be
     * the same as the ID in the table map even which was processed before this
     * row event. */
    memcpy(&table_id, ptr, table_id_size);
    ptr += table_id_size;

    /** Replication flags, currently ignored for the most part. */
    uint16_t flags = 0;
    memcpy(&flags, ptr, 2);
    ptr += 2;

    if (table_id == TABLE_DUMMY_ID && flags & ROW_EVENT_END_STATEMENT)
    {
        /** This is an dummy event which should release all table maps. Right
         * now we just return without processing the rows. */
        return true;
    }

    /** Newer replication events have extra data stored in the header. MariaDB
     * 10.1 does not use these and instead use the v1 events */
    if (hdr->event_type > DELETE_ROWS_EVENTv1)
    {
        /** Version 2 row event, skip extra data */
        uint16_t extra_len = 0;
        memcpy(&extra_len, ptr, 2);
        ptr += 2 + extra_len;
    }

    /** Number of columns in the table */
    uint64_t ncolumns = leint_consume(&ptr);

    /** If full row image is used, all columns are present. Currently only full
     * row image is supported and thus the bitfield should be all ones. In
     * the future partial row images could be used if the bitfield containing
     * the columns that are present in this event is used. */
    const int coldata_size = (ncolumns + 7) / 8;
    uint8_t col_present[coldata_size];
    memcpy(&col_present, ptr, coldata_size);
    ptr += coldata_size;

    /** Update events have the before and after images of the row. This can be
     * used to calculate a "delta" of sorts if necessary. Currently we store
     * both the before and the after images. */
    uint8_t col_update[coldata_size];
    if (hdr->event_type == UPDATE_ROWS_EVENTv1 ||
        hdr->event_type == UPDATE_ROWS_EVENTv2)
    {
        memcpy(&col_update, ptr, coldata_size);
        ptr += coldata_size;
    }

    /** There should always be a table map event prior to a row event.
     * TODO: Make the active_maps dynamic */
    TABLE_MAP *map = router->active_maps[table_id % sizeof(router->active_maps)];
    ss_dassert(map);

    if (map)
    {
        char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
        snprintf(table_ident, sizeof(table_ident), "%s.%s", map->database, map->table);
        AVRO_TABLE* table = hashtable_fetch(router->open_tables, table_ident);
        TABLE_CREATE* create = map->table_create;

        if (table && create && ncolumns == map->columns)
        {
            avro_value_t record;
            avro_generic_value_new(table->avro_writer_iface, &record);

            /** Each event has one or more rows in it. The number of rows is not known
             * beforehand so we must continue processing them until we reach the end
             * of the event. */
            int rows = 0;
            while (ptr - start < hdr->event_size - BINLOG_EVENT_HDR_LEN)
            {
                /** Add the current GTID and timestamp */
                int event_type = get_event_type(hdr->event_type);
                prepare_record(router, hdr, event_type, &record);
                ptr = process_row_event_data(map, create, &record, ptr, col_present);
                avro_file_writer_append_value(table->avro_file, &record);

                /** Update rows events have the before and after images of the
                 * affected rows so we'll process them as another record with
                 * a different type */
                if (event_type == UPDATE_EVENT)
                {
                    prepare_record(router, hdr, UPDATE_EVENT_AFTER, &record);
                    ptr = process_row_event_data(map, create, &record, ptr, col_present);
                    avro_file_writer_append_value(table->avro_file, &record);
                }

                rows++;
            }

            add_used_table(router, table_ident);
            avro_value_decref(&record);
            rval = true;
        }
        else if (table == NULL)
        {
            MXS_ERROR("Avro file handle was not found for table %s.%s. See earlier"
                      " errors for more details.", map->database, map->table);
        }
        else if (create == NULL)
        {
            MXS_ERROR("Create table statement for %s.%s was not found from the "
                      "binary logs or the stored schema was not correct.",
                      map->database, map->table);
        }
        else
        {
            MXS_ERROR("Row event and table map event have different column counts."
                      " Only full row image is currently supported.");
        }
    }
    else
    {
        MXS_ERROR("Row event for unknown table mapped to ID %lu. Data will not "
                  "be processed.", table_id);
    }

    return rval;
}

/**
 * @brief Unpack numeric types
 *
 * Convert the stored value into an Avro value and pack it in the record.
 *
 * @param field Avro value in a record
 * @param type Type of the field
 * @param metadata Field metadata
 * @param value Pointer to the start of the in-memory representation of the data
 */
void set_numeric_field_value(avro_value_t *field, uint8_t type, uint8_t *metadata, uint8_t *value)
{
    int64_t i = 0;

    switch (type)
    {
        case TABLE_COL_TYPE_TINY:
            i = *value;
            avro_value_set_int(field, i);
            break;

        case TABLE_COL_TYPE_SHORT:
            memcpy(&i, value, 2);
            avro_value_set_int(field, i);
            break;

        case TABLE_COL_TYPE_INT24:
            memcpy(&i, value, 3);
            avro_value_set_int(field, i);
            break;

        case TABLE_COL_TYPE_LONG:
            memcpy(&i, value, 4);
            avro_value_set_int(field, i);
            break;

        case TABLE_COL_TYPE_LONGLONG:
            memcpy(&i, value, 8);
            avro_value_set_int(field, i);
            break;

        case TABLE_COL_TYPE_FLOAT:
            memcpy(&i, value, 4);
            avro_value_set_float(field, (float)i);
            break;

        case TABLE_COL_TYPE_DOUBLE:
            memcpy(&i, value, 8);
            avro_value_set_float(field, (double)i);
            break;

        default:
            break;
    }
}

/**
 * @brief Check if a bit is set
 *
 * @param ptr Pointer to start of bitfield
 * @param columns Number of columns (bits)
 * @param current_column Zero indexed column number
 * @return True if the bit is set
 */
static bool bit_is_set(uint8_t *ptr, int columns, int current_column)
{
    if (current_column >= 8)
    {
        ptr += current_column / 8;
        current_column = current_column % 8;
    }

    return ((*ptr) & (1 << current_column));
}

/**
 * @brief Get the length of the metadata for a particular field
 *
 * @param type Type of the field
 * @return Length of the metadata for this field
 */
int get_metadata_len(uint8_t type)
{
    switch (type)
    {
        case TABLE_COL_TYPE_STRING:
        case TABLE_COL_TYPE_VAR_STRING:
        case TABLE_COL_TYPE_VARCHAR:
        case TABLE_COL_TYPE_DECIMAL:
        case TABLE_COL_TYPE_NEWDECIMAL:
        case TABLE_COL_TYPE_ENUM:
        case TABLE_COL_TYPE_SET:
        case TABLE_COL_TYPE_BIT:
            return 2;

        case TABLE_COL_TYPE_BLOB:
        case TABLE_COL_TYPE_FLOAT:
        case TABLE_COL_TYPE_DOUBLE:
        case TABLE_COL_TYPE_DATETIME2:
        case TABLE_COL_TYPE_TIMESTAMP2:
        case TABLE_COL_TYPE_TIME2:
            return 1;

        default:
            return 0;
    }
}

/**
 * @brief Extract the values from a single row  in a row event
 *
 * @param map Table map event associated with this row
 * @param create Table creation associated with this row
 * @param record Avro record used for storing this row
 * @param ptr Pointer to the start of the row data, should be after the row event header
 * @param columns_present The bitfield holding the columns that are present for
 * this row event. Currently this should be a bitfield which has all bits set.
 * @return Pointer to the first byte after the current row event
 */
uint8_t* process_row_event_data(TABLE_MAP *map, TABLE_CREATE *create, avro_value_t *record,
                                uint8_t *ptr, uint8_t *columns_present)
{
    int npresent = 0;
    avro_value_t field;
    long ncolumns = map->columns;
    uint8_t *metadata = map->column_metadata;
    size_t metadata_offset = 0;

    /** BIT type values use the extra bits in the row event header */
    int extra_bits = (((ncolumns + 7) / 8) * 8) - ncolumns;

    /** Store the null value bitmap */
    uint8_t *null_bitmap = ptr;
    ptr += (ncolumns + 7) / 8;

    for (long i = 0; i < map->columns && npresent < ncolumns; i++)
    {
        ss_dassert(create->columns == map->columns);
        avro_value_get_by_name(record, create->column_names[i], &field, NULL);

        if (bit_is_set(columns_present, ncolumns, i))
        {
            npresent++;
            if (bit_is_set(null_bitmap, ncolumns, i))
            {
                avro_value_set_null(&field);
            }
            else if (column_is_fixed_string(map->column_types[i]))
            {
                /** ENUM and SET are stored as STRING types with the type stored
                 * in the metadata. */
                if (fixed_string_is_enum(metadata[metadata_offset]))
                {
                    uint8_t val[metadata[metadata_offset + 1]];
                    uint64_t bytes = unpack_enum(ptr, &metadata[metadata_offset], val);
                    char strval[32];

                    /** Right now only ENUMs/SETs with less than 256 values
                     * are printed correctly */
                    snprintf(strval, sizeof(strval), "%hhu", val[0]);
                    if (bytes > 1 && warn_large_enumset)
                    {
                        warn_large_enumset = true;
                        MXS_WARNING("ENUM/SET values larger than 255 values aren't supported.");
                    }
                    avro_value_set_string(&field, strval);
                    ptr += bytes;
                }
                else
                {
                    uint8_t bytes = *ptr;
                    char str[bytes + 1];
                    memcpy(str, ptr + 1, bytes);
                    str[bytes] = '\0';
                    avro_value_set_string(&field, str);
                    ptr += bytes + 1;
                }
            }
            else if (column_is_bit(map->column_types[i]))
            {
                uint64_t value = 0;
                int width = metadata[metadata_offset] + metadata[metadata_offset + 1] * 8;
                int bits_in_nullmap = MIN(width, extra_bits);
                extra_bits -= bits_in_nullmap;
                width -= bits_in_nullmap;
                size_t bytes = width / 8;

                // TODO: extract the bytes
                if (!warn_bit)
                {
                    warn_bit = true;
                    MXS_WARNING("BIT is not currently supported, values are stored as 0.");
                }
                avro_value_set_int(&field, value);
                ptr += bytes;
            }
            else if (column_is_decimal(map->column_types[i]))
            {
                const int dec_dig = 9;
                int precision = metadata[metadata_offset];
                int decimals = metadata[metadata_offset + 1];
                int dig_bytes[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
                int ipart = precision - decimals;
                int ipart1 = ipart / dec_dig;
                int fpart1 = decimals / dec_dig;
                int ipart2 = ipart - ipart1 * dec_dig;
                int fpart2 = decimals - fpart1 * dec_dig;
                int ibytes = ipart1 * 4 + dig_bytes[ipart2];
                int fbytes = fpart1 * 4 + dig_bytes[fpart2];
                ptr += ibytes + fbytes;

                // TODO: Add support for DECIMAL
                if (!warn_decimal)
                {
                    warn_decimal = true;
                    MXS_WARNING("DECIMAL is not currently supported, values are stored as 0.");
                }
                avro_value_set_int(&field, 0);
            }
            else if (column_is_variable_string(map->column_types[i]))
            {
                size_t sz;
                char *str = lestr_consume(&ptr, &sz);
                char buf[sz + 1];
                memcpy(buf, str, sz);
                buf[sz] = '\0';
                avro_value_set_string(&field, buf);
            }
            else if (column_is_blob(map->column_types[i]))
            {
                uint8_t bytes = metadata[metadata_offset];
                uint64_t len = 0;
                memcpy(&len, ptr, bytes);
                ptr += bytes;
                avro_value_set_bytes(&field, ptr, len);
                ptr += len;
            }
            else if (column_is_temporal(map->column_types[i]))
            {
                char buf[80];
                struct tm tm;
                ptr += unpack_temporal_value(map->column_types[i], ptr, &metadata[metadata_offset], &tm);
                format_temporal_value(buf, sizeof(buf), map->column_types[i], &tm);
                avro_value_set_string(&field, buf);
            }
            /** All numeric types (INT, LONG, FLOAT etc.) */
            else
            {
                uint8_t lval[16];
                memset(lval, 0, sizeof(lval));
                ptr += unpack_numeric_field(ptr, map->column_types[i],
                                            &metadata[metadata_offset], lval);
                set_numeric_field_value(&field, map->column_types[i], &metadata[metadata_offset], lval);
            }
            ss_dassert(metadata_offset <= map->column_metadata_size);
            metadata_offset += get_metadata_len(map->column_types[i]);
        }
    }

    return ptr;
}
