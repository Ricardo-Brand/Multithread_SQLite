
# 🧵 Multithread SQLite

Projeto em C que demonstra a utilização de SQLite em contexto multithread, com foco em sincronização, acesso concorrente ao banco e habilidades de programação de sistemas.

##

## 📌 Sobre o Projeto

Este projeto explora como integrar o SQLite com um programa multithread em C, permitindo que múltiplas threads façam operações de leitura e escrita no mesmo banco de dados de forma controlada.

O foco principal é entender como gerenciar concorrência, sincronização de threads e acesso seguro ao banco de dados SQLite.

##

## 🧠 Tecnologias Utilizadas

💻 Linguagem C

🧵 Multithreading (pthread)

📦 SQLite — banco de dados

🔒 Sincronização de threads

🧪 Controle de acesso a recursos compartilhados

##

## 🚀 Funcionalidades

✔ Criação e abertura de banco SQLite

✔ Execução de operações em paralelo

✔ Sincronização entre threads

✔ Operações de leitura e escrita seguras
##

## 📥 Como Executar

### Passos

**1 - Clone o repositório:**

```http
git clone https://github.com/Ricardo-Brand/Multithread_SQLite.git
```

**2 - Entre na pasta:**

```http
cd Multithread_SQLite
```

**3 - Compile:**

```http
gcc -o multithread_sqlite main.c -lpthread -lsqlite3
```

> **Nota:** Certifique-se de ter instalado as bibliotecas `pthread` e `sqlite3` no seu sistema.

**4 - Rode o programa:**

```http
./multithread_sqlite
```

##

## 💡 Como Funciona (Resumo Técnico)

1 - O programa inicializa um banco de dados SQLite

2 - Cria múltiplas threads que acessam o banco

3 - Utiliza mecanismos de sincronização para evitar conflitos

4 - Executa inserções e consultas de forma concorrente

##

## 🎓 O que Aprendi com este Projeto

- Criação e gerenciamento de threads em C

- Uso de SQLite embutido

- Sincronização de threads e proteção de recursos compartilhados

- Organização de código modular e projetos de sistemas

- Como integrar C com bibliotecas externas

##

## 📜 Licença

Este projeto está sob a licença MIT.
