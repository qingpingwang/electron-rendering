const { ChatOpenAI } = require('@langchain/openai');
const { HumanMessage, AIMessageChunk, ToolMessage } = require('@langchain/core/messages');
const { createAgent } = require('langchain');
const { SqliteSaver } = require('@langchain/langgraph-checkpoint-sqlite');
const { z } = require('zod');
const fs = require('fs');
const path = require('path');

const db = require('../db');
const { createEditorTools } = require('./tools');
const { SYSTEM_PROMPT } = require('./prompts');
const { serializeLangGraphMessages } = require('./history_serialize');

let agent = null;
let checkpointer = null;
/** 当前工程 UUID，作为 LangGraph thread_id */
let activeThreadId = null;

const RESOURCE_SCHEMA = z.object({
    resource_id: z.string(),
    resource_name: z.string(),
    resource_path: z.string(),
    resource_type: z.enum(['video', 'audio', 'image']),
    resource_size: z.number().optional(),
    added_at: z.string().optional(),
});

const AGENT_STATE_SCHEMA = z.object({
    resources: z.array(RESOURCE_SCHEMA).optional(),
});

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

function initAgent() {
    if (agent) return;

    const config = getConfig();
    const apiKey = config.OPENAI_API_KEY || process.env.OPENAI_API_KEY || undefined;
    let baseURL = config.OPENAI_BASE_URL || process.env.OPENAI_BASE_URL || undefined;
    const modelName = config.OPENAI_MODEL_NAME || process.env.OPENAI_MODEL_NAME || undefined;

    if (!apiKey || !modelName || !baseURL) {
        console.warn('[Agent] Missing OPENAI_API_KEY / MODEL_NAME / BASE_URL in .env');
        return;
    }

    if (baseURL) baseURL = baseURL.replace(/\/chat\/completions\/?$/i, '').replace(/\/$/, '');

    process.env.OPENAI_API_KEY = apiKey;
    process.env.OPENAI_BASE_URL = baseURL;

    const llm = new ChatOpenAI({
        modelName,
        temperature: 0.7,
        openAIApiKey: apiKey,
        configuration: baseURL ? { baseURL } : undefined,
    });

    const tools = createEditorTools();
    const saver = ensureCheckpointer();

    agent = createAgent({
        model: llm,
        tools,
        systemPrompt: SYSTEM_PROMPT,
        checkpointer: saver,
        stateSchema: AGENT_STATE_SCHEMA,
    });

    console.log('[Agent] Initialized with model:', modelName, '| LangGraph SqliteSaver → app.db');
}

function ensureAgentReady() {
    initAgent();
    if (!agent) {
        throw new Error('Agent 未初始化（请检查 .env 的 OPENAI 配置）');
    }
    return agent;
}

async function loadDisplayHistory(threadId) {
    if (!threadId) return [];
    const a = ensureAgentReady();
    try {
        const state = await a.graph.getState({ configurable: { thread_id: threadId } });
        return serializeLangGraphMessages(state.values?.messages);
    } catch (e) {
        console.warn('[Agent] getState failed:', e.message);
        return [];
    }
}

async function handleUserMessage(userText, callbacks = {}, options = {}) {
    const a = ensureAgentReady();
    const threadId = options.threadId || activeThreadId;
    if (options.threadId) activeThreadId = options.threadId;

    if (!threadId) {
        console.warn('[Agent] No active project (thread_id). Open a project first.');
        if (callbacks.onDone) callbacks.onDone('发生错误: 当前没有活动项目，请先打开项目。');
        return;
    }

    const { onThinking, onToken, onToolCall, onToolResult, onDone } = callbacks;

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
                if (onToolResult) onToolResult(chunk.name, chunk.content);
                continue;
            }

            if (!(chunk instanceof AIMessageChunk)) continue;

            if (chunk.id !== currentMsgId) {
                currentMsgId = chunk.id;
                toolCallBuf = {};
            }

            if (chunk.tool_call_chunks?.length) {
                for (const tc of chunk.tool_call_chunks) {
                    if (tc.index === undefined) continue;
                    if (!toolCallBuf[tc.index]) {
                        toolCallBuf[tc.index] = { id: '', name: '', args: '' };
                    }
                    const entry = toolCallBuf[tc.index];
                    if (tc.id) entry.id = tc.id;
                    if (tc.name) entry.name += tc.name;
                    if (tc.args) entry.args += tc.args;

                    if (tc.name && entry.name) {
                        let args = {};
                        try {
                            args = JSON.parse(entry.args);
                        } catch {
                            /* partial JSON */
                        }
                        if (onToolCall) onToolCall(entry.name, args);
                    }
                }
                continue;
            }

            if (chunk.content) {
                aiResponse += chunk.content;
                if (onToken) onToken(chunk.content);
            }
        }

        if (onDone) onDone(aiResponse);
    } catch (err) {
        console.error('[Agent] Error:', err);
        const errorMsg = `发生错误: ${err.message}`;
        if (onDone) onDone(errorMsg);
    }
}

async function onProjectOpened(uuid) {
    activeThreadId = uuid || null;
    return loadDisplayHistory(uuid);
}

async function deleteProjectSession(uuid) {
    if (!uuid) return false;
    ensureCheckpointer();
    await checkpointer.deleteThread(uuid);
    if (activeThreadId === uuid) activeThreadId = null;
    return true;
}

/**
 * 参考 server.py 风格：初始化会话（thread）
 */
async function initThread(threadId) {
    if (!threadId) throw new Error('missing thread_id');
    const a = ensureAgentReady();
    const config = { configurable: { thread_id: threadId } };
    try {
        const state = await a.graph.getState(config);
        const hasMessages = Array.isArray(state?.values?.messages) && state.values.messages.length > 0;
        const hasResources = Array.isArray(state?.values?.resources) && state.values.resources.length > 0;
        const hasAnyState = !!state?.values && Object.keys(state.values).length > 0;
        // 只要 thread 已有状态（哪怕尚未聊天，但已有 resources），就不要重置。
        if (hasMessages || hasResources || hasAnyState) {
            return { success: true, message: 'thread_already_exists' };
        }
    } catch {
        // 不存在状态时继续初始化
    }

    // 创建空 checkpoint（与官方 checkpointer 结合）
    await a.graph.updateState(config, { messages: [], resources: [] });
    return { success: true, thread_id: threadId };
}

/**
 * 参考 server.py 风格：获取会话历史
 */
async function getHistory(threadId) {
    if (!threadId) throw new Error('missing thread_id');
    const messages = await loadDisplayHistory(threadId);
    return { success: true, messages };
}

/**
 * 参考 server.py 风格：删除会话
 */
async function deleteThread(threadId) {
    if (!threadId) throw new Error('missing thread_id');
    const ok = await deleteProjectSession(threadId);
    return { success: ok };
}

/**
 * 参考 server.py 风格：获取素材列表（当前项目媒体库）
 */
function getResources(threadId) {
    if (!threadId) throw new Error('missing thread_id');
    return getResourcesState(threadId).then((items) => ({
        success: true,
        resources: items,
    }));
}

async function getResourcesState(threadId) {
    const a = ensureAgentReady();
    const state = await a.graph.getState({ configurable: { thread_id: threadId } }).catch(() => null);
    const resources = state?.values?.resources;
    if (!Array.isArray(resources)) return [];
    return resources
        .map(normalizeResource)
        .filter(Boolean);
}

async function setResourcesState(threadId, resources) {
    const a = ensureAgentReady();
    await a.graph.updateState(
        { configurable: { thread_id: threadId } },
        { resources }
    );
}

async function addResources(threadId, items) {
    if (!threadId) throw new Error('missing thread_id');
    const current = await getResourcesState(threadId);
    const byPath = new Set(current.map((i) => i.resource_path));
    const merged = [...current];
    for (const raw of items || []) {
        const item = normalizeResource(raw);
        if (!item || !item.resource_path || byPath.has(item.resource_path)) continue;
        merged.push(item);
        byPath.add(item.resource_path);
    }
    await setResourcesState(threadId, merged);
    return { success: true, resources: merged };
}

async function removeResource(threadId, resourceId) {
    if (!threadId) throw new Error('missing thread_id');
    const current = await getResourcesState(threadId);
    const next = current.filter((i) => i.resource_id !== resourceId);
    const changed = next.length !== current.length;
    if (changed) await setResourcesState(threadId, next);
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
    onProjectOpened,
    deleteProjectSession,
    initThread,
    getHistory,
    deleteThread,
    getResources,
    addResources,
    removeResource,
};
