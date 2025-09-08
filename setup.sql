CREATE TABLE 'cards' (
    'id' INTEGER PRIMARY KEY AUTOINCREMENT,
    'name' TEXT NOT NULL,
    'code' INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', DATETIME('subsec', 'utc'))),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', DATETIME('subsec', 'utc'))),
    UNIQUE('name','code')
) STRICT;

CREATE TABLE 'users' (
    'id' INTEGER PRIMARY KEY AUTOINCREMENT,
    'name' TEXT NOT NULL,
    'created_at' TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', DATETIME('subsec', 'utc'))),
    'updated_at' TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', DATETIME('subsec', 'utc')))
) STRICT;
