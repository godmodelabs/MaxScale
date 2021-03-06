#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl.
#
# Change Date: 2019-07-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

# Check that all links to Markdown format files point to existing local files

homedir=$(pwd)

find . -name '*.md'|while read file
do
    cd "$(dirname "$file")"
    grep -o '\[.*\]([^#].*[.]md)' "$(basename "$file")"| sed -e 's/\[.*\](\(.*\))/\1/'|while read i
    do
        if [ ! -f "$i" ]
        then
            echo "Link $i in $file is not correct!"
        fi
    done
    cd "$homedir"
done
