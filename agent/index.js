const { ChatOpenAI } = require('@langchain/openai');
const { AIMessageChunk, ToolMessage } = require('@langchain/core/messages');
const { createAgent } = require('langchain');
const fs = require('fs');
const path = require('path');

const { createEditorTools } = require('./tools');
const { SYSTEM_PROMPT } = require('./prompts');
const ChatManager = require('./chat_manager');

let agent = null;
let chatManager = null;

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

function initAgent() {
    chatManager = new ChatManager();

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

    agent = createAgent({
        model: llm,
        tools,
        systemPrompt: SYSTEM_PROMPT,
    });

    console.log('[Agent] Initialized with model:', modelName);
}

async function handleUserMessage(userText, callbacks = {}) {
    if (!agent) initAgent();
    if (!agent) return;

    const { onThinking, onToken, onToolCall, onToolResult, onDone } = callbacks;

    chatManager.addUserMessage(userText);
    if (onThinking) onThinking();

    try {
        const input = { messages: chatManager.getHistory() };
        const stream = await agent.stream(input, { streamMode: 'messages' });

        let currentMsgId = null;
        let currentRole = null;
        let aiResponse = '';
        let toolCallBuf = {};

        for await (const [chunk, metadata] of stream) {
            if (chunk instanceof ToolMessage) {
                if (onToolResult) onToolResult(chunk.name, chunk.content);
                chatManager.addToolResult(chunk.name, chunk.content);
                continue;
            }

            if (!(chunk instanceof AIMessageChunk)) continue;

            // 新消息开始
            if (chunk.id !== currentMsgId) {
                currentMsgId = chunk.id;
                currentRole = null;
                toolCallBuf = {};
            }

            // tool call chunks
            if (chunk.tool_call_chunks?.length) {
                currentRole = 'tool_call';
                for (const tc of chunk.tool_call_chunks) {
                    if (tc.index === undefined) continue;
                    if (!toolCallBuf[tc.index]) {
                        toolCallBuf[tc.index] = { id: '', name: '', args: '' };
                    }
                    const entry = toolCallBuf[tc.index];
                    if (tc.id) entry.id = tc.id;
                    if (tc.name) entry.name += tc.name;
                    if (tc.args) entry.args += tc.args;

                    // 名字刚收齐时通知前端
                    if (tc.name && entry.name) {
                        let args = {};
                        try { args = JSON.parse(entry.args); } catch {}
                        if (onToolCall) onToolCall(entry.name, args);
                        chatManager.addToolCall(entry.name, args);
                    }
                }
                continue;
            }

            // 普通文本 token
            if (chunk.content) {
                currentRole = 'ai';
                aiResponse += chunk.content;
                if (onToken) onToken(chunk.content);
            }
        }

        chatManager.addAIMessage(aiResponse);
        if (onDone) onDone(aiResponse);
    } catch (err) {
        console.error('[Agent] Error:', err);
        const errorMsg = `发生错误: ${err.message}`;
        chatManager.addAIMessage(errorMsg);
        if (onDone) onDone(errorMsg);
    }
}

function onProjectOpened(uuid) {
    if (!chatManager) chatManager = new ChatManager();
    chatManager.loadSession(uuid);
    return chatManager.getSerializedHistory();
}

module.exports = { initAgent, handleUserMessage, onProjectOpened };
