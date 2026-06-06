/**
 * 渲染资源 mode：编辑 shader + 生成 config.json 这类资源。
 * 工具：读文件（任意位置）、写文件（沙箱内）、列目录（任意位置）。
 */
const { DynamicStructuredTool } = require('@langchain/core/tools');
const { z } = require('zod');
const fs = require('fs');
const path = require('path');

const sandbox = require('../sandbox');

const MAX_READ_BYTES = 1024 * 1024;       // 1 MB 单次读上限
const MAX_LIST_ENTRIES = 500;              // 单次 ls 上限

const SYSTEM_PROMPT = fs.readFileSync(path.join(__dirname, 'resource_prompt.md'), 'utf-8');


function _safeStat(p) {
    try { return fs.statSync(p); } catch { return null; }
}

function createTools() {
    return [
        new DynamicStructuredTool({
            name: 'list_dir',
            description: '列出目录内容。相对路径基于沙箱根目录解析；也接受绝对路径（用于参考外部资料）。',
            schema: z.object({
                path: z.string().default('.').describe('目录路径，相对沙箱根或绝对路径'),
            }),
            func: async ({ path: p }) => {
                try {
                    const full = sandbox.resolveReadPath(p || '.');
                    const stat = _safeStat(full);
                    if (!stat) {
                        return JSON.stringify({ error: `path not found: ${full}` });
                    }
                    if (!stat.isDirectory()) {
                        return JSON.stringify({ error: `not a directory: ${full}` });
                    }
                    const entries = fs.readdirSync(full, { withFileTypes: true });
                    const truncated = entries.length > MAX_LIST_ENTRIES;
                    const slice = truncated ? entries.slice(0, MAX_LIST_ENTRIES) : entries;
                    const items = slice.map((e) => {
                        const childPath = path.join(full, e.name);
                        const childStat = _safeStat(childPath);
                        return {
                            name: e.name,
                            type: e.isDirectory() ? 'dir' : (e.isFile() ? 'file' : 'other'),
                            size: childStat?.isFile() ? childStat.size : undefined,
                        };
                    });
                    return JSON.stringify({
                        resolved: full,
                        sandboxRoot: sandbox.getSandboxRoot(),
                        truncated,
                        total: entries.length,
                        items,
                    });
                } catch (e) {
                    return JSON.stringify({ error: e.message });
                }
            },
        }),

        new DynamicStructuredTool({
            name: 'read_file',
            description: '读取文本文件（最大 1 MB）。相对路径基于沙箱根；可读任意位置。',
            schema: z.object({
                path: z.string().describe('文件路径，相对沙箱根或绝对路径'),
            }),
            func: async ({ path: p }) => {
                try {
                    const full = sandbox.resolveReadPath(p);
                    const stat = _safeStat(full);
                    if (!stat) return JSON.stringify({ error: `file not found: ${full}` });
                    if (!stat.isFile()) return JSON.stringify({ error: `not a file: ${full}` });
                    if (stat.size > MAX_READ_BYTES) {
                        return JSON.stringify({
                            error: `file too large (${stat.size} bytes, limit ${MAX_READ_BYTES})`,
                        });
                    }
                    const content = fs.readFileSync(full, 'utf-8');
                    return JSON.stringify({ resolved: full, size: stat.size, content });
                } catch (e) {
                    return JSON.stringify({ error: e.message });
                }
            },
        }),

        new DynamicStructuredTool({
            name: 'write_file',
            description: '写入文本文件，自动创建父目录。**仅沙箱内允许**：路径必须落在 RESOURCE_SANDBOX 之下。',
            schema: z.object({
                path: z.string().describe('沙箱内相对路径，如 my_effect/shaders/pass0.frag'),
                content: z.string().describe('文件内容'),
            }),
            func: async ({ path: p, content }) => {
                try {
                    const full = sandbox.resolveWritePath(p);
                    fs.mkdirSync(path.dirname(full), { recursive: true });
                    fs.writeFileSync(full, content, 'utf-8');

                    // §6.4 auto-mount：仅 config.json 写完触发；shader 分次写不重复 load
                    if (full.endsWith('config.json') && typeof window !== 'undefined' && typeof window.__onResourceWritten === 'function') {
                        try { window.__onResourceWritten(full); } catch { /* UI 可能未就绪 */ }
                    }

                    return JSON.stringify({
                        ok: true,
                        resolved: full,
                        bytes: Buffer.byteLength(content, 'utf-8'),
                    });
                } catch (e) {
                    return JSON.stringify({ error: e.message });
                }
            },
        }),
    ];
}

module.exports = {
    id: 'resource',
    label: '渲染资源',
    systemPrompt: SYSTEM_PROMPT,
    createTools,
};
