const { marked } = require('marked');
const agentClient = require('./agent_client');

marked.setOptions({ breaks: true, gfm: true });

const WELCOME_MESSAGES = {
    editor: `👋 **您好！我是视频编辑助手**

**我可以帮您：**
• 查看和修改工程信息
• 修改文字图层内容
• 调整图层属性（位置、缩放、透明度等）
• 管理素材库

**使用示例：**
• "帮我把标题改成 Hello World"
• "调整图层位置到居中"
• "查看当前工程信息"`,

    resource: `🎨 **您好！我是渲染资源工程助手**

**我可以帮您：**
• 编写 GLSL Shader（顶点/片段着色器）
• 生成 \`config.json\` 渲染资源配置
• 配置内/外部 uniform 参数

**安全约定：**
• 读取：可读任意位置（参考资料、其他工程等）
• 写入：仅限沙箱目录内

**使用示例：**
• "新建一个高斯模糊效果，外部参数 sigma 范围 0~10"
• "把 simple_effect 复制一份改成红色调滤镜"
• "看一下 blur_effect 的 config 怎么配的"`,
};

const HEADER_LABELS = {
    editor: 'AI 编辑助手',
    resource: '渲染资源助手',
};

const messagesEl = document.getElementById('messages');
const inputEl = document.getElementById('input');
const sendBtn = document.getElementById('btn-send');
const toggleMediaBtn = document.getElementById('btn-toggle-media');
const headerLabelEl = document.querySelector('header span.text-sm.font-semibold');

let currentAIBubble = null;
let currentTokens = '';
let isStreaming = false;
let currentProjectUUID = null;
let currentMode = agentClient.DEFAULT_MODE || 'editor';

function applyModeUI(mode) {
    if (headerLabelEl) headerLabelEl.textContent = HEADER_LABELS[mode] || HEADER_LABELS.editor;
    // 媒体库按钮仅在视频编辑模式可见
    if (toggleMediaBtn) {
        toggleMediaBtn.style.display = (mode === 'editor') ? '' : 'none';
    }
    // 输入提示
    if (inputEl) {
        inputEl.placeholder = (mode === 'resource')
            ? '描述你想要的 shader 或资源配置...'
            : '输入你的编辑需求...';
    }
}

/** 已发送的用户消息（倒序：0 = 最近一条），Shift+↑/↓ 像终端一样翻阅 */
let _inputHistory = [];
let _inputHistoryIndex = -1;
let _inputBeforeHistory = '';

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

function _getInputHistoryMessage(index) {
    if (index < 0 || index >= _inputHistory.length) return null;
    return _inputHistory[index];
}

/** @param {number} direction 1 = ↑ 更早一条；-1 = ↓ 更近一条 */
function navigateInputHistory(direction) {
    if (_inputHistory.length === 0) return;
    if (_inputHistoryIndex === -1) {
        _inputBeforeHistory = inputEl.value;
    }
    const newIndex = _inputHistoryIndex + direction;
    if (direction === 1) {
        if (newIndex >= _inputHistory.length) return;
        _inputHistoryIndex = newIndex;
        const m = _getInputHistoryMessage(newIndex);
        if (m !== null) inputEl.value = m;
    } else {
        if (newIndex < 0) {
            _inputHistoryIndex = -1;
            inputEl.value = _inputBeforeHistory;
            _inputBeforeHistory = '';
        } else {
            _inputHistoryIndex = newIndex;
            const m = _getInputHistoryMessage(newIndex);
            if (m !== null) inputEl.value = m;
        }
    }
    updateSendButton();
}

function onInputElInput() {
    updateSendButton();
    if (_inputHistoryIndex !== -1) {
        const hist = _getInputHistoryMessage(_inputHistoryIndex);
        if (hist !== null && inputEl.value !== hist) {
            _inputHistoryIndex = -1;
            _inputBeforeHistory = inputEl.value;
        }
    }
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

/** 结束当前流式 AI 气泡（思考段 / 上一段 assistant），便于后续工具块或下一条 assistant 新开气泡 */
function finalizeStreamingAIBubble() {
    if (!currentAIBubble) return;
    const row = currentAIBubble.closest('.flex.justify-start');
    const raw = currentTokens || '';
    if (raw.trim()) {
        currentAIBubble.innerHTML = marked.parse(raw);
    } else if (row) {
        row.remove();
    }
    currentAIBubble = null;
    currentTokens = '';
}

function appendToken(token) {
    if (!currentAIBubble) startAIMessage();
    currentTokens += token;
    currentAIBubble.textContent = currentTokens;
    scrollToBottom();
}

function finalizeMessage(fullMessage) {
    const isError = typeof fullMessage === 'string' && fullMessage.startsWith('发生错误');
    if (isError) {
        if (currentAIBubble) {
            currentAIBubble.innerHTML = marked.parse(fullMessage);
        } else if (fullMessage) {
            addAIBubble(fullMessage);
        }
    } else if (currentAIBubble) {
        currentAIBubble.innerHTML = marked.parse(currentTokens || '');
    } else if (fullMessage && String(fullMessage).trim()) {
        addAIBubble(fullMessage);
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
            : '';

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
    finalizeStreamingAIBubble();
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
        } catch { }

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
        } catch { }

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

    if (_inputHistory.length === 0 || _inputHistory[0] !== text) {
        _inputHistory.unshift(text);
    }
    _inputHistoryIndex = -1;
    _inputBeforeHistory = '';

    isStreaming = true;
    addUserMessage(text);
    inputEl.value = '';
    updateSendButton();

    agentClient.sendMessage(text, {
        onThinking: showThinking,
        onToken: appendToken,
        onToolCall: showToolCall,
        onToolResult: showToolResult,
        onSegmentBreak: finalizeStreamingAIBubble,
        onDone: finalizeMessage,
    }, {
        threadId: currentProjectUUID,
        mode: currentMode,
    });
}

// ---- 历史记录 ----

agentClient.onHistoryLoaded((history, meta = {}) => {
    if (meta.mode) currentMode = meta.mode;
    applyModeUI(currentMode);

    messagesEl.innerHTML = '';
    currentAIBubble = null;
    currentTokens = '';
    isStreaming = false;

    _inputHistory = [];
    _inputHistoryIndex = -1;
    _inputBeforeHistory = '';

    addAIBubble(WELCOME_MESSAGES[currentMode] || WELCOME_MESSAGES.editor);

    if (history && history.length > 0) {
        const humanLines = [];
        for (const msg of history) {
            if (msg.role === 'human') humanLines.push(msg.content);
        }
        _inputHistory = humanLines.slice().reverse();

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
    currentMode = options.mode || agentClient.DEFAULT_MODE || 'editor';
    applyModeUI(currentMode);
    await agentClient.notifyProjectOpened(uuid, { ...options, mode: currentMode });
    if (currentMode === 'editor' && mediaLib) await mediaLib.setProject(uuid);
};

window.__deleteProjectChatContext = function (uuid, mode) {
    return agentClient.deleteThread(uuid, mode);
};

window.__agentApi = {
    initThread:   (threadId, mode) => agentClient.initThread(threadId, mode),
    getHistory:   (threadId, mode) => agentClient.getHistory(threadId, mode),
    deleteThread: (threadId, mode) => agentClient.deleteThread(threadId, mode),
    getResources: (threadId, mode) => agentClient.getResources(threadId, mode),
};

// ---- Event bindings ----

sendBtn.addEventListener('click', sendMessage);

inputEl.addEventListener('keydown', (e) => {
    if (e.isComposing || e.keyCode === 229) return;
    if (e.shiftKey && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) {
        e.preventDefault();
        navigateInputHistory(e.key === 'ArrowUp' ? 1 : -1);
        return;
    }
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        sendMessage();
    }
});

inputEl.addEventListener('input', onInputElInput);

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

applyModeUI(currentMode);
updateSendButton();
