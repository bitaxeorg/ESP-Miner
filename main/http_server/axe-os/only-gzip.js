const fs = require('fs');
const path = require('path');

const directory = './dist/axe-os';

function processDirectory(dirPath) {
  fs.readdir(dirPath, (err, files) => {
    if (err) throw err;

    const gzBase = new Set(files.filter(f => f.endsWith('.gz')).map(f => f.slice(0, -3)));

    for (const file of files) {
      const filePath = path.join(dirPath, file);

      fs.stat(filePath, (err, stats) => {
        if (err) throw err;

        // If it's a directory, process it recursively
        if (stats.isDirectory()) {
          return processDirectory(filePath);
        }
        // If it's a file and doesn't end with .gz, unlink it
        if (gzBase.has(file) && !file.endsWith('.gz')) {
          fs.unlink(filePath, (err) => {
            if (err) throw err;
            console.log(`Removed file: ${filePath}`);
          });
        }
      });
    }
  });
}

processDirectory(directory);
