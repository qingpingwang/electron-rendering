const agent = require('../agent');

let _initialized = false;
let _historyLoadedCallback = null;

function _ensureInit() {
    if (!_initialized) {
        agent.initAgent();
        _initialized = true;
    }
}

function sendMessage(message, callbacks = {}, options = {}) {
    _ensureInit();
    agent.handleUserMessage(message, callbacks, options);
}

function onHistoryLoaded(callback) {
    _historyLoadedCallback = callback;
}

async function notifyProjectOpened(uuid, options = {}) {
    _ensureInit();
    const mode = options.mode || agent.DEFAULT_MODE;
    if (options.isNewThread) {
        await agent.initThread(uuid, mode);
    }
    const { messages: history = [] } = await agent.getHistory(uuid, mode);
    if (_historyLoadedCallback) _historyLoadedCallback(history, { mode });
}

async function initThread(threadId, mode) {
    _ensureInit();
    return agent.initThread(threadId, mode);
}

async function getHistory(threadId, mode) {
    _ensureInit();
    return agent.getHistory(threadId, mode);
}

async function deleteThread(threadId, mode) {
    _ensureInit();
    return agent.deleteThread(threadId, mode);
}

async function listResourceThreads() {
    _ensureInit();
    return agent.listResourceThreads();
}

async function getResources(threadId, mode) {
    _ensureInit();
    return agent.getResources(threadId, mode);
}

module.exports = {
    sendMessage,
    onHistoryLoaded,
    notifyProjectOpened,
    initThread,
    getHistory,
    deleteThread,
    listResourceThreads,
    getResources,
    DEFAULT_MODE: agent.DEFAULT_MODE,
};
