/**
 * 视频编辑 mode：迁移自原 agent/tools.js + agent/prompts.js。
 * 行为完全等价，只是从全局单例搬到 mode 注册表。
 */
const { DynamicStructuredTool } = require('@langchain/core/tools');
const { dynamicSystemPromptMiddleware } = require('langchain');
const { z } = require('zod');
const { ipcRenderer } = require('electron');
const crypto = require('crypto');

const RESOURCE_SCHEMA = z.object({
    resource_id: z.string(),
    resource_name: z.string(),
    resource_path: z.string(),
    resource_type: z.enum(['video', 'audio', 'image']),
    resource_size: z.number().optional(),
    added_at: z.string().optional(),
});

const STATE_SCHEMA = z.object({
    resources: z.array(RESOURCE_SCHEMA).optional(),
});

const SYSTEM_PROMPT = `你是一个专业的视频编辑助手，集成在一个基于 Electron + C++ 的视频编辑器中。

## 关于「系统提示中的工程数据」

- 在同一条用户消息触发的**每一次模型调用前**，系统会**自动**在提示词末尾追加两类**实时快照**（二者**数据源不同**，**不要混为一谈**）：
  1. **当前视频工程协议**（完整 JSON，来自编辑器内存导出；其中的 \`materials\` / 轨道引用等属于工程侧）
  2. **素材库**（应用侧会话登记的 \`resources\` 列表；**与协议里的素材无对应关系**，只表示「已登记到素材库的文件」）
- 上述快照始终代表**最新状态**，你**无需**再调用工具仅为了「看一眼工程结构」。
- **操作历史**（例如先后改了哪些字、拖了哪些素材）**不会**完整编码在上述 JSON 里；请根据**历史聊天记录**、**工具调用与 Tool 消息**来推断用户已做过的步骤。

## 核心能力

1. 依据系统提示中的**最新工程协议**理解画布、轨道与图层结构；**素材库一节**仅用于知晓应用侧登记了哪些文件，**不能**替代协议中的工程素材信息
2. 修改文字图层的文本内容
3. 调整图层属性（透明度、位置、缩放、旋转、可见性）
4. 控制播放时间线

## 工作流程

1. 先阅读系统提示中**工程协议**（轨道/图层等）与**素材库**（独立列表）两节，分别理解工程状态与登记文件
2. 根据用户需求选择合适的工具执行操作
3. 操作完成后简要确认结果

## 回复规范

- 简洁专业，直接回答
- 执行操作后说明做了什么
- 如果无法执行（如项目未加载），明确告知原因
- 使用 Markdown 格式增强可读性`;

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

function fetchProjectProtocol() { return callEditor('getProjectProtocol'); }
function fetchTextLayerDigest() { return callEditor('getTextLayerDigest'); }

function createTools() {
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

/**
 * 动态系统提示：每次进模型前 await 拉取「文字快览 + 完整协议 + 素材库」。
 * 由 agent/index.js 注入 getResourcesState 回调，避免循环依赖。
 */
function createMiddleware({ getResourcesState }) {
    return [
        dynamicSystemPromptMiddleware(async (_state, runtime) => {
            const threadId = runtime.configurable?.thread_id;

            let textDigestBlock = '';
            try {
                const digest = await fetchTextLayerDigest();
                if (digest?.loaded && Array.isArray(digest.layers) && digest.layers.length > 0) {
                    textDigestBlock =
                        '## 文字图层当前文案（实时快照，优先用于核对改字）\n\n' +
                        '以下为编辑器内存中**每个文字图层**的 `layerId` 与当前 `text`；**每次调用本模型前**都会重新从编辑器拉取。' +
                        '`update_text` 成功后，**下一轮**本节即反映新文案（比下方完整协议里的嵌套 `content` 更易读）。\n\n' +
                        '```json\n' +
                        JSON.stringify(digest.layers, null, 2) +
                        '\n```\n\n';
                } else if (digest && !digest.loaded) {
                    textDigestBlock =
                        '## 文字图层当前文案（实时快照）\n\n' +
                        '⚠️ 当前无法列出文字图层（工程未加载或不可用）' +
                        (digest.message ? `：${digest.message}` : '') +
                        '。\n\n';
                }
            } catch (e) {
                textDigestBlock =
                    `## 文字图层当前文案（实时快照）\n\n⚠️ 获取文字图层快照失败：${e.message}\n\n`;
            }

            let protocolBlock = '';
            try {
                const proto = await fetchProjectProtocol();
                if (proto?.loaded && proto.protocol) {
                    protocolBlock =
                        '## 当前视频工程协议（实时快照）\n\n' +
                        '以下为**当前编辑器内存中**导出的完整工程协议 JSON；在**每次调用模型前**都会重新获取，代表**最新状态**。\n\n' +
                        '> **重要**：此前对时间线、图层等的**具体操作过程**不会完整记录在本 JSON 中；请结合**历史聊天记录**与**工具调用 / Tool 消息**推断已执行过的编辑步骤。\n\n' +
                        '```json\n' +
                        proto.protocol +
                        '\n```\n\n';
                } else {
                    protocolBlock =
                        '## 当前视频工程协议（实时快照）\n\n' +
                        '⚠️ 当前没有已加载的工程，或无法导出协议。' +
                        (proto?.message ? `（${proto.message}）` : '') +
                        '\n\n';
                }
            } catch (e) {
                protocolBlock =
                    `## 当前视频工程协议（实时快照）\n\n⚠️ 获取协议失败：${e.message}\n\n`;
            }

            let resourcesBlock = '';
            try {
                const resources = threadId ? await getResourcesState(threadId) : [];
                if (resources.length > 0) {
                    resourcesBlock =
                        '## 素材库（应用侧会话登记，与工程协议无关）\n\n' +
                        '以下为当前会话在应用侧维护的**素材库**列表（LangGraph 状态 `resources`）。**仅描述「用户/应用登记到素材库的文件」**。\n\n' +
                        '> **与上方视频工程协议的关系**：工程协议 JSON 里的 `materials`、轨道片段引用等，与**本节素材库**是**两套独立数据**，**没有一一对应关系**；不要假设本节某条记录必然出现在协议中，也不要用本节去「解释」协议里的素材字段。\n\n' +
                        `共 **${resources.length}** 条；在**每次模型调用前**从检查点读取，为**最新快照**。\n\n` +
                        '> **操作历史**：是否已拖入时间轴、剪辑顺序等请以**聊天记录与工具结果**推断；本节**只**反映素材库登记，不涉及时间轴操作。\n\n' +
                        '```json\n' +
                        `${JSON.stringify(resources, null, 2)}\n` +
                        '```\n\n';
                } else {
                    resourcesBlock =
                        '## 素材库（应用侧会话登记，与工程协议无关）\n\n' +
                        '⚠️ 当前会话的素材库中暂无登记记录（与上方工程协议里是否已有 `materials` **无关**；协议有素材不代表本节一定有条目）。如需使用素材库能力，请先让用户将文件加入素材库。\n\n';
                }
            } catch (e) {
                resourcesBlock =
                    `## 素材库（应用侧会话登记，与工程协议无关）\n\n⚠️ 读取素材库状态失败：${e.message}\n\n`;
            }

            return textDigestBlock + protocolBlock + resourcesBlock;
        }),
    ];
}

module.exports = {
    id: 'editor',
    label: '视频编辑',
    systemPrompt: SYSTEM_PROMPT,
    stateSchema: STATE_SCHEMA,
    createTools,
    createMiddleware,
};
