const fs = require('fs');
const path = require('path');

const version = require('child_process').execSync('git describe --tags --always --dirty').toString().trim();

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
