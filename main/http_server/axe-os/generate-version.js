const fs = require('fs');
const path = require('path');

const buildInfo = {
  version: process.env.APP_VERSION,
  commit: require('child_process').execSync('git rev-parse HEAD').toString().trim(),
  buildTime: new Date().toISOString()
};

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.json');
fs.writeFileSync(outputPath, JSON.stringify(buildInfo, null, 2));

console.log(`Generated ${outputPath} with version ${buildInfo.version}`);