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

#include <unistd.h>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <gwdirs.h>
#include <log_manager.h>
#include <mysql_client_server_protocol.h>
#include <query_classifier.h>
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istream;
using std::map;
using std::ostream;
using std::string;
using std::stringstream;

namespace
{

char USAGE[] =
    "usage: compare [-r count] [-d] [-1 classfier1] [-2 classifier2] "
        "[-A args] [-B args] [-v [0..2]] [-s statement]|[file]]\n\n"
    "-r    redo the test the specified number of times; 0 means forever, default is 1\n"
    "-d    don't stop after first failed query\n"
    "-1    the first classifier, default qc_mysqlembedded\n"
    "-2    the second classifier, default qc_sqlite\n"
    "-A    arguments for the first classifier\n"
    "-B    arguments for the second classifier\n"
    "-s    compare single statement\n"
    "-S    strict, also require that the parse result is identical\n"
    "-v 0, only return code\n"
    "   1, query and result for failed cases\n"
    "   2, all queries, and result for failed cases\n"
    "   3, all queries and all results\n";


enum verbosity_t
{
    VERBOSITY_MIN,
    VERBOSITY_NORMAL,
    VERBOSITY_EXTENDED,
    VERBOSITY_MAX
};

struct State
{
    bool query_printed;
    string query;
    verbosity_t verbosity;
    bool result_printed;
    bool stop_at_error;
    bool strict;
    size_t line;
    size_t n_statements;
    size_t n_errors;
    struct timespec time1;
    struct timespec time2;
} global = { false,            // query_printed
             "",               // query
             VERBOSITY_NORMAL, // verbosity
             false,            // result_printed
             true,             // stop_at_error
             false,            // strict
             0,                // line
             0,                // n_statements
             0,                // n_errors
             { 0, 0 },         // time1
             { 0,  0} };       // time2

ostream& operator << (ostream& out, qc_parse_result_t x)
{
    switch (x)
    {
    case QC_QUERY_INVALID:
        out << "QC_QUERY_INVALID";
        break;

    case QC_QUERY_TOKENIZED:
        out << "QC_QUERY_TOKENIZED";
        break;

    case QC_QUERY_PARTIALLY_PARSED:
        out << "QC_QUERY_PARTIALLY_PARSED";
        break;

    case QC_QUERY_PARSED:
        out << "QC_QUERY_PARSED";
        break;

    default:
        out << "static_cast<c_parse_result_t>(" << (int)x << ")";
    }

    return out;
}

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length() + 1;
    size_t gwbuf_len = len + MYSQL_HEADER_LEN + 1;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, s.c_str(), s.length() + 1);

    return gwbuf;
}

QUERY_CLASSIFIER* load_classifier(const char* name)
{
    bool loaded = false;
    size_t len = strlen(name);
    char libdir[len + 1];

    sprintf(libdir, "../%s", name);

    set_libdir(strdup(libdir));

    QUERY_CLASSIFIER *pClassifier = qc_load(name);

    if (!pClassifier)
    {
        cerr << "error: Could not load classifier " << name << "." << endl;
    }

    return pClassifier;
}

QUERY_CLASSIFIER* get_classifier(const char* zName, const char* zArgs)
{
    QUERY_CLASSIFIER* pClassifier = load_classifier(zName);

    if (pClassifier)
    {
        if (!pClassifier->qc_init(zArgs))
        {
            cerr << "error: Could not init classifier " << zName << "." << endl;
            qc_unload(pClassifier);
            pClassifier = 0;
        }
    }

    return pClassifier;
}

void put_classifier(QUERY_CLASSIFIER* pClassifier)
{
    if (pClassifier)
    {
        pClassifier->qc_end();
        qc_unload(pClassifier);
    }
}

bool get_classifiers(const char* zName1, const char* zArgs1, QUERY_CLASSIFIER** ppClassifier1,
                     const char* zName2, const char* zArgs2, QUERY_CLASSIFIER** ppClassifier2)
{
    bool rc = false;

    QUERY_CLASSIFIER* pClassifier1 = get_classifier(zName1, zArgs1);

    if (pClassifier1)
    {
        QUERY_CLASSIFIER* pClassifier2 = get_classifier(zName2, zArgs2);

        if (pClassifier2)
        {
            *ppClassifier1 = pClassifier1;
            *ppClassifier2 = pClassifier2;
            rc = true;
        }
        else
        {
            put_classifier(pClassifier1);
        }
    }

    return rc;
}

void put_classifiers(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2)
{
    put_classifier(pClassifier1);
    put_classifier(pClassifier2);
}

void report_query()
{
    cout << "(" << global.line << "): " << global.query << endl;
    global.query_printed = true;
}

void report(bool success, const string& s)
{
    if (success)
    {
        if (global.verbosity >= VERBOSITY_NORMAL)
        {
            if (global.verbosity >= VERBOSITY_EXTENDED)
            {
                if (!global.query_printed)
                {
                    report_query();
                }

                if (global.verbosity >= VERBOSITY_MAX)
                {
                    cout << s << endl;
                    global.result_printed = true;
                }
            }
        }
    }
    else
    {
        if (global.verbosity >= VERBOSITY_NORMAL)
        {
            if (!global.query_printed)
            {
                report_query();
            }

            cout << s << endl;
            global.result_printed = true;
        }
    }
}

static timespec timespec_subtract(const timespec& later, const timespec& earlier)
{
    timespec result = { 0, 0 };

    ss_dassert((later.tv_sec > earlier.tv_sec) ||
               ((later.tv_sec == earlier.tv_sec) && (later.tv_nsec > earlier.tv_nsec)));

    if (later.tv_nsec >= earlier.tv_nsec)
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec;
        result.tv_nsec = later.tv_nsec - earlier.tv_nsec;
    }
    else
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec - 1;
        result.tv_nsec = 1000000000 + later.tv_nsec - earlier.tv_nsec;
    }

    return result;
}

static void update_time(timespec* pResult, timespec& start, timespec& finish)
{
    timespec difference = timespec_subtract(finish, start);

    long nanosecs = pResult->tv_nsec + difference.tv_nsec;

    if (nanosecs > 1000000000)
    {
        pResult->tv_sec += 1;
        pResult->tv_nsec += (nanosecs - 1000000000);
    }
    else
    {
        pResult->tv_nsec = nanosecs;
    }

    pResult->tv_sec += difference.tv_sec;
}

bool compare_parse(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                   QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_parse                 : ";

    struct timespec start;
    struct timespec finish;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    qc_parse_result_t rv1 = pClassifier1->qc_parse(pCopy1);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time1, start, finish);

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    qc_parse_result_t rv2 = pClassifier2->qc_parse(pCopy2);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time2, start, finish);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << rv1;
        success = true;
    }
    else
    {
        if (global.strict)
        {
            ss << "ERR: ";
        }
        else
        {
            ss << "INF: ";
            success = true;
        }

        ss << rv1 << " != " << rv2;
    }

    report(success, ss.str());

    return success;
}

bool compare_get_type(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                      QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_type              : ";

    uint32_t rv1 = pClassifier1->qc_get_type(pCopy1);
    uint32_t rv2 = pClassifier2->qc_get_type(pCopy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        char* types = qc_types_to_string(rv1);
        ss << "Ok : " << types;
        free(types);
        success = true;
    }
    else
    {
        uint32_t rv1b = rv1;

        if (rv1b & QUERY_TYPE_WRITE)
        {
            rv1b &= ~(uint32_t)QUERY_TYPE_READ;
        }

        uint32_t rv2b = rv2;

        if (rv2b & QUERY_TYPE_WRITE)
        {
            rv2b &= ~(uint32_t)QUERY_TYPE_READ;
        }

        if (rv1b & QUERY_TYPE_READ)
        {
            rv1b &= ~(uint32_t)QUERY_TYPE_LOCAL_READ;
        }

        if (rv2b & QUERY_TYPE_READ)
        {
            rv2b &= ~(uint32_t)QUERY_TYPE_LOCAL_READ;
        }

        char* types1 = qc_types_to_string(rv1);
        char* types2 = qc_types_to_string(rv2);

        if (rv1b == rv2b)
        {
            ss << "WRN: " << types1 << " != " << types2;
            success = true;
        }
        else
        {
            ss << "ERR: " << types1 << " != " << types2;
        }
        free(types1);
        free(types2);
    }

    report(success, ss.str());

    return success;
}

bool compare_get_operation(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                           QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_operation         : ";

    qc_query_op_t rv1 = pClassifier1->qc_get_operation(pCopy1);
    qc_query_op_t rv2 = pClassifier2->qc_get_operation(pCopy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << qc_op_to_string(rv1);
        success = true;
    }
    else
    {
        ss << "ERR: " << qc_op_to_string(rv1) << " != " << qc_op_to_string(rv2);
    }

    report(success, ss.str());

    return success;
}

bool compare_get_created_table_name(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                    QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_created_table_name: ";

    char* rv1 = pClassifier1->qc_get_created_table_name(pCopy1);
    char* rv2 = pClassifier2->qc_get_created_table_name(pCopy2);

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (rv1 && rv2 && (strcmp(rv1, rv2) == 0)))
    {
        ss << "Ok : " << (rv1 ? rv1 : "NULL");
        success = true;
    }
    else
    {
        ss << "ERR: " << (rv1 ? rv1 : "NULL") << " != " << (rv2 ? rv2 : "NULL");
    }

    report(success, ss.str());

    free(rv1);
    free(rv2);

    return success;
}

bool compare_is_drop_table_query(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                 QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_is_drop_table_query   : ";

    bool rv1 = pClassifier1->qc_is_drop_table_query(pCopy1);
    bool rv2 = pClassifier2->qc_is_drop_table_query(pCopy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << rv1;
        success = true;
    }
    else
    {
        ss << "ERR: " << rv1 << " != " << rv2;
    }

    report(success, ss.str());

    return success;
}

bool compare_is_real_query(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                           QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_is_real_query         : ";

    bool rv1 = pClassifier1->qc_is_real_query(pCopy1);
    bool rv2 = pClassifier2->qc_is_real_query(pCopy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << rv1;
        success = true;
    }
    else
    {
        ss << "ERR: " << rv1 << " != " << rv2;
    }

    report(success, ss.str());

    return success;
}

bool compare_strings(const char* const* strings1, const char* const* strings2, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const char* s1 = strings1[i];
        const char* s2 = strings2[i];

        if (strcmp(s1, s2) != 0)
        {
            return false;
        }
    }

    return true;
}

void free_strings(char** strings, int n)
{
    if (strings)
    {
        for (int i = 0; i < n; ++i)
        {
            free(strings[i]);
        }

        free(strings);
    }
}

void print_names(ostream& out, const char* const* strings, int n)
{
    if (strings)
    {
        for (int i = 0; i < n; ++i)
        {
            out << strings[i];
            if (i < n - 1)
            {
                out << ", ";
            }
        }
    }
    else
    {
        out << "NULL";
    }
}

bool compare_get_table_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                             QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2,
                             bool full)
{
    bool success = false;
    const char* HEADING;

    if (full)
    {
        HEADING = "qc_get_table_names(full) : ";
    }
    else
    {
        HEADING = "qc_get_table_names       : ";
    }

    int n1 = 0;
    int n2 = 0;

    char** rv1 = pClassifier1->qc_get_table_names(pCopy1, &n1, full);
    char** rv2 = pClassifier2->qc_get_table_names(pCopy2, &n2, full);

    // The order need not be the same, so let's compare a set.
    std::set<string> names1;
    std::set<string> names2;

    if (rv1)
    {
        std::copy(rv1, rv1 + n1, inserter(names1, names1.begin()));
    }

    if (rv2)
    {
        std::copy(rv2, rv2 + n2, inserter(names2, names2.begin()));
    }

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (names1 == names2))
    {
        if (n1 == n2)
        {
            ss << "Ok : ";
            print_names(ss, rv1, n1);
        }
        else
        {
            ss << "WRN: ";
            print_names(ss, rv1, n1);
            ss << " != ";
            print_names(ss, rv2, n2);
        }

        success = true;
    }
    else
    {
        ss << "ERR: ";
        print_names(ss, rv1, n1);
        ss << " != ";
        print_names(ss, rv2, n2);
    }

    report(success, ss.str());

    free_strings(rv1, n1);
    free_strings(rv2, n2);

    return success;
}

bool compare_query_has_clause(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                              QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_query_has_clause      : ";

    bool rv1 = pClassifier1->qc_query_has_clause(pCopy1);
    bool rv2 = pClassifier2->qc_query_has_clause(pCopy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << rv1;
        success = true;
    }
    else
    {
        ss << "ERR: " << rv1 << " != " << rv2;
    }

    report(success, ss.str());

    return success;
}

void add_fields(std::set<string>& m, const char* fields)
{
    const char* begin = fields;
    const char* end = begin;

    // As long as we have not reached the end.
    while (*end != 0)
    {
        // Walk over everything but whitespace.
        while (!isspace(*end) && (*end != 0))
        {
            ++end;
        }

        // Insert whatever we found.
        m.insert(string(begin, end - begin));

        // Walk over all whitespace.
        while (isspace(*end) && (*end != 0))
        {
            ++end;
        }

        // Move begin to the next non-whitespace character.
        begin = end;
    }

    if (begin != end)
    {
        m.insert(string(begin, end - begin));
    }
}

ostream& operator << (ostream& o, const std::set<string>& s)
{
    std::set<string>::iterator i = s.begin();

    while (i != s.end())
    {
        o << *i;

        ++i;
        if (i != s.end())
        {
            o << " ";
        }
    }

    return o;
}

bool compare_get_affected_fields(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                 QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_affected_fields   : ";

    char* rv1 = pClassifier1->qc_get_affected_fields(pCopy1);
    char* rv2 = pClassifier2->qc_get_affected_fields(pCopy2);

    std::set<string> fields1;
    std::set<string> fields2;

    if (rv1)
    {
        add_fields(fields1, rv1);
    }

    if (rv2)
    {
        add_fields(fields2, rv2);
    }

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (rv1 && rv2 && (fields1 == fields2)))
    {
        ss << "Ok : " << fields1;
        success = true;
    }
    else
    {
        ss << "ERR: ";
        if (rv1)
        {
            ss << fields1;
        }
        else
        {
            ss << "NULL";
        }

        ss << " != ";

        if (rv2)
        {
            ss << fields2;
        }
        else
        {
            ss << "NULL";
        }
    }

    report(success, ss.str());

    free(rv1);
    free(rv2);

    return success;
}

bool compare_get_database_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_database_names    : ";

    int n1 = 0;
    int n2 = 0;

    char** rv1 = pClassifier1->qc_get_database_names(pCopy1, &n1);
    char** rv2 = pClassifier2->qc_get_database_names(pCopy2, &n2);

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || ((n1 == n2) && compare_strings(rv1, rv2, n1)))
    {
        ss << "Ok : ";
        print_names(ss, rv1, n1);
        success = true;
    }
    else
    {
        ss << "ERR: ";
        print_names(ss, rv1, n1);
        ss << " != ";
        print_names(ss, rv2, n2);
    }

    report(success, ss.str());

    free_strings(rv1, n1);
    free_strings(rv2, n2);

    return success;
}

bool compare(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, const string& s)
{
    GWBUF* pCopy1 = create_gwbuf(s);
    GWBUF* pCopy2 = create_gwbuf(s);

    int errors = 0;

    errors += !compare_parse(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_type(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_operation(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_created_table_name(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_is_drop_table_query(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_is_real_query(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_table_names(pClassifier1, pCopy1, pClassifier2, pCopy2, false);
    errors += !compare_get_table_names(pClassifier1, pCopy1, pClassifier2, pCopy2, true);
    errors += !compare_query_has_clause(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_affected_fields(pClassifier1, pCopy1, pClassifier2, pCopy2);
    errors += !compare_get_database_names(pClassifier1, pCopy1, pClassifier2, pCopy2);

    gwbuf_free(pCopy1);
    gwbuf_free(pCopy2);

    if (global.result_printed)
    {
        cout << endl;
    }

    return errors == 0;
}

inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
}

inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
}

static void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

enum skip_action_t
{
    SKIP_NOTHING,        // Skip nothing.
    SKIP_BLOCK,          // Skip until the end of next { ... }
    SKIP_DELIMITER,      // Skip the new delimiter.
    SKIP_LINE,           // Skip current line.
    SKIP_NEXT_STATEMENT, // Skip statement starting on line following this line.
    SKIP_STATEMENT,      // Skip statment starting on this line.
    SKIP_TERMINATE,      // Cannot handle this, terminate.
};

typedef std::map<std::string, skip_action_t> KeywordActionMapping;

static KeywordActionMapping mtl_keywords;

void init_keywords()
{
    struct Keyword
    {
        const char* z_keyword;
        skip_action_t action;
    };

    static const Keyword KEYWORDS[] =
    {
        { "append_file",                SKIP_LINE },
        { "cat_file",                   SKIP_LINE },
        { "change_user",                SKIP_LINE },
        { "character_set",              SKIP_LINE },
        { "chmod",                      SKIP_LINE },
        { "connect",                    SKIP_LINE },
        { "connection",                 SKIP_LINE },
        { "copy_file",                  SKIP_LINE },
        { "dec",                        SKIP_LINE },
        { "delimiter",                  SKIP_DELIMITER },
        { "die",                        SKIP_LINE },
        { "diff_files",                 SKIP_LINE },
        { "dirty_close",                SKIP_LINE },
        { "disable_abort_on_error",     SKIP_LINE },
        { "disable_connect_log",        SKIP_LINE },
        { "disable_info",               SKIP_LINE },
        { "disable_metadata",           SKIP_LINE },
        { "disable_parsing",            SKIP_LINE },
        { "disable_ps_protocol",        SKIP_LINE },
        { "disable_query_log",          SKIP_LINE },
        { "disable_reconnect",          SKIP_LINE },
        { "disable_result_log",         SKIP_LINE },
        { "disable_rpl_parse",          SKIP_LINE },
        { "disable_session_track_info", SKIP_LINE },
        { "disable_warnings",           SKIP_LINE },
        { "disconnect",                 SKIP_LINE },
        { "echo",                       SKIP_LINE },
        { "enable_abort_on_error",      SKIP_LINE },
        { "enable_connect_log",         SKIP_LINE },
        { "enable_info",                SKIP_LINE },
        { "enable_metadata",            SKIP_LINE },
        { "enable_parsing",             SKIP_LINE },
        { "enable_ps_protocol",         SKIP_LINE },
        { "enable_query_log",           SKIP_LINE },
        { "enable_reconnect",           SKIP_LINE },
        { "enable_result_log",          SKIP_LINE },
        { "enable_rpl_parse",           SKIP_LINE },
        { "enable_session_track_info",  SKIP_LINE },
        { "enable_warnings",            SKIP_LINE },
        { "end_timer",                  SKIP_LINE },
        { "error",                      SKIP_NEXT_STATEMENT },
        { "eval",                       SKIP_STATEMENT },
        { "exec",                       SKIP_LINE },
        { "exit",                       SKIP_LINE },
        { "file_exists",                SKIP_LINE },
        { "horizontal_results",         SKIP_LINE },
        { "if",                         SKIP_BLOCK },
        { "inc",                        SKIP_LINE },
        { "let",                        SKIP_LINE },
        { "let",                        SKIP_LINE },
        { "list_files",                 SKIP_LINE },
        { "list_files_append_file",     SKIP_LINE },
        { "list_files_write_file",      SKIP_LINE },
        { "lowercase_result",           SKIP_LINE },
        { "mkdir",                      SKIP_LINE },
        { "move_file",                  SKIP_LINE },
        { "output",                     SKIP_LINE },
        { "perl",                       SKIP_TERMINATE },
        { "ping",                       SKIP_LINE },
        { "print",                      SKIP_LINE },
        { "query",                      SKIP_LINE },
        { "query_get_value",            SKIP_LINE },
        { "query_horizontal",           SKIP_LINE },
        { "query_vertical",             SKIP_LINE },
        { "real_sleep",                 SKIP_LINE },
        { "reap",                       SKIP_LINE },
        { "remove_file",                SKIP_LINE },
        { "remove_files_wildcard",      SKIP_LINE },
        { "replace_column",             SKIP_LINE },
        { "replace_regex",              SKIP_LINE },
        { "replace_result",             SKIP_LINE },
        { "require",                    SKIP_LINE },
        { "reset_connection",           SKIP_LINE },
        { "result",                     SKIP_LINE },
        { "result_format",              SKIP_LINE },
        { "rmdir",                      SKIP_LINE },
        { "same_master_pos",            SKIP_LINE },
        { "send",                       SKIP_LINE },
        { "send_eval",                  SKIP_LINE },
        { "send_quit",                  SKIP_LINE },
        { "send_shutdown",              SKIP_LINE },
        { "skip",                       SKIP_LINE },
        { "sleep",                      SKIP_LINE },
        { "sorted_result",              SKIP_LINE },
        { "source",                     SKIP_LINE },
        { "start_timer",                SKIP_LINE },
        { "sync_slave_with_master",     SKIP_LINE },
        { "sync_with_master",           SKIP_LINE },
        { "system",                     SKIP_LINE },
        { "vertical_results",           SKIP_LINE },
        { "while",                      SKIP_BLOCK },
        { "write_file",                 SKIP_LINE },
    };

    const size_t N_KEYWORDS = sizeof(KEYWORDS)/sizeof(KEYWORDS[0]);

    for (size_t i = 0; i < N_KEYWORDS; ++i)
    {
        mtl_keywords[KEYWORDS[i].z_keyword] = KEYWORDS[i].action;
    }
}

skip_action_t get_action(const string& keyword)
{
    skip_action_t action = SKIP_NOTHING;

    string key(keyword);

    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    KeywordActionMapping::iterator i = mtl_keywords.find(key);

    if (i != mtl_keywords.end())
    {
        action = i->second;
    }

    return action;
}

void skip_block(istream& in)
{
    int c;

    // Find first '{'
    while (in && ((c = in.get()) != '{'))
    {
        if (c == '\n')
        {
            ++global.line;
        }
    }

    int n = 1;

    while ((n > 0) && in)
    {
        c = in.get();

        switch (c)
        {
        case '{':
            ++n;
            break;

        case '}':
            --n;
            break;

        case '\n':
            ++global.line;
            break;

        default:
            ;
        }
    }
}


int run(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, istream& in)
{
    bool stop = false; // Whether we should exit.
    bool skip = false; // Whether next statement should be skipped.
    char delimiter = ';';
    string query;

    while (!stop && std::getline(in, query))
    {
        trim(query);

        global.line++;

        if (!query.empty() && (query.at(0) != '#'))
        {
            if (!skip)
            {
                if (query.substr(0, 2) == "--")
                {
                    query = query.substr(2);
                    trim(query);
                }

                string::iterator i = std::find_if(query.begin(), query.end(),
                                                  std::ptr_fun<int,int>(std::isspace));
                string keyword = query.substr(0, i - query.begin());

                skip_action_t action = get_action(keyword);

                switch (action)
                {
                case SKIP_NOTHING:
                    break;

                case SKIP_BLOCK:
                    skip_block(in);
                    continue;

                case SKIP_DELIMITER:
                    query = query.substr(i - query.begin());
                    trim(query);
                    if (query.length() > 0)
                    {
                        delimiter = query.at(0);
                    }
                    continue;

                case SKIP_LINE:
                    continue;

                case SKIP_NEXT_STATEMENT:
                    skip = true;
                    continue;

                case SKIP_STATEMENT:
                    skip = true;
                    break;

                case SKIP_TERMINATE:
                    cout << "error: Cannot handle line " << global.line
                         << ", terminating: " << query << endl;
                    stop = true;
                    break;
                }
            }

            global.query += query;

            char c = query.at(query.length() - 1);

            if (c == delimiter)
            {
                if (c != ';')
                {
                    // If the delimiter was something else but ';' we need to
                    // remove that before giving the query to the classifiers.
                    global.query.erase(global.query.length() - 1);
                }

                if (!skip)
                {
                    global.query_printed = false;
                    global.result_printed = false;

                    ++global.n_statements;

                    if (global.verbosity >= VERBOSITY_EXTENDED)
                    {
                        // In case the execution crashes, we want the query printed.
                        report_query();
                    }

                    bool success = compare(pClassifier1, pClassifier2, global.query);

                    if (!success)
                    {
                        ++global.n_errors;

                        if (global.stop_at_error)
                        {
                            stop = true;
                        }
                    }
                }
                else
                {
                    skip = false;
                }

                global.query.clear();
            }
            else
            {
                global.query += " ";
            }
        }
        else if (query.substr(0, 7) == "--error")
        {
            // Next statement is supposed to fail, no need to check.
            skip = true;
        }
    }

    return global.n_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, const string& statement)
{
    global.query = statement;

    ++global.n_statements;

    if (global.verbosity >= VERBOSITY_EXTENDED)
    {
        // In case the execution crashes, we want the query printed.
        report_query();
    }

    if (!compare(pClassifier1, pClassifier2, global.query))
    {
        ++global.n_errors;
    }

    return global.n_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_SUCCESS;

    const char* zClassifier1 = "qc_mysqlembedded";
    const char* zClassifier2 = "qc_sqlite";
    const char* zClassifier1Args = NULL;
    const char* zClassifier2Args = "log_unrecognized_statements=1";
    const char* zStatement = NULL;

    size_t rounds = 1;
    int v = VERBOSITY_NORMAL;
    int c;
    while ((c = getopt(argc, argv, "r:d1:2:v:A:B:s:S")) != -1)
    {
        switch (c)
        {
        case 'r':
            rounds = atoi(optarg);
            break;

        case 'v':
            v = atoi(optarg);
            break;

        case '1':
            zClassifier1 = optarg;
            break;

        case '2':
            zClassifier2 = optarg;
            break;

        case 'A':
            zClassifier1Args = optarg;
            break;

        case 'B':
            zClassifier2Args = optarg;
            break;

        case 'd':
            global.stop_at_error = false;
            break;

        case 's':
            zStatement = optarg;
            break;

        case 'S':
            global.strict = true;
            break;

        default:
            rc = EXIT_FAILURE;
            break;
        };
    }

    if ((rc == EXIT_SUCCESS) && (v >= VERBOSITY_MIN && v <= VERBOSITY_MAX))
    {
        init_keywords();

        rc = EXIT_FAILURE;
        global.verbosity = static_cast<verbosity_t>(v);

        int n = argc - (optind - 1);

        if ((n == 1) || (n == 2))
        {
            set_datadir(strdup("/tmp"));
            set_langdir(strdup("."));
            set_process_datadir(strdup("/tmp"));

            if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
            {
                QUERY_CLASSIFIER* pClassifier1;
                QUERY_CLASSIFIER* pClassifier2;

                if (get_classifiers(zClassifier1, zClassifier1Args, &pClassifier1,
                                    zClassifier2, zClassifier2Args, &pClassifier2))
                {
                    size_t round = 0;
                    bool terminate = false;

                    do
                    {
                        ++round;

                        global.n_statements = 0;
                        global.n_errors = 0;
                        global.query_printed = false;
                        global.result_printed = false;

                        if (zStatement)
                        {
                            rc = run(pClassifier1, pClassifier2, zStatement);
                        }
                        else if (n == 1)
                        {
                            rc = run(pClassifier1, pClassifier2, cin);
                        }
                        else
                        {
                            ss_dassert(n == 2);

                            ifstream in(argv[argc - 1]);

                            if (in)
                            {
                                rc = run(pClassifier1, pClassifier2, in);
                            }
                            else
                            {
                                terminate = true;
                                cerr << "error: Could not open " << argv[argc - 1] << "." << endl;
                            }
                        }

                        cout << "\n"
                             << "Statements: " << global.n_statements << endl
                             << "Errors    : " << global.n_errors << endl;

                        if (!terminate && ((rounds == 0) || (round < rounds)))
                        {
                            cout << endl;
                        }
                    }
                    while (!terminate && ((rounds == 0) || (round < rounds)));

                    put_classifiers(pClassifier1, pClassifier2);

                    cout << "\n";
                    cout << "1st classifier: "
                         << global.time1.tv_sec << "."
                         << global.time1.tv_nsec
                         << endl;
                    cout << "2nd classifier: "
                         << global.time2.tv_sec << "."
                         << global.time2.tv_nsec
                         << endl;
                }

                mxs_log_finish();
            }
            else
            {
                cerr << "error: Could not initialize log." << endl;
            }
        }
        else
        {
            cout << USAGE << endl;
        }
    }
    else
    {
        cout << USAGE << endl;
    }

    return rc;
}
