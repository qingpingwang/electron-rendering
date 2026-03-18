const { ChatOpenAI } = require('@langchain/openai');
const { HumanMessage, SystemMessage, AIMessage, ToolMessage } = require('@langchain/core/messages');
const { ipcMain } = require('electron');
const fs = require('fs');
const path = require('path');

const { createEditorTools, setEditorWebContents } = require('./tools');
const { SYSTEM_PROMPT } = require('./prompts');
const ChatManager = require('./chat_manager');

let model = null;
let tools = [];
let toolMap = {};
let chatManager = null;
let _chatWebContents = null;

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

function initAgent(editorWebContents, chatWebContents) {
    setEditorWebContents(editorWebContents);
    _chatWebContents = chatWebContents;
    chatManager = new ChatManager();

    const config = getConfig();
    const apiKey = config.OPENAI_API_KEY || process.env.OPENAI_API_KEY || '';
    const baseURL = config.OPENAI_BASE_URL || process.env.OPENAI_BASE_URL || undefined;
    const modelName = config.OPENAI_MODEL_NAME || process.env.OPENAI_MODEL_NAME || 'gpt-4o-mini';

    if (!apiKey) {
        console.warn('[Agent] No OPENAI_API_KEY found. Set it in .env file.');
    }

    const llm = new ChatOpenAI({
        modelName,
        temperature: 0.7,
        streaming: true,
        openAIApiKey: apiKey,
        configuration: baseURL ? { baseURL } : undefined,
    });

    tools = createEditorTools();
    toolMap = {};
    for (const t of tools) {
        toolMap[t.name] = t;
    }

    model = llm.bindTools(tools);

    _setupChatIPC();
    console.log('[Agent] Initialized with model:', modelName);
}

function _setupChatIPC() {
    ipcMain.on('chat-message', async (_event, { message }) => {
        if (!model || !_chatWebContents || _chatWebContents.isDestroyed()) return;
        await handleUserMessage(message);
    });

    ipcMain.on('project-opened', (_event, { uuid }) => {
        onProjectOpened(uuid);
    });
}

async function handleUserMessage(userText) {
    chatManager.addUserMessage(userText);
    sendToChat('ai-status', { status: 'thinking' });

    try {
        const messages = [
            new SystemMessage(SYSTEM_PROMPT),
            ...chatManager.getHistory(),
        ];

        let aiResponse = '';
        const MAX_ITERATIONS = 10;

        for (let i = 0; i < MAX_ITERATIONS; i++) {
            let fullContent = '';
            let toolCalls = [];

            const stream = await model.stream(messages);
            let chunks = [];

            for await (const chunk of stream) {
                chunks.push(chunk);

                if (chunk.content) {
                    fullContent += chunk.content;
                    sendToChat('ai-token', { token: chunk.content });
                }

                if (chunk.tool_call_chunks?.length) {
                    for (const tc of chunk.tool_call_chunks) {
                        if (tc.index !== undefined) {
                            while (toolCalls.length <= tc.index) {
                                toolCalls.push({ id: '', name: '', args: '' });
                            }
                            const entry = toolCalls[tc.index];
                            if (tc.id) entry.id = tc.id;
                            if (tc.name) entry.name += tc.name;
                            if (tc.args) entry.args += tc.args;
                        }
                    }
                }
            }

            const aiMsg = new AIMessage({
                content: fullContent,
                tool_calls: toolCalls.filter(tc => tc.name).map(tc => ({
                    id: tc.id,
                    name: tc.name,
                    args: safeParseJSON(tc.args),
                })),
            });
            messages.push(aiMsg);

            const validToolCalls = aiMsg.tool_calls?.filter(tc => tc.name) || [];
            if (validToolCalls.length === 0) {
                aiResponse = fullContent;
                break;
            }

            for (const tc of validToolCalls) {
                sendToChat('ai-tool-call', { toolName: tc.name, args: JSON.stringify(tc.args) });

                let result;
                try {
                    const tool = toolMap[tc.name];
                    if (!tool) throw new Error(`Unknown tool: ${tc.name}`);
                    result = await tool.invoke(tc.args);
                } catch (err) {
                    result = JSON.stringify({ error: err.message });
                }

                sendToChat('ai-tool-result', { output: result });
                messages.push(new ToolMessage({ content: result, tool_call_id: tc.id }));
            }
        }

        chatManager.addAIMessage(aiResponse);
        sendToChat('ai-done', { fullMessage: aiResponse });
    } catch (err) {
        console.error('[Agent] Error:', err);
        const errorMsg = `发生错误: ${err.message}`;
        chatManager.addAIMessage(errorMsg);
        sendToChat('ai-done', { fullMessage: errorMsg });
    }
}

function sendToChat(channel, data) {
    if (_chatWebContents && !_chatWebContents.isDestroyed()) {
        _chatWebContents.send(channel, data);
    }
}

function safeParseJSON(str) {
    try {
        return JSON.parse(str);
    } catch {
        return {};
    }
}

function onProjectOpened(uuid) {
    if (!chatManager) return;
    chatManager.loadSession(uuid);
    if (_chatWebContents && !_chatWebContents.isDestroyed()) {
        _chatWebContents.send('chat-history-loaded', {
            uuid,
            history: chatManager.getSerializedHistory(),
        });
    }
}

function updateWebContents(editorWebContents, chatWebContents) {
    setEditorWebContents(editorWebContents);
    _chatWebContents = chatWebContents;
}

module.exports = { initAgent, updateWebContents, onProjectOpened };
