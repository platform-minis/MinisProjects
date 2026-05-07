import { CompiledProgram } from './codegen';
import { isJumpOp } from './opcodes';

// Serializes a CompiledProgram to the .mbc binary format.
//
// Format:
//   Header    [8]: magic[2] version[1] flags[1] global_count[1] func_count[1] reserved[2]
//   IntPool:       count[2] + int32_LE[] × n
//   FltPool:       count[2] + float32_LE[] × n
//   StrPool:       count[2] + (len[1] + bytes) × n
//   Functions:     param_count[1] local_count[1] code_size[2] code[] × func_count
//
// Jump targets in each function's code are function-relative (0 = first byte of that
// function's code). During packing they are adjusted to absolute prog[] offsets.

export function pack(prog: CompiledProgram): Uint8Array {
    const buf: number[] = [];

    function writeU8(v: number)  { buf.push(v & 0xFF); }
    function writeU16(v: number) { buf.push(v & 0xFF, (v >> 8) & 0xFF); }
    function writeI32(v: number) {
        buf.push(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >>> 24) & 0xFF);
    }
    function writeF32(v: number) {
        const a = new Float32Array(1); a[0] = v;
        const b = new Uint8Array(a.buffer);
        buf.push(b[0], b[1], b[2], b[3]);
    }

    // ── Header ──
    writeU8(0x4D); writeU8(0x43);               // magic 'MC'
    writeU8(0x01);                               // version
    writeU8(0x00);                               // flags
    writeU8(prog.globalCount & 0xFF);            // global_count
    writeU8(prog.functions.length & 0xFF);       // func_count
    writeU16(0);                                 // reserved

    // ── Int pool ──
    writeU16(prog.intPool.length);
    for (const v of prog.intPool) writeI32(v);

    // ── Float pool ──
    writeU16(prog.fltPool.length);
    for (const v of prog.fltPool) writeF32(v);

    // ── String pool ──
    writeU16(prog.strPool.length);
    for (const s of prog.strPool) {
        const bytes = Buffer.from(s, 'utf8');
        writeU8(Math.min(bytes.length, 47));     // max 47 chars (MINIS_STR_LEN-1)
        for (let i = 0; i < Math.min(bytes.length, 47); i++) writeU8(bytes[i]);
    }

    // ── Compute absolute code offsets for each function ──
    // We need to know where each function's code starts in the final buffer.
    // First: calculate the size of the function table header bytes (without code),
    // then lay out code sections.

    // Size of everything written so far = header of each function (4 bytes each)
    const funcHeaderBytes = prog.functions.length * 4; // param[1]+local[1]+size[2]
    let   headerEnd = buf.length + funcHeaderBytes;

    // Compute absolute start offset for each function's code
    const codeOffsets: number[] = [];
    let offset = headerEnd;
    for (const f of prog.functions) {
        codeOffsets.push(offset);
        offset += f.code.length;
    }

    // ── Emit functions (header + adjusted code) ──
    for (let fi = 0; fi < prog.functions.length; fi++) {
        const f = prog.functions[fi];
        const codeOff = codeOffsets[fi];

        writeU8(f.paramCount);
        writeU8(f.localCount);
        writeU16(f.code.length);

        // Copy code, adjusting jump targets to absolute offsets
        const code = f.code.slice();
        for (let i = 0; i < code.length; i += 3) {
            const op = code[i];
            if (isJumpOp(op)) {
                const rel = code[i+1] | (code[i+2] << 8);
                const abs = rel + codeOff;
                code[i+1] = abs & 0xFF;
                code[i+2] = (abs >> 8) & 0xFF;
            }
        }
        for (const b of code) writeU8(b);
    }

    return new Uint8Array(buf);
}

// Disassemble a .mbc file to human-readable text (for debugging)
export function disassemble(data: Uint8Array): string {
    const lines: string[] = [];
    let i = 0;
    const r8  = () => data[i++];
    const r16 = () => { const v = data[i] | (data[i+1] << 8); i += 2; return v; };
    const ri32= () => { const v = data[i]|(data[i+1]<<8)|(data[i+2]<<16)|(data[i+3]<<24); i+=4; return v; };

    if (data[0] !== 0x4D || data[1] !== 0x43) { return 'invalid magic'; }
    i = 2;
    const ver = r8(); const flags = r8(); const nGlobals = r8(); const nFuncs = r8(); r16();
    lines.push(`MinisC bytecode v${ver} | globals=${nGlobals} funcs=${nFuncs}`);

    const ni = r16(); const intPool: number[] = [];
    for (let k=0;k<ni;k++) intPool.push(ri32());
    lines.push(`IntPool[${ni}]: ${intPool.join(', ')}`);

    const nf = r16(); const fltPool: number[] = [];
    for (let k=0;k<nf;k++) { const buf=new Uint8Array(4);for(let b=0;b<4;b++)buf[b]=data[i+b];i+=4;fltPool.push(new Float32Array(buf.buffer)[0]); }
    lines.push(`FltPool[${nf}]: ${fltPool.join(', ')}`);

    const ns = r16(); const strPool: string[] = [];
    for (let k=0;k<ns;k++) { const len=r8(); let s=''; for(let b=0;b<len;b++) s+=String.fromCharCode(data[i++]); strPool.push(s); }
    lines.push(`StrPool[${ns}]: ${strPool.map((s,i)=>`[${i}]"${s}"`).join(', ')}`);

    const OPNAMES: Record<number,string> = {
        0x00:'NOP',0x01:'POP',0x02:'DUP',0x03:'DUP2',0x04:'SWAP',
        0x05:'PUSH_NULL',0x06:'PUSH_TRUE',0x07:'PUSH_FALSE',
        0x08:'PUSH_IMM',0x09:'PUSH_INT',0x0A:'PUSH_FLT',0x0B:'PUSH_STR',
        0x0C:'LOAD_G',0x0D:'STORE_G',0x0E:'LOAD_L',0x0F:'STORE_L',
        0x10:'IADD',0x11:'ISUB',0x12:'IMUL',0x13:'IDIV',0x14:'IMOD',0x15:'INEG',
        0x16:'FADD',0x17:'FSUB',0x18:'FMUL',0x19:'FDIV',0x1A:'FNEG',
        0x1B:'BAND',0x1C:'BOR',0x1D:'BXOR',0x1E:'BNOT',0x1F:'BSHL',0x20:'BSHR',
        0x21:'IEQ',0x22:'INE',0x23:'ILT',0x24:'ILE',0x25:'IGT',0x26:'IGE',
        0x27:'FEQ',0x28:'FNE',0x29:'FLT',0x2A:'FLE',0x2B:'FGT',0x2C:'FGE',
        0x2D:'SEQ',0x2E:'SNE',0x2F:'LAND',0x30:'LOR',0x31:'LNOT',
        0x32:'I2F',0x33:'F2I',0x34:'I2B',0x35:'F2B',
        0x36:'I2S',0x37:'F2S',0x38:'B2S',0x39:'S2I',0x3A:'S2F',
        0x3B:'JUMP',0x3C:'JUMP_T',0x3D:'JUMP_F',0x3E:'JUMP_TK',0x3F:'JUMP_FK',
        0x40:'CALL',0x41:'CALL_N',0x42:'RET',0x43:'RET_V',
        0x44:'NEW_ARR',0x45:'ARR_GET',0x46:'ARR_SET',0x47:'ARR_LEN',
        0x48:'STR_CAT',0x49:'STR_LEN',0x4A:'STR_CHAR',
        0x4B:'IINC_L',0x4C:'IDEC_L',0x4D:'IINC_G',0x4E:'IDEC_G',
    };

    for (let f=0;f<nFuncs;f++) {
        const pc=r8(); const lc=r8(); const csz=r16();
        lines.push(`\n--- func[${f}] params=${pc} locals=${lc} code=${csz}b ---`);
        const end = i + csz;
        while (i < end) {
            const addr = i; const op = r8(); const opr = r16();
            const name = OPNAMES[op] ?? `0x${op.toString(16).padStart(2,'0')}`;
            lines.push(`  ${addr.toString(16).padStart(4,'0')}  ${name.padEnd(10)} ${opr}`);
        }
    }
    return lines.join('\n');
}
