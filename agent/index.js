const { ChatOpenAI } = require('@langchain/openai');
const { SystemMessage } = require('@langchain/core/messages');
const fs = require('fs');
const path = require('path');

const { SYSTEM_PROMPT } = require('./prompts');
const ChatManager = require('./chat_manager');

let model = null;
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
    const baseURL = config.OPENAI_BASE_URL || process.env.OPENAI_BASE_URL || undefined;
    const modelName = config.OPENAI_MODEL_NAME || process.env.OPENAI_MODEL_NAME || undefined;

    if (!apiKey || !modelName || !baseURL) {
        console.warn('[Agent] No OPENAI_API_KEY, OPENAI_MODEL_NAME, or OPENAI_BASE_URL found. Set it in .env file.');
        return;
    }

    // 渲染进程的 process.env 默认没有 .env，LangChain 内部会读 process.env，这里同步一份
    process.env.OPENAI_API_KEY = apiKey;
    process.env.OPENAI_BASE_URL = baseURL;
    process.env.OPENAI_MODEL_NAME = modelName;

    const llm = new ChatOpenAI({
        modelName,
        temperature: 0.7,
        streaming: true,
        openAIApiKey: apiKey,
        configuration: baseURL ? { baseURL } : undefined,
    });

    model = llm;
    console.log('[Agent] Initialized with model:', modelName);
}

async function handleUserMessage(userText, callbacks = {}) {
    if (!model) initAgent();

    const { onThinking, onToken, onDone } = callbacks;

    chatManager.addUserMessage(userText);
    if (onThinking) onThinking();

    try {
        const messages = [
            new SystemMessage(SYSTEM_PROMPT),
            ...chatManager.getHistory(),
        ];

        let aiResponse = '';
        const stream = await model.stream(messages);

        for await (const chunk of stream) {
            if (chunk.content) {
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
