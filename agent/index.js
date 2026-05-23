const { ChatOpenAI } = require('@langchain/openai');
const { HumanMessage, AIMessageChunk, ToolMessage } = require('@langchain/core/messages');
const { createAgent } = require('langchain');
const { SqliteSaver } = require('@langchain/langgraph-checkpoint-sqlite');
const { z } = require('zod');
const fs = require('fs');
const path = require('path');

const db = require('../db');
const { serializeLangGraphMessages } = require('./history_serialize');
const { MODES, DEFAULT_MODE, getMode, listModes } = require('./modes');

let llm = null;
let checkpointer = null;
const agents = {};                      // mode -> compiled LangGraph agent

/** thread_id 命名规则：所有 mode 统一带前缀，零特殊情况 */
function resolveThreadId(uuid, mode) {
    if (!uuid) return null;
    if (!MODES[mode]) throw new Error(`Unknown agent mode: ${mode}`);
    return `${mode}:${uuid}`;
}

function getConfig() {
    const dotenvPath = path.join(__dirname, '..', '.env');
    try {
        const content = fs.readFileSync(dotenvPath, 'utf-8');
        const config = {};
        for (const line of content.split('\n')) {
            const trimmed = line.trim();
            if (!trimmed || trimmed.startsWith('#')) continue;
            const eqIdx = trimmed.indexOf('=');
            if (eqIdx === -1) continue;
            config[trimmed.slice(0, eqIdx).trim()] = trimmed.slice(eqIdx + 1).trim();
        }
        return config;
    } catch {
        return {};
    }
}

function ensureCheckpointer() {
    if (!checkpointer) {
        db.init();
        checkpointer = new SqliteSaver(db.getDb());
        checkpointer.setup();
    }
    return checkpointer;
}

function ensureLLM() {
    if (llm) return llm;

    const config = getConfig();
    const apiKey = config.OPENAI_API_KEY || process.env.OPENAI_API_KEY || undefined;
    let baseURL = config.OPENAI_BASE_URL || process.env.OPENAI_BASE_URL || undefined;
    const modelName = config.OPENAI_MODEL_NAME || process.env.OPENAI_MODEL_NAME || undefined;

    if (!apiKey || !modelName || !baseURL) {
        throw new Error('Missing OPENAI_API_KEY / MODEL_NAME / BASE_URL in .env');
    }

    if (baseURL) baseURL = baseURL.replace(/\/chat\/completions\/?$/i, '').replace(/\/$/, '');

    process.env.OPENAI_API_KEY = apiKey;
    process.env.OPENAI_BASE_URL = baseURL;

    const tempRaw = config.OPENAI_TEMPERATURE ?? process.env.OPENAI_TEMPERATURE;
    let temperature = 0.2;
    if (tempRaw !== undefined && tempRaw !== '') {
        const t = Number(tempRaw);
        if (!Number.isNaN(t)) temperature = Math.min(2, Math.max(0, t));
    }

    llm = new ChatOpenAI({
        modelName,
        temperature,
        openAIApiKey: apiKey,
        configuration: baseURL ? { baseURL } : undefined,
        modelKwargs: { caching: { type: 'disabled' } },
    });
    console.log('[Agent] LLM initialized:', modelName);
    return llm;
}

/**
 * 按 mode 懒加载 compiled agent。
 * 各 mode 独立 systemPrompt / tools / middleware / stateSchema，互不干扰。
 */
function getAgent(mode) {
    if (agents[mode]) return agents[mode];

    const def = getMode(mode);
    const model = ensureLLM();
    const saver = ensureCheckpointer();

    const middleware = def.createMiddleware
        ? def.createMiddleware({ getResourcesState })
        : [];

    agents[mode] = createAgent({
        model,
        tools: def.createTools(),
        systemPrompt: def.systemPrompt,
        checkpointer: saver,
        stateSchema: def.stateSchema || z.object({}),
        middleware,
    });
    console.log(`[Agent] Compiled mode "${mode}" (${def.label})`);
    return agents[mode];
}

function initAgent() {
    try {
        ensureLLM();
        ensureCheckpointer();
    } catch (e) {
        console.warn('[Agent]', e.message);
    }
}

/** 展平 chunk.content（含 reasoning 等块） */
function textFromMessageContent(content) {
    if (content == null || content === '') return '';
    if (typeof content === 'string') return content;
    if (Array.isArray(content)) {
        return content
            .map((p) => {
                if (p == null) return '';
                if (typeof p === 'string') return p;
                if (typeof p !== 'object') return String(p);
                if (p.type === 'text' && p.text != null) return String(p.text);
                if (p.type === 'reasoning') {
                    if (p.reasoning != null) return String(p.reasoning);
                    if (p.text != null) return String(p.text);
                }
                return '';
            })
            .join('');
    }
    return String(content);
}

function flushPendingToolCallChunks(buf, onToolCall) {
    if (!onToolCall || !buf) return;
    for (const k of Object.keys(buf)) {
        const entry = buf[k];
        if (!entry || entry.notified || !entry.name) continue;
        try {
            const raw = entry.args.trim();
            const args = raw ? JSON.parse(raw) : {};
            entry.notified = true;
            onToolCall(entry.name, args, entry.id || undefined);
        } catch {
            /* JSON 仍不完整 */
        }
    }
}

async function loadDisplayHistory(uuid, mode) {
    const threadId = resolveThreadId(uuid, mode);
    if (!threadId) return [];
    const a = getAgent(mode);
    try {
        const state = await a.graph.getState({ configurable: { thread_id: threadId } });
        return serializeLangGraphMessages(state.values?.messages);
    } catch (e) {
        console.warn(`[Agent] getState failed for ${threadId}:`, e.message);
        return [];
    }
}

async function handleUserMessage(userText, callbacks = {}, options = {}) {
    const mode = options.mode || DEFAULT_MODE;
    const uuid = options.threadId;
    const threadId = resolveThreadId(uuid, mode);

    const { onThinking, onToken, onToolCall, onToolResult, onDone, onSegmentBreak } = callbacks;

    if (!threadId) {
        console.warn('[Agent] No active project (thread_id). Open a project first.');
        if (onDone) onDone('发生错误: 当前没有活动项目，请先打开项目。');
        return;
    }

    let a;
    try {
        a = getAgent(mode);
    } catch (e) {
        if (onDone) onDone(`发生错误: ${e.message}`);
        return;
    }

    if (onThinking) onThinking();

    const runnableConfig = {
        configurable: { thread_id: threadId },
        streamMode: 'messages',
    };

    try {
        const input = { messages: [new HumanMessage(userText)] };
        const stream = await a.stream(input, runnableConfig);

        let currentMsgId = null;
        let aiResponse = '';
        let toolCallBuf = {};

        for await (const [chunk, _metadata] of stream) {
            if (chunk instanceof ToolMessage) {
                const tid = chunk.tool_call_id || undefined;
                if (onToolResult) onToolResult(chunk.name, chunk.content, tid);
                continue;
            }

            if (!(chunk instanceof AIMessageChunk)) continue;

            if (chunk.id !== currentMsgId) {
                const hasTools =
                    (chunk.tool_call_chunks?.length > 0) || (chunk.tool_calls?.length > 0);
                const text = textFromMessageContent(chunk.content).trim();
                if (!hasTools && !text) continue;

                flushPendingToolCallChunks(toolCallBuf, onToolCall);
                if (currentMsgId != null) onSegmentBreak?.();

                currentMsgId = chunk.id;
                toolCallBuf = {};
            }

            if (chunk.tool_call_chunks?.length) {
                for (const tc of chunk.tool_call_chunks) {
                    if (tc.index === undefined) continue;
                    if (!toolCallBuf[tc.index]) {
                        toolCallBuf[tc.index] = { id: '', name: '', args: '', notified: false };
                    }
                    const entry = toolCallBuf[tc.index];
                    if (tc.id && !entry.id) entry.id = tc.id;
                    if (tc.name) entry.name += tc.name;
                    if (tc.args) entry.args += tc.args;

                    if (!entry.notified && entry.name) {
                        try {
                            const raw = entry.args.trim();
                            if (raw === '') continue;
                            const args = JSON.parse(raw);
                            entry.notified = true;
                            if (onToolCall) onToolCall(entry.name, args, entry.id || undefined);
                        } catch {
                            /* 等待后续 args 分片 */
                        }
                    }
                }
            }

            const piece = textFromMessageContent(chunk.content);
            if (piece) {
                aiResponse += piece;
                if (onToken) onToken(piece);
            }
        }

        flushPendingToolCallChunks(toolCallBuf, onToolCall);

        if (onDone) onDone(aiResponse);
    } catch (err) {
        console.error('[Agent] Error:', err);
        const errorMsg = `发生错误: ${err.message}`;
        if (onDone) onDone(errorMsg);
    }
}

async function initThread(uuid, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    const threadId = resolveThreadId(uuid, mode);
    const a = getAgent(mode);
    const config = { configurable: { thread_id: threadId } };

    let existingState = null;
    try {
        existingState = await a.graph.getState(config);
    } catch { /* thread doesn't exist yet */ }

    // hasAnyState 已覆盖 hasMessages/hasResources：只要有任何 key 说明已初始化
    const hasAnyState = !!existingState?.values && Object.keys(existingState.values).length > 0;
    if (hasAnyState) {
        return { success: true, message: 'thread_already_exists' };
    }

    await a.graph.updateState(config, { messages: [], resources: [] });
    return { success: true, thread_id: threadId };
}

async function getHistory(uuid, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    const messages = await loadDisplayHistory(uuid, mode);
    return { success: true, messages };
}

async function deleteThread(uuid, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    const threadId = resolveThreadId(uuid, mode);
    ensureCheckpointer();
    await checkpointer.deleteThread(threadId);
    return { success: true };
}

/**
 * 通过 checkpointer.list() 枚举所有 resource 对话，
 * 不依赖任何自定义 SQL 表。
 * 参考：server.py get_all_thread_ids / list_threads
 */
async function listResourceThreads() {
    const saver = ensureCheckpointer();
    const a = getAgent('resource');

    const seen    = new Set();
    const results = [];

    try {
        for await (const item of saver.list({})) {
            const threadId = item.config?.configurable?.thread_id;
            if (!threadId || !threadId.startsWith('resource:') || seen.has(threadId)) continue;
            seen.add(threadId);

            const uuid = threadId.slice('resource:'.length);
            try {
                const state = await a.graph.getState({ configurable: { thread_id: threadId } });
                const msgs       = serializeLangGraphMessages(state?.values?.messages);
                const firstHuman = msgs.find(m => m.role === 'human');
                results.push({
                    uuid,
                    title:     firstHuman?.content || '',
                    updatedAt: item.checkpoint?.ts  || new Date().toISOString(),
                });
            } catch { /* 跳过损坏的 thread */ }
        }
    } catch (e) {
        console.warn('[Agent] listResourceThreads failed:', e.message);
    }

    return results.sort((a, b) => new Date(b.updatedAt) - new Date(a.updatedAt));
}

/** 兼容旧调用名 —— 已合并，直接走 deleteThread */

// ---- 素材库（仅 editor mode 使用，但 API 保持通用）----

function getResources(uuid, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    return getResourcesState(resolveThreadId(uuid, mode)).then((items) => ({
        success: true,
        resources: items,
    }));
}

async function getResourcesState(threadIdOrUuid, modeMaybe) {
    // 兼容两种调用：(threadId) 或 (uuid, mode)
    const threadId = modeMaybe === undefined
        ? threadIdOrUuid
        : resolveThreadId(threadIdOrUuid, modeMaybe);
    if (!threadId) return [];
    // editor mode 走 stateSchema 含 resources；其他 mode 没有这个字段，直接空
    const mode = threadId.split(':')[0];
    if (!MODES[mode]) return [];
    const a = getAgent(mode);
    const state = await a.graph.getState({ configurable: { thread_id: threadId } }).catch(() => null);
    const resources = state?.values?.resources;
    if (!Array.isArray(resources)) return [];
    return resources.map(normalizeResource).filter(Boolean);
}

async function setResourcesState(uuid, mode, resources) {
    const threadId = resolveThreadId(uuid, mode);
    const a = getAgent(mode);
    await a.graph.updateState(
        { configurable: { thread_id: threadId } },
        { resources }
    );
}

async function addResources(uuid, items, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    const current = await getResourcesState(uuid, mode);
    const byPath = new Set(current.map((i) => i.resource_path));
    const merged = [...current];
    for (const raw of items || []) {
        const item = normalizeResource(raw);
        if (!item || !item.resource_path || byPath.has(item.resource_path)) continue;
        merged.push(item);
        byPath.add(item.resource_path);
    }
    await setResourcesState(uuid, mode, merged);
    return { success: true, resources: merged };
}

async function removeResource(uuid, resourceId, mode = DEFAULT_MODE) {
    if (!uuid) throw new Error('missing thread_id');
    const current = await getResourcesState(uuid, mode);
    const next = current.filter((i) => i.resource_id !== resourceId);
    const changed = next.length !== current.length;
    if (changed) await setResourcesState(uuid, mode, next);
    return { success: changed, resources: next };
}

function normalizeResource(raw) {
    if (!raw || typeof raw !== 'object') return null;
    const id = raw.resource_id || raw.id;
    const name = raw.resource_name || raw.name;
    const p = raw.resource_path || raw.path;
    const type = raw.resource_type || raw.type;
    if (!id || !name || !p || !type) return null;
    return {
        resource_id: id,
        resource_name: name,
        resource_path: p,
        resource_type: type,
        resource_size: raw.resource_size ?? raw.size ?? 0,
        added_at: raw.added_at || raw.addedAt || new Date().toISOString(),
    };
}

module.exports = {
    initAgent,
    handleUserMessage,
    initThread,
    getHistory,
    deleteThread,
    listResourceThreads,
    getResources,
    addResources,
    removeResource,
    listModes,
    DEFAULT_MODE,
};
