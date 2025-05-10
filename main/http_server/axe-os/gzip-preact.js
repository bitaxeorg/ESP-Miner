import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const sourceDirectory = path.join(__dirname, 'dist/axe-os');

// Function to gzip a file
function gzipFile(filePath) {
  const fileContents = fs.readFileSync(filePath);
  const gzippedPath = `${filePath}.gz`;

  const gzipped = zlib.gzipSync(fileContents, {
    level: 9, // Maximum compression
  });

  fs.writeFileSync(gzippedPath, gzipped);
  console.log(`Gzipped: ${filePath} -> ${gzippedPath}`);

  // Remove the original file
  fs.unlinkSync(filePath);
  console.log(`Removed original: ${filePath}`);
}

// Function to process a directory recursively
function processDirectory(dirPath) {
  const items = fs.readdirSync(dirPath);

  for (const item of items) {
    const itemPath = path.join(dirPath, item);
    const stats = fs.statSync(itemPath);

    if (stats.isDirectory()) {
      // Recursively process subdirectories
      processDirectory(itemPath);
    } else if (!itemPath.endsWith('.gz')) {
      // Gzip files that are not already gzipped
      gzipFile(itemPath);
    }
  }
}

// Main execution
console.log(`Processing directory: ${sourceDirectory}`);
processDirectory(sourceDirectory);
console.log('All files have been gzipped successfully.');