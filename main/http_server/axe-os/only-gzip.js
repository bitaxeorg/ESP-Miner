const fs = require('fs');
const path = require('path');

const directory = './dist/axe-os';

fs.readdir(directory, (err, files) => {
    if (err) throw err;

    for (const file of files) {
        const filePath = path.join(directory, file);

        fs.stat(filePath, (err, stats) => {
            if (err) throw err;

            if (stats.isDirectory()) {
                // If it's a directory, call rmdir after deleting its contents
                fs.rm(filePath, { recursive: true }, (err) => {
                    if (err) throw err;
                    console.log(`Removed directory: ${filePath}`);
                });
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
