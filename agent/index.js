const { ChatOpenAI } = require('@langchain/openai');
const { HumanMessage, AIMessageChunk, ToolMessage } = require('@langchain/core/messages');
const { createAgent, dynamicSystemPromptMiddleware } = require('langchain');
const { SqliteSaver } = require('@langchain/langgraph-checkpoint-sqlite');
const { z } = require('zod');
const fs = require('fs');
const path = require('path');

const db = require('../db');
const { createEditorTools, fetchProjectProtocol, fetchTextLayerDigest } = require('./tools');
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

/**
 * LangChain 动态系统提示词：每次进模型前 await 拉取「文字快览 + 完整协议 + 素材库」。
 * wrapModelCall 在工具执行后的下一轮同样会重新执行本函数并重新 IPC（数据会更新）。
 */
const dynamicProjectContextMiddleware = dynamicSystemPromptMiddleware(async (_state, runtime) => {
    const threadId = runtime.configurable?.thread_id;

    let textDigestBlock = '';
    try {
        const digest = await fetchTextLayerDigest();
        if (digest?.loaded && Array.isArray(digest.layers) && digest.layers.length > 0) {
            textDigestBlock =
                '## 文字图层当前文案（实时快照，优先用于核对改字）\n\n' +
                '以下为编辑器内存中**每个文字图层**的 `layerId` 与当前 `text`；**每次调用本模型前**都会重新从编辑器拉取。' +
                '`update_text` 成功后，**下一轮**本节即反映新文案（比下方完整协议里的嵌套 `content` 更易读）。\n\n' +
                '```json\n' +
                JSON.stringify(digest.layers, null, 2) +
                '\n```\n\n';
        } else if (digest && !digest.loaded) {
            textDigestBlock =
                '## 文字图层当前文案（实时快照）\n\n' +
                '⚠️ 当前无法列出文字图层（工程未加载或不可用）' +
                (digest.message ? `：${digest.message}` : '') +
                '。\n\n';
        }
    } catch (e) {
        textDigestBlock =
            `## 文字图层当前文案（实时快照）\n\n⚠️ 获取文字图层快照失败：${e.message}\n\n`;
    }

    let protocolBlock = '';
    try {
        const proto = await fetchProjectProtocol();
        if (proto?.loaded && proto.protocol) {
            protocolBlock =
                '## 当前视频工程协议（实时快照）\n\n' +
                '以下为**当前编辑器内存中**导出的完整工程协议 JSON；在**每次调用模型前**都会重新获取，代表**最新状态**。\n\n' +
                '> **重要**：此前对时间线、图层等的**具体操作过程**不会完整记录在本 JSON 中；请结合**历史聊天记录**与**工具调用 / Tool 消息**推断已执行过的编辑步骤。\n\n' +
                '```json\n' +
                proto.protocol +
                '\n```\n\n';
        } else {
            protocolBlock =
                '## 当前视频工程协议（实时快照）\n\n' +
                '⚠️ 当前没有已加载的工程，或无法导出协议。' +
                (proto?.message ? `（${proto.message}）` : '') +
                '\n\n';
        }
    } catch (e) {
        protocolBlock =
            `## 当前视频工程协议（实时快照）\n\n⚠️ 获取协议失败：${e.message}\n\n`;
    }

    let resourcesBlock = '';
    try {
        const resources = threadId ? await getResourcesState(threadId) : [];
        if (resources.length > 0) {
            resourcesBlock =
                '## 素材库（应用侧会话登记，与工程协议无关）\n\n' +
                '以下为当前会话在应用侧维护的**素材库**列表（LangGraph 状态 `resources`）。**仅描述「用户/应用登记到素材库的文件」**。\n\n' +
                '> **与上方视频工程协议的关系**：工程协议 JSON 里的 `materials`、轨道片段引用等，与**本节素材库**是**两套独立数据**，**没有一一对应关系**；不要假设本节某条记录必然出现在协议中，也不要用本节去「解释」协议里的素材字段。\n\n' +
                `共 **${resources.length}** 条；在**每次模型调用前**从检查点读取，为**最新快照**。\n\n` +
                '> **操作历史**：是否已拖入时间轴、剪辑顺序等请以**聊天记录与工具结果**推断；本节**只**反映素材库登记，不涉及时间轴操作。\n\n' +
                '```json\n' +
                `${JSON.stringify(resources, null, 2)}\n` +
                '```\n\n';
        } else {
            resourcesBlock =
                '## 素材库（应用侧会话登记，与工程协议无关）\n\n' +
                '⚠️ 当前会话的素材库中暂无登记记录（与上方工程协议里是否已有 `materials` **无关**；协议有素材不代表本节一定有条目）。如需使用素材库能力，请先让用户将文件加入素材库。\n\n';
        }
    } catch (e) {
        resourcesBlock =
            `## 素材库（应用侧会话登记，与工程协议无关）\n\n⚠️ 读取素材库状态失败：${e.message}\n\n`;
    }

    return textDigestBlock + protocolBlock + resourcesBlock;
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

    const tempRaw = config.OPENAI_TEMPERATURE ?? process.env.OPENAI_TEMPERATURE;
    let temperature = 0.2;
    if (tempRaw !== undefined && tempRaw !== '') {
        const t = Number(tempRaw);
        if (!Number.isNaN(t)) temperature = Math.min(2, Math.max(0, t));
    }

    const llm = new ChatOpenAI({
        modelName,
        temperature,
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
        middleware: [dynamicProjectContextMiddleware],
    });

    console.log('[Agent] Initialized with model:', modelName, '| LangGraph SqliteSaver → app.db');
}

/** 流式 tool_call_chunks 在「仅含 args 的 chunk」里往往不带 name；须在 args JSON 完整解析成功后再通知 UI 一次。 */
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
                const tid = chunk.tool_call_id || undefined;
                if (onToolResult) onToolResult(chunk.name, chunk.content, tid);
                continue;
            }

            if (!(chunk instanceof AIMessageChunk)) continue;

            if (chunk.id !== currentMsgId) {
                flushPendingToolCallChunks(toolCallBuf, onToolCall);
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
                continue;
            }

            if (chunk.content) {
                aiResponse += chunk.content;
                if (onToken) onToken(chunk.content);
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
