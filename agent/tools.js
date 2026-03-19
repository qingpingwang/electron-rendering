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

function createEditorTools() {
    return [
        new DynamicStructuredTool({
            name: 'get_project_info',
            description: '获取当前已加载项目的完整信息，包括画布尺寸、时长、帧率、所有轨道和图层列表。在执行任何编辑操作前，应先调用此工具了解项目结构。',
            schema: z.object({}),
            func: async () => {
                const result = await callEditor('getProjectInfo');
                return JSON.stringify(result, null, 2);
            },
        }),

        new DynamicStructuredTool({
            name: 'update_text',
            description: '修改指定文字图层的文本内容。需要提供准确的图层名称和新文本。',
            schema: z.object({
                layerName: z.string().describe('要修改的文字图层名称'),
                text: z.string().describe('新的文本内容'),
            }),
            func: async ({ layerName, text }) => {
                const result = await callEditor('updateText', { layerName, text });
                return JSON.stringify(result);
            },
        }),

        new DynamicStructuredTool({
            name: 'set_layer_property',
            description: '修改图层的视觉属性。可修改的属性：alpha(透明度0-1), visible(可见性), scaleX/scaleY(缩放), rotation(旋转角度), transformX/transformY(位移)。',
            schema: z.object({
                layerName: z.string().describe('图层名称'),
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

module.exports = { createEditorTools };
