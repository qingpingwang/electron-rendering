const fs = require('fs');
const path = require('path');
const { randomUUID } = require('crypto');

const FILE_NAME = 'draft_meta_info.json';

function entries(config, base) {
    return [['videos', 'video'], ['audios', 'audio'], ['images', 'image'], ['svgs', 'svg'], ['effects', 'effect'], ['transitions', 'transition'], ['filters', 'filter']].flatMap(([key, type]) =>
        (config.materials?.[key] || []).filter(item => item.path).map(item => ({
            id: item.id || randomUUID(),
            extra_info: item.name || path.basename(item.path),
            metetype: type,
            file_Path: path.relative(base, path.resolve(base, item.path)),
            duration: Math.round(item.durationUs ?? item.duration ?? 0),
            width: item.width || 0,
            height: item.height || 0,
            import_time: Math.floor(Date.now() / 1000),
        })));
}

function uniqueMaterials(items, base) {
    const result = new Map();
    for (const item of items) {
        const key = path.resolve(base, item.file_Path);
        if (!result.has(key)) { result.set(key, item); }
    }
    return [...result.values()];
}

function imported(meta) {
    return meta.draft_materials.find(group => group.type === 0).value;
}

// Convert only for the editor UI; the on-disk representation follows Jianying.
function getMaterials(meta) {
    return imported(meta).map(item => ({
        id: item.id, name: item.extra_info, type: item.metetype, path: item.file_Path,
        duration: item.duration, width: item.width, height: item.height,
    }));
}

function writeJson(file, value) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const temporary = `${file}.${randomUUID()}.tmp`;
    try {
        fs.writeFileSync(temporary, JSON.stringify(value, null, 2) + '\n');
        fs.renameSync(temporary, file);
    } finally {
        if (fs.existsSync(temporary)) { fs.unlinkSync(temporary); }
    }
}

function write(base, meta) {
    writeJson(path.join(base, FILE_NAME), meta);
    repairStore(base, imported(meta));
    return meta;
}

// Called before loading the SDK: reconciliation changes only project sidecar files.
function ensure(base, config, name) {
    const file = path.join(base, FILE_NAME);
    const now = Date.now() * 1000;
    const exists = fs.existsSync(file);
    const previous = exists ? JSON.parse(fs.readFileSync(file, 'utf8')) : {};
    const meta = {
        draft_id: config.id || randomUUID(),
        draft_name: name || config.name || path.basename(base),
        tm_draft_create: now,
        tm_draft_modified: now,
        ...JSON.parse(JSON.stringify(previous)),
        tm_duration: Math.round(config.duration || 0),
        draft_materials: JSON.parse(JSON.stringify(previous.draft_materials || [])),
    };
    let group = meta.draft_materials.find(group => group.type === 0);
    if (!group) {
        group = { type: 0, value: [] };
        meta.draft_materials.push(group);
    }
    group.value = uniqueMaterials([...(group.value || []), ...entries(config, base)], base);
    if (!exists || JSON.stringify(meta) !== JSON.stringify(previous)) {
        meta.tm_draft_modified = now;
        return write(base, meta);
    }
    repairStore(base, imported(meta));
    return meta;
}

function register(base, config, videos) {
    const meta = ensure(base, config);
    const group = meta.draft_materials.find(group => group.type === 0);
    group.value = uniqueMaterials([...group.value, ...entries({ materials: { videos } }, base)], base);
    meta.tm_draft_modified = Date.now() * 1000;
    return write(base, meta);
}

function save(base, config) {
    const meta = ensure(base, config);
    meta.tm_duration = Math.round(config.duration || 0);
    meta.tm_draft_modified = Date.now() * 1000;
    // Removing a clip never removes its imported source from the material library.
    const group = meta.draft_materials.find(group => group.type === 0);
    group.value = uniqueMaterials([...group.value, ...entries(config, base)], base);
    return write(base, meta);
}

function repairStore(base, materials) {
    const file = path.join(base, 'draft_virtual_store.json');
    const exists = fs.existsSync(file);
    const store = exists ? JSON.parse(fs.readFileSync(file, 'utf8')) : {
        draft_virtual_store: [
            { type: 0, value: [{ id: '', display_name: '' }] },
            { type: 1, value: [] },
        ],
    };
    const before = JSON.stringify(store);
    store.draft_virtual_store ||= [];
    for (const type of [0, 1]) {
        if (!store.draft_virtual_store.some(group => group.type === type)) {
            store.draft_virtual_store.push({ type, value: [] });
        }
    }
    const folderGroup = store.draft_virtual_store.find(group => group.type === 0);
    const relationGroup = store.draft_virtual_store.find(group => group.type === 1);
    const folders = folderGroup.value ||= [];
    const relations = relationGroup.value ||= [];
    if (!folders.some(folder => folder.id === '')) {
        folders.unshift({ id: '', display_name: '' });
    }
    const folderIds = new Set(folders.map(folder => folder.id));
    for (const relation of relations) {
        if (!folderIds.has(relation.parent_id)) {
            relation.parent_id = '';
        }
    }
    const known = new Set(relations.map(item => item.child_id));
    let changed = !exists || before !== JSON.stringify(store);
    for (const material of materials) {
        if (!known.has(material.id)) {
            relations.push({ child_id: material.id, parent_id: '' });
            known.add(material.id);
            changed = true;
        }
    }
    if (changed) {
        writeJson(file, store);
    }
    return store;
}

module.exports = { ensure, register, save, getMaterials };
