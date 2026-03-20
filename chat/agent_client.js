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
    if (options.isNewThread) {
        await agent.initThread(uuid);
    }
    const { messages: history = [] } = await agent.getHistory(uuid);
    if (_historyLoadedCallback) _historyLoadedCallback(history);
}

async function deleteProjectSession(uuid) {
    _ensureInit();
    return agent.deleteThread(uuid);
}

async function initThread(threadId) {
    _ensureInit();
    return agent.initThread(threadId);
}

async function getHistory(threadId) {
    _ensureInit();
    return agent.getHistory(threadId);
}

async function deleteThread(threadId) {
    _ensureInit();
    return agent.deleteThread(threadId);
}

async function getResources(threadId) {
    _ensureInit();
    return agent.getResources(threadId);
}

module.exports = {
    sendMessage,
    onHistoryLoaded,
    notifyProjectOpened,
    deleteProjectSession,
    initThread,
    getHistory,
    deleteThread,
    getResources,
};
