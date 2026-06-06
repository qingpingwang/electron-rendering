const fs = require('fs');
const { getDb } = require('./connection');

const SQL = {
    LIST:       'SELECT id, project_uuid AS projectUuid, name, path, type, size, added_at AS addedAt FROM media_items WHERE project_uuid = ? ORDER BY added_at DESC',
    INSERT:     'INSERT INTO media_items (id, project_uuid, name, path, type, size, added_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
    DELETE:     'DELETE FROM media_items WHERE id = ?',
    LIST_PATHS: 'SELECT id, path FROM media_items WHERE project_uuid = ?',
};

function list(projectUuid) {
    return getDb().prepare(SQL.LIST).all(projectUuid);
}

function add(projectUuid, item) {
    getDb().prepare(SQL.INSERT).run(
        item.id, projectUuid, item.name, item.path, item.type, item.size || 0, item.addedAt || new Date().toISOString()
    );
}

function remove(id) {
    return getDb().prepare(SQL.DELETE).run(id).changes > 0;
}

function pruneInvalid(projectUuid) {
    const items = getDb().prepare(SQL.LIST_PATHS).all(projectUuid);
    const invalid = items.filter(m => !fs.existsSync(m.path));
    if (invalid.length === 0) return 0;

    const del = getDb().prepare(SQL.DELETE);
    getDb().transaction(() => {
        for (const { id } of invalid) del.run(id);
    })();
    return invalid.length;
}

module.exports = { list, add, remove, pruneInvalid };
