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

#include <gwdirs.h>
#include <maxscale/alloc.h>
#include <gw.h>

/**
 * Set the configuration file directory
 * @param str Path to directory
 */
void set_configdir(char* str)
{
    MXS_FREE(configdir);
    clean_up_pathname(str);
    configdir = str;
}

/**
 * Set the log file directory
 * @param str Path to directory
 */
void set_logdir(char* str)
{
    MXS_FREE(logdir);
    clean_up_pathname(str);
    logdir = str;
}

/**
 * Set the language file directory
 * @param str Path to directory
 */
void set_langdir(char* str)
{
    MXS_FREE(langdir);
    clean_up_pathname(str);
    langdir = str;
}

/**
 * Set the PID file directory
 * @param str Path to directory
 */
void set_piddir(char* str)
{
    MXS_FREE(piddir);
    clean_up_pathname(str);
    piddir = str;
}

/**
 * Set the cache directory
 * @param str Path to directory
 */
void set_cachedir(char* param)
{
    MXS_FREE(cachedir);
    clean_up_pathname(param);
    cachedir = param;
}

/**
 * Set the data directory
 * @param str Path to directory
 */
void set_datadir(char* param)
{
    MXS_FREE(maxscaledatadir);
    clean_up_pathname(param);
    maxscaledatadir = param;
}

/**
 * Set the data directory
 * @param str Path to directory
 */
void set_process_datadir(char* param)
{
    MXS_FREE(processdatadir);
    clean_up_pathname(param);
    processdatadir = param;
}

/**
 * Set the library directory. Modules will be loaded from here.
 * @param str Path to directory
 */
void set_libdir(char* param)
{
    MXS_FREE(libdir);
    clean_up_pathname(param);
    libdir = param;
}

/**
 * Set the executable directory. Internal processes will look for executables
 * from here.
 * @param str Path to directory
 */
void set_execdir(char* param)
{
    MXS_FREE(execdir);
    clean_up_pathname(param);
    execdir = param;
}

/**
 * Get the directory with all the modules.
 * @return The module directory
 */
char* get_libdir()
{
    return libdir ? libdir : (char*) default_libdir;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_cachedir()
{
    return cachedir ? cachedir : (char*) default_cachedir;
}

/**
 * Get the MaxScale data directory
 * @return The path to the data directory
 */
char* get_datadir()
{
    return maxscaledatadir ? maxscaledatadir : (char*) default_datadir;
}

/**
 * Get the process specific data directory
 * @return The path to the process specific directory
 */
char* get_process_datadir()
{
    return processdatadir ? processdatadir : (char*) default_datadir;
}

/**
 * Get the configuration file directory
 * @return The path to the configuration file directory
 */
char* get_configdir()
{
    return configdir ? configdir : (char*) default_configdir;
}

/**
 * Get the PID file directory which contains maxscale.pid
 * @return Path to the PID file directory
 */
char* get_piddir()
{
    return piddir ? piddir : (char*) default_piddir;
}

/**
 * Return the log file directory
 * @return Path to the log file directory
 */
char* get_logdir()
{
    return logdir ? logdir : (char*) default_logdir;
}

/**
 * Path to the directory which contains the errmsg.sys language file
 * @return Path to the language file directory
 */
char* get_langdir()
{
    return langdir ? langdir : (char*) default_langdir;
}

/**
 * Get the directory with the executables.
 * @return The executables directory
 */
char* get_execdir()
{
    return execdir ? execdir : (char*) default_execdir;
}
