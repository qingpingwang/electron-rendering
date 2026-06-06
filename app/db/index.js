const connection = require('./connection');
const projects = require('./projects');
const resourceProjects = require('./resource_projects');
const media = require('./media');

module.exports = {
    init: connection.init,
    close: connection.close,
    getDb: connection.getDb,
    projects,
    resourceProjects,
    media,
};
