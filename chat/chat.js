const { marked } = require('marked');
const agentClient = require('./agent_client');

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
let currentProjectUUID = null;

// ---- Media Library (lazy loaded) ----
let mediaLib = null;

function getMediaLibrary() {
    if (!mediaLib) {
        mediaLib = require('./media_library');
        mediaLib.setStreamingCheck(() => isStreaming);
        mediaLib.init();
        if (currentProjectUUID) mediaLib.setProject(currentProjectUUID);
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

function _formatArgs(args) {
    if (!args || (typeof args === 'object' && Object.keys(args).length === 0)) return '{}';
    if (typeof args === 'string') {
        try { return JSON.stringify(JSON.parse(args), null, 2); } catch { return args; }
    }
    return JSON.stringify(args, null, 2);
}

function _formatResult(result) {
    if (!result) return '';
    if (typeof result === 'string') {
        try { return JSON.stringify(JSON.parse(result), null, 2); } catch { return result; }
    }
    return JSON.stringify(result, null, 2);
}

function _createToolBlock(toolName, args, opts = {}) {
    const { done = false, result = null, error = false, toolCallId = '' } = opts;
    const el = document.createElement('div');
    el.className = 'flex px-10 animate-[fade-in_0.2s_ease]';

    const stateClass = error ? 'error' : done ? 'done' : '';
    const iconName = error ? 'error' : done ? 'check_circle' : '';
    const iconColor = error ? 'text-red-400' : 'text-green-400';

    const spinnerHtml = !done && !error
        ? '<div class="tool-spinner"></div>'
        : `<span class="material-symbols-rounded text-sm ${iconColor}">${iconName}</span>`;

    const idRow =
        toolCallId ?
            `<div class="tool-block-section tool-block-section--meta">
                <div class="tool-block-label">tool_call_id</div>
                <div class="tool-block-code tool-block-code--id">${escapeHtml(toolCallId)}</div>
            </div>`
        :   '';

    let bodyHtml = `
        ${idRow}
        <div class="tool-block-section">
            <div class="tool-block-label">参数</div>
            <div class="tool-block-code">${escapeHtml(_formatArgs(args))}</div>
        </div>
    `;
    if (result !== null) {
        bodyHtml += `
            <div class="tool-block-section">
                <div class="tool-block-label">结果</div>
                <div class="tool-block-code">${escapeHtml(_formatResult(result))}</div>
            </div>
        `;
    }

    el.innerHTML = `
        <div class="tool-block ${stateClass}">
            <div class="tool-block-header">
                ${spinnerHtml}
                <span class="tool-block-name">${escapeHtml(toolName)}</span>
                <span class="material-symbols-rounded tool-block-toggle">expand_more</span>
            </div>
            <div class="tool-block-body">${bodyHtml}</div>
        </div>
    `;

    const block = el.querySelector('.tool-block');
    if (toolCallId) block.dataset.toolCallId = toolCallId;
    block.querySelector('.tool-block-header').addEventListener('click', () => {
        block.classList.toggle('expanded');
    });

    messagesEl.appendChild(el);
    scrollToBottom();
    return block;
}

function showToolCall(toolName, args, toolCallId) {
    removeThinking();
    _createToolBlock(toolName, args, { toolCallId: toolCallId || '' });
}

function _findOpenToolBlockByCallId(toolCallId) {
    if (!toolCallId) return null;
    const esc =
        typeof CSS !== 'undefined' && typeof CSS.escape === 'function'
            ? CSS.escape(toolCallId)
            : toolCallId.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    try {
        return messagesEl.querySelector(
            `.tool-block[data-tool-call-id="${esc}"]:not(.done):not(.error)`,
        );
    } catch {
        return null;
    }
}

function showToolResult(toolName, result, toolCallId) {
    let block = _findOpenToolBlockByCallId(toolCallId);
    if (!block) {
        const pending = messagesEl.querySelectorAll('.tool-block:not(.done):not(.error)');
        if (pending.length > 0) block = pending[pending.length - 1];
    }
    if (block) {

        let isError = false;
        try {
            const parsed = typeof result === 'string' ? JSON.parse(result) : result;
            isError = parsed && parsed.error;
        } catch {}

        block.classList.add(isError ? 'error' : 'done');

        const header = block.querySelector('.tool-block-header');
        const spinner = header.querySelector('.tool-spinner');
        if (spinner) {
            const icon = document.createElement('span');
            icon.className = `material-symbols-rounded text-sm ${isError ? 'text-red-400' : 'text-green-400'}`;
            icon.textContent = isError ? 'error' : 'check_circle';
            spinner.replaceWith(icon);
        }

        const body = block.querySelector('.tool-block-body');
        const section = document.createElement('div');
        section.className = 'tool-block-section';
        section.innerHTML = `
            <div class="tool-block-label">结果</div>
            <div class="tool-block-code">${escapeHtml(_formatResult(result))}</div>
        `;
        body.appendChild(section);
    }
    scrollToBottom();
}

// ---- History tool rendering ----

let _pendingHistoryToolBlock = null;

function addHistoryToolCall(toolName, args, toolCallId) {
    _pendingHistoryToolBlock = _createToolBlock(toolName, args, {
        done: false,
        toolCallId: toolCallId || '',
    });
    _pendingHistoryToolBlock.classList.remove('animate-[fade-in_0.2s_ease]');
}

function addHistoryToolResult(toolName, result, toolCallId) {
    const block = _findOpenToolBlockByCallId(toolCallId) || _pendingHistoryToolBlock;
    if (block) {
        let isError = false;
        try {
            const parsed = typeof result === 'string' ? JSON.parse(result) : result;
            isError = parsed && parsed.error;
        } catch {}

        block.classList.add(isError ? 'error' : 'done');

        const header = block.querySelector('.tool-block-header');
        const spinner = header.querySelector('.tool-spinner');
        if (spinner) {
            const icon = document.createElement('span');
            icon.className = `material-symbols-rounded text-sm ${isError ? 'text-red-400' : 'text-green-400'}`;
            icon.textContent = isError ? 'error' : 'check_circle';
            spinner.replaceWith(icon);
        }

        if (result) {
            const body = block.querySelector('.tool-block-body');
            const section = document.createElement('div');
            section.className = 'tool-block-section';
            section.innerHTML = `
                <div class="tool-block-label">结果</div>
                <div class="tool-block-code">${escapeHtml(_formatResult(result))}</div>
            `;
            body.appendChild(section);
        }

        if (_pendingHistoryToolBlock === block) _pendingHistoryToolBlock = null;
    }
}

// ---- Chat actions ----

function sendMessage() {
    const text = inputEl.value.trim();
    if (!text || isStreaming) return;

    isStreaming = true;
    addUserMessage(text);
    inputEl.value = '';
    updateSendButton();

    agentClient.sendMessage(text, {
        onThinking: showThinking,
        onToken: appendToken,
        onToolCall: showToolCall,
        onToolResult: showToolResult,
        onDone: finalizeMessage,
    }, {
        threadId: currentProjectUUID,
    });
}

// ---- 历史记录 ----

agentClient.onHistoryLoaded((history) => {
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
            } else if (msg.role === 'tool_call') {
                let argsObj = msg.args;
                if (typeof argsObj === 'string') {
                    try {
                        argsObj = argsObj.trim() ? JSON.parse(argsObj) : {};
                    } catch {
                        argsObj = {};
                    }
                }
                addHistoryToolCall(msg.toolName, argsObj, msg.toolCallId);
            } else if (msg.role === 'tool_result') {
                addHistoryToolResult(msg.toolName, msg.result, msg.toolCallId);
            }
        }
    }
    updateSendButton();
    scrollToBottom();
});

window.__onProjectOpened = async function (uuid, options = {}) {
    currentProjectUUID = uuid;
    await agentClient.notifyProjectOpened(uuid, options);
    if (mediaLib) await mediaLib.setProject(uuid);
};

window.__deleteProjectChatContext = function (uuid) {
    return agentClient.deleteProjectSession(uuid);
};

window.__agentApi = {
    initThread: (threadId) => agentClient.initThread(threadId),
    getHistory: (threadId) => agentClient.getHistory(threadId),
    deleteThread: (threadId) => agentClient.deleteThread(threadId),
    getResources: (threadId) => agentClient.getResources(threadId),
};

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
