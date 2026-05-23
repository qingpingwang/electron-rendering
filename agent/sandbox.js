/**
 * 渲染资源沙箱：写入限制在 RESOURCE_SANDBOX 内，读取允许任意路径。
 * 设计原则：把"路径在不在沙箱内"做成无分支的统一判断。
 */
const fs   = require('fs');
const path = require('path');

const ROOT_DIR = path.join(__dirname, '..');

let _sandboxRoot = null;

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

function getSandboxRoot() {
    if (_sandboxRoot) return _sandboxRoot;
    const env = _readEnv();
    const rel = env.RESOURCE_SANDBOX || './test/resources';
    _sandboxRoot = path.resolve(ROOT_DIR, rel);
    if (!fs.existsSync(_sandboxRoot)) {
        fs.mkdirSync(_sandboxRoot, { recursive: true });
    }
    return _sandboxRoot;
}

/**
 * 解析读路径：允许任意位置。
 * - 相对路径：基于沙箱根目录解析（方便 LLM 直接写 "blur_effect/config.json"）
 * - 绝对路径：原样使用
 */
function resolveReadPath(p) {
    if (!p || typeof p !== 'string') {
        throw new Error('path must be a non-empty string');
    }
    if (path.isAbsolute(p)) return path.normalize(p);
    return path.resolve(getSandboxRoot(), p);
}

/**
 * 解析写路径：必须落在沙箱内。
 * 拒绝越界（path traversal）和绝对路径越界。
 */
function resolveWritePath(p) {
    if (!p || typeof p !== 'string') {
        throw new Error('path must be a non-empty string');
    }
    const root     = getSandboxRoot();
    const resolved = path.isAbsolute(p) ? path.normalize(p) : path.resolve(root, p);
    const rel      = path.relative(root, resolved);
    if (rel === '' || rel.startsWith('..') || path.isAbsolute(rel)) {
        throw new Error(`Path "${p}" escapes sandbox root (${root})`);
    }
    return resolved;
}

module.exports = {
    getSandboxRoot,
    resolveReadPath,
    resolveWritePath,
};
