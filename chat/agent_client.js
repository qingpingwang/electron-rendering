const agent = require('../agent');

let _initialized = false;
let _historyLoadedCallback = null;

function _ensureInit() {
    if (!_initialized) {
        agent.initAgent();
        _initialized = true;
    }
}

function sendMessage(message, callbacks = {}) {
    _ensureInit();
    agent.handleUserMessage(message, callbacks);
}

function onHistoryLoaded(callback) {
    _historyLoadedCallback = callback;
}

function notifyProjectOpened(uuid) {
    _ensureInit();
    const history = agent.onProjectOpened(uuid);
    if (_historyLoadedCallback) _historyLoadedCallback(history);
}

module.exports = { sendMessage, onHistoryLoaded, notifyProjectOpened };
