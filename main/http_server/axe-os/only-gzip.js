const fs = require('fs');
const path = require('path');

const directory = './dist/axe-os';

function processDirectory(dirPath) {
    fs.readdir(dirPath, (err, files) => {
        if (err) throw err;

        for (const file of files) {
            const filePath = path.join(dirPath, file);

            fs.stat(filePath, (err, stats) => {
                if (err) throw err;

                if (stats.isDirectory()) {
                    // If it's a directory, process it recursively
                    processDirectory(filePath);
                } else if (!file.endsWith('.gz')) {
                    // If it's a file and doesn't end with .gz, unlink it
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
