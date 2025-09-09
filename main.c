
#define SQLITE_ENABLE_NORMALIZE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>
#include "sqlite3.h"
#include "sql-stmts.h"
#include <pthread.h>
#include <openssl/rand.h>

#define THREAD_COUNT 5
#define DB_FLAGS SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_WAL | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW
#define DB_ACCOUNTS 1000
#define DB_TRANSACTION_AMOUNT 1000
#define DB_VALUE_TRANSACTION 200

static pthread_mutex_t DB_MUTEX;

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
    sqlite3_stmt *stmt = NULL;
    const char *sql;
    const char *tail = sql_create_table_cards;
    const char *end = tail + sql_create_table_cards_len - 1;
    size_t len;
    int rc;
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
            fprintf(stderr, "tail: %s\n", tail);
            return false;
        }

        sql = sqlite3_expanded_sql(stmt);
        if (sql != NULL)
        {
            printf("-------------\n%s\n-------------\n", sql);
            sqlite3_free((void *)sql);
        }

        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE) {
            fprintf(stderr, "step failed: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        if (sqlite3_finalize(stmt) != SQLITE_OK)
        {
            fprintf(stderr, "sqlite3_finalize failed\n");
            return false;
        }
    }
    return true;
}

bool transaction(sqlite3 *db){
    sqlite3_stmt *stmt = NULL;
    int saldo[2], conta[2];
    unsigned int raw;

    saldo[0] = 0;
    saldo[1] = 0;

init:
    RAND_bytes((unsigned char*)&raw, sizeof(raw));
    conta[0] = (raw % DB_ACCOUNTS) + 1;

    RAND_bytes((unsigned char*)&raw, sizeof(raw));
    conta[1] = (raw % DB_ACCOUNTS) + 1;

    if(conta[0] == conta[1])
        goto init;

    if(sqlite3_prepare_v2(db, "SELECT * FROM conta WHERE id = ? OR id = ?;", -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha no 1º sqlite3_prepare_v2\n");
        return false;
    }

    if(sqlite3_bind_int(stmt, 1, conta[0]) != SQLITE_OK ||
        sqlite3_bind_int(stmt, 2, conta[1]) != SQLITE_OK){
        fprintf(stderr, "Falha no 1º sqlite3_bind_int\n");
        return false;
    }

    if(sqlite3_step(stmt) != SQLITE_ROW){
        fprintf(stderr, "Falha no 1º sqlite3_step\n");
        return false;
    }

    saldo[0] = sqlite3_column_int(stmt, 1);
    if(saldo[0] < DB_VALUE_TRANSACTION){
        fprintf(stderr, "Saldo insuficiente\n");
        goto end;
    }

    if(sqlite3_step(stmt) != SQLITE_ROW){
        fprintf(stderr, "Falha no 2º sqlite3_step\n");
        return false;
    }

    saldo[1] = sqlite3_column_int(stmt, 1);
    if(saldo[1] < DB_VALUE_TRANSACTION){
        fprintf(stderr, "Saldo insuficiente\n");
        goto end;
    }

    // Irá transferir o valor de <DB_VALUE_TRANSACTION> de uma conta para outra
    saldo[0] -= DB_VALUE_TRANSACTION;
    saldo[1] += DB_VALUE_TRANSACTION;

    if(stmt){
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    
    if(sqlite3_prepare_v2(db, "UPDATE conta SET saldo = ? WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha no 2º sqlite3_prepare_v2\n");
        return false;
    }

    if(sqlite3_bind_int(stmt, 1, saldo[0]) != SQLITE_OK || 
        sqlite3_bind_int(stmt, 2, conta[0]) != SQLITE_OK){
        fprintf(stderr, "Falha no 2º sqlite3_bind_int\n");
        return false;
    }

    if(sqlite3_step(stmt) != SQLITE_DONE){
        fprintf(stderr, "ERRO: %s\n", sqlite3_errmsg(db));
        fprintf(stderr, "Falha no 3º sqlite3_step\n");
        return false;
    }

    sqlite3_reset(stmt);

    if(sqlite3_bind_int(stmt, 1, saldo[1]) != SQLITE_OK ||
        sqlite3_bind_int(stmt, 2, conta[1]) != SQLITE_OK){
        fprintf(stderr, "Falha no 3º sqlite3_bind_int\n");
        return false;
    }

    if(sqlite3_step(stmt) != SQLITE_DONE){
        fprintf(stderr, "Falha no 4º sqlite_step\n");
        return false;
    }

end:
    if(stmt){
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

void saldo_total(const char *storage){
    sqlite3_stmt *stmt = NULL;
    sqlite3 *db;
    int saldo_conta = 0, saldo_total = 0;

    if(sqlite3_open_v2(storage, &db, DB_FLAGS, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha ao abrir conexao do banco de dados\n");
        return;
    }

    if(sqlite3_prepare_v2(db, "SELECT * FROM conta;", -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha no sqlite3_prepare_v2\n");
        return;
    }

    while(sqlite3_step(stmt) == SQLITE_ROW){
        saldo_conta = sqlite3_column_int(stmt, 1);

        saldo_total += saldo_conta;
    }

    if(stmt){
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if(sqlite3_close(db) != SQLITE_OK){
        fprintf(stderr, "Falha ao fechar conexao do banco de dados\n");
        return;
    }

    printf("saldo total: %d\n", saldo_total);

    return;
}

static void *run(void *arg){
    const char *storage = (const char *)arg;
    pthread_t thread = pthread_self();
    sqlite3 *db;
    
    printf("Thread executando: %lu\n", thread);

    pthread_mutex_lock(&DB_MUTEX);
    if(sqlite3_open_v2(storage, &db, DB_FLAGS, NULL) != SQLITE_OK){
        return NULL;
    }

    for(int i = 0; i < DB_TRANSACTION_AMOUNT; i++){
        if(!transaction(db))
            printf("Transação deu erro\n");
    }

    if(sqlite3_close(db) != SQLITE_OK){
        return NULL;
    }
    pthread_mutex_unlock(&DB_MUTEX);

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

    saldo_total(storage);

    return true;
}

static bool seed_db(sqlite3 *db){
    sqlite3_stmt *stmt = NULL;
    size_t total = 0;
    const char *sql = "INSERT INTO conta (saldo) VALUES (?);";

    if(sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha no sqlite3_exec: 'BEGIN'\n");
        return false;
    }

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK){
        fprintf(stderr, "Falha no sqlite3_prepare_v2\n");
        fprintf(stderr, "ERROR: %s\n", sqlite3_errmsg(db));
        goto rollback;
    }

    for(int i = 0; i < DB_ACCOUNTS; i++){

        if(sqlite3_bind_int(stmt, 1, 1000) != SQLITE_OK){
            fprintf(stderr, "Falha no sqlite3_bind_int\n");
            goto rollback;
        }
        if(sqlite3_step(stmt) != SQLITE_DONE){
            fprintf(stderr, "Falha no sqlite3_step\n");
            goto rollback;
        }

        total += 1000;

        sqlite3_reset(stmt);
    }

    if(stmt){
        if(sqlite3_reset(stmt) != SQLITE_OK){
            fprintf(stderr, "Falha no sqlite3_finalize\n");
            goto rollback;
        }
    }

    if(sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK){
        fprintf(stderr, "Falha no sqlite3_exec: 'COMMIT'\n");
        return false;
    }

    printf("Total: %zu\n", total);
    return true;

rollback:
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return false;
}

static bool start(const char *str)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    
    char *errmsg;

    db = NULL;
    errmsg = NULL;
    stmt = NULL;
    if (sqlite3_config(SQLITE_CONFIG_MULTITHREAD, 1) != SQLITE_OK)
    {
        fprintf(stderr, "sqlite3_config failed\n");
        return 1;
    }

    if (sqlite3_open_v2(str, &db, DB_FLAGS, NULL) != SQLITE_OK)
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

    if(!seed_db(db)){
        fprintf(stderr, "seed_db failed\n");
        goto end;
    }

    printf("TABLE CREATED!\n");
    stmt = NULL;

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
    if (strncat(cwd, "/storage.db", sizeof(cwd) - 1) == NULL) {
        perror("strncat() error");
        return 1;
    }

    while ((opt = sqlite3_compileoption_get(i++)) != NULL)
    {
        printf("%s\n", opt);
    }

    pthread_mutex_init(&DB_MUTEX, NULL);

    printf("Starting...!\n");
    status = start((const char *)cwd);
    printf("DONE!!\n");
    pthread_mutex_destroy(&DB_MUTEX);
    return status;
}
