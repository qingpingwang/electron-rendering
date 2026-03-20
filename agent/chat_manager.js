const { HumanMessage, AIMessage } = require('@langchain/core/messages');
const fs = require('fs');
const path = require('path');

const CHAT_DIR = path.join(__dirname, '..', 'test', 'chat_history');

function _ensureDir() {
    if (!fs.existsSync(CHAT_DIR)) {
        fs.mkdirSync(CHAT_DIR, { recursive: true });
    }
}

function _filePath(uuid) {
    return path.join(CHAT_DIR, `${uuid}.json`);
}

class ChatManager {
    constructor() {
        this.history = [];
        this._displayHistory = [];
        this._uuid = null;
    }

    loadSession(uuid) {
        this._uuid = uuid;
        this.history = [];
        this._displayHistory = [];

        if (!uuid) return;

        try {
            const fp = _filePath(uuid);
            if (!fs.existsSync(fp)) return;
            const raw = JSON.parse(fs.readFileSync(fp, 'utf-8'));
            if (!Array.isArray(raw)) return;

            for (const entry of raw) {
                if (entry.role === 'human') {
                    const msg = new HumanMessage(entry.content);
                    msg.additional_kwargs = { timestamp: entry.timestamp };
                    this.history.push(msg);
                } else if (entry.role === 'ai') {
                    const msg = new AIMessage(entry.content);
                    msg.additional_kwargs = { timestamp: entry.timestamp };
                    this.history.push(msg);
                }
                // tool_call / tool_result 不进 LangChain history，只进 displayHistory
                if (['human', 'ai', 'tool_call', 'tool_result'].includes(entry.role)) {
                    this._displayHistory.push(entry);
                }
            }
        } catch {
            this.history = [];
            this._displayHistory = [];
        }
    }

    _persist() {
        if (!this._uuid) return;
        _ensureDir();
        fs.writeFileSync(_filePath(this._uuid), JSON.stringify(this._displayHistory, null, 2), 'utf-8');
    }

    addUserMessage(text) {
        const ts = new Date().toISOString();
        const msg = new HumanMessage(text);
        msg.additional_kwargs = { timestamp: ts };
        this.history.push(msg);

        this._displayHistory.push({ role: 'human', content: text, timestamp: ts });
        this._persist();
    }

    addAIMessage(text) {
        const ts = new Date().toISOString();
        const msg = new AIMessage(text);
        msg.additional_kwargs = { timestamp: ts };
        this.history.push(msg);

        this._displayHistory.push({ role: 'ai', content: text, timestamp: ts });
        this._persist();
    }

    addToolCall(toolName, args) {
        this._displayHistory.push({
            role: 'tool_call',
            toolName,
            args: typeof args === 'string' ? args : JSON.stringify(args),
            timestamp: new Date().toISOString(),
        });
        this._persist();
    }

    addToolResult(toolName, result) {
        this._displayHistory.push({
            role: 'tool_result',
            toolName,
            result: typeof result === 'string' ? result : JSON.stringify(result),
            timestamp: new Date().toISOString(),
        });
        this._persist();
    }

    getHistory() {
        return [...this.history];
    }

    getSerializedHistory() {
        return [...this._displayHistory];
    }
}

module.exports = ChatManager;
