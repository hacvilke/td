'use strict';

// =============================================================================
// TD Engine — TDScript Compiler (JavaScript implementation)
// File: tools/tdscript/tdscript.js
//
// Pure-JS port of src/scripting/tdscript/{lexer,parser,codegen_js}.cpp.
//
// Why a JS port alongside the C++ compiler?
//   - The CLI workflow (`td script compile foo.td`) needs to run on Node
//     without requiring a Wasm build of the engine first.
//   - The browser-based authoring tools (future) can use this same module
//     via a <script> tag — no Wasm fetch needed for the compiler itself.
//   - The C++ compiler exists so that the engine (running natively or in
//     Wasm) can hot-reload TDScript files without a JS runtime. The two
//     implementations MUST stay in sync; the tests in tests/tdscript/
//     exercise both paths.
//
// Status: REAL. Lexer + parser + JS codegen all functional. The C++ side
// compiles the same grammar; the JS side is the primary dev workflow.
// =============================================================================

// -----------------------------------------------------------------------------
// Token kinds
// -----------------------------------------------------------------------------
const TK = {
  LParen: '(', RParen: ')', LBrace: '{', RBrace: '}', LBracket: '[', RBracket: ']',
  Semicolon: ';', Comma: ',', Dot: '.', Colon: ':', Arrow: '->', At: '@',
  Assign: '=', Plus: '+', Minus: '-', Star: '*', Slash: '/', Percent: '%',
  Eq: '==', NotEq: '!=', Lt: '<', Gt: '>', LtEq: '<=', GtEq: '>=',
  And: '&&', Or: '||', Not: '!',
  PlusAssign: '+=', MinusAssign: '-=', StarAssign: '*=', SlashAssign: '/=',
  IntLit: 'Int', FloatLit: 'Float', StringLit: 'String', BoolLit: 'Bool',
  Ident: 'Ident', EOF: 'EOF',
  // Keywords
  KwImport: 'import', KwStruct: 'struct', KwClass: 'class',
  KwPublic: 'public', KwPrivate: 'private', KwProtected: 'protected',
  KwVoid: 'void', KwReturn: 'return', KwIf: 'if', KwElse: 'else',
  KwFor: 'for', KwWhile: 'while', KwBreak: 'break', KwContinue: 'continue',
  KwTrue: 'true', KwFalse: 'false', KwNull: 'null',
  KwReplicated: 'replicated', KwVar: 'var', KwConst: 'const', KwFunction: 'function',
  KwInt32: 'int32', KwUint32: 'uint32', KwInt64: 'int64', KwUint64: 'uint64',
  KwFloat: 'float', KwDouble: 'double', KwBool: 'bool', KwString: 'string', KwAuto: 'auto',
};

const KEYWORDS = Object.assign(Object.create(null), {
  import: TK.KwImport, struct: TK.KwStruct, class: TK.KwClass,
  public: TK.KwPublic, private: TK.KwPrivate, protected: TK.KwProtected,
  void: TK.KwVoid, return: TK.KwReturn, if: TK.KwIf, else: TK.KwElse,
  for: TK.KwFor, while: TK.KwWhile, break: TK.KwBreak, continue: TK.KwContinue,
  true: TK.KwTrue, false: TK.KwFalse, null: TK.KwNull,
  replicated: TK.KwReplicated, var: TK.KwVar, const: TK.KwConst, function: TK.KwFunction,
  int32: TK.KwInt32, uint32: TK.KwUint32, int64: TK.KwInt64, uint64: TK.KwUint64,
  float: TK.KwFloat, double: TK.KwDouble, bool: TK.KwBool, string: TK.KwString, auto: TK.KwAuto,
});

// -----------------------------------------------------------------------------
// Lexer
// -----------------------------------------------------------------------------
function tokenize(src) {
  const tokens = [];
  let pos = 0, line = 1, col = 1;
  const errors = [];

  function peek(off = 0) { return pos + off < src.length ? src[pos + off] : '\0'; }
  function adv() {
    const c = src[pos++];
    if (c === '\n') { line++; col = 1; } else { col++; }
    return c;
  }
  function match(c) { if (peek() === c) { adv(); return true; } return false; }

  function skipWsAndComments() {
    while (pos < src.length) {
      const c = peek();
      if (c === ' ' || c === '\t' || c === '\r' || c === '\n') adv();
      else if (c === '/' && peek(1) === '/') { while (pos < src.length && peek() !== '\n') adv(); }
      else if (c === '/' && peek(1) === '*') {
        adv(); adv();
        while (pos < src.length && !(peek() === '*' && peek(1) === '/')) adv();
        if (pos < src.length) { adv(); adv(); }
      } else break;
    }
  }

  function lexIdentOrKw() {
    const sL = line, sC = col;
    let s = '';
    while (pos < src.length && /[A-Za-z0-9_]/.test(peek())) s += adv();
    // Use `in` to check own keys only (KEYWORDS has null prototype, so
    // inherited methods like toString/hasOwnProperty won't false-positive)
    const k = (s in KEYWORDS) ? KEYWORDS[s] : TK.Ident;
    tokens.push({ kind: k, text: s, line: sL, col: sC });
  }

  function lexNumber() {
    const sL = line, sC = col;
    let s = '', isFloat = false;
    while (pos < src.length && /\d/.test(peek())) s += adv();
    if (peek() === '.' && /\d/.test(peek(1))) {
      isFloat = true; s += adv();
      while (pos < src.length && /\d/.test(peek())) s += adv();
    }
    if (peek() === 'e' || peek() === 'E') {
      isFloat = true; s += adv();
      if (peek() === '+' || peek() === '-') s += adv();
      while (pos < src.length && /\d/.test(peek())) s += adv();
    }
    if (peek() === 'f' || peek() === 'F') { isFloat = true; adv(); }
    tokens.push({ kind: isFloat ? TK.FloatLit : TK.IntLit, text: s, line: sL, col: sC });
  }

  function lexString() {
    const sL = line, sC = col;
    adv(); // opening "
    let s = '';
    while (pos < src.length && peek() !== '"') {
      const c = peek();
      if (c === '\\') {
        adv();
        const esc = adv();
        if (esc === 'n') s += '\n';
        else if (esc === 't') s += '\t';
        else if (esc === 'r') s += '\r';
        else if (esc === '"') s += '"';
        else if (esc === '\\') s += '\\';
        else if (esc === '0') s += '\0';
        else { s += '\\'; s += esc; }
      } else s += adv();
    }
    if (pos >= src.length) errors.push({ message: 'unterminated string literal', line: sL, col: sC });
    else adv();
    tokens.push({ kind: TK.StringLit, text: s, line: sL, col: sC });
  }

  function lexOp() {
    const sL = line, sC = col;
    const c = adv();
    let k = TK.Ident, t = c;
    switch (c) {
      case '(': k = TK.LParen; break;
      case ')': k = TK.RParen; break;
      case '{': k = TK.LBrace; break;
      case '}': k = TK.RBrace; break;
      case '[': k = TK.LBracket; break;
      case ']': k = TK.RBracket; break;
      case ';': k = TK.Semicolon; break;
      case ',': k = TK.Comma; break;
      case '.': k = TK.Dot; break;
      case ':': k = TK.Colon; break;
      case '@': k = TK.At; break;
      case '+':
        if (peek() === '=') { adv(); k = TK.PlusAssign; t = '+='; }
        else k = TK.Plus; break;
      case '-':
        if (peek() === '=') { adv(); k = TK.MinusAssign; t = '-='; }
        else k = TK.Minus; break;
      case '*':
        if (peek() === '=') { adv(); k = TK.StarAssign; t = '*='; }
        else k = TK.Star; break;
      case '/':
        if (peek() === '=') { adv(); k = TK.SlashAssign; t = '/='; }
        else k = TK.Slash; break;
      case '%': k = TK.Percent; break;
      case '=':
        if (peek() === '=') { adv(); k = TK.Eq; t = '=='; }
        else k = TK.Assign; break;
      case '!':
        if (peek() === '=') { adv(); k = TK.NotEq; t = '!='; }
        else k = TK.Not; break;
      case '<':
        if (peek() === '=') { adv(); k = TK.LtEq; t = '<='; }
        else k = TK.Lt; break;
      case '>':
        if (peek() === '=') { adv(); k = TK.GtEq; t = '>='; }
        else k = TK.Gt; break;
      case '&':
        if (peek() === '&') { adv(); k = TK.And; t = '&&'; }
        else { errors.push({ message: 'bitwise & not supported (use &&)', line: sL, col: sC }); k = TK.And; }
        break;
      case '|':
        if (peek() === '|') { adv(); k = TK.Or; t = '||'; }
        else { errors.push({ message: 'bitwise | not supported (use ||)', line: sL, col: sC }); k = TK.Or; }
        break;
      default:
        errors.push({ message: `unexpected character '${c}'`, line: sL, col: sC });
        k = TK.Semicolon;
    }
    tokens.push({ kind: k, text: t, line: sL, col: sC });
  }

  while (true) {
    skipWsAndComments();
    if (pos >= src.length) break;
    const c = peek();
    if (/[A-Za-z_]/.test(c)) lexIdentOrKw();
    else if (/\d/.test(c)) lexNumber();
    else if (c === '"') lexString();
    else lexOp();
  }
  tokens.push({ kind: TK.EOF, text: '<eof>', line, col });
  return { tokens, errors };
}

// -----------------------------------------------------------------------------
// Parser (recursive descent)
// -----------------------------------------------------------------------------
const NK = {
  Module: 'Module', ImportStmt: 'ImportStmt', StructDecl: 'StructDecl', ClassDecl: 'ClassDecl',
  FieldDecl: 'FieldDecl', MethodDecl: 'MethodDecl', ParamDecl: 'ParamDecl',
  Block: 'Block', IfStmt: 'IfStmt', ForStmt: 'ForStmt', WhileStmt: 'WhileStmt',
  ReturnStmt: 'ReturnStmt', BreakStmt: 'BreakStmt', ContinueStmt: 'ContinueStmt',
  VarDeclStmt: 'VarDeclStmt', ExprStmt: 'ExprStmt',
  IntLiteral: 'IntLiteral', FloatLiteral: 'FloatLiteral', StringLiteral: 'StringLiteral',
  BoolLiteral: 'BoolLiteral', NullLiteral: 'NullLiteral', Identifier: 'Identifier',
  MemberAccess: 'MemberAccess', IndexAccess: 'IndexAccess', Call: 'Call',
  Unary: 'Unary', Binary: 'Binary', Assign: 'Assign', CompoundAssign: 'CompoundAssign',
  TypeRef: 'TypeRef',
};

function makeNode(kind) { return { kind, children: [] }; }

const PREC = {
  [TK.Or]: 1, [TK.And]: 2,
  [TK.Eq]: 3, [TK.NotEq]: 3,
  [TK.Lt]: 4, [TK.Gt]: 4, [TK.LtEq]: 4, [TK.GtEq]: 4,
  [TK.Plus]: 5, [TK.Minus]: 5,
  [TK.Star]: 6, [TK.Slash]: 6, [TK.Percent]: 6,
};

function parse(tokens) {
  let pos = 0;
  const errors = [];
  function peek(off = 0) { return pos + off < tokens.length ? tokens[pos + off] : tokens[tokens.length - 1]; }
  function adv() { return tokens[pos++]; }
  function check(k) { return peek().kind === k; }
  function match(k) { if (peek().kind === k) { adv(); return true; } return false; }
  function expect(k, what) {
    if (peek().kind === k) return adv();
    const t = peek();
    errors.push({ message: `expected ${what} but got '${t.text}'`, line: t.line, col: t.col });
    return { kind: k, text: '<error>', line: t.line, col: t.col };
  }
  function emitErr(msg, line, col) { errors.push({ message: msg, line, col }); }

  function parseOptionalVisibility() {
    if (match(TK.KwPublic)) return 'public';
    if (match(TK.KwPrivate)) return 'private';
    if (match(TK.KwProtected)) return 'protected';
    return 'public';
  }

  function parseType() {
    const t = makeNode(NK.TypeRef);
    const k = peek().kind;
    const typeKws = {
      [TK.KwInt32]: 'int32', [TK.KwUint32]: 'uint32', [TK.KwInt64]: 'int64',
      [TK.KwUint64]: 'uint64', [TK.KwFloat]: 'float', [TK.KwDouble]: 'double',
      [TK.KwBool]: 'bool', [TK.KwString]: 'string', [TK.KwVoid]: 'void', [TK.KwAuto]: 'auto',
    };
    if (typeKws[k]) { t.text = typeKws[k]; adv(); }
    else if (k === TK.Ident) { t.text = peek().text; adv(); }
    else {
      emitErr(`expected a type but got '${peek().text}'`, peek().line, peek().col);
      t.text = '<error>';
    }
    // generic: Type<...>
    if (match(TK.Lt)) {
      let gen = '<';
      while (!check(TK.Gt) && !check(TK.EOF)) {
        const inner = parseType();
        gen += inner.text;
        if (match(TK.Comma)) gen += ', ';
      }
      expect(TK.Gt, "'>'");
      gen += '>';
      t.text += gen;
    }
    return t;
  }

  function parseRpcDecorator() {
    if (!check(TK.Ident)) {
      emitErr("expected 'rpc' after '@'", peek().line, peek().col);
      return 'reliable';
    }
    const tok = adv();
    if (tok.text !== 'rpc') {
      emitErr(`unknown decorator '@${tok.text}' (only @rpc is supported)`, tok.line, tok.col);
      return 'reliable';
    }
    let mode = 'reliable';
    if (match(TK.LParen)) {
      if (!check(TK.Ident)) {
        emitErr("expected 'reliable' or 'unreliable' in @rpc(...)", peek().line, peek().col);
      } else {
        const m = adv();
        if (m.text === 'reliable' || m.text === 'unreliable') mode = m.text;
        else emitErr(`invalid rpc mode '${m.text}'`, m.line, m.col);
      }
      expect(TK.RParen, "')'");
    }
    return mode;
  }

  function parseParams() {
    const params = [];
    expect(TK.LParen, "'('");
    if (!check(TK.RParen)) {
      do {
        const type = parseType();
        const name = expect(TK.Ident, "parameter name");
        const p = makeNode(NK.ParamDecl);
        p.text = name.text; p.type = type; p.line = name.line; p.col = name.col;
        params.push(p);
      } while (match(TK.Comma));
    }
    expect(TK.RParen, "')'");
    return params;
  }

  function parseBlock() {
    expect(TK.LBrace, "'{'");
    const block = makeNode(NK.Block);
    while (!check(TK.RBrace) && !check(TK.EOF)) block.children.push(parseStatement());
    expect(TK.RBrace, "'}'");
    return block;
  }

  function parseStatement() {
    const k = peek().kind;
    switch (k) {
      case TK.LBrace: return parseBlock();
      case TK.KwIf: return parseIf();
      case TK.KwFor: return parseFor();
      case TK.KwWhile: return parseWhile();
      case TK.KwReturn: return parseReturn();
      case TK.KwBreak: adv(); expect(TK.Semicolon, "';'"); return makeNode(NK.BreakStmt);
      case TK.KwContinue: adv(); expect(TK.Semicolon, "';'"); return makeNode(NK.ContinueStmt);
      case TK.KwVar:
      case TK.KwConst:
      case TK.KwInt32: case TK.KwUint32: case TK.KwInt64: case TK.KwUint64:
      case TK.KwFloat: case TK.KwDouble: case TK.KwBool: case TK.KwString:
      case TK.KwVoid: case TK.KwAuto:
        return parseVarDecl();
      default:
        // Detect custom-type var decl: `Ident Ident [= ...]` or `Ident<...> Ident [= ...]`
        // This is a var decl if the next token is an identifier AND the token after
        // is another identifier (the variable name).
        if (k === TK.Ident) {
          let lookahead = 1;
          // Skip generic arguments: Ident<...>
          if (peek(1).kind === TK.Lt) {
            // Find the matching '>' (skip nested <>)
            let depth = 1; lookahead = 2;
            while (depth > 0 && peek(lookahead).kind !== TK.EOF) {
              if (peek(lookahead).kind === TK.Lt) depth++;
              else if (peek(lookahead).kind === TK.Gt) depth--;
              lookahead++;
            }
          }
          if (peek(lookahead).kind === TK.Ident) {
            return parseVarDecl();
          }
        }
        return parseExprStatement();
    }
  }

  function parseVarDecl() {
    const stmt = makeNode(NK.VarDeclStmt);
    const isConst = match(TK.KwConst);
    const isVar = match(TK.KwVar);
    if (!isConst && !isVar) stmt.type = parseType();
    const name = expect(TK.Ident, "variable name");
    stmt.text = name.text;
    if (match(TK.Assign)) stmt.initExpr = parseExpression();
    expect(TK.Semicolon, "';'");
    return stmt;
  }

  function parseIf() {
    expect(TK.KwIf, "'if'");
    expect(TK.LParen, "'('");
    const n = makeNode(NK.IfStmt);
    n.cond = parseExpression();
    expect(TK.RParen, "')'");
    n.thenBranch = parseStatement();
    if (match(TK.KwElse)) n.elseBranch = parseStatement();
    return n;
  }

  function parseFor() {
    expect(TK.KwFor, "'for'");
    expect(TK.LParen, "'('");
    const n = makeNode(NK.ForStmt);
    const declKws = [TK.KwVar, TK.KwConst, TK.KwInt32, TK.KwUint32, TK.KwInt64, TK.KwUint64,
                     TK.KwFloat, TK.KwDouble, TK.KwBool, TK.KwString, TK.KwAuto];
    if (!check(TK.Semicolon)) {
      if (declKws.indexOf(peek().kind) >= 0) n.lhs = parseVarDecl();
      else n.lhs = parseExprStatement();
    } else expect(TK.Semicolon, "';'");
    if (!check(TK.Semicolon)) n.cond = parseExpression();
    expect(TK.Semicolon, "';'");
    if (!check(TK.RParen)) n.step = parseExpression();
    expect(TK.RParen, "')'");
    n.thenBranch = parseStatement();
    return n;
  }

  function parseWhile() {
    expect(TK.KwWhile, "'while'");
    expect(TK.LParen, "'('");
    const n = makeNode(NK.WhileStmt);
    n.cond = parseExpression();
    expect(TK.RParen, "')'");
    n.thenBranch = parseStatement();
    return n;
  }

  function parseReturn() {
    expect(TK.KwReturn, "'return'");
    const n = makeNode(NK.ReturnStmt);
    if (!check(TK.Semicolon)) n.initExpr = parseExpression();
    expect(TK.Semicolon, "';'");
    return n;
  }

  function parseExprStatement() {
    const n = makeNode(NK.ExprStmt);
    n.initExpr = parseExpression();
    expect(TK.Semicolon, "';'");
    return n;
  }

  function parseExpression() { return parseAssignment(); }

  function parseAssignment() {
    const left = parseBinary(1);
    const k = peek().kind;
    if (k === TK.Assign) {
      adv();
      const right = parseAssignment();
      const n = makeNode(NK.Assign);
      n.lhs = left; n.rhs = right;
      return n;
    }
    if ([TK.PlusAssign, TK.MinusAssign, TK.StarAssign, TK.SlashAssign].indexOf(k) >= 0) {
      const op = adv().text;
      const right = parseAssignment();
      const n = makeNode(NK.CompoundAssign);
      n.text = op; n.lhs = left; n.rhs = right;
      return n;
    }
    return left;
  }

  function parseBinary(minPrec) {
    let left = parseUnary();
    while (true) {
      const k = peek().kind;
      const p = PREC[k] || 0;
      if (p < minPrec) break;
      const op = adv().text;
      const right = parseBinary(p + 1);
      const n = makeNode(NK.Binary);
      n.text = op; n.lhs = left; n.rhs = right;
      left = n;
    }
    return left;
  }

  function parseUnary() {
    if (check(TK.Not) || check(TK.Minus)) {
      const op = adv();
      const n = makeNode(NK.Unary);
      n.text = op.text; n.lhs = parseUnary();
      return n;
    }
    return parsePostfix();
  }

  function parsePostfix() {
    let expr = parsePrimary();
    while (true) {
      if (check(TK.Dot)) {
        adv();
        const name = expect(TK.Ident, "member name after '.'");
        const n = makeNode(NK.MemberAccess);
        n.lhs = expr; n.text = name.text;
        expr = n;
      } else if (check(TK.LBracket)) {
        adv();
        const idx = parseExpression();
        expect(TK.RBracket, "']'");
        const n = makeNode(NK.IndexAccess);
        n.lhs = expr; n.rhs = idx;
        expr = n;
      } else if (check(TK.LParen)) {
        expect(TK.LParen, "'('");
        const call = makeNode(NK.Call);
        call.lhs = expr;
        if (!check(TK.RParen)) {
          do { call.children.push(parseExpression()); } while (match(TK.Comma));
        }
        expect(TK.RParen, "')'");
        expr = call;
      } else break;
    }
    return expr;
  }

  function parsePrimary() {
    const t = peek();
    switch (t.kind) {
      case TK.IntLit: adv(); { const n = makeNode(NK.IntLiteral); n.text = t.text; n.intVal = parseInt(t.text, 10); return n; }
      case TK.FloatLit: adv(); { const n = makeNode(NK.FloatLiteral); n.text = t.text; n.floatVal = parseFloat(t.text); return n; }
      case TK.StringLit: adv(); { const n = makeNode(NK.StringLiteral); n.text = t.text; return n; }
      case TK.KwTrue: adv(); { const n = makeNode(NK.BoolLiteral); n.boolVal = true; return n; }
      case TK.KwFalse: adv(); { const n = makeNode(NK.BoolLiteral); n.boolVal = false; return n; }
      case TK.KwNull: adv(); return makeNode(NK.NullLiteral);
      case TK.Ident: adv(); { const n = makeNode(NK.Identifier); n.text = t.text; return n; }
      case TK.LParen: adv(); { const e = parseExpression(); expect(TK.RParen, "')'"); return e; }
      default:
        emitErr(`unexpected token '${t.text}' in expression`, t.line, t.col);
        adv(); return makeNode(NK.NullLiteral);
    }
  }

  function parseImport() {
    expect(TK.KwImport, "'import'");
    const tok = expect(TK.StringLit, "string literal after 'import'");
    expect(TK.Semicolon, "';'");
    const n = makeNode(NK.ImportStmt);
    n.text = tok.text;
    return n;
  }

  function parseStruct() {
    expect(TK.KwStruct, "'struct'");
    const name = expect(TK.Ident, "struct name");
    const node = makeNode(NK.StructDecl);
    node.text = name.text;
    expect(TK.LBrace, "'{'");
    while (!check(TK.RBrace) && !check(TK.EOF)) {
      const vis = parseOptionalVisibility();
      const isRepl = match(TK.KwReplicated);
      const type = parseType();
      const fname = expect(TK.Ident, "field name");
      const f = makeNode(NK.FieldDecl);
      f.text = fname.text; f.type = type; f.isReplicated = isRepl; f.visibility = vis;
      if (match(TK.Assign)) f.initExpr = parseExpression();
      expect(TK.Semicolon, "';'");
      node.children.push(f);
    }
    expect(TK.RBrace, "'}'");
    return node;
  }

  function parseClass() {
    expect(TK.KwClass, "'class'");
    const name = expect(TK.Ident, "class name");
    const node = makeNode(NK.ClassDecl);
    node.text = name.text;
    expect(TK.LBrace, "'{'");
    while (!check(TK.RBrace) && !check(TK.EOF)) {
      let rpc = null;
      if (match(TK.At)) rpc = parseRpcDecorator();
      const vis = parseOptionalVisibility();
      const isRepl = match(TK.KwReplicated);
      const type = parseType();
      const mname = expect(TK.Ident, "member name");
      if (check(TK.LParen)) {
        const m = makeNode(NK.MethodDecl);
        m.text = mname.text; m.type = type; m.rpcMode = rpc; m.visibility = vis;
        m.children = parseParams();
        m.body = parseBlock();
        node.children.push(m);
      } else {
        const f = makeNode(NK.FieldDecl);
        f.text = mname.text; f.type = type; f.isReplicated = isRepl; f.visibility = vis;
        if (match(TK.Assign)) f.initExpr = parseExpression();
        expect(TK.Semicolon, "';'");
        node.children.push(f);
      }
    }
    expect(TK.RBrace, "'}'");
    return node;
  }

  function parseModule() {
    const mod = makeNode(NK.Module);
    while (!check(TK.EOF)) {
      const k = peek().kind;
      if (k === TK.KwImport) mod.children.push(parseImport());
      else if (k === TK.KwStruct) mod.children.push(parseStruct());
      else if (k === TK.KwClass) mod.children.push(parseClass());
      else if (k === TK.EOF) break;
      else {
        emitErr(`expected 'import', 'struct', or 'class' but got '${peek().text}'`, peek().line, peek().col);
        adv();
      }
    }
    return mod;
  }

  const module = parseModule();
  return { module, errors };
}

// -----------------------------------------------------------------------------
// JS Code Generator
// -----------------------------------------------------------------------------
function generateJs(module) {
  const lines = [];
  let indent = 0;
  function pad() { return '  '.repeat(indent); }
  function emit(s) { lines.push(pad() + s); }
  function raw(s) { lines.push(s); }
  function blank() { lines.push(''); }

  function defaultInitFor(tdType) {
    if (['int32','uint32','int64','uint64','float','double'].indexOf(tdType) >= 0) return '0';
    if (tdType === 'bool') return 'false';
    if (tdType === 'string') return '""';
    if (tdType === 'void' || tdType === 'auto') return 'null';
    return 'null';
  }

  function escStr(s) {
    let out = '"';
    for (const c of s) {
      if (c === '"') out += '\\"';
      else if (c === '\\') out += '\\\\';
      else if (c === '\n') out += '\\n';
      else if (c === '\r') out += '\\r';
      else if (c === '\t') out += '\\t';
      else out += c;
    }
    return out + '"';
  }

  function emitExpr(n) {
    if (!n) return 'null';
    switch (n.kind) {
      case NK.IntLiteral: case NK.FloatLiteral: return n.text;
      case NK.StringLiteral: return escStr(n.text);
      case NK.BoolLiteral: return n.boolVal ? 'true' : 'false';
      case NK.NullLiteral: return 'null';
      case NK.Identifier: return n.text;
      case NK.MemberAccess: return emitExpr(n.lhs) + '.' + n.text;
      case NK.IndexAccess: return emitExpr(n.lhs) + '[' + emitExpr(n.rhs) + ']';
      case NK.Call: return emitExpr(n.lhs) + '(' + n.children.map(emitExpr).join(', ') + ')';
      case NK.Binary: return '(' + emitExpr(n.lhs) + ' ' + n.text + ' ' + emitExpr(n.rhs) + ')';
      case NK.Unary: return '(' + n.text + emitExpr(n.lhs) + ')';
      case NK.Assign: return emitExpr(n.lhs) + ' = ' + emitExpr(n.rhs);
      case NK.CompoundAssign: return emitExpr(n.lhs) + ' ' + n.text + ' ' + emitExpr(n.rhs);
      default: return '/* unknown */';
    }
  }

  // Emit an if / else-if / else chain. Handles arbitrary depth without
  // corrupting the shared `indent` variable (the previous implementation
  // stashed indent into a closure and never restored it, leaving indent=0
  // after the IIFE returned and then crashing on indent-- with RangeError).
  function emitIfChain(n) {
    // First (or only) if.
    emit('if (' + emitExpr(n.cond) + ') {');
    indent++;
    if (n.thenBranch && n.thenBranch.kind === NK.Block) n.thenBranch.children.forEach(emitStmt);
    else emitStmt(n.thenBranch);
    indent--;

    // Walk the else-chain.
    let cur = n.elseBranch;
    while (cur) {
      if (cur.kind === NK.IfStmt) {
        // else if (...) { ... }
        emit('} else if (' + emitExpr(cur.cond) + ') {');
        indent++;
        const tb = cur.thenBranch;
        if (tb && tb.kind === NK.Block) tb.children.forEach(emitStmt);
        else emitStmt(tb);
        indent--;
        cur = cur.elseBranch;
      } else {
        // Plain else { ... } — terminates the chain.
        emit('} else {');
        indent++;
        if (cur.kind === NK.Block) cur.children.forEach(emitStmt);
        else emitStmt(cur);
        indent--;
        cur = null;
      }
    }
    emit('}');
  }

  function emitStmt(n) {
    if (!n) return;
    switch (n.kind) {
      case NK.Block:
        emit('{');
        indent++;
        n.children.forEach(emitStmt);
        indent--;
        emit('}');
        break;
      case NK.VarDeclStmt:
        emit('let ' + n.text + (n.initExpr ? ' = ' + emitExpr(n.initExpr) : '') + ';');
        break;
      case NK.IfStmt:
        emitIfChain(n);
        break;
      case NK.ForStmt: {
        let init = '; ';
        if (n.lhs) {
          if (n.lhs.kind === NK.VarDeclStmt) {
            init = 'let ' + n.lhs.text + (n.lhs.initExpr ? ' = ' + emitExpr(n.lhs.initExpr) : '') + '; ';
          } else {
            init = emitExpr(n.lhs.initExpr) + '; ';
          }
        }
        const cond = n.cond ? emitExpr(n.cond) : '';
        const step = n.step ? emitExpr(n.step) : '';
        emit('for (' + init + cond + '; ' + step + ') {');
        indent++;
        if (n.thenBranch && n.thenBranch.kind === NK.Block) n.thenBranch.children.forEach(emitStmt);
        else emitStmt(n.thenBranch);
        indent--;
        emit('}');
        break;
      }
      case NK.WhileStmt:
        emit('while (' + emitExpr(n.cond) + ') {');
        indent++;
        if (n.thenBranch && n.thenBranch.kind === NK.Block) n.thenBranch.children.forEach(emitStmt);
        else emitStmt(n.thenBranch);
        indent--;
        emit('}');
        break;
      case NK.ReturnStmt:
        emit('return' + (n.initExpr ? ' ' + emitExpr(n.initExpr) : '') + ';');
        break;
      case NK.BreakStmt: emit('break;'); break;
      case NK.ContinueStmt: emit('continue;'); break;
      case NK.ExprStmt:
        emit(emitExpr(n.initExpr) + ';');
        break;
      default: break;
    }
  }

  function emitClass(n) {
    emit('global.' + n.text + ' = class ' + n.text + ' {');
    indent++;
    emit('constructor() {');
    indent++;
    const replicatedFields = [];
    for (const m of n.children) {
      if (m.kind === NK.FieldDecl) {
        if (m.isReplicated) {
          emit('this._' + m.text + ' = ' + (m.initExpr ? emitExpr(m.initExpr) : defaultInitFor(m.type ? m.type.text : 'auto')) + ';');
          replicatedFields.push(m.text);
        } else {
          emit('this.' + m.text + ' = ' + (m.initExpr ? emitExpr(m.initExpr) : defaultInitFor(m.type ? m.type.text : 'auto')) + ';');
        }
      }
    }
    indent--;
    emit('}');
    blank();
    for (const m of n.children) {
      if (m.kind === NK.MethodDecl) {
        const params = m.children.map(p => p.text).join(', ');
        emit(m.text + '(' + params + ') {');
        indent++;
        if (m.body && m.body.kind === NK.Block) m.body.children.forEach(emitStmt);
        indent--;
        emit('}');
        blank();
      }
    }
    // Replicated field getters/setters on prototype
    for (const fname of replicatedFields) {
      emit('static get _replicated_' + fname + '() { return "' + fname + '"; }');
    }
    indent--;
    emit('};');
    blank();
    // Register RPCs at module load
    for (const m of n.children) {
      if (m.kind === NK.MethodDecl && m.rpcMode) {
        emit("if (typeof __td_rpc_register === 'function') {");
        indent++;
        emit("__td_rpc_register('" + n.text + "', '" + m.text + "', '" + m.rpcMode + "', function(instance, args) { return instance." + m.text + ".apply(instance, args); });");
        indent--;
        emit('}');
      }
    }
    // Register replicated fields at module load
    if (replicatedFields.length > 0) {
      emit("if (typeof __td_repl_register === 'function') {");
      indent++;
      emit("__td_repl_register('" + n.text + "', " + JSON.stringify(replicatedFields) + ");");
      indent--;
      emit('}');
    }
    blank();
  }

  function emitStruct(n) {
    emit('global.' + n.text + ' = class ' + n.text + ' {');
    indent++;
    emit('constructor() {');
    indent++;
    for (const f of n.children) {
      if (f.kind === NK.FieldDecl) {
        emit('this.' + f.text + ' = ' + (f.initExpr ? emitExpr(f.initExpr) : defaultInitFor(f.type ? f.type.text : 'auto')) + ';');
      }
    }
    indent--;
    emit('}');
    indent--;
    emit('};');
    blank();
  }

  raw("// Auto-generated by TDScript compiler (target: js). Do not edit by hand.");
  raw("'use strict';");
  blank();
  for (const child of module.children) {
    if (child.kind === NK.ImportStmt) {
      raw("try { require('" + child.text + "'); } catch (e) { /* browser: pre-loaded */ }");
    } else if (child.kind === NK.StructDecl) {
      emitStruct(child);
    } else if (child.kind === NK.ClassDecl) {
      emitClass(child);
    }
  }
  return lines.join('\n');
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
function compile(source, target) {
  target = target || 'js';
  const { tokens, errors: lexErrors } = tokenize(source);
  const { module, errors: parseErrors } = parse(tokens);
  const errors = lexErrors.concat(parseErrors);
  if (errors.length > 0) {
    const msg = errors.map(e => `[line ${e.line}:${e.col}] ${e.message}`).join('\n');
    return { ok: false, error: msg, code: '' };
  }
  if (target === 'js') {
    return { ok: true, code: generateJs(module), error: '' };
  }
  if (target === 'cpp') {
    return { ok: false, error: 'C++ codegen target is not yet implemented (use "js")', code: '' };
  }
  return { ok: false, error: 'unknown target: ' + target, code: '' };
}

module.exports = { compile, tokenize, parse, generateJs, TK, NK };

// =============================================================================
// CLI entrypoint — `node tdscript.js <file.td> [-o out.js] [--target js|cpp]`
//
// When invoked directly (not required as a module), compile a single .td file
// and either write the output to -o PATH or print it to stdout.
// =============================================================================
if (require.main === module) {
  const fs = require('fs');
  const path = require('path');

  const argv = process.argv.slice(2);
  if (argv.length === 0 || argv[0] === '--help' || argv[0] === '-h') {
    console.log('Usage: tdscript.js <file.td> [-o out.js] [--target js|cpp]');
    console.log('       tdscript.js --help');
    console.log('');
    console.log('Compiles a .td source file to JavaScript (default) or C++ (stub).');
    console.log('If -o is omitted, the output is written to stdout.');
    process.exit(0);
  }

  let inFile = null;
  let outFile = null;
  let target = 'js';
  for (let i = 0; i < argv.length; i++) {
    const t = argv[i];
    if (t === '-o' || t === '--out') {
      outFile = argv[++i];
    } else if (t === '--target') {
      target = argv[++i];
    } else if (t.startsWith('-')) {
      console.error('Unknown option: ' + t);
      process.exit(2);
    } else {
      inFile = t;
    }
  }

  if (!inFile) {
    console.error('No input file specified.');
    process.exit(2);
  }
  if (!fs.existsSync(inFile)) {
    console.error('Input file not found: ' + inFile);
    process.exit(2);
  }

  const src = fs.readFileSync(inFile, 'utf8');
  const result = compile(src, target);
  if (!result.ok) {
    console.error(result.error);
    process.exit(1);
  }
  if (outFile) {
    fs.writeFileSync(outFile, result.code, 'utf8');
    console.error('Wrote ' + path.resolve(outFile));
  } else {
    process.stdout.write(result.code);
  }
}
