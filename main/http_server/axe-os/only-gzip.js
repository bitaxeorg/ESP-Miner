const fs = require('fs');
const path = require('path');

const directory = './dist/axe-os';

fs.readdir(directory, (err, files) => {
  if (err) throw err;

  for (const file of files) {
    if (!file.endsWith('.gz')) {
      fs.unlink(path.join(directory, file), err => {
        if (err) throw err;
      });
    }
  }
});