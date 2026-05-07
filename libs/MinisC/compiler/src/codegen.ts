import * as A from './ast';
import { Op } from './opcodes';
import { buildNativeMap, NativeInfo } from './natives';

// ── Types ─────────────────────────────────────────────────────────────────────

export interface CompiledFunction {
    name:        string;
    paramCount:  number;
    localCount:  number;
    code:        number[];  // flat bytecode bytes (3 per instruction, function-relative jump targets)
}

export interface CompiledProgram {
    globalCount: number;
    intPool:     number[];
    fltPool:     number[];
    strPool:     string[];
    functions:   CompiledFunction[];  // index 0 = implicit main
}

// ── Variable descriptor ───────────────────────────────────────────────────────

interface VarInfo {
    name:    string;
    type:    A.CType;
    scope:   'local' | 'global';
    index:   number;
}

interface FuncInfo {
    name:      string;
    index:     number;
    params:    A.Param[];
    retType:   A.CType;
}

interface LoopCtx {
    breakPatches:    number[];   // positions of JUMP operands to patch to exit
    continuePatches: number[];   // positions of JUMP operands to patch to update/head
}

// ── Codegen ───────────────────────────────────────────────────────────────────

export class Codegen {
    private intPool:  number[] = [];
    private fltPool:  number[] = [];
    private strPool:  string[] = [];

    private globals:  VarInfo[] = [];
    private functions: FuncInfo[] = [];
    private nativeMap = buildNativeMap();

    private compiled: CompiledFunction[] = [];

    // Per-function state
    private curCode:   number[] = [];
    private locals:    VarInfo[][] = [];  // scope stack (frames)
    private loopStack: LoopCtx[] = [];
    private curFunc!:  FuncInfo;

    // ── Entry point ──────────────────────────────────────────────────────────

    compile(program: A.Program): CompiledProgram {
        // Pass 1: collect all function signatures
        this.collectDecls(program);

        // Pass 2: compile implicit main (top-level statements)
        this.compileMain(program);

        // Pass 3: compile each function
        for (const decl of program.decls) {
            if (decl.kind === 'FuncDecl') this.compileFuncDecl(decl);
        }

        return {
            globalCount: this.globals.length,
            intPool: this.intPool,
            fltPool: this.fltPool,
            strPool: this.strPool,
            functions: this.compiled,
        };
    }

    // ── Pass 1: collect ───────────────────────────────────────────────────────

    private collectDecls(program: A.Program): void {
        // func[0] = main (implicit)
        this.functions.push({ name: '__main', index: 0, params: [], retType: 'void' });

        for (const decl of program.decls) {
            if (decl.kind === 'FuncDecl') {
                if (this.findFunc(decl.name)) throw new Error(`Duplicate function: ${decl.name}`);
                this.functions.push({
                    name: decl.name,
                    index: this.functions.length,
                    params: decl.params,
                    retType: decl.retType,
                });
            }
        }
    }

    // ── Pass 2: implicit main ─────────────────────────────────────────────────

    private compileMain(program: A.Program): void {
        this.curFunc = this.functions[0];
        this.curCode = [];
        this.locals = [[]];  // one scope level for main
        this.loopStack = [];

        for (const decl of program.decls) {
            if (decl.kind === 'VarDeclStmt') {
                this.compileGlobalVarDecl(decl);
            } else if (decl.kind !== 'FuncDecl') {
                this.compileStmt(decl as A.Stmt);
            }
        }

        // implicit return
        this.emit(Op.RETURN);

        this.compiled.push({
            name: '__main',
            paramCount: 0,
            localCount: 0,  // main has no locals (uses globals)
            code: this.curCode,
        });
    }

    private compileGlobalVarDecl(decl: A.VarDeclStmt): void {
        const existing = this.globals.find(g => g.name === decl.name);
        if (existing) throw new Error(`Duplicate global: ${decl.name} at line ${decl.line}`);
        const idx = this.globals.length;
        this.globals.push({ name: decl.name, type: decl.type, scope: 'global', index: idx });

        if (A.isArray(decl.type)) {
            // Allocate array
            if (decl.size) {
                this.compileExpr(decl.size);
                this.emit(Op.NEW_ARRAY, 0);  // 0 = pop size from stack
            } else {
                this.emit(Op.NEW_ARRAY, 1);  // default size 1
            }
            this.emit(Op.STORE_GLOBAL, idx);
        } else if (decl.init) {
            const exprType = this.compileExpr(decl.init);
            this.coerce(exprType, decl.type);
            this.emit(Op.STORE_GLOBAL, idx);
        } else {
            this.emit(Op.PUSH_NULL);
            this.emit(Op.STORE_GLOBAL, idx);
        }
    }

    // ── Pass 3: function ──────────────────────────────────────────────────────

    private compileFuncDecl(decl: A.FuncDecl): void {
        const fi = this.findFunc(decl.name)!;
        this.curFunc = fi;
        this.curCode = [];
        this.locals = [[]];  // push function scope
        this.loopStack = [];

        // Register params as local[0..n-1]
        for (const p of decl.params) {
            this.declareLocal(p.name, p.type);
        }

        this.compileBlock(decl.body);

        // Implicit return at end
        this.emit(Op.RETURN);

        const localCount = this.countLocals();
        this.compiled.push({
            name: decl.name,
            paramCount: decl.params.length,
            localCount,
            code: this.curCode,
        });
    }

    // ── Statements ────────────────────────────────────────────────────────────

    private compileStmt(stmt: A.Stmt): void {
        switch (stmt.kind) {
            case 'VarDeclStmt':   this.compileLocalVarDecl(stmt); break;
            case 'Block':         this.compileBlock(stmt); break;
            case 'ExprStmt':      this.compileExprStmt(stmt); break;
            case 'IfStmt':        this.compileIf(stmt); break;
            case 'WhileStmt':     this.compileWhile(stmt); break;
            case 'ForStmt':       this.compileFor(stmt); break;
            case 'ReturnStmt':    this.compileReturn(stmt); break;
            case 'BreakStmt':     this.compileBreak(); break;
            case 'ContinueStmt':  this.compileContinue(); break;
        }
    }

    private compileBlock(block: A.Block): void {
        this.pushScope();
        for (const s of block.stmts) this.compileStmt(s);
        this.popScope();
    }

    private compileExprStmt(stmt: A.ExprStmt): void {
        const t = this.compileExpr(stmt.expr);
        if (t !== 'void') this.emit(Op.POP);
    }

    private compileLocalVarDecl(decl: A.VarDeclStmt): void {
        const idx = this.declareLocal(decl.name, decl.type);
        if (A.isArray(decl.type)) {
            if (decl.size) {
                this.compileExpr(decl.size);
                this.emit(Op.NEW_ARRAY, 0);
            } else {
                this.emit(Op.NEW_ARRAY, 1);
            }
            this.emit(Op.STORE_LOCAL, idx);
        } else if (decl.init) {
            const exprType = this.compileExpr(decl.init);
            this.coerce(exprType, decl.type);
            this.emit(Op.STORE_LOCAL, idx);
        } else {
            this.emit(Op.PUSH_NULL);
            this.emit(Op.STORE_LOCAL, idx);
        }
    }

    private compileIf(stmt: A.IfStmt): void {
        this.compileExpr(stmt.cond);
        const falseJump = this.emitJump(Op.JUMP_FALSE);

        this.compileBlock(stmt.then);

        if (stmt.elseif || stmt.els) {
            const skipJump = this.emitJump(Op.JUMP);
            this.patchJump(falseJump);
            if (stmt.elseif) this.compileIf(stmt.elseif);
            else if (stmt.els) this.compileBlock(stmt.els);
            this.patchJump(skipJump);
        } else {
            this.patchJump(falseJump);
        }
    }

    private compileWhile(stmt: A.WhileStmt): void {
        const head = this.currentPos();
        this.compileExpr(stmt.cond);
        const exitJump = this.emitJump(Op.JUMP_FALSE);

        const ctx: LoopCtx = { breakPatches: [], continuePatches: [] };
        this.loopStack.push(ctx);
        this.compileBlock(stmt.body);
        this.loopStack.pop();

        // continue → jump back to head (condition re-evaluation)
        for (const p of ctx.continuePatches) this.patchAt(p, head);
        this.emitAbsJump(head);

        const exit = this.currentPos();
        this.patchJump(exitJump);
        for (const p of ctx.breakPatches) this.patchAt(p, exit);
    }

    private compileFor(stmt: A.ForStmt): void {
        this.pushScope();

        // Init
        if (stmt.init) {
            if (stmt.init.kind === 'VarDeclStmt') this.compileLocalVarDecl(stmt.init);
            else this.compileExprStmt(stmt.init);
        }

        const head = this.currentPos();

        // Condition
        let exitJump = -1;
        if (stmt.cond) {
            this.compileExpr(stmt.cond);
            exitJump = this.emitJump(Op.JUMP_FALSE);
        }

        // Body
        const ctx: LoopCtx = { breakPatches: [], continuePatches: [] };
        this.loopStack.push(ctx);
        this.compileBlock(stmt.body);
        this.loopStack.pop();

        // Update head (where continue jumps)
        const updateHead = this.currentPos();
        for (const p of ctx.continuePatches) this.patchAt(p, updateHead);

        if (stmt.update) {
            const t = this.compileExpr(stmt.update);
            if (t !== 'void') this.emit(Op.POP);
        }

        this.emitAbsJump(head);

        const exit = this.currentPos();
        if (exitJump >= 0) this.patchJump(exitJump);
        for (const p of ctx.breakPatches) this.patchAt(p, exit);

        this.popScope();
    }

    private compileReturn(stmt: A.ReturnStmt): void {
        if (stmt.value) {
            const t = this.compileExpr(stmt.value);
            this.coerce(t, this.curFunc.retType);
            this.emit(Op.RETURN_VAL);
        } else {
            this.emit(Op.RETURN);
        }
    }

    private compileBreak(): void {
        if (this.loopStack.length === 0) throw new Error('break outside loop');
        const pos = this.emitJump(Op.JUMP);
        this.loopStack[this.loopStack.length - 1].breakPatches.push(pos);
    }

    private compileContinue(): void {
        if (this.loopStack.length === 0) throw new Error('continue outside loop');
        const pos = this.emitJump(Op.JUMP);
        this.loopStack[this.loopStack.length - 1].continuePatches.push(pos);
    }

    // ── Expressions ───────────────────────────────────────────────────────────
    // Returns the resulting CType ('void' if expression produces no stack value)

    private compileExpr(expr: A.Expr): A.CType {
        switch (expr.kind) {
            case 'IntLit':    return this.compileIntLit(expr);
            case 'FloatLit':  return this.compileFloatLit(expr);
            case 'BoolLit':   this.emit(expr.value ? Op.PUSH_TRUE : Op.PUSH_FALSE); return 'bool';
            case 'StringLit': this.emit(Op.PUSH_STR, this.addStr(expr.value)); return 'string';
            case 'NullLit':   this.emit(Op.PUSH_NULL); return 'void';
            case 'Ident':     return this.compileLoad(expr);
            case 'BinaryExpr':return this.compileBinary(expr);
            case 'UnaryExpr': return this.compileUnary(expr);
            case 'PostfixExpr':return this.compilePostfix(expr);
            case 'AssignExpr':return this.compileAssign(expr);
            case 'CallExpr':  return this.compileCall(expr);
            case 'IndexExpr': return this.compileIndex(expr);
        }
    }

    private compileIntLit(expr: A.IntLit): A.CType {
        const v = expr.value;
        if (v >= -32768 && v <= 32767) {
            this.emit(Op.PUSH_IMM, v & 0xFFFF);
        } else {
            this.emit(Op.PUSH_INT, this.addInt(v));
        }
        return 'int';
    }

    private compileFloatLit(expr: A.FloatLit): A.CType {
        this.emit(Op.PUSH_FLT, this.addFlt(expr.value));
        return 'float';
    }

    private compileLoad(expr: A.Ident): A.CType {
        const v = this.resolveVar(expr.name, expr.line);
        if (v.scope === 'local')  this.emit(Op.LOAD_LOCAL,  v.index);
        else                      this.emit(Op.LOAD_GLOBAL, v.index);
        return v.type;
    }

    private compileStore(name: string, line: number): void {
        const v = this.resolveVar(name, line);
        if (v.scope === 'local')  this.emit(Op.STORE_LOCAL,  v.index);
        else                      this.emit(Op.STORE_GLOBAL, v.index);
    }

    private compileIndex(expr: A.IndexExpr): A.CType {
        this.compileExpr(expr.array);
        this.compileExpr(expr.index);
        this.emit(Op.ARR_GET);
        // Return element type
        if (expr.array.kind === 'Ident') {
            const v = this.resolveVar(expr.array.name, (expr.array as A.Ident).line);
            return A.baseType(v.type);
        }
        return 'int'; // fallback
    }

    private compileBinary(expr: A.BinaryExpr): A.CType {
        // Short-circuit && and ||
        if (expr.op === '&&') return this.compileShortCircuit(expr, false);
        if (expr.op === '||') return this.compileShortCircuit(expr, true);

        const lt = this.compileExpr(expr.left);
        const rt = this.compileExpr(expr.right);

        // String concatenation
        if (expr.op === '+' && (lt === 'string' || rt === 'string')) {
            // Ensure both sides are strings
            if (lt !== 'string') this.insertConversion(lt, 'string', true);
            if (rt !== 'string') this.insertConversion(rt, 'string', false);
            this.emit(Op.STR_CAT);
            return 'string';
        }

        // Numeric promotion: if one side is float, promote int to float
        const useFloat = lt === 'float' || rt === 'float';
        if (useFloat) {
            if (lt === 'int') this.emitBeforeTop(Op.I2F);  // promote left (below top)
            if (rt === 'int') this.emit(Op.I2F);           // promote right (top)
        }

        const isComp = ['==','!=','<','<=','>','>='].includes(expr.op);
        const isBit  = ['&','|','^','<<','>>'].includes(expr.op);

        if (isComp) {
            if (useFloat) {
                const opMap: Record<string,Op> = {'==':Op.FEQ,'!=':Op.FNE,'<':Op.FLT,'<=':Op.FLE,'>':Op.FGT,'>=':Op.FGE};
                this.emit(opMap[expr.op]!); return 'bool';
            } else {
                const opMap: Record<string,Op> = {'==':Op.IEQ,'!=':Op.INE,'<':Op.ILT,'<=':Op.ILE,'>':Op.IGT,'>=':Op.IGE};
                this.emit(opMap[expr.op]!); return 'bool';
            }
        }
        if (isBit) {
            const opMap: Record<string,Op> = {'&':Op.BAND,'|':Op.BOR,'^':Op.BXOR,'<<':Op.BSHL,'>>':Op.BSHR};
            this.emit(opMap[expr.op]!); return 'int';
        }
        if (useFloat) {
            const opMap: Record<string,Op> = {'+':Op.FADD,'-':Op.FSUB,'*':Op.FMUL,'/':Op.FDIV};
            this.emit(opMap[expr.op]!); return 'float';
        }
        const opMap: Record<string,Op> = {'+':Op.IADD,'-':Op.ISUB,'*':Op.IMUL,'/':Op.IDIV,'%':Op.IMOD};
        this.emit(opMap[expr.op]!); return 'int';
    }

    private compileShortCircuit(expr: A.BinaryExpr, isOr: boolean): A.CType {
        // For &&: if left is false, skip right (result = left)
        // For ||: if left is true,  skip right (result = left)
        this.compileExpr(expr.left);
        const shortJump = this.emitJump(isOr ? Op.JUMP_TRUE_K : Op.JUMP_FALSE_K);
        this.emit(Op.POP);  // discard left, evaluate right
        this.compileExpr(expr.right);
        this.patchJump(shortJump);
        return 'bool';
    }

    // Insert conversion opcode for a value that is NOT at the top of stack
    // (used when right operand hasn't been pushed yet — insert before top)
    private emitBeforeTop(op: Op): void {
        // We need to insert an opcode "below" the top item.
        // Strategy: emit SWAP, emit op, emit SWAP (for unary conversions of the second-to-top)
        this.emit(Op.SWAP);
        this.emit(op);
        this.emit(Op.SWAP);
    }

    private insertConversion(from: A.CType, to: A.CType, isLeft: boolean): void {
        if (isLeft) this.emitBeforeTop(this.convOp(from, to)!);
        else        this.emit(this.convOp(from, to)!);
    }

    private compileUnary(expr: A.UnaryExpr): A.CType {
        // Cast expressions (stored as UnaryExpr with op = type name)
        if (expr.op === 'int' as A.UnaryOp) {
            const t = this.compileExpr(expr.operand); this.coerce(t, 'int'); return 'int';
        }
        if (expr.op === 'float' as A.UnaryOp) {
            const t = this.compileExpr(expr.operand); this.coerce(t, 'float'); return 'float';
        }
        if (expr.op === 'bool' as A.UnaryOp) {
            const t = this.compileExpr(expr.operand); this.coerce(t, 'bool'); return 'bool';
        }
        if (expr.op === 'string' as A.UnaryOp) {
            const t = this.compileExpr(expr.operand); this.coerce(t, 'string'); return 'string';
        }

        switch (expr.op) {
            case '-': {
                const t = this.compileExpr(expr.operand);
                this.emit(t === 'float' ? Op.FNEG : Op.INEG);
                return t;
            }
            case '!': this.compileExpr(expr.operand); this.emit(Op.LNOT); return 'bool';
            case '~': this.compileExpr(expr.operand); this.emit(Op.BNOT); return 'int';
            case '++': return this.compilePrefixInc(expr.operand, 1);
            case '--': return this.compilePrefixInc(expr.operand, -1);
        }
        throw new Error(`Unknown unary op: ${expr.op}`);
    }

    private compilePrefixInc(operand: A.Expr, delta: 1 | -1): A.CType {
        // ++x → load x, add 1, store x, result = new value
        const t = this.compileExpr(operand);
        this.emit(Op.PUSH_IMM, delta === 1 ? 1 : 0xFFFF); // 0xFFFF = -1 as int16
        this.emit(Op.IADD);
        this.emit(Op.DUP);  // dup new value for the result
        this.compileStoreFromExpr(operand);
        return t === 'float' ? 'float' : 'int';
    }

    private compilePostfix(expr: A.PostfixExpr): A.CType {
        // i++ → load i, dup, add 1, store i; result = original value
        const t = this.compileExpr(expr.operand);
        this.emit(Op.DUP);
        this.emit(Op.PUSH_IMM, expr.op === '++' ? 1 : 0xFFFF);
        this.emit(Op.IADD);
        this.compileStoreFromExpr(expr.operand);
        return t === 'float' ? 'float' : 'int';
    }

    private compileStoreFromExpr(expr: A.Expr): void {
        if (expr.kind === 'Ident') {
            this.compileStore(expr.name, expr.line);
        } else if (expr.kind === 'IndexExpr') {
            // For arr[i] store: we need arr_ref and idx on stack, then SWAP with value
            // Stack state: [new_value]
            // We need: [arr_ref, idx, new_value] → ARR_SET
            // Emit DUP of new_value then load arr+idx then reorder... simpler:
            // Actually at this point stack has new_value on top.
            // We need to get arr_ref and idx below it: use a temp approach.
            // Simplification: not supported in this pass (limitation of simple codegen).
            // For assignment to arr[i], use compileAssign which handles it properly.
            throw new Error('prefix/postfix on array element not supported; use arr[i] = arr[i] + 1');
        }
    }

    private compileAssign(expr: A.AssignExpr): A.CType {
        const { op, target, value } = expr;

        if (target.kind === 'Ident') {
            const v = this.resolveVar(target.name, target.line);

            if (op === '=') {
                const t = this.compileExpr(value);
                this.coerce(t, v.type);
                this.emit(Op.DUP);  // leave value on stack as expression result
                if (v.scope === 'local')  this.emit(Op.STORE_LOCAL,  v.index);
                else                      this.emit(Op.STORE_GLOBAL, v.index);
            } else {
                // Compound: load, compute, store
                if (v.scope === 'local')  this.emit(Op.LOAD_LOCAL,  v.index);
                else                      this.emit(Op.LOAD_GLOBAL, v.index);
                const rt = this.compileExpr(value);
                this.emitCompound(op, v.type, rt);
                this.emit(Op.DUP);
                if (v.scope === 'local')  this.emit(Op.STORE_LOCAL,  v.index);
                else                      this.emit(Op.STORE_GLOBAL, v.index);
            }
            return v.type;
        }

        if (target.kind === 'IndexExpr') {
            // arr[i] = val  →  stack: [arr_ref, i, val] → ARR_SET (no result for compound)
            this.compileExpr(target.array);
            this.compileExpr(target.index);
            if (op === '=') {
                const t = this.compileExpr(value);
                this.emit(Op.ARR_SET);
                return t;
            } else {
                // Compound on array element: need DUP2 to get arr+idx for both GET and SET
                this.emit(Op.DUP2);             // [arr,i,arr,i]
                this.emit(Op.ARR_GET);          // [arr,i,old]
                const rt = this.compileExpr(value);
                this.emitCompound(op, 'int', rt);  // [arr,i,new]
                this.emit(Op.ARR_SET);
                return 'int';
            }
        }

        throw new Error('Invalid assignment target');
    }

    private emitCompound(op: A.AssignOp, ltype: A.CType, rtype: A.CType): void {
        const useFloat = ltype === 'float' || rtype === 'float';
        if (useFloat && rtype === 'int') this.emit(Op.I2F);
        if (useFloat && ltype === 'int') this.emitBeforeTop(Op.I2F);
        switch (op) {
            case '+=': this.emit(useFloat ? Op.FADD : Op.IADD); break;
            case '-=': this.emit(useFloat ? Op.FSUB : Op.ISUB); break;
            case '*=': this.emit(useFloat ? Op.FMUL : Op.IMUL); break;
            case '/=': this.emit(useFloat ? Op.FDIV : Op.IDIV); break;
            case '%=': this.emit(Op.IMOD); break;
        }
    }

    private compileCall(expr: A.CallExpr): A.CType {
        // Native?
        const native = this.nativeMap.get(expr.callee);
        if (native) {
            if (expr.args.length !== native.info.argc) {
                throw new Error(`Native '${expr.callee}' expects ${native.info.argc} args at line ${expr.line}`);
            }
            for (const arg of expr.args) this.compileExpr(arg);
            this.emit(Op.CALL_NATIVE, native.idx);
            return native.info.retType;
        }

        // Script function?
        const fi = this.findFunc(expr.callee);
        if (!fi) throw new Error(`Unknown function '${expr.callee}' at line ${expr.line}`);
        if (expr.args.length !== fi.params.length) {
            throw new Error(`Function '${expr.callee}' expects ${fi.params.length} args at line ${expr.line}`);
        }
        for (let i = 0; i < expr.args.length; i++) {
            const t = this.compileExpr(expr.args[i]);
            this.coerce(t, fi.params[i].type);
        }
        this.emit(Op.CALL, fi.index);
        return fi.retType;
    }

    // ── Type coercion ─────────────────────────────────────────────────────────

    private coerce(from: A.CType, to: A.CType): void {
        if (from === to) return;
        const op = this.convOp(from, to);
        if (op !== null) this.emit(op);
    }

    private convOp(from: A.CType, to: A.CType): Op | null {
        if (from === 'int'   && to === 'float')  return Op.I2F;
        if (from === 'float' && to === 'int')    return Op.F2I;
        if (from === 'int'   && to === 'bool')   return Op.I2B;
        if (from === 'float' && to === 'bool')   return Op.F2B;
        if (from === 'int'   && to === 'string') return Op.I2S;
        if (from === 'float' && to === 'string') return Op.F2S;
        if (from === 'bool'  && to === 'string') return Op.B2S;
        if (from === 'string'&& to === 'int')    return Op.S2I;
        if (from === 'string'&& to === 'float')  return Op.S2F;
        return null;  // incompatible or already correct — no conversion emitted
    }

    // ── Scope / variable management ───────────────────────────────────────────

    private pushScope(): void  { this.locals.push([]); }
    private popScope(): void   { this.locals.pop(); }

    private declareLocal(name: string, type: A.CType): number {
        const idx = this.countLocals();
        this.locals[this.locals.length - 1].push({ name, type, scope: 'local', index: idx });
        return idx;
    }

    private countLocals(): number {
        return this.locals.reduce((sum, scope) => sum + scope.length, 0);
    }

    private resolveVar(name: string, line: number): VarInfo {
        // Inner scopes first
        for (let i = this.locals.length - 1; i >= 0; i--) {
            const v = this.locals[i].find(v => v.name === name);
            if (v) return v;
        }
        // Then globals
        const g = this.globals.find(g => g.name === name);
        if (g) return g;
        throw new Error(`Undefined variable '${name}' at line ${line}`);
    }

    private findFunc(name: string): FuncInfo | undefined {
        return this.functions.find(f => f.name === name);
    }

    // ── Constant pools ────────────────────────────────────────────────────────

    private addInt(v: number): number {
        const idx = this.intPool.indexOf(v);
        if (idx >= 0) return idx;
        this.intPool.push(v);
        return this.intPool.length - 1;
    }

    private addFlt(v: number): number {
        const idx = this.fltPool.findIndex(f => f === v);
        if (idx >= 0) return idx;
        this.fltPool.push(v);
        return this.fltPool.length - 1;
    }

    private addStr(v: string): number {
        const idx = this.strPool.indexOf(v);
        if (idx >= 0) return idx;
        this.strPool.push(v);
        return this.strPool.length - 1;
    }

    // ── Bytecode emission ─────────────────────────────────────────────────────

    private emit(op: Op, operand = 0): void {
        this.curCode.push(op, operand & 0xFF, (operand >> 8) & 0xFF);
    }

    private currentPos(): number { return this.curCode.length; }

    // Emit a jump with placeholder operand; returns position of operand bytes
    private emitJump(op: Op): number {
        const pos = this.curCode.length + 1;  // position of operand in code
        this.emit(op, 0);
        return pos;
    }

    // Emit an absolute jump to a known target
    private emitAbsJump(target: number): void {
        this.emit(Op.JUMP, target);
    }

    // Patch jump operand at 'operandPos' to point to current position
    private patchJump(operandPos: number): void {
        const target = this.curCode.length;
        this.curCode[operandPos]     = target & 0xFF;
        this.curCode[operandPos + 1] = (target >> 8) & 0xFF;
    }

    // Patch jump operand at 'operandPos' to point to 'target'
    private patchAt(operandPos: number, target: number): void {
        this.curCode[operandPos]     = target & 0xFF;
        this.curCode[operandPos + 1] = (target >> 8) & 0xFF;
    }
}

export function codegen(program: A.Program): CompiledProgram {
    return new Codegen().compile(program);
}
