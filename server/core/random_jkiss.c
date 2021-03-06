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
 * @file random_jkiss.c  -  Random number generator for the MariaDB Corporation MaxScale
 *
 * See http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf for discussion of random
 * number generators (RNGs).
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 26/08/15     Martin Brampton Initial implementation
 *
 * @endverbatim
 */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <spinlock.h>
#include <random_jkiss.h>

/* Public domain code for JKISS RNG - Comment header added */

/* If possible, the seed variables will be set from /dev/urandom but
 * should that fail, these arbitrary numbers will be used as a last resort.
 */
static unsigned int x = 123456789, y = 987654321, z = 43219876, c = 6543217; /* Seed variables */
static bool init = false;

static SPINLOCK random_jkiss_spinlock = SPINLOCK_INIT;

static unsigned int random_jkiss_devrand(void);
static void random_init_jkiss(void);

/***
 *
 * Return a pseudo-random number that satisfies major tests for random sequences
 *
 * @return  uint    Random number
 *
 */
unsigned int
random_jkiss(void)
{
    unsigned long long t;
    unsigned int result;

    spinlock_acquire(&random_jkiss_spinlock);
    if (!init)
    {
        /* Must set init first because initialisation calls this function */
        init = true;
        spinlock_release(&random_jkiss_spinlock);
        random_init_jkiss();
        spinlock_acquire(&random_jkiss_spinlock);
    }
    x = 314527869 * x + 1234567;
    y ^= y << 5;
    y ^= y >> 7;
    y ^= y << 22;
    t = 4294584393ULL * z + c;
    c = t >> 32;
    z = t;
    result = x + y + z;
    spinlock_release(&random_jkiss_spinlock);
    return result;
}

/* Own code adapted from http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf */

/***
 *
 * Obtain a seed random number from /dev/urandom if available.
 *
 * @return  uint    Random number
 *
 */
static unsigned int
random_jkiss_devrand(void)
{
    int fn;
    unsigned int r;

    if ((fn = open("/dev/urandom", O_RDONLY)) == -1)
    {
        return 0;
    }

    if (read(fn, &r, sizeof(r)) != sizeof(r))
    {
        r = 0;
    }
    close(fn);
    return r;
}

/***
 *
 * Initialise the generator using /dev/urandom if available, and warm up
 * with 1000 iterations
 *
 */
static void
random_init_jkiss(void)
{
    int newrand, i;

    spinlock_acquire(&random_jkiss_spinlock);
    if ((newrand = random_jkiss_devrand()) != 0)
    {
        x = newrand;
    }

    if ((newrand = random_jkiss_devrand()) != 0)
    {
        y = newrand;
    }

    if ((newrand = random_jkiss_devrand()) != 0)
    {
        z = newrand;
    }

    if ((newrand = random_jkiss_devrand()) != 0)
    {
        c = newrand % 698769068 + 1; /* Should be less than 698769069 */
    }
    spinlock_release(&random_jkiss_spinlock);

    /* "Warm up" our random number generator */
    for (i = 0; i < 100; i++)
    {
        random_jkiss();
    }
}
