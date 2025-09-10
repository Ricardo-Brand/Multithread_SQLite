#define SQLITE_ENABLE_NORMALIZE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sql-stmts.h"
#include "sqlite3.h"

/* Quantidade de threads executando transações em paralelo */
#define THREAD_COUNT 10
/* Quantidade de contas criadas */
#define ACCOUNTS_TOTAL 100
/* Quantidade de transações que cada thread deve executar */
#define TRANSACTIONS_PER_THREAD 1000
/* Valor máximo que será transferido em cada transação */
#define TRANSFER_AMOUNT 1
/* ativa/desativa transações, altere esse valor e compare os resultados */
#define WITH_TRANSACTION 1

/* Flags de inicialização do SQLITE */
#define DB_FLAGS                                                 \
  SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_WAL | \
      SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW

/* Quantos microsegundos tem 1 millisegundo, utilizado pelo usleep */
#define MILLISECONDS 1000

static int THREAD_ID;

static const char *skip_spaces_and_comments(const char *ptr, const char *end) {
  while (ptr < end) {
    switch (*ptr) {
      case ' ':
      case '\t':
      case '\n':
        ptr++;
        continue;
      case '-':
        if ((++ptr) == end || *ptr != '-') continue;
        // skip -- comments --
        ptr = memchr(ptr, '\n', (size_t) (end - ptr));
        break;
      case '/':
        if ((++ptr) == end || *ptr != '*') continue;
        if ((ptr += 2) >= end) return end;
        // skip /* comments */
        do {
          ptr = memchr(ptr, '/', (size_t) (end - ptr));
        } while (ptr != NULL && *((++ptr) - 2) != '*');
        break;
      default:
        return ptr;
    }
    if (ptr == NULL) break;
    ptr++;
  }
  return end;
}

/**
 * Executa o conteúdo do arquivo `setup.sql`.
 */
static bool load_sql_file(sqlite3 *db) {
  sqlite3_stmt *stmt = NULL;
  const char *sql;
  const char *tail = sql_create_table_cards;
  const char *end = tail + sql_create_table_cards_len - 1;
  size_t len;
  int rc;
  while (tail < end) {
    tail = skip_spaces_and_comments(tail, end);
    if (tail >= end) break;

    len = (size_t) (end - tail);
    stmt = NULL;
    if (sqlite3_prepare_v3(db, tail, len, SQLITE_PREPARE_NORMALIZE, &stmt,
                           &tail) != SQLITE_OK) {
      fprintf(stderr, "sqlite3_prepare_v3 failed\n");
      fprintf(stderr, "tail: %s\n", tail);
      return false;
    }

    sql = sqlite3_expanded_sql(stmt);
    if (sql != NULL) {
      printf("-------------\n%s\n-------------\n", sql);
      sqlite3_free((void *) sql);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      fprintf(stderr, "step failed: %s\n", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return false;
    }

    if (sqlite3_finalize(stmt) != SQLITE_OK) {
      fprintf(stderr, "sqlite3_finalize failed\n");
      return false;
    }
  }
  return true;
}

/**
 * Verifica o saldo de uma conta, e salva o resultado em `*saldo`
 */
int balance_of(sqlite3_stmt *stmt, uint32_t account, uint64_t *saldo) {
  int status;
  if (stmt == NULL || saldo == NULL) return SQLITE_MISUSE;

  status = sqlite3_bind_int64(stmt, 1, (uint64_t) account);
  if (status != SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_bind_int64\n");
    goto end;
  }

  status = sqlite3_step(stmt);
  if (status != SQLITE_ROW) {
    fprintf(stderr, "Falha no sqlite3_step\n");
    goto end;
  }

  if (sqlite3_column_type(stmt, 1) != SQLITE_INTEGER) {
    fprintf(stderr, "Falha no sqlite3_column_type\n");
    status = SQLITE_ERROR;
    goto end;
  }
  *saldo = sqlite3_column_int64(stmt, 1);

  status = sqlite3_step(stmt);
  switch (status) {
    case SQLITE_DONE:
      status = sqlite3_reset(stmt);
      stmt = NULL;
      break;
    case SQLITE_BUSY:
      break;
    default:
      fprintf(stderr, "Falha no sqlite3_step\n");
  }
end:
  if (stmt) sqlite3_reset(stmt);
  return status;
}

/**
 * Sobreescreve o saldo de uma conta
 */
int set_balance_of(sqlite3_stmt *stmt, uint32_t account, uint64_t saldo) {
  int status;
  if (stmt == NULL) return SQLITE_MISUSE;

  status = sqlite3_bind_int(stmt, 2, account);
  if (status != SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_bind_int\n");
    goto end;
  }

  status = sqlite3_bind_int64(stmt, 1, saldo);
  if (status != SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_bind_int64\n");
    goto end;
  }

  status = sqlite3_step(stmt);
  switch (status) {
    case SQLITE_DONE:
      status = sqlite3_reset(stmt);
      stmt = NULL;
      break;
    case SQLITE_BUSY:
      break;
    default:
      fprintf(stderr, "Falha no sqlite3_step\n");
  }
end:
  if (stmt) sqlite3_reset(stmt);

  return status;
}

int transaction(int thread_id, const char *storage, uint32_t origem,
                uint32_t destino, uint64_t valor) {
  sqlite3_stmt *select_stmt = NULL, *update_stmt = NULL;
  int status;
  uint64_t saldo[2];
  sqlite3 *db = NULL;
#if defined(WITH_TRANSACTION) && WITH_TRANSACTION != 0
  int tx = 0;
#endif

  saldo[0] = 0;
  saldo[1] = 0;

  status = sqlite3_open_v2(storage, &db, DB_FLAGS, NULL);
  if (status != SQLITE_OK) {
    return status;
  }

  // Prepare SELECT
  status =
      sqlite3_prepare_v3(db, "SELECT * FROM conta WHERE id = ? LIMIT 1;", -1,
                         SQLITE_PREPARE_PERSISTENT, &select_stmt, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "(%d) Falha no 1º sqlite3_prepare_v2\n", thread_id);
    fprintf(stderr, "(%d) ERRO: %s\n", thread_id, sqlite3_errmsg(db));
    goto end;
  }

  // Prepare UPDATE
  status =
      sqlite3_prepare_v3(db, "UPDATE conta SET saldo = ? WHERE id = ?;", -1,
                         SQLITE_PREPARE_PERSISTENT, &update_stmt, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "(%d) Falha no 2º sqlite3_prepare_v2\n", thread_id);
    fprintf(stderr, "(%d) ERRO: %s\n", thread_id, sqlite3_errmsg(db));
    goto end;
  }

#if defined(WITH_TRANSACTION) && WITH_TRANSACTION != 0
  // START TRANSACTION
  status = sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);
  if (status != SQLITE_OK) {
    tx = 0;
    if (status != SQLITE_BUSY) {
      fprintf(stderr, "Falha no sqlite3_exec: 'BEGIN'\n");
      fprintf(stderr, "(%d) ERRO: %s\n", thread_id, sqlite3_errmsg(db));
    }
    goto end;
  }
  tx = 1;
#endif

  // Check origem account saldo.
  status = balance_of(select_stmt, origem, &saldo[0]);
  if (status != SQLITE_OK) {
    fprintf(stderr, "(%d) balance_of(origem) failed: %s\n", thread_id,
            sqlite3_errmsg(db));
    goto end;
  }

  if (saldo[0] < valor) {
    fprintf(stderr,
            "(%d) saldo insuficiente, precisa %" PRIu64 " possui %" PRIu64 "\n",
            thread_id, valor, saldo[0]);
    status = SQLITE_ERROR;
    goto end;
  }

  // Check destino account saldo
  status = balance_of(select_stmt, destino, &saldo[1]);
  if (status != SQLITE_OK) {
    fprintf(stderr, "(%d) balance_of(destino) failed: %s\n", thread_id,
            sqlite3_errmsg(db));
    goto end;
  }

  // Transfere o valor de uma conta para outra
  saldo[0] -= valor;  // origem
  saldo[1] += valor;  // destino

  status = set_balance_of(update_stmt, origem, saldo[0]);
  if (status != SQLITE_OK) {
    if (status != SQLITE_BUSY)
      fprintf(stderr, "(%d) set_balance_of(origem) ERRO: %s\n", thread_id,
              sqlite3_errmsg(db));
    goto end;
  }

  status = set_balance_of(update_stmt, destino, saldo[1]);
  if (status != SQLITE_OK) {
    if (status != SQLITE_BUSY)
      fprintf(stderr, "(%d) set_balance_of(destino) ERRO: %s\n", thread_id,
              sqlite3_errmsg(db));
    goto end;
  }
end:
#if defined(WITH_TRANSACTION) && WITH_TRANSACTION != 0
  if (tx) {
    if (status == SQLITE_OK) {
      status = sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
      if (status != SQLITE_OK && status != SQLITE_BUSY) {
        fprintf(stderr, "(%d) Falha no sqlite3_exec: 'COMMIT'\n", thread_id);
        fprintf(stderr, "(%d) ERRO: %s\n", thread_id, sqlite3_errmsg(db));
      }
    } else {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }
  }
#endif

  if (select_stmt) {
    sqlite3_finalize(select_stmt);
    select_stmt = NULL;
  }

  if (update_stmt) {
    sqlite3_finalize(update_stmt);
    update_stmt = NULL;
  }

  if (db) {
    sqlite3_close(db);
    db = NULL;
  }
  return status;
}

/**
 * Verifica o saldo total de todas as conta.
 */
int saldo_total(const char *storage, uint64_t *saldo) {
  int status = 0;
  sqlite3_stmt *stmt = NULL;
  sqlite3 *db = NULL;

  status = sqlite3_open_v2(storage, &db, DB_FLAGS, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "Falha ao abrir conexao do banco de dados\n");
    return status;
  }

  status =
      sqlite3_prepare_v2(db, "SELECT SUM(saldo) FROM conta;", -1, &stmt, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "Falha ao criar a query\n");
    goto end;
  }

  switch (sqlite3_step(stmt)) {
    case SQLITE_ROW:
      break;
    case SQLITE_DONE:
      fprintf(stderr, "Nenhuma conta encontrada.\n");
      break;
    default:
      fprintf(stderr, "sqlite3_step falhou: %s\n", sqlite3_errmsg(db));
      goto end;
  }

  /* Verifica se foi retornado só uma coluna */
  if (sqlite3_column_count(stmt) != 1) {
    fprintf(stderr, "Quantidade inesperada de colunas: %d\n",
            sqlite3_column_count(stmt));
    goto end;
  }

  /* Verifica se a coluna é do tipo inteiro */
  if (sqlite3_column_type(stmt, 0) != SQLITE_INTEGER) {
    fprintf(stderr, "Coluna 1 não existe ou não é do tipo inteiro: %d \n",
            sqlite3_column_type(stmt, 1));
    goto end;
  }

  /* Le o valor da coluna */
  *saldo = sqlite3_column_int64(stmt, 0);

  /* Garante que foi retorna só uma linha de resultado */
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    fprintf(stderr, "Sqlite3 retornou um resultado inesperado\n");
    status = SQLITE_ERROR;
    goto end;
  }

end:
  if (stmt) {
    sqlite3_finalize(stmt);
    stmt = NULL;
  }
  if (status == SQLITE_OK) {
    if ((status = sqlite3_close(db)) != SQLITE_OK)
      fprintf(stderr, "Falha ao fechar conexao do banco de dados\n");
  } else {
    sqlite3_close(db);
  }
  return status;
}

/**
 * Executa `TRANSACTIONS_PER_THREAD` transações transferindo
 * um saldo entre contas aleatórias.
 */
static void *run(void *arg) {
  const char *storage = (const char *) arg;
  unsigned int thread_id, origem, destino;
  int i, status, retries;
  useconds_t delay;

  /* Contador para o número de tentativas */
  retries = 0;

  /*
   * Reserva um ID sequencial único para essa thread.
   * __atomic_fetch_add é uma feature do `C11` para incrementar
   * um número atómicamente entre várias threads.
   * https://gcc.gnu.org/onlinedocs/gcc-4.8.2/gcc/_005f_005fatomic-Builtins.html
   */
  thread_id = __atomic_fetch_add(&THREAD_ID, 1, __ATOMIC_SEQ_CST);

  printf("Thread %2d executando...\n", thread_id);

  for (i = 0; i < TRANSACTIONS_PER_THREAD; i++) {
    status = SQLITE_OK;

    /* Sorteia uma conta `origem` e `destino` aleatóriamente */
    do {
      RAND_bytes((unsigned char *) &origem, sizeof(origem));
      origem = (origem % ACCOUNTS_TOTAL) + 1;

      RAND_bytes((unsigned char *) &destino, sizeof(destino));
      destino = (destino % ACCOUNTS_TOTAL) + 1;
    } while (origem == destino); /* sorteia denovo se `origem` == `destino` */

    delay = 10;
    do {
      /* Transfere um saldo de `origem` para `destino` */
      status =
          transaction(thread_id, storage, origem, destino, TRANSFER_AMOUNT);

      /* Se o status é `SQLITE_BUSY` tente denovo */
      if (status == SQLITE_BUSY) {
        retries += 1;
        // printf("(%d) RETRY TX %d: %d -> %d -> %d\n", thread_id, i, origem,
        //        destino, TRANSFER_AMOUNT);

        /* Espere alguns milissegundos até o lock ser liberado */
        usleep(delay * MILLISECONDS);
        /* Espere mais 50 milissegundos no próximo RETRY */
        delay += 50;
      }
    } while (status ==
             SQLITE_BUSY);  // tente novamente apenas se for SQLITE_BUSY.

    // Pare tudo em caso de falha.
    if (status != SQLITE_OK) break;

    /* Espere 5 millisegundos antes da próxima transação */
    usleep(5 * MILLISECONDS);
  }

  printf("Thread %2d executou %4d transações com %3d tentativas.\n", thread_id,
         i, retries);
  return NULL;
}

/*
 * Inicia `THREAD_COUNT` novas threads para acessar o banco de dados.
 * Também compara o saldo de todas as contas antes e depois.
 */
static bool start_threads(const char *storage) {
  pthread_t threads[THREAD_COUNT];
  uint64_t saldo_antes, saldo_depois;

  if (saldo_total(storage, &saldo_antes) != SQLITE_OK) return false;
  printf(" saldo antes: %" PRIu64 "\n", saldo_antes);

  for (int i = 0; i < THREAD_COUNT; i++) {
    pthread_create(&threads[i], NULL, run, (void *) storage);
  }
  printf("Threads criadas com sucesso\n");

  for (int i = 0; i < THREAD_COUNT; i++) {
    pthread_join(threads[i], NULL);
  }
  printf("Threads finalizadas\n");

  if (saldo_total(storage, &saldo_depois) != SQLITE_OK) return false;
  printf("saldo depois: %" PRIu64 "\n", saldo_depois);

  if (saldo_antes < saldo_depois) {
    printf("FALHA! foi criado dinheiro: +%" PRIu64 "\n",
           saldo_depois - saldo_antes);
  } else if (saldo_antes > saldo_depois) {
    printf("FALHA! sumiu dinheiro: -%" PRIu64 "\n", saldo_antes - saldo_depois);
  } else {
    printf("Sucesso!\n");
  }

  return true;
}

/**
 * Popula o banco de dados criando `ACCOUNTS_TOTAL` contas novas.
 * Cada conta tem exatamente R$10000 de saldo.
 */
static bool seed_db(sqlite3 *db) {
  sqlite3_stmt *stmt = NULL;
  size_t total = 0;
  const char *sql = "INSERT INTO conta (saldo) VALUES (?);";

  if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_exec: 'BEGIN'\n");
    return false;
  }

  if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &stmt, NULL) !=
      SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_prepare_v3\n");
    fprintf(stderr, "ERROR: %s\n", sqlite3_errmsg(db));
    goto rollback;
  }

  for (int i = 0; i < ACCOUNTS_TOTAL; i++) {
    if (sqlite3_bind_int64(stmt, 1, 10000) != SQLITE_OK) {
      fprintf(stderr, "Falha no sqlite3_bind_int\n");
      goto rollback;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      fprintf(stderr, "Falha no sqlite3_step\n");
      goto rollback;
    }

    total += 10000;
    sqlite3_reset(stmt);
  }

  if (stmt) {
    if (sqlite3_reset(stmt) != SQLITE_OK) {
      fprintf(stderr, "Falha no sqlite3_finalize\n");
      goto rollback;
    }
  }

  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
    fprintf(stderr, "Falha no sqlite3_exec: 'COMMIT'\n");
    return false;
  }

  printf("Total: %zu\n", total);
  return true;

rollback:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  return false;
}

/**
 * Configura e roda os testes:
 * 1 - Configura o banco de dados: foreign_keys + journal_mode
 * 2 - Cria a tabela `conta` chamando `load_sql_file`.
 * 3 - Popula o banco de dados chamando `seed_db`.
 * 4 - Roda o teste em várias threads chamando `start_threads`.
 */
static bool start(const char *str) {
  sqlite3 *db;
  sqlite3_stmt *stmt;

  char *errmsg;

  db = NULL;
  errmsg = NULL;
  stmt = NULL;
  if (sqlite3_config(SQLITE_CONFIG_MULTITHREAD, 1) != SQLITE_OK) {
    fprintf(stderr, "sqlite3_config failed\n");
    return 1;
  }

  if (sqlite3_open_v2(str, &db, DB_FLAGS, NULL) != SQLITE_OK) {
    fprintf(stderr, "sqlite3_open failed\n");
    return 1;
  }

  if (sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, &errmsg) !=
      SQLITE_OK) {
    fprintf(stderr, "PRAGMA foreign_keys=ON; failed: %s\n", errmsg);
    goto end;
  }

  if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, &errmsg) !=
      SQLITE_OK) {
    fprintf(stderr, "PRAGMA journal_mode=WAL; failed: %s\n", errmsg);
    goto end;
  }

  if (!load_sql_file(db)) {
    fprintf(stderr, "load_sql_file failed\n");
    goto end;
  }

  if (!seed_db(db)) {
    fprintf(stderr, "seed_db failed\n");
    goto end;
  }

  printf("TABLE CREATED!\n");
  stmt = NULL;

  sqlite3_close(db);
  start_threads(str);
  return true;

end:
  if (stmt != NULL) sqlite3_finalize(stmt);

  return sqlite3_close(db) == SQLITE_OK;
}

int main(void) {
  int status;
  int i = 0;
  const char *opt = NULL;
  char cwd[PATH_MAX];
  memset((void *) cwd, 0, sizeof(cwd));
  if (getcwd(cwd, sizeof(cwd) - 1) != NULL)
    printf("Current working dir: %s\n", cwd);
  else {
    perror("getcwd() error");
    return 1;
  }
  if (strncat(cwd, "/storage.db", sizeof(cwd) - 1) == NULL) {
    perror("strncat() error");
    return 1;
  }

  while ((opt = sqlite3_compileoption_get(i++)) != NULL) {
    printf("%s\n", opt);
  }
  THREAD_ID = 1;

  printf("Starting...!\n");
  status = start((const char *) cwd);
  printf("DONE!!\n");
  return status;
}