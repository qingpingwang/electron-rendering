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
        this._uuid = null;
    }

    loadSession(uuid) {
        this._uuid = uuid;
        this.history = [];

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
            }
        } catch {
            this.history = [];
        }
    }

    _persist() {
        if (!this._uuid) return;
        _ensureDir();
        const serialized = this.history.map(msg => ({
            role: msg._getType(),
            content: typeof msg.content === 'string' ? msg.content : JSON.stringify(msg.content),
            timestamp: msg.additional_kwargs?.timestamp || null,
        }));
        fs.writeFileSync(_filePath(this._uuid), JSON.stringify(serialized, null, 2), 'utf-8');
    }

    addUserMessage(text) {
        const msg = new HumanMessage(text);
        msg.additional_kwargs = { timestamp: new Date().toISOString() };
        this.history.push(msg);
        this._persist();
    }

    addAIMessage(text) {
        const msg = new AIMessage(text);
        msg.additional_kwargs = { timestamp: new Date().toISOString() };
        this.history.push(msg);
        this._persist();
    }

    getHistory() {
        return [...this.history];
    }

    getSerializedHistory() {
        return this.history.map(msg => ({
            role: msg._getType(),
            content: typeof msg.content === 'string' ? msg.content : JSON.stringify(msg.content),
            timestamp: msg.additional_kwargs?.timestamp || null,
        }));
    }

}

module.exports = ChatManager;
