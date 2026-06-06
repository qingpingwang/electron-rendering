/**
 * Agent mode 注册表。新增 mode 只需在这里加一行。
 * 每个 mode 必须导出 { id, label, systemPrompt, createTools, [stateSchema, createMiddleware] }。
 */
const editor = require('./editor');
const resource = require('./resource');

const MODES = {
    [editor.id]:   editor,
    [resource.id]: resource,
};

const DEFAULT_MODE = editor.id;

function getMode(mode) {
    const def = MODES[mode];
    if (!def) throw new Error(`Unknown agent mode: ${mode}`);
    return def;
}

function listModes() {
    return Object.values(MODES).map(({ id, label }) => ({ id, label }));
}

module.exports = { MODES, DEFAULT_MODE, getMode, listModes };
