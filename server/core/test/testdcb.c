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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 05-09-2014   Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <listener.h>
#include <dcb.h>

/**
 * test1    Allocate a dcb and do lots of other things
 *
  */
static int
test1()
{
    DCB   *dcb, *extra, *clone;
    int     size = 100;
    int     bite1 = 35;
    int     bite2 = 60;
    int     bite3 = 10;
    int     buflen;
    SERV_LISTENER dummy;
    /* Single buffer tests */
    ss_dfprintf(stderr,
                "testdcb : creating buffer with type DCB_ROLE_SERVICE_LISTENER");
    dcb = dcb_alloc(DCB_ROLE_SERVICE_LISTENER, &dummy);
    printDCB(dcb);
    ss_info_dassert(dcb_isvalid(dcb), "New DCB must be valid");
    ss_dfprintf(stderr, "\t..done\nAllocated dcb.");
    clone = dcb_clone(dcb);
    ss_dfprintf(stderr, "\t..done\nCloned dcb");
    printAllDCBs();
    ss_info_dassert(true, "Something is true");
    ss_dfprintf(stderr, "\t..done\n");
    dcb_close(dcb);
    ss_dfprintf(stderr, "Freed original dcb");
    ss_info_dassert(!dcb_isvalid(dcb), "Freed DCB must not be valid");
    ss_dfprintf(stderr, "\t..done\nMake clone DCB a zombie");
    clone->state = DCB_STATE_NOPOLLING;
    dcb_close(clone);
    ss_info_dassert(dcb_get_zombies() == clone, "Clone DCB must be start of zombie list now");
    ss_dfprintf(stderr, "\t..done\nProcess the zombies list");
    dcb_process_zombies(0);
    ss_dfprintf(stderr, "\t..done\nCheck clone no longer valid");
    ss_info_dassert(!dcb_isvalid(clone), "After zombie processing, clone DCB must not be valid");
    ss_dfprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();

    exit(result);
}



