#!/usr/bin/env python3
import os
import sys

def generate_c_arrays(input_dir, output_c_path, output_h_path):
    files = []
    
    print(f"Scanning directory: {input_dir}")
    # Traverse directory recursively
    for root, _, filenames in os.walk(input_dir):
        for filename in filenames:
            full_path = os.path.join(root, filename)
            rel_path = os.path.relpath(full_path, input_dir)
            
            # Determine C-friendly path and gzip flag
            if rel_path.endswith('.gz'):
                url_path = '/' + rel_path[:-3]  # Remove .gz
                is_gzipped = True
            else:
                url_path = '/' + rel_path
                is_gzipped = False
                
            # Normalize path slashes for URL format
            url_path = url_path.replace('\\', '/')
            files.append((full_path, url_path, is_gzipped))

    # Sort files to ensure deterministic output
    files.sort(key=lambda x: x[1])

    # Generate .h header file contents
    h_content = """#ifndef EMBEDDED_WEB_UI_H
#define EMBEDDED_WEB_UI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *path;
    const uint8_t *data;
    size_t size;
    bool is_gzipped;
} EmbeddedFile;

const EmbeddedFile* get_embedded_file(const char *path);

#endif // EMBEDDED_WEB_UI_H
"""

    # Generate .c file contents
    c_content = []
    c_content.append('#include "embedded_web_ui.h"')
    c_content.append('#include <string.h>')
    c_content.append('')

    total_embedded_size = 0
    # Write each file's raw bytes as a static array
    for idx, (full_path, url_path, is_gzipped) in enumerate(files):
        with open(full_path, 'rb') as f:
            data = f.read()
        
        file_size = len(data)
        total_embedded_size += file_size
        gzip_status = "gzipped" if is_gzipped else "raw"
        print(f"  -> Embedding asset: {url_path} ({file_size} bytes, {gzip_status})")
            
        c_content.append(f'// {url_path}')
        c_content.append(f'static const uint8_t file_data_{idx}[] = {{')
        
        # Write bytes in chunks of 12 for readability and to prevent compiler line limits
        bytes_str = []
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            bytes_str.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk))
        c_content.append(',\n'.join(bytes_str))
        c_content.append('};')
        c_content.append('')

    # Write the lookup array
    c_content.append('static const EmbeddedFile embedded_files[] = {')
    for idx, (_, url_path, is_gzipped) in enumerate(files):
        gzip_str = 'true' if is_gzipped else 'false'
        c_content.append(f'    {{ "{url_path}", file_data_{idx}, sizeof(file_data_{idx}), {gzip_str} }},')
    c_content.append('};')
    c_content.append('')
    c_content.append('static const size_t embedded_files_count = sizeof(embedded_files) / sizeof(embedded_files[0]);')
    c_content.append('')
    c_content.append("""const EmbeddedFile* get_embedded_file(const char *path) {
    for (size_t i = 0; i < embedded_files_count; i++) {
        if (strcmp(embedded_files[i].path, path) == 0) {
            return &embedded_files[i];
        }
    }
    return NULL;
}""")

    # Write the output files
    os.makedirs(os.path.dirname(output_c_path), exist_ok=True)
    os.makedirs(os.path.dirname(output_h_path), exist_ok=True)
    
    with open(output_c_path, 'w') as f:
        f.write('\n'.join(c_content) + '\n')
        
    with open(output_h_path, 'w') as f:
        f.write(h_content)

    print(f"Successfully embedded {len(files)} files (total size: {total_embedded_size} bytes).")
    print(f"Output files generated:\n  - C: {output_c_path}\n  - H: {output_h_path}")

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: embed_web_ui.py <input_dir> <output_c> <output_h>")
        sys.exit(1)
    generate_c_arrays(sys.argv[1], sys.argv[2], sys.argv[3])
