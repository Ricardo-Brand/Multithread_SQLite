#!/usr/bin/env bash
set -eux

# Make sure we are in the same directory as this script.
cd "$(dirname "$0")"

# -L/home/ubuntu/projects/sqlite/workspace/lib \
# -lsqlite3 \
if [[ -f 'build/main'  ]]; then
    rm build/main
fi
if [[ -f 'build/main.o'  ]]; then
    rm build/main.o
fi

printf "/* Don't modify, this file is auto-generated */\n" > sql-stmts.h
printf "#ifndef _SQL_STMTS_H_\n#define _SQL_STMTS_H_\n\n" >> sql-stmts.h
printf "%s\0" "$(cat ./setup.sql)" | xxd -i --name sql_create_table_cards >> sql-stmts.h
printf "#endif /* _SQL_STMTS_H_ */\n" >> sql-stmts.h
sed -i "" 's/^unsigned/const/' sql-stmts.h

export CC=/usr/bin/gcc
# export CC=/usr/bin/clang

"${CC}" \
    -Wall \
    -O2 \
    -I/opt/homebrew/include \
    -pthread \
    -c \
    ./main.c \
    -o build/main.o

"${CC}" \
    -o build/main \
    build/main.o \
    -I/opt/homebrew/include \
    -L/opt/homebrew/lib \
    -lsqlite3 \
    -lpthread \
    -Wall \
    -O2 \
    -lm \
    -lz \

if [[ -f 'storage.db'  ]]; then
    rm storage.db
fi

./build/main
