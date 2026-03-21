const { DynamicStructuredTool } = require('@langchain/core/tools');
const { z } = require('zod');
const { ipcRenderer } = require('electron');
const crypto = require('crypto');

function callEditor(action, params = {}) {
    return new Promise((resolve, reject) => {
        const id = crypto.randomUUID();
        const timeout = setTimeout(() => {
            ipcRenderer.removeListener('tool-result', handler);
            reject(new Error(`Tool call "${action}" timed out`));
        }, 15000);

        const handler = (_event, result) => {
            if (result.id === id) {
                clearTimeout(timeout);
                ipcRenderer.removeListener('tool-result', handler);
                resolve(result.data);
            }
        };
        ipcRenderer.on('tool-result', handler);
        ipcRenderer.send('tool-call', { id, action, params });
    });
}

/** 动态系统提示词拉取当前工程协议（经主进程转发到编辑器，不作为模型工具） */
function fetchProjectProtocol() {
    return callEditor('getProjectProtocol');
}

/** 与 export 同源的文字图层 id→文案，每次进模型前拉取；改字工具成功后此处即反映新文案 */
function fetchTextLayerDigest() {
    return callEditor('getTextLayerDigest');
}

function createEditorTools() {
    return [
        new DynamicStructuredTool({
            name: 'update_text',
            description: '修改指定文字图层的文本内容。需要提供准确的图层 id（协议 segment 的 id 字段，不是 material id）。',
            schema: z.object({
                layerId: z.string().describe('文字图层的 id（与工程协议中 segment 的 id 一致）'),
                text: z.string().describe('新的文本内容'),
            }),
            func: async ({ layerId, text }) => {
                const result = await callEditor('updateText', { layerId, text });
                return JSON.stringify(result);
            },
        }),

        new DynamicStructuredTool({
            name: 'set_layer_property',
            description: '修改图层的视觉属性。可修改的属性：alpha(透明度0-1), visible(可见性), scaleX/scaleY(缩放), rotation(旋转角度), transformX/transformY(位移)。',
            schema: z.object({
                layerId: z.string().describe('图层 id（协议 segment 的 id）'),
                property: z.enum([
                    'alpha', 'visible', 'scaleX', 'scaleY',
                    'rotation', 'transformX', 'transformY',
                ]).describe('要修改的属性名'),
                value: z.union([z.number(), z.boolean()]).describe('新的属性值'),
            }),
            func: async (params) => {
                const result = await callEditor('setLayerProperty', params);
                return JSON.stringify(result);
            },
        }),

        new DynamicStructuredTool({
            name: 'set_current_time',
            description: '跳转到指定时间点（毫秒）并刷新画面。',
            schema: z.object({
                timeMs: z.number().describe('目标时间点，单位毫秒'),
            }),
            func: async ({ timeMs }) => {
                const result = await callEditor('setCurrentTime', { timeMs });
                return JSON.stringify(result);
            },
        }),
    ];
}

module.exports = { createEditorTools, fetchProjectProtocol, fetchTextLayerDigest };
