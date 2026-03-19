const { ipcRenderer } = require('electron');
const { marked } = require('marked');

marked.setOptions({ breaks: true, gfm: true });

const WELCOME_MESSAGE = `👋 **您好！我是视频编辑助手**

**我可以帮您：**
• 查看和修改工程信息
• 修改文字图层内容
• 调整图层属性（位置、缩放、透明度等）
• 管理素材库

**使用示例：**
• "帮我把标题改成 Hello World"
• "调整图层位置到居中"
• "查看当前工程信息"`;

const messagesEl = document.getElementById('messages');
const inputEl = document.getElementById('input');
const sendBtn = document.getElementById('btn-send');
const toggleMediaBtn = document.getElementById('btn-toggle-media');

let currentAIBubble = null;
let currentTokens = '';
let isStreaming = false;

// ---- Media Library (lazy loaded) ----
let mediaLib = null;

function getMediaLibrary() {
    if (!mediaLib) {
        mediaLib = require('./media_library');
        mediaLib.setStreamingCheck(() => isStreaming);
        mediaLib.init();
    }
    return mediaLib;
}

// ---- Shared helpers ----

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function formatTime(ts) {
    if (!ts) return '';
    const d = new Date(ts);
    return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

function scrollToBottom() {
    requestAnimationFrame(() => { messagesEl.scrollTop = messagesEl.scrollHeight; });
}

function updateSendButton() {
    sendBtn.disabled = isStreaming || !inputEl.value.trim();
}

function _createAIBubbleEl(timestamp) {
    const time = formatTime(timestamp || new Date());
    const el = document.createElement('div');
    el.className = 'flex justify-start gap-2 animate-[fade-in_0.2s_ease]';
    el.innerHTML = `
        <div class="w-8 h-8 rounded-full bg-slate-700/60 flex items-center justify-center shrink-0 mt-0.5">
            <span class="material-symbols-rounded text-sm text-indigo-300">smart_toy</span>
        </div>
        <div class="max-w-[80%] flex flex-col">
            <div class="msg-bubble bg-bubble-agent rounded-2xl rounded-bl-md px-4 py-2.5 text-sm leading-relaxed text-slate-200">
                <div class="message-content"></div>
            </div>
            <span class="text-[10px] text-slate-600 mt-1 px-1">${time}</span>
        </div>
    `;
    messagesEl.appendChild(el);
    return el.querySelector('.message-content');
}

// ---- Message rendering ----

function addUserMessage(text, timestamp) {
    const time = formatTime(timestamp || new Date());
    const el = document.createElement('div');
    el.className = 'flex justify-end gap-2 animate-[fade-in_0.2s_ease]';
    el.innerHTML = `
        <div class="max-w-[80%] flex flex-col items-end">
            <div class="bg-bubble-user text-white rounded-2xl rounded-br-md px-4 py-2.5 text-sm leading-relaxed break-words message-content"></div>
            <span class="text-[10px] text-slate-600 mt-1 px-1">${time}</span>
        </div>
        <div class="w-8 h-8 rounded-full bg-primary/80 flex items-center justify-center shrink-0 mt-0.5">
            <span class="material-symbols-rounded text-sm text-white">person</span>
        </div>
    `;
    el.querySelector('.message-content').innerHTML = marked.parse(text);
    messagesEl.appendChild(el);
    scrollToBottom();
}

function addAIBubble(content, timestamp) {
    const contentEl = _createAIBubbleEl(timestamp);
    contentEl.innerHTML = marked.parse(content);
    scrollToBottom();
}

function startAIMessage(timestamp) {
    removeThinking();
    currentTokens = '';
    isStreaming = true;
    currentAIBubble = _createAIBubbleEl(timestamp);
    scrollToBottom();
}

function appendToken(token) {
    if (!currentAIBubble) startAIMessage();
    currentTokens += token;
    currentAIBubble.textContent = currentTokens;
    scrollToBottom();
}

function finalizeMessage(fullMessage) {
    const text = fullMessage || currentTokens;
    if (currentAIBubble) {
        currentAIBubble.innerHTML = marked.parse(text);
    } else if (text) {
        addAIBubble(text);
    }

    currentAIBubble = null;
    currentTokens = '';
    isStreaming = false;
    updateSendButton();
    scrollToBottom();

    document.querySelectorAll('.tool-chip:not(.done)').forEach(el => {
        el.classList.add('done');
    });
    removeThinking();
}

// ---- Thinking / Tool status ----

function showThinking() {
    removeThinking();
    const el = document.createElement('div');
    el.id = 'thinking';
    el.className = 'flex items-center gap-2 px-2 animate-[fade-in_0.2s_ease]';
    el.innerHTML = `
        <div class="w-8 h-8 rounded-full bg-slate-700/60 flex items-center justify-center shrink-0">
            <span class="material-symbols-rounded text-sm text-indigo-300">smart_toy</span>
        </div>
        <div class="flex items-center gap-2 px-3 py-2 rounded-xl bg-bubble-agent">
            <div class="flex gap-1">
                <div class="typing-dot"></div>
                <div class="typing-dot"></div>
                <div class="typing-dot"></div>
            </div>
            <span class="text-xs text-slate-500">思考中...</span>
        </div>
    `;
    messagesEl.appendChild(el);
    scrollToBottom();
}

function removeThinking() {
    document.getElementById('thinking')?.remove();
}

function showToolCall(toolName) {
    removeThinking();
    const el = document.createElement('div');
    el.className = 'flex items-center gap-2 px-10 animate-[fade-in_0.2s_ease]';
    el.innerHTML = `
        <div class="tool-chip">
            <div class="tool-spinner"></div>
            <span>正在执行: ${escapeHtml(toolName)}</span>
        </div>
    `;
    messagesEl.appendChild(el);
    scrollToBottom();
}

function showToolResult() {
    const pending = messagesEl.querySelectorAll('.tool-chip:not(.done)');
    if (pending.length > 0) {
        const chip = pending[pending.length - 1];
        chip.classList.add('done');
        chip.querySelector('.tool-spinner')?.remove();
        const icon = document.createElement('span');
        icon.className = 'material-symbols-rounded text-sm text-green-400';
        icon.textContent = 'check_circle';
        chip.prepend(icon);
    }
}

// ---- Chat actions ----

function sendMessage() {
    const text = inputEl.value.trim();
    if (!text || isStreaming) return;

    addUserMessage(text);
    inputEl.value = '';
    updateSendButton();

    ipcRenderer.send('chat-message', { message: text });
}

// ---- IPC handlers ----

ipcRenderer.on('ai-status', (_event, { status }) => {
    if (status === 'thinking') showThinking();
});

ipcRenderer.on('ai-token', (_event, { token }) => {
    appendToken(token);
});

ipcRenderer.on('ai-tool-call', (_event, { toolName }) => {
    showToolCall(toolName);
});

ipcRenderer.on('ai-tool-result', () => {
    showToolResult();
});

ipcRenderer.on('ai-done', (_event, { fullMessage }) => {
    finalizeMessage(fullMessage);
});

ipcRenderer.on('chat-history-loaded', (_event, { history }) => {
    messagesEl.innerHTML = '';
    currentAIBubble = null;
    currentTokens = '';
    isStreaming = false;

    addAIBubble(WELCOME_MESSAGE);

    if (history && history.length > 0) {
        for (const msg of history) {
            if (msg.role === 'human') {
                addUserMessage(msg.content, msg.timestamp);
            } else if (msg.role === 'ai') {
                addAIBubble(msg.content, msg.timestamp);
            }
        }
    }
    updateSendButton();
    scrollToBottom();
});

// ---- Event bindings ----

sendBtn.addEventListener('click', sendMessage);

inputEl.addEventListener('keydown', (e) => {
    if (e.isComposing || e.keyCode === 229) return;
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        sendMessage();
    }
});

inputEl.addEventListener('input', updateSendButton);

toggleMediaBtn.addEventListener('click', () => {
    getMediaLibrary().toggle();
});

// ---- Resizer ----

(function initResizer() {
    const resizer = document.getElementById('resizer');
    const chatPanel = document.getElementById('chatPanel');

    if (!resizer || !messagesEl || !chatPanel) return;

    let isResizing = false;
    let startY = 0;
    let startH = 0;

    resizer.addEventListener('mousedown', (e) => {
        isResizing = true;
        startY = e.clientY;
        startH = messagesEl.offsetHeight;
        resizer.classList.add('dragging');
        document.body.style.cursor = 'ns-resize';
        document.body.style.userSelect = 'none';
        e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
        if (!isResizing) return;
        const newH = startH + (e.clientY - startY);
        const header = chatPanel.querySelector('header');
        const available = chatPanel.offsetHeight - (header ? header.offsetHeight : 0) - resizer.offsetHeight;
        if (newH >= 100 && newH <= available - 120) {
            messagesEl.style.height = newH + 'px';
        }
    });

    document.addEventListener('mouseup', () => {
        if (!isResizing) return;
        isResizing = false;
        resizer.classList.remove('dragging');
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    });
})();

updateSendButton();
