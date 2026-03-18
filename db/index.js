const connection = require('./connection');
const projects = require('./projects');
const media = require('./media');

module.exports = {
    init: connection.init,
    close: connection.close,
    getDb: connection.getDb,
    projects,
    media,
};
