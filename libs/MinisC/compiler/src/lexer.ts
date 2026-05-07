export const enum TT {
    // Literals
    INT_LIT, FLOAT_LIT, STRING_LIT,
    // Keywords
    KW_INT, KW_FLOAT, KW_BOOL, KW_STRING, KW_VOID,
    KW_IF, KW_ELSE, KW_WHILE, KW_FOR,
    KW_RETURN, KW_BREAK, KW_CONTINUE,
    KW_TRUE, KW_FALSE, KW_NULL,
    // Identifiers
    IDENT,
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    AMP, PIPE, CARET, TILDE, LTLT, GTGT,
    PLUSPLUS, MINUSMINUS,
    PLUSEQ, MINUSEQ, STAREQ, SLASHEQ, PERCENTEQ,
    EQ, EQEQ, BANG, BANGEQ,
    LT, LTEQ, GT, GTEQ,
    AMPAMP, PIPEPIPE,
    // Punctuation
    SEMI, COMMA, LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    // Special
    EOF,
}

export interface Token {
    type:   TT;
    value:  string;
    line:   number;
}

const KEYWORDS: Record<string, TT> = {
    int: TT.KW_INT, float: TT.KW_FLOAT, bool: TT.KW_BOOL, string: TT.KW_STRING, void: TT.KW_VOID,
    if: TT.KW_IF, else: TT.KW_ELSE, while: TT.KW_WHILE, for: TT.KW_FOR,
    return: TT.KW_RETURN, break: TT.KW_BREAK, continue: TT.KW_CONTINUE,
    true: TT.KW_TRUE, false: TT.KW_FALSE, null: TT.KW_NULL,
};

export function tokenize(src: string): Token[] {
    const tokens: Token[] = [];
    let i = 0, line = 1;

    function peek(offset = 0): string { return src[i + offset] ?? ''; }
    function advance(): string        { const c = src[i++]; if (c === '\n') line++; return c; }
    function match(c: string): boolean { if (src[i] === c) { i++; return true; } return false; }
    function tok(type: TT, value: string): void { tokens.push({ type, value, line }); }
    function error(msg: string): never { throw new Error(`Lexer error at line ${line}: ${msg}`); }

    while (i < src.length) {
        const start = i;
        const c = advance();

        // Whitespace
        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') continue;

        // Line comment
        if (c === '/' && peek() === '/') {
            while (i < src.length && src[i] !== '\n') i++;
            continue;
        }
        // Block comment
        if (c === '/' && peek() === '*') {
            i++;
            while (i < src.length) {
                const ch = advance();
                if (ch === '*' && src[i] === '/') { i++; break; }
            }
            continue;
        }

        // String literals
        if (c === '"') {
            let s = '';
            while (i < src.length && src[i] !== '"') {
                const ch = advance();
                if (ch === '\\') {
                    const esc = advance();
                    switch (esc) {
                        case 'n':  s += '\n'; break;
                        case 't':  s += '\t'; break;
                        case 'r':  s += '\r'; break;
                        case '"':  s += '"';  break;
                        case '\\': s += '\\'; break;
                        case '0':  s += '\0'; break;
                        default:   s += esc;
                    }
                } else {
                    s += ch;
                }
            }
            if (i >= src.length) error('unterminated string literal');
            i++; // closing "
            tok(TT.STRING_LIT, s);
            continue;
        }

        // Number literals
        if (c >= '0' && c <= '9') {
            let num = c;
            // Hex
            if (c === '0' && (peek() === 'x' || peek() === 'X')) {
                num += advance();
                while (/[0-9a-fA-F]/.test(peek())) num += advance();
                tok(TT.INT_LIT, String(parseInt(num, 16)));
                continue;
            }
            while (peek() >= '0' && peek() <= '9') num += advance();
            if (peek() === '.' && src[i + 1] >= '0' && src[i + 1] <= '9') {
                num += advance(); // '.'
                while (peek() >= '0' && peek() <= '9') num += advance();
                if (peek() === 'f' || peek() === 'F') advance(); // optional f suffix
                tok(TT.FLOAT_LIT, num);
            } else {
                tok(TT.INT_LIT, num);
            }
            continue;
        }
        // Float starting with digit (already handled) or leading dot
        if (c === '.' && peek() >= '0' && peek() <= '9') {
            let num = '0.';
            while (peek() >= '0' && peek() <= '9') num += advance();
            tok(TT.FLOAT_LIT, num);
            continue;
        }

        // Identifiers and keywords
        if (c === '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            let id = c;
            while (peek() === '_' || (peek() >= 'a' && peek() <= 'z') ||
                   (peek() >= 'A' && peek() <= 'Z') || (peek() >= '0' && peek() <= '9')) {
                id += advance();
            }
            tok(KEYWORDS[id] ?? TT.IDENT, id);
            continue;
        }

        // Operators and punctuation
        switch (c) {
            case '+': tok(match('+') ? TT.PLUSPLUS  : match('=') ? TT.PLUSEQ   : TT.PLUS,   c); break;
            case '-': tok(match('-') ? TT.MINUSMINUS: match('=') ? TT.MINUSEQ  : TT.MINUS,  c); break;
            case '*': tok(match('=') ? TT.STAREQ    : TT.STAR,   c); break;
            case '/': tok(match('=') ? TT.SLASHEQ   : TT.SLASH,  c); break;
            case '%': tok(match('=') ? TT.PERCENTEQ : TT.PERCENT,c); break;
            case '=': tok(match('=') ? TT.EQEQ      : TT.EQ,     c); break;
            case '!': tok(match('=') ? TT.BANGEQ     : TT.BANG,   c); break;
            case '<': tok(match('<') ? TT.LTLT : match('=') ? TT.LTEQ : TT.LT, c); break;
            case '>': tok(match('>') ? TT.GTGT : match('=') ? TT.GTEQ : TT.GT, c); break;
            case '&': tok(match('&') ? TT.AMPAMP     : TT.AMP,    c); break;
            case '|': tok(match('|') ? TT.PIPEPIPE   : TT.PIPE,   c); break;
            case '^': tok(TT.CARET,    c); break;
            case '~': tok(TT.TILDE,    c); break;
            case ';': tok(TT.SEMI,     c); break;
            case ',': tok(TT.COMMA,    c); break;
            case '(': tok(TT.LPAREN,   c); break;
            case ')': tok(TT.RPAREN,   c); break;
            case '{': tok(TT.LBRACE,   c); break;
            case '}': tok(TT.RBRACE,   c); break;
            case '[': tok(TT.LBRACKET, c); break;
            case ']': tok(TT.RBRACKET, c); break;
            default: error(`unexpected character '${c}'`);
        }
    }

    tok(TT.EOF, '');
    return tokens;
}
