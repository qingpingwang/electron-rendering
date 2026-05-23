const { getDb } = require('./connection');

const SQL = {
    LIST:   'SELECT uuid, name, folder, updated_at AS updatedAt FROM resource_projects ORDER BY updated_at DESC',
    GET:    'SELECT uuid, name, folder, updated_at AS updatedAt FROM resource_projects WHERE uuid = ?',
    INSERT: 'INSERT INTO resource_projects (uuid, name, folder, updated_at) VALUES (?, ?, ?, ?)',
    DELETE: 'DELETE FROM resource_projects WHERE uuid = ?',
};

function list() {
    return getDb().prepare(SQL.LIST).all();
}

function get(uuid) {
    return getDb().prepare(SQL.GET).get(uuid);
}

function create({ uuid, name, folder }) {
    const now = new Date().toISOString();
    getDb().prepare(SQL.INSERT).run(uuid, name, folder, now);
    return { uuid, name, folder, updatedAt: now };
}

function update(uuid, fields) {
    const sets = [];
    const vals = [];
    if (fields.name !== undefined)      { sets.push('name = ?');       vals.push(fields.name); }
    if (fields.folder !== undefined)    { sets.push('folder = ?');     vals.push(fields.folder); }
    if (fields.updatedAt !== undefined) { sets.push('updated_at = ?'); vals.push(fields.updatedAt); }
    if (sets.length === 0) return false;
    vals.push(uuid);
    return getDb().prepare(`UPDATE resource_projects SET ${sets.join(', ')} WHERE uuid = ?`).run(...vals).changes > 0;
}

function remove(uuid) {
    return getDb().prepare(SQL.DELETE).run(uuid).changes > 0;
}

module.exports = { list, get, create, update, remove };
