export type CType = 'int' | 'float' | 'bool' | 'string' | 'void'
                  | 'int[]' | 'float[]' | 'bool[]' | 'string[]';

export function baseType(t: CType): CType {
    if (t.endsWith('[]')) return t.slice(0, -2) as CType;
    return t;
}
export function isArray(t: CType): boolean { return t.endsWith('[]'); }
export function arrayOf(t: CType): CType   { return (t + '[]') as CType; }

// ── Statements ───────────────────────────────────────────────────────────────

export interface Program    { kind: 'Program';    decls: Decl[]; }
export interface FuncDecl   { kind: 'FuncDecl';   retType: CType; name: string; params: Param[]; body: Block; line: number; }
export interface VarDeclStmt{ kind: 'VarDeclStmt';type: CType;   name: string; size?: Expr; init?: Expr; line: number; }
export interface Block      { kind: 'Block';      stmts: Stmt[]; }
export interface ExprStmt   { kind: 'ExprStmt';   expr: Expr; }
export interface IfStmt     { kind: 'IfStmt';     cond: Expr; then: Block; elseif?: IfStmt; els?: Block; }
export interface WhileStmt  { kind: 'WhileStmt';  cond: Expr; body: Block; }
export interface ForStmt    { kind: 'ForStmt';    init?: VarDeclStmt | ExprStmt; cond?: Expr; update?: Expr; body: Block; }
export interface ReturnStmt { kind: 'ReturnStmt'; value?: Expr; }
export interface BreakStmt  { kind: 'BreakStmt'; }
export interface ContinueStmt { kind: 'ContinueStmt'; }

export type Decl = FuncDecl | VarDeclStmt;
export type Stmt = VarDeclStmt | Block | ExprStmt | IfStmt | WhileStmt | ForStmt
                 | ReturnStmt | BreakStmt | ContinueStmt;

// ── Expressions ──────────────────────────────────────────────────────────────

export interface IntLit    { kind: 'IntLit';    value: number; }
export interface FloatLit  { kind: 'FloatLit';  value: number; }
export interface BoolLit   { kind: 'BoolLit';   value: boolean; }
export interface StringLit { kind: 'StringLit'; value: string; }
export interface NullLit   { kind: 'NullLit'; }
export interface Ident     { kind: 'Ident';     name: string; line: number; }
export interface BinaryExpr{ kind: 'BinaryExpr'; op: BinaryOp; left: Expr; right: Expr; }
export interface UnaryExpr { kind: 'UnaryExpr';  op: UnaryOp;  operand: Expr; }
export interface PostfixExpr{kind: 'PostfixExpr';op: '++' | '--'; operand: Expr; }
export interface AssignExpr{ kind: 'AssignExpr'; op: AssignOp; target: Expr; value: Expr; }
export interface CallExpr  { kind: 'CallExpr';   callee: string; args: Expr[]; line: number; }
export interface IndexExpr { kind: 'IndexExpr';  array: Expr;    index: Expr; }

export type Expr = IntLit | FloatLit | BoolLit | StringLit | NullLit
                 | Ident | BinaryExpr | UnaryExpr | PostfixExpr
                 | AssignExpr | CallExpr | IndexExpr;

export type BinaryOp = '+' | '-' | '*' | '/' | '%'
                     | '==' | '!=' | '<' | '<=' | '>' | '>='
                     | '&&' | '||'
                     | '&' | '|' | '^' | '<<' | '>>';

export type UnaryOp  = '-' | '!' | '~' | '++' | '--';   // ++ and -- are prefix here

export type AssignOp = '=' | '+=' | '-=' | '*=' | '/=' | '%=';

export interface Param { type: CType; name: string; }
