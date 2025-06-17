const fs = require('fs');

// Compute GCD for fraction reduction
function gcd(a, b) {
    while (b) {
        const t = b;
        b = a % b;
        a = t;
    }
    return a;
}

// Parameter ranges and constraints
const MIN_FB = 160;
const MAX_FB = 239;
const REFDIV_VALUES = [1, 2];
const POSTDIV1_VALUES = [1, 2, 3, 4, 5, 6, 7];
const VCO_MAX = 3000.0; // Set to 6000 for all frequencies
const MAX_FREQ = 1500.0; // Maximum frequency in MHz
const MAX_DIFF = 1e-6; // Float precision for exact frequency match

// Generate all possible fractions (frequencies)
const fractions = [];
for (const refdiv of REFDIV_VALUES) {
    for (const postdiv1 of POSTDIV1_VALUES) {
        for (let postdiv2 = 1; postdiv2 <= postdiv1; postdiv2++) {
            const D = refdiv * postdiv1 * postdiv2;
            for (let fb = MIN_FB; fb <= MAX_FB; fb++) {
                const g = gcd(fb, D);
                const num = fb / g;
                const denom = D / g;
                const freq = 25.0 * num / denom;
                fractions.push({ num, denom, freq });
            }
        }
    }
}

// Deduplicate fractions by sorting and comparing
fractions.sort((a, b) => {
    const cross = a.num * b.denom - b.num * a.denom;
    if (cross !== 0) return cross;
    return a.denom - b.denom;
});

const uniqueFractions = [fractions[0]];
for (let i = 1; i < fractions.length; i++) {
    if (fractions[i].num !== uniqueFractions[uniqueFractions.length - 1].num ||
        fractions[i].denom !== uniqueFractions[uniqueFractions.length - 1].denom) {
        uniqueFractions.push(fractions[i]);
    }
}

console.log(`Found ${uniqueFractions.length} unique frequencies`);

// Generate table entries
const table = [];
for (const { freq: targetFreq } of uniqueFractions) {
    let bestParam = {
        fb_divider: 0,
        refdiv: 0,
        postdiv1: 0,
        postdiv2: 0,
        vco_freq: 1e9
    };
    let foundValid = false;

    for (const refdiv of REFDIV_VALUES) {
        for (const postdiv1 of POSTDIV1_VALUES) {
            for (let postdiv2 = 1; postdiv2 <= postdiv1; postdiv2++) {
                const D = refdiv * postdiv1 * postdiv2;
                const fb_f = targetFreq * D / 25.0;
                const fb = Math.round(fb_f);
                if (fb < MIN_FB || fb > MAX_FB) continue;

                const actualFreq = 25.0 * fb / D;
                const diff = Math.abs(targetFreq - actualFreq);
                if (diff > MAX_DIFF) continue;

                const vcoFreq = 25.0 * fb / refdiv;
                if (vcoFreq > VCO_MAX) continue;

                if (actualFreq >= MAX_FREQ) continue;

                // Update if better (lower VCO, lower refdiv, lower postdivs)
                if (!foundValid || vcoFreq < bestParam.vco_freq ||
                    (vcoFreq === bestParam.vco_freq && refdiv < bestParam.refdiv) ||
                    (vcoFreq === bestParam.vco_freq && refdiv === bestParam.refdiv && postdiv1 < bestParam.postdiv1) ||
                    (vcoFreq === bestParam.vco_freq && refdiv === bestParam.refdiv && postdiv1 === bestParam.postdiv1 && postdiv2 < bestParam.postdiv2)) {

                    if (bestParam.fb_divider != 0) {
                        console.log(`Evicting entry ${targetFreq.toFixed(6)} MHz: fb=${bestParam.fb_divider}, refdiv=${bestParam.refdiv}, postdiv1=${bestParam.postdiv1}, postdiv2=${bestParam.postdiv2}, vcoFreq=${bestParam.vco_freq.toFixed(6)} MHz`);
                    }
                    bestParam = {
                        fb_divider: fb,
                        refdiv,
                        postdiv1,
                        postdiv2,
                        vco_freq: vcoFreq
                    };
                    foundValid = true;
                }
            }
        }
    }

    if (foundValid) {
        table.push({
            freq: targetFreq,
            fb_divider: bestParam.fb_divider,
            refdiv: bestParam.refdiv,
            postdiv1: bestParam.postdiv1,
            postdiv2: bestParam.postdiv2,
            vcoFreq: bestParam.vco_freq
        });
    }
}

// Sort table by frequency
table.sort((a, b) => a.freq - b.freq);

// Generate C header file
let output = `#ifndef PLL_TABLE_H\n#define PLL_TABLE_H\n\n`;
output += `#define PLL_TABLE_SIZE ${table.length}\n\n`;
output += `typedef struct {\n`;
output += `    float freq;\n`;
output += `    uint8_t fb_divider;\n`;
output += `    uint8_t refdiv;\n`;
output += `    uint8_t postdiv1;\n`;
output += `    uint8_t postdiv2;\n`;
output += `} pll_entry_t;\n\n`;
output += `const pll_entry_t pll_table[PLL_TABLE_SIZE] = {\n`;

table.forEach((entry, index) => {
    output += `    {${entry.freq.toFixed(6)}f, ${entry.fb_divider}, ${entry.refdiv}, ${entry.postdiv1}, ${entry.postdiv2}}${index < table.length - 1 ? ',' : ''} /* vcoFreq: ${entry.vcoFreq} */\n`;
});

output += `};\n\n#endif // PLL_TABLE_H\n`;

// Write to file
fs.writeFileSync('pll_table.h', output);

console.log(`Generated pll_table.h with ${table.length} entries (${(table.length * 8 / 1024).toFixed(2)} KB)`);