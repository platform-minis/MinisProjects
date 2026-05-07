import { Token, TT } from './lexer';
import * as A from './ast';

export class Parser {
    private tokens: Token[];
    private pos = 0;

    constructor(tokens: Token[]) { this.tokens = tokens; }

    private peek(offset = 0): Token { return this.tokens[Math.min(this.pos + offset, this.tokens.length - 1)]; }
    private advance(): Token        { return this.tokens[this.pos++]; }
    private check(t: TT): boolean   { return this.peek().type === t; }
    private match(...ts: TT[]): boolean {
        for (const t of ts) { if (this.check(t)) { this.advance(); return true; } }
        return false;
    }
    private expect(t: TT, msg?: string): Token {
        if (!this.check(t)) {
            const tok = this.peek();
            throw new Error(`Parse error at line ${tok.line}: expected ${msg ?? TT[t]}, got '${tok.value}'`);
        }
        return this.advance();
    }
    private error(msg: string): never {
        throw new Error(`Parse error at line ${this.peek().line}: ${msg}`);
    }

    // ── Type parsing ──────────────────────────────────────────────────────────
    private parseType(): A.CType {
        const tok = this.peek();
        let base: A.CType;
        switch (tok.type) {
            case TT.KW_INT:    this.advance(); base = 'int';    break;
            case TT.KW_FLOAT:  this.advance(); base = 'float';  break;
            case TT.KW_BOOL:   this.advance(); base = 'bool';   break;
            case TT.KW_STRING: this.advance(); base = 'string'; break;
            case TT.KW_VOID:   this.advance(); base = 'void';   break;
            default: this.error(`expected type, got '${tok.value}'`);
        }
        return base;
    }

    private isTypeToken(t: TT): boolean {
        return t === TT.KW_INT || t === TT.KW_FLOAT || t === TT.KW_BOOL ||
               t === TT.KW_STRING || t === TT.KW_VOID;
    }

    // ── Program ───────────────────────────────────────────────────────────────
    parseProgram(): A.Program {
        const decls: A.Decl[] = [];
        while (!this.check(TT.EOF)) {
            if (this.isTypeToken(this.peek().type) && this.peek(1).type === TT.IDENT) {
                const type = this.parseType();
                const name = this.expect(TT.IDENT).value;
                // Function declaration: type name ( ...
                if (this.check(TT.LPAREN)) {
                    decls.push(this.parseFuncDecl(type, name));
                } else {
                    // Top-level variable declaration
                    decls.push(this.parseVarDeclTail(type, name));
                }
            } else {
                this.error(`unexpected token '${this.peek().value}'`);
            }
        }
        return { kind: 'Program', decls };
    }

    private parseFuncDecl(retType: A.CType, name: string): A.FuncDecl {
        const line = this.peek().line;
        this.expect(TT.LPAREN, '(');
        const params: A.Param[] = [];
        if (!this.check(TT.RPAREN)) {
            do {
                const pt = this.parseType();
                const pn = this.expect(TT.IDENT).value;
                params.push({ type: pt, name: pn });
            } while (this.match(TT.COMMA));
        }
        this.expect(TT.RPAREN, ')');
        const body = this.parseBlock();
        return { kind: 'FuncDecl', retType, name, params, body, line };
    }

    // ── Statements ────────────────────────────────────────────────────────────
    private parseBlock(): A.Block {
        this.expect(TT.LBRACE, '{');
        const stmts: A.Stmt[] = [];
        while (!this.check(TT.RBRACE) && !this.check(TT.EOF)) {
            stmts.push(this.parseStmt());
        }
        this.expect(TT.RBRACE, '}');
        return { kind: 'Block', stmts };
    }

    private parseStmt(): A.Stmt {
        const tok = this.peek();

        // Variable declaration
        if (this.isTypeToken(tok.type)) {
            const type = this.parseType();
            const name = this.expect(TT.IDENT).value;
            return this.parseVarDeclTail(type, name);
        }

        switch (tok.type) {
            case TT.LBRACE:    return this.parseBlock();
            case TT.KW_IF:     return this.parseIf();
            case TT.KW_WHILE:  return this.parseWhile();
            case TT.KW_FOR:    return this.parseFor();
            case TT.KW_RETURN: return this.parseReturn();
            case TT.KW_BREAK:  this.advance(); this.expect(TT.SEMI); return { kind: 'BreakStmt' };
            case TT.KW_CONTINUE:this.advance();this.expect(TT.SEMI); return { kind: 'ContinueStmt' };
            default: {
                const expr = this.parseExpr();
                this.expect(TT.SEMI, ';');
                return { kind: 'ExprStmt', expr };
            }
        }
    }

    private parseVarDeclTail(type: A.CType, name: string): A.VarDeclStmt {
        const line = this.peek().line;
        let size: A.Expr | undefined;
        // Array declaration: type name[size]
        if (this.match(TT.LBRACKET)) {
            size = this.parseExpr();
            this.expect(TT.RBRACKET, ']');
            type = A.arrayOf(type);
        }
        let init: A.Expr | undefined;
        if (this.match(TT.EQ)) {
            init = this.parseExpr();
        }
        this.expect(TT.SEMI, ';');
        return { kind: 'VarDeclStmt', type, name, size, init, line };
    }

    private parseIf(): A.IfStmt {
        this.expect(TT.KW_IF);
        this.expect(TT.LPAREN, '(');
        const cond = this.parseExpr();
        this.expect(TT.RPAREN, ')');
        const then = this.parseBlock();
        let elseif: A.IfStmt | undefined;
        let els: A.Block | undefined;
        if (this.match(TT.KW_ELSE)) {
            if (this.check(TT.KW_IF)) {
                elseif = this.parseIf();
            } else {
                els = this.parseBlock();
            }
        }
        return { kind: 'IfStmt', cond, then, elseif, els };
    }

    private parseWhile(): A.WhileStmt {
        this.expect(TT.KW_WHILE);
        this.expect(TT.LPAREN, '(');
        const cond = this.parseExpr();
        this.expect(TT.RPAREN, ')');
        const body = this.parseBlock();
        return { kind: 'WhileStmt', cond, body };
    }

    private parseFor(): A.ForStmt {
        this.expect(TT.KW_FOR);
        this.expect(TT.LPAREN, '(');

        let init: A.VarDeclStmt | A.ExprStmt | undefined;
        if (!this.check(TT.SEMI)) {
            if (this.isTypeToken(this.peek().type)) {
                const type = this.parseType();
                const name = this.expect(TT.IDENT).value;
                init = this.parseVarDeclTail(type, name); // includes the ;
            } else {
                const expr = this.parseExpr();
                this.expect(TT.SEMI, ';');
                init = { kind: 'ExprStmt', expr };
            }
        } else {
            this.expect(TT.SEMI);
        }

        let cond: A.Expr | undefined;
        if (!this.check(TT.SEMI)) cond = this.parseExpr();
        this.expect(TT.SEMI, ';');

        let update: A.Expr | undefined;
        if (!this.check(TT.RPAREN)) update = this.parseExpr();
        this.expect(TT.RPAREN, ')');

        const body = this.parseBlock();
        return { kind: 'ForStmt', init, cond, update, body };
    }

    private parseReturn(): A.ReturnStmt {
        this.expect(TT.KW_RETURN);
        if (this.check(TT.SEMI)) { this.advance(); return { kind: 'ReturnStmt' }; }
        const value = this.parseExpr();
        this.expect(TT.SEMI, ';');
        return { kind: 'ReturnStmt', value };
    }

    // ── Expressions (Pratt-style precedence climbing) ─────────────────────────
    parseExpr(): A.Expr { return this.parseAssign(); }

    private parseAssign(): A.Expr {
        const left = this.parseLogicalOr();
        const tok = this.peek();
        let op: A.AssignOp | null = null;
        switch (tok.type) {
            case TT.EQ:        op = '=';  break;
            case TT.PLUSEQ:    op = '+='; break;
            case TT.MINUSEQ:   op = '-='; break;
            case TT.STAREQ:    op = '*='; break;
            case TT.SLASHEQ:   op = '/='; break;
            case TT.PERCENTEQ: op = '%='; break;
        }
        if (op !== null) {
            this.advance();
            const value = this.parseAssign();
            return { kind: 'AssignExpr', op, target: left, value };
        }
        return left;
    }

    private parseLogicalOr(): A.Expr {
        let left = this.parseLogicalAnd();
        while (this.check(TT.PIPEPIPE)) {
            this.advance();
            left = { kind: 'BinaryExpr', op: '||', left, right: this.parseLogicalAnd() };
        }
        return left;
    }

    private parseLogicalAnd(): A.Expr {
        let left = this.parseBitOr();
        while (this.check(TT.AMPAMP)) {
            this.advance();
            left = { kind: 'BinaryExpr', op: '&&', left, right: this.parseBitOr() };
        }
        return left;
    }

    private parseBitOr(): A.Expr {
        let left = this.parseBitXor();
        while (this.check(TT.PIPE)) {
            this.advance();
            left = { kind: 'BinaryExpr', op: '|', left, right: this.parseBitXor() };
        }
        return left;
    }

    private parseBitXor(): A.Expr {
        let left = this.parseBitAnd();
        while (this.check(TT.CARET)) {
            this.advance();
            left = { kind: 'BinaryExpr', op: '^', left, right: this.parseBitAnd() };
        }
        return left;
    }

    private parseBitAnd(): A.Expr {
        let left = this.parseEquality();
        while (this.check(TT.AMP)) {
            this.advance();
            left = { kind: 'BinaryExpr', op: '&', left, right: this.parseEquality() };
        }
        return left;
    }

    private parseEquality(): A.Expr {
        let left = this.parseRelational();
        while (this.check(TT.EQEQ) || this.check(TT.BANGEQ)) {
            const op = this.advance().type === TT.EQEQ ? '==' : '!=';
            left = { kind: 'BinaryExpr', op, left, right: this.parseRelational() };
        }
        return left;
    }

    private parseRelational(): A.Expr {
        let left = this.parseShift();
        for (;;) {
            let op: A.BinaryOp | null = null;
            switch (this.peek().type) {
                case TT.LT:   op = '<';  break; case TT.LTEQ: op = '<='; break;
                case TT.GT:   op = '>';  break; case TT.GTEQ: op = '>='; break;
            }
            if (!op) break;
            this.advance();
            left = { kind: 'BinaryExpr', op, left, right: this.parseShift() };
        }
        return left;
    }

    private parseShift(): A.Expr {
        let left = this.parseAdd();
        while (this.check(TT.LTLT) || this.check(TT.GTGT)) {
            const op: A.BinaryOp = this.advance().type === TT.LTLT ? '<<' : '>>';
            left = { kind: 'BinaryExpr', op, left, right: this.parseAdd() };
        }
        return left;
    }

    private parseAdd(): A.Expr {
        let left = this.parseMul();
        while (this.check(TT.PLUS) || this.check(TT.MINUS)) {
            const op: A.BinaryOp = this.advance().type === TT.PLUS ? '+' : '-';
            left = { kind: 'BinaryExpr', op, left, right: this.parseMul() };
        }
        return left;
    }

    private parseMul(): A.Expr {
        let left = this.parseUnary();
        while (this.check(TT.STAR) || this.check(TT.SLASH) || this.check(TT.PERCENT)) {
            const t = this.advance().type;
            const op: A.BinaryOp = t === TT.STAR ? '*' : t === TT.SLASH ? '/' : '%';
            left = { kind: 'BinaryExpr', op, left, right: this.parseUnary() };
        }
        return left;
    }

    private parseUnary(): A.Expr {
        const tok = this.peek();
        if (tok.type === TT.BANG)  { this.advance(); return { kind: 'UnaryExpr', op: '!',  operand: this.parseUnary() }; }
        if (tok.type === TT.MINUS) { this.advance(); return { kind: 'UnaryExpr', op: '-',  operand: this.parseUnary() }; }
        if (tok.type === TT.TILDE) { this.advance(); return { kind: 'UnaryExpr', op: '~',  operand: this.parseUnary() }; }
        if (tok.type === TT.PLUSPLUS)   { this.advance(); return { kind: 'UnaryExpr', op: '++', operand: this.parseUnary() }; }
        if (tok.type === TT.MINUSMINUS) { this.advance(); return { kind: 'UnaryExpr', op: '--', operand: this.parseUnary() }; }
        return this.parsePostfix();
    }

    private parsePostfix(): A.Expr {
        let expr = this.parsePrimary();
        for (;;) {
            if (this.match(TT.LBRACKET)) {
                const index = this.parseExpr();
                this.expect(TT.RBRACKET, ']');
                expr = { kind: 'IndexExpr', array: expr, index };
            } else if (this.check(TT.PLUSPLUS)) {
                this.advance();
                expr = { kind: 'PostfixExpr', op: '++', operand: expr };
            } else if (this.check(TT.MINUSMINUS)) {
                this.advance();
                expr = { kind: 'PostfixExpr', op: '--', operand: expr };
            } else {
                break;
            }
        }
        return expr;
    }

    private parsePrimary(): A.Expr {
        const tok = this.advance();
        switch (tok.type) {
            case TT.INT_LIT:    return { kind: 'IntLit',    value: parseInt(tok.value, 10) };
            case TT.FLOAT_LIT:  return { kind: 'FloatLit',  value: parseFloat(tok.value) };
            case TT.KW_TRUE:    return { kind: 'BoolLit',   value: true };
            case TT.KW_FALSE:   return { kind: 'BoolLit',   value: false };
            case TT.KW_NULL:    return { kind: 'NullLit' };
            case TT.STRING_LIT: return { kind: 'StringLit', value: tok.value };
            case TT.IDENT: {
                // Function call
                if (this.check(TT.LPAREN)) {
                    this.advance();
                    const args: A.Expr[] = [];
                    if (!this.check(TT.RPAREN)) {
                        do { args.push(this.parseExpr()); } while (this.match(TT.COMMA));
                    }
                    this.expect(TT.RPAREN, ')');
                    return { kind: 'CallExpr', callee: tok.value, args, line: tok.line };
                }
                return { kind: 'Ident', name: tok.value, line: tok.line };
            }
            case TT.LPAREN: {
                // Cast or grouped expression
                // Cast: (type) expr
                if (this.isTypeToken(this.peek().type) && this.peek(1).type === TT.RPAREN) {
                    const castType = this.parseType();
                    this.expect(TT.RPAREN, ')');
                    const operand = this.parseUnary();
                    // Represent cast as a unary with special op
                    // We encode casts as (int), (float), (bool), (string)
                    const op = castType as A.UnaryOp; // reuse UnaryOp field, value = type name
                    return { kind: 'UnaryExpr', op, operand } as A.UnaryExpr;
                }
                const expr = this.parseExpr();
                this.expect(TT.RPAREN, ')');
                return expr;
            }
            default:
                throw new Error(`Parse error at line ${tok.line}: unexpected '${tok.value}'`);
        }
    }
}

export function parse(tokens: Token[]): A.Program {
    return new Parser(tokens).parseProgram();
}
