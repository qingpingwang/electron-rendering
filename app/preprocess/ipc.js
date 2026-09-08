const { ipcMain, app } = require('electron');
const { preprocess, stopAll } = require('./cache');
function registerPreprocessIPC() {
    ipcMain.handle('media-cache:prepare', async (event, { videoPaths, cacheDir, requestId }) => {
        return preprocess(videoPaths, cacheDir, progress => {
            if (!event.sender.isDestroyed()) { event.sender.send('media-cache:progress', { requestId, progress }); }
        });
    });
    app.on('before-quit', stopAll);
}
module.exports = registerPreprocessIPC;
