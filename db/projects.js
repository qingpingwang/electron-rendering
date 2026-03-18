const { getDb } = require('./connection');

const SQL = {
    LIST:   'SELECT uuid, config_path AS configPath, name, duration, updated_at AS updatedAt FROM projects ORDER BY updated_at DESC',
    GET:    'SELECT uuid, config_path AS configPath, name, duration, updated_at AS updatedAt FROM projects WHERE uuid = ?',
    INSERT: 'INSERT INTO projects (uuid, config_path, name, duration, updated_at) VALUES (?, ?, ?, ?, ?)',
    DELETE: 'DELETE FROM projects WHERE uuid = ?',
};

function list() {
    return getDb().prepare(SQL.LIST).all();
}

function get(uuid) {
    return getDb().prepare(SQL.GET).get(uuid);
}

function create({ uuid, configPath, name, duration }) {
    const now = new Date().toISOString();
    const dur = duration || 5000;
    getDb().prepare(SQL.INSERT).run(uuid, configPath, name, dur, now);
    return { uuid, configPath, name, duration: dur, updatedAt: now };
}

function update(uuid, fields) {
    const sets = [];
    const vals = [];
    if (fields.duration !== undefined)  { sets.push('duration = ?');   vals.push(fields.duration); }
    if (fields.updatedAt !== undefined) { sets.push('updated_at = ?'); vals.push(fields.updatedAt); }
    if (fields.name !== undefined)      { sets.push('name = ?');       vals.push(fields.name); }
    if (sets.length === 0) return false;
    vals.push(uuid);
    return getDb().prepare(`UPDATE projects SET ${sets.join(', ')} WHERE uuid = ?`).run(...vals).changes > 0;
}

function remove(uuid) {
    return getDb().prepare(SQL.DELETE).run(uuid).changes > 0;
}

module.exports = { list, get, create, update, remove };
