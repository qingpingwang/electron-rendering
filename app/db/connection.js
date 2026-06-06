const fs = require('fs');
const path = require('path');

const ROOT_DIR = path.join(__dirname, '..');

let _db = null;
let Database = null;

function _readEnv() {
    try {
        const content = fs.readFileSync(path.join(ROOT_DIR, '.env'), 'utf-8');
        const cfg = {};
        for (const line of content.split('\n')) {
            const t = line.trim();
            if (!t || t.startsWith('#')) continue;
            const eq = t.indexOf('=');
            if (eq === -1) continue;
            cfg[t.slice(0, eq).trim()] = t.slice(eq + 1).trim();
        }
        return cfg;
    } catch {
        return {};
    }
}

function init() {
    if (_db) return _db;

    if (!Database) {
        Database = require('better-sqlite3');
    }

    const env = _readEnv();
    const dbRelPath = env.DATABASE_PATH || 'data/app.db';
    const dbPath = path.resolve(ROOT_DIR, dbRelPath);
    const dbDir = path.dirname(dbPath);

    if (!fs.existsSync(dbDir)) {
        fs.mkdirSync(dbDir, { recursive: true });
    }

    _db = new Database(dbPath);
    _db.pragma('journal_mode = WAL');
    _db.pragma('foreign_keys = ON');

    _runMigrations();
    return _db;
}

function getDb() {
    if (!_db) throw new Error('Database not initialized. Call init() first.');
    return _db;
}

function close() {
    if (_db) {
        _db.close();
        _db = null;
    }
}

function _runMigrations() {
    _db.exec(`
        CREATE TABLE IF NOT EXISTS _migrations (
            version  INTEGER PRIMARY KEY,
            name     TEXT NOT NULL,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    `);

    const migrations = require('./migrations');
    const applied = new Set(
        _db.prepare('SELECT version FROM _migrations').all().map(r => r.version)
    );

    for (const m of migrations) {
        if (applied.has(m.version)) continue;
        _db.transaction(() => {
            m.up(_db);
            _db.prepare('INSERT INTO _migrations (version, name) VALUES (?, ?)').run(m.version, m.name);
        })();
        console.log(`[DB] Migration ${m.version}: ${m.name}`);
    }
}

module.exports = { init, getDb, close };
