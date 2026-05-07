#!/usr/bin/env node
import * as fs from 'fs';
import * as path from 'path';
import { tokenize } from './lexer';
import { parse } from './parser';
import { codegen } from './codegen';
import { pack, disassemble } from './packer';

function usage(): void {
    console.error(`
MinisC Compiler — compile C-like scripts to .mbc bytecode for MinisVM

Usage:
  minisc <input.c> [options]

Options:
  -o <file>        Output .mbc file (default: <input>.mbc)
  --disasm         Also print disassembly to stdout
  --disasm-only    Only disassemble an existing .mbc file (input must be .mbc)
  --hex            Print bytecode as hex array (for embedding in C/Arduino)
  -h, --help       Show this help
`);
}

function main(): void {
    const args = process.argv.slice(2);
    if (args.length === 0 || args.includes('-h') || args.includes('--help')) {
        usage(); process.exit(0);
    }

    const input    = args[0];
    const disasmOnly = args.includes('--disasm-only');
    const doDisasm   = args.includes('--disasm') || disasmOnly;
    const doHex      = args.includes('--hex');
    const outIdx     = args.indexOf('-o');
    const outFile    = outIdx >= 0 ? args[outIdx + 1]
                     : input.replace(/\.[^.]+$/, '.mbc');

    // Disassemble existing .mbc
    if (disasmOnly) {
        const data = fs.readFileSync(input);
        console.log(disassemble(new Uint8Array(data)));
        return;
    }

    // Read source
    let src: string;
    try { src = fs.readFileSync(input, 'utf8'); }
    catch (e) { console.error(`Error reading '${input}': ${(e as Error).message}`); process.exit(1); }

    // Compile
    let bytecode: Uint8Array;
    try {
        const tokens  = tokenize(src!);
        const ast     = parse(tokens);
        const program = codegen(ast);
        bytecode      = pack(program);
    } catch (e) {
        console.error(`Compile error: ${(e as Error).message}`);
        process.exit(1);
    }

    // Write .mbc
    fs.writeFileSync(outFile, Buffer.from(bytecode!));
    const kb = (bytecode!.length / 1024).toFixed(2);
    console.log(`✓  ${path.basename(input)}  →  ${outFile}  (${bytecode!.length} bytes / ${kb} KB)`);

    // Optional disassembly
    if (doDisasm) {
        console.log('');
        console.log(disassemble(bytecode!));
    }

    // Optional hex dump
    if (doHex) {
        const hex = Array.from(bytecode!)
            .map(b => `0x${b.toString(16).padStart(2, '0')}`)
            .join(', ');
        const varName = path.basename(input, path.extname(input))
            .replace(/[^a-zA-Z0-9_]/g, '_');
        console.log(`\n// Embed in your sketch:`);
        console.log(`const uint8_t ${varName}_mbc[] = {\n  ${hex}\n};`);
        console.log(`const size_t ${varName}_mbc_len = ${bytecode!.length};`);
    }
}

main();
