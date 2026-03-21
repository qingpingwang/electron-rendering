const { HumanMessage, AIMessage, ToolMessage, SystemMessage } = require('@langchain/core/messages');

function _ts(msg) {
    const k = msg.additional_kwargs || {};
    if (k.timestamp) return k.timestamp;
    const meta = msg.response_metadata || {};
    if (meta.created_at) return new Date(meta.created_at * 1000).toISOString();
    return new Date().toISOString();
}

function _stringifyContent(content) {
    if (typeof content === 'string') return content;
    if (Array.isArray(content)) {
        return content
            .filter((p) => p && p.type === 'text' && p.text)
            .map((p) => p.text)
            .join('');
    }
    if (content && typeof content === 'object') {
        try {
            return JSON.stringify(content);
        } catch {
            return String(content);
        }
    }
    return content != null ? String(content) : '';
}

/**
 * 将 LangGraph checkpoint 中的 messages 转为聊天 UI 使用的条目（与 chat.js 历史渲染一致）
 */
function serializeLangGraphMessages(messages) {
    const out = [];
    if (!Array.isArray(messages)) return out;

    for (const msg of messages) {
        if (SystemMessage.isInstance(msg)) continue;

        if (HumanMessage.isInstance(msg)) {
            out.push({
                role: 'human',
                content: _stringifyContent(msg.content),
                timestamp: _ts(msg),
            });
            continue;
        }

        if (AIMessage.isInstance(msg)) {
            if (msg.tool_calls?.length) {
                for (const tc of msg.tool_calls) {
                    let argsStr = '{}';
                    if (tc.args !== undefined && tc.args !== null) {
                        argsStr = typeof tc.args === 'string' ? tc.args : JSON.stringify(tc.args);
                    }
                    out.push({
                        role: 'tool_call',
                        toolName: tc.name || '',
                        args: argsStr,
                        toolCallId: tc.id || '',
                        timestamp: _ts(msg),
                    });
                }
            }
            const text = _stringifyContent(msg.content).trim();
            if (text) {
                out.push({
                    role: 'ai',
                    content: text,
                    timestamp: _ts(msg),
                });
            }
            continue;
        }

        if (ToolMessage.isInstance(msg)) {
            out.push({
                role: 'tool_result',
                toolName: msg.name || '',
                result: _stringifyContent(msg.content),
                toolCallId: msg.tool_call_id || '',
                timestamp: _ts(msg),
            });
        }
    }

    return out;
}

module.exports = { serializeLangGraphMessages };
