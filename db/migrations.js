const fs = require('fs');
const path = require('path');

const ROOT_DIR = path.join(__dirname, '..');

module.exports = [
    {
        version: 1,
        name: 'create_projects_and_media_items',
        up(db) {
            db.exec(`
                CREATE TABLE IF NOT EXISTS projects (
                    uuid        TEXT PRIMARY KEY,
                    config_path TEXT NOT NULL,
                    name        TEXT NOT NULL,
                    duration    INTEGER DEFAULT 5000,
                    updated_at  TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS media_items (
                    id           TEXT PRIMARY KEY,
                    project_uuid TEXT NOT NULL,
                    name         TEXT NOT NULL,
                    path         TEXT NOT NULL,
                    type         TEXT NOT NULL CHECK(type IN ('video','audio','image')),
                    size         INTEGER,
                    added_at     TEXT NOT NULL,
                    FOREIGN KEY (project_uuid) REFERENCES projects(uuid) ON DELETE CASCADE
                );
            `);
        },
    },
    {
        version: 2,
        name: 'migrate_json_history',
        up(db) {
            const historyFile = path.join(ROOT_DIR, 'test', 'history.json');
            if (!fs.existsSync(historyFile)) return;

            try {
                const items = JSON.parse(fs.readFileSync(historyFile, 'utf-8'));
                if (!Array.isArray(items)) return;

                const insert = db.prepare(
                    'INSERT OR IGNORE INTO projects (uuid, config_path, name, duration, updated_at) VALUES (?, ?, ?, ?, ?)'
                );
                for (const r of items) {
                    insert.run(r.uuid, r.configPath, r.name, r.duration || 5000, r.updatedAt || new Date().toISOString());
                }
                console.log(`[DB] Migrated ${items.length} project(s) from history.json`);
            } catch (e) {
                console.warn('[DB] Failed to migrate history.json:', e.message);
            }
        },
    },
    {
        version: 3,
        name: 'migrate_json_media_meta',
        up(db) {
            const testDir = path.join(ROOT_DIR, 'test');
            if (!fs.existsSync(testDir)) return;

            try {
                const files = fs.readdirSync(testDir).filter(f => f.startsWith('media_meta_') && f.endsWith('.json'));
                if (files.length === 0) return;

                const insert = db.prepare(
                    'INSERT OR IGNORE INTO media_items (id, project_uuid, name, path, type, size, added_at) VALUES (?, ?, ?, ?, ?, ?, ?)'
                );
                for (const file of files) {
                    const uuid = file.replace('media_meta_', '').replace('.json', '');
                    const items = JSON.parse(fs.readFileSync(path.join(testDir, file), 'utf-8'));
                    if (!Array.isArray(items)) continue;
                    for (const m of items) {
                        insert.run(m.id, uuid, m.name, m.path, m.type, m.size || 0, m.addedAt || new Date().toISOString());
                    }
                }
                console.log(`[DB] Migrated media metadata from ${files.length} file(s)`);
            } catch (e) {
                console.warn('[DB] Failed to migrate media metadata:', e.message);
            }
        },
    },
];
