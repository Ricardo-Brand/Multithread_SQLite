
#define SQLITE_ENABLE_NORMALIZE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
// #include <limits.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>
#include "sqlite3.h"
#include "sql-stmts.h"
#include <pthread.h>
// #include <sqlite3ext.h>

#define THREAD_COUNT 5

typedef struct StrList StrList;
struct StrList
{
    StrList *pNextStr; /* Next string literal in the list */
    char z[8];         /* Dequoted value for the string */
};

static const char *skip_spaces_and_comments(const char *ptr, const char *end)
{
    while (ptr < end)
    {
        switch (*ptr)
        {
        case ' ':
        case '\t':
        case '\n':
            ptr++;
            continue;
        case '-':
            if ((++ptr) == end || *ptr != '-')
                continue;
            // skip -- comments --
            ptr = memchr(ptr, '\n', (size_t)(end - ptr));
            break;
        case '/':
            if ((++ptr) == end || *ptr != '*')
                continue;
            if ((ptr += 2) >= end)
                return end;
            // skip /* comments */
            do
            {
                ptr = memchr(ptr, '/', (size_t)(end - ptr));
            } while (ptr != NULL && *((++ptr) - 2) != '*');
            break;
        default:
            return ptr;
        }
        if (ptr == NULL)
            break;
        ptr++;
    }
    return end;
}

static bool load_sql_file(sqlite3 *db)
{
    sqlite3_stmt *stmt;
    const char *sql;
    const char *tail = sql_create_table_cards;
    const char *end = tail + sql_create_table_cards_len - 1;
    size_t len;
    while (tail < end)
    {
        tail = skip_spaces_and_comments(tail, end);
        if (tail >= end)
            break;

        len = (size_t)(end - tail);
        stmt = NULL;
        if (sqlite3_prepare_v3(db, tail, len, SQLITE_PREPARE_NORMALIZE, &stmt, &tail) != SQLITE_OK)
        {
            fprintf(stderr, "sqlite3_prepare_v3 failed\n");
            return false;
        }

        sql = sqlite3_expanded_sql(stmt);
        if (sql != NULL)
        {
            printf("-------------\n%s\n-------------\n", sql);
            sqlite3_free((void *)sql);
        }

        if (sqlite3_finalize(stmt) != SQLITE_OK)
        {
            fprintf(stderr, "sqlite3_finalize failed\n");
            return false;
        }
    }
    return true;
}

// static bool insert_stmt(sqlite3_stmt *stmt, const char *name, int64_t code)
// {
//     const char *sql = NULL;
//     if (sqlite3_clear_bindings(stmt) != SQLITE_OK)
//     {
//         fprintf(stderr, "sqlite3_clear_bindings failed\n");
//         goto err;
//     }
//     if (sqlite3_reset(stmt) != SQLITE_OK)
//     {
//         fprintf(stderr, "sqlite3_reset failed\n");
//         goto err;
//     }
//     if (sqlite3_bind_text(stmt, 1, name, strlen(name), NULL) != SQLITE_OK)
//     {
//         fprintf(stderr, "sqlite3_bind_text failed\n");
//         goto err;
//     }
//     if (sqlite3_bind_int64(stmt, 2, code) != SQLITE_OK)
//     {
//         fprintf(stderr, "sqlite3_bind_int64 failed\n");
//         goto err;
//     }
//     // if ((sql = sqlite3_expanded_sql(stmt)) != NULL) {
//     //     printf("%s\n", sql);
//     //     sqlite3_free((void*)sql);
//     // }
//     if ((sql = sqlite3_normalized_sql(stmt)) != NULL)
//     {
//         printf("%s\n", sql);
//         // sqlite3_free((void*)sql);
//     }
//     if (sqlite3_step(stmt) != SQLITE_DONE)
//     {
//         fprintf(stderr, "sqlite3_step failed\n");
//         goto err;
//     }
//     return true;
// err:
//     sqlite3_clear_bindings(stmt);
//     sqlite3_reset(stmt);
//     return false;
// }

static void *run(void *arg){
    const char *storage = (const char *)arg;
    pthread_t thread = pthread_self();
    printf("thread: %p\nstorage: %s\n", thread, storage);


    return NULL;
}

static bool start_thread(const char *storage){
    pthread_t threads[THREAD_COUNT];
    for(int i = 0; i < THREAD_COUNT; i++){
        pthread_create(&threads[i], NULL, run, (void *)storage);
    }
    printf("Threads criadas com sucesso\n");

    for(int i = 0; i < THREAD_COUNT; i++){
        pthread_join(threads[i], NULL);
    }
    printf("Threads finalizadas\n");

    return true;
}

static bool start(const char *str)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    
    // const char *str = PROJ_BASEDIR "/storage.sqlite";
    char *errmsg;
    // char temp[1024];
    int status = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_WAL | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW;

    db = NULL;
    errmsg = NULL;
    stmt = NULL;
    if (sqlite3_config(SQLITE_CONFIG_MULTITHREAD, 1) != SQLITE_OK)
    {
        fprintf(stderr, "sqlite3_config failed\n");
        return 1;
    }

    if (sqlite3_open_v2(str, &db, status, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "sqlite3_open failed\n");
        return 1;
    }

    if (sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "PRAGMA foreign_keys=ON; failed: %s\n", errmsg);
        goto end;
    }

    if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "PRAGMA journal_mode=WAL; failed: %s\n", errmsg);
        goto end;
    }

    if (!load_sql_file(db))
    {
        fprintf(stderr, "load_sql_file failed\n");
        goto end;
    }

    // if (sqlite3_step(stmt) != SQLITE_DONE) {
    //     fprintf(stderr, "sqlite3_step failed\n");
    //     goto end;
    // }

    // if (sqlite3_finalize(stmt) != SQLITE_OK) {
    //     fprintf(stderr, "sqlite3_finalize failed\n");
    //     stmt = NULL;
    //     goto end;
    // }

    printf("TABLE CREATED!\n");
    stmt = NULL;
    // status = sqlite3_prepare_v3(db, "INSERT INTO cards (name, code) VALUES (?1, ?2);", -1, SQLITE_PREPARE_PERSISTENT, &stmt, NULL);
    // if (status != SQLITE_OK)
    // {
    //     fprintf(stderr, "sqlite3_prepare_v3 failed: %s\n", sqlite3_errmsg(db));
    //     goto end;
    // }

    // if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &errmsg) != SQLITE_OK)
    // {
    //     fprintf(stderr, "BEGIN IMMEDIATE failed: %s\n", str);
    //     goto end;
    // }

    // for (int i = 0; i < 10; i++)
    // {
    //     snprintf(temp, sizeof(temp), "user (%d)", i + 1);
    //     if (!insert_stmt(stmt, temp, (int64_t)(i + 1000)))
    //     {
    //         fprintf(stderr, "insert_stmt failed, name: %s, code: %d\n", temp, i + 1000);
    //         if (sqlite3_exec(db, "ROLLBACK", NULL, NULL, &errmsg) != SQLITE_OK)
    //         {
    //             fprintf(stderr, "ROLLBACK failed: %s\n", str);
    //             goto end;
    //         }
    //         goto end;
    //     }
    // }

    // if (sqlite3_exec(db, "COMMIT", NULL, NULL, &errmsg) != SQLITE_OK)
    // {
    //     fprintf(stderr, "COMMIT failed: %s\n", errmsg);
    //     goto end;
    // }

    sqlite3_close(db);
    start_thread(str);
    return true;

end:
    if (stmt != NULL)
        sqlite3_finalize(stmt);

    return sqlite3_close(db) == SQLITE_OK;
}

int main(void)
{
    int status;
    int i = 0;
    const char *opt = NULL;
    char cwd[PATH_MAX];
    memset((void *)cwd, 0, sizeof(cwd));
    if (getcwd(cwd, sizeof(cwd) - 1) != NULL)
        printf("Current working dir: %s\n", cwd);
    else
    {
        perror("getcwd() error");
        return 1;
    }
    if (strncat(cwd, "/storage.sqlite", sizeof(cwd) - 1) == NULL) {
        perror("strncat() error");
        return 1;
    }

    while ((opt = sqlite3_compileoption_get(i++)) != NULL)
    {
        printf("%s\n", opt);
    }

    printf("Starting...!\n");
    status = start((const char *)cwd);
    printf("DONE!!\n");
    return status;
}
