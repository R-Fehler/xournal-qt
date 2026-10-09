// The function plotter's expression language: what a teacher or a student types ("x^2 - 2x + 1", "2sin(x)",
// "sqrt x", "a*x^2 + b", "1,5x") parsed into a tree, evaluated by closures (never eval or Function of what was typed),
// and written as LaTeX for the formula next to the curve.
//
//   parse(text, {variable: "x", decimalComma: true}) → {ok: true, tree, params: ["a", "b"]}
//                                                     | {ok: false, error: "…", position}
//   compile(tree) → function (vars) → number          (vars: {x: 1, a: 2, …})
//   toLatex(tree, {decimalComma}) → "x^{2} - 2x + 1"
//
// The language: numbers (1.5; 1,5 with a decimal comma where it cannot separate arguments), + - * / ^ (also ²,
// ³, ·, ×, ÷, −), implicit multiplication (2x, 3(x+1), 2pi, x sin x, (x+1)(x-1)), unary minus (-x^2 is -(x^2)),
// powers to the right (2^3^2 = 2^9), |x| for abs, functions with or without parentheses (sin x, sin 2x = sin(2x),
// sin^2 x = sin(x)^2), pi/π and e, and parameters: any other single letter (a, b, k, …), which the dialog gives
// sliders.

export const FUNCTIONS = {
    sin: { f: Math.sin, tex: "\\sin" },
    cos: { f: Math.cos, tex: "\\cos" },
    tan: { f: Math.tan, tex: "\\tan" },
    cot: { f: function (x) { return 1 / Math.tan(x) }, tex: "\\cot" },
    sec: { f: function (x) { return 1 / Math.cos(x) }, tex: "\\sec" },
    csc: { f: function (x) { return 1 / Math.sin(x) }, tex: "\\csc" },
    asin: { f: Math.asin, tex: "\\arcsin" },
    acos: { f: Math.acos, tex: "\\arccos" },
    atan: { f: Math.atan, tex: "\\arctan" },
    arcsin: { f: Math.asin, tex: "\\arcsin" },
    arccos: { f: Math.acos, tex: "\\arccos" },
    arctan: { f: Math.atan, tex: "\\arctan" },
    sinh: { f: Math.sinh, tex: "\\sinh" },
    cosh: { f: Math.cosh, tex: "\\cosh" },
    tanh: { f: Math.tanh, tex: "\\tanh" },
    sqrt: { f: Math.sqrt, tex: null },
    cbrt: { f: Math.cbrt, tex: null },
    abs: { f: Math.abs, tex: null },
    exp: { f: Math.exp, tex: null },
    ln: { f: Math.log, tex: "\\ln" },
    log: { f: Math.log10, tex: "\\log", args: [1, 2] },  // log(x) = log10, log(b, x) = log_b(x)
    lg: { f: Math.log10, tex: "\\lg" },
    log2: { f: Math.log2, tex: "\\log_{2}" },
    floor: { f: Math.floor, tex: null },
    ceil: { f: Math.ceil, tex: null },
    round: { f: Math.round, tex: null },
    sign: { f: Math.sign, tex: "\\operatorname{sgn}" },
    sgn: { f: Math.sign, tex: "\\operatorname{sgn}" },
    min: { f: Math.min, tex: "\\min", args: [2, 9] },
    max: { f: Math.max, tex: "\\max", args: [2, 9] },
    root: { f: function (x, n) { return x < 0 && n % 2 === 1 ? -Math.pow(-x, 1 / n) : Math.pow(x, 1 / n) }, tex: null, args: [2, 2] }
}
const CONSTANTS = { pi: Math.PI, "π": Math.PI, e: Math.E }
/// Names longest first: a run of letters is split into these and single letters
const NAMES = Object.keys(FUNCTIONS).concat(["pi"]).sort(function (a, b) { return b.length - a.length })

function isDigit(c) { return c >= "0" && c <= "9" }
function isLetter(c) { return /[A-Za-zα-ωΑ-Ωπ]/.test(c) }

/// The edit distance of two short words (for "did you mean")
export function distance(a, b) {
    const d = []
    for (let i = 0; i <= a.length; ++i) d.push([i])
    for (let j = 1; j <= b.length; ++j) d[0][j] = j
    for (let i = 1; i <= a.length; ++i) {
        for (let j = 1; j <= b.length; ++j) {
            d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1))
            if (i > 1 && j > 1 && a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) d[i][j] = Math.min(d[i][j], d[i - 2][j - 2] + 1)
        }
    }
    return d[a.length][b.length]
}
/// The known function a typo is closest to ("" none close)
export function closest(word) {
    let best = "", bestD = 99
    Object.keys(FUNCTIONS).forEach(function (n) {
        const dd = distance(word.toLowerCase(), n)
        if (dd < bestD) { bestD = dd; best = n }
    })
    const limit = word.length >= 4 ? 2 : 1
    return bestD <= limit ? best : ""
}

class ParseError {
    constructor(message, position) { this.message = message; this.position = position }
}

// --- tokens ---------------------------------------------------------------------------------------------------------

/// [{t: "num" | "name" | "op" | "(" | ")" | "," | "|", v, p (position)}]
function tokenize(text, options) {
    const s = text.replace(/[−–]/g, "-").replace(/[·×∙⋅]/g, "*").replace(/÷/g, "/")
                  .replace(/²/g, "^2").replace(/³/g, "^3")
    // Where a comma between digits is a decimal comma: not inside the parentheses of a function with several
    // arguments (min(1,5)), unless those use ";" between them
    const frames = []  // per open parenthesis: {multi, semicolon}
    function closingOf(i) {
        let depth = 0
        for (let j = i; j < s.length; ++j) {
            if (s[j] === "(") ++depth
            else if (s[j] === ")" && --depth === 0) return j
        }
        return s.length
    }
    const out = []
    let i = 0
    while (i < s.length) {
        const c = s[i]
        if (c === " " || c === "\t") { ++i; continue }
        if (isDigit(c) || (c === "." && isDigit(s[i + 1] || ""))) {
            const start = i
            let num = ""
            while (i < s.length && isDigit(s[i])) num += s[i++]
            const top = frames.length > 0 ? frames[frames.length - 1] : null
            const commaIsDecimal = options.decimalComma !== false && (!top || !top.multi || top.semicolon)
            if ((s[i] === "." || (s[i] === "," && commaIsDecimal)) && isDigit(s[i + 1] || "")) {
                num += "."
                ++i
                while (i < s.length && isDigit(s[i])) num += s[i++]
            } else if (s[i] === "." && !isDigit(s[i + 1] || "")) {
                ++i  // ("2." is 2)
            }
            if (s[i] === "e" || s[i] === "E") {  // 1e-3 (not 2e: that is 2·e)
                const m = /^[eE][+-]?\d+/.exec(s.slice(i))
                if (m) { num += m[0]; i += m[0].length }
            }
            out.push({ t: "num", v: Number(num), p: start, src: s.slice(start, i) })
            continue
        }
        if (isLetter(c)) {
            const start = i
            while (i < s.length && (isLetter(s[i]) || (isDigit(s[i]) && s.slice(start, i) === "log" && s[i] === "2"))) ++i
            out.push({ t: "name", v: s.slice(start, i), p: start })
            continue
        }
        if (c === "(") {
            const prev = out.length > 0 ? out[out.length - 1] : null
            const fn = prev && prev.t === "name" && FUNCTIONS[prev.v.toLowerCase()] ? FUNCTIONS[prev.v.toLowerCase()] : null
            const end = closingOf(i)
            frames.push({ multi: !!(fn && fn.args && fn.args[1] > 1), semicolon: s.slice(i, end).indexOf(";") >= 0 })
            out.push({ t: "(", p: i++ })
            continue
        }
        if (c === ")") { frames.pop(); out.push({ t: ")", p: i++ }); continue }
        if (c === "[" || c === "{") { frames.push({ multi: false, semicolon: false }); out.push({ t: "(", p: i++ }); continue }
        if (c === "]" || c === "}") { frames.pop(); out.push({ t: ")", p: i++ }); continue }
        if (c === "," || c === ";") { out.push({ t: ",", p: i++ }); continue }
        if (c === "|") { out.push({ t: "|", p: i++ }); continue }
        if ("+-*/^".indexOf(c) >= 0) {
            if (c === "*" && s[i + 1] === "*") { out.push({ t: "op", v: "^", p: i }); i += 2; continue }
            out.push({ t: "op", v: c, p: i++ })
            continue
        }
        if (c === "=") throw new ParseError("type only the right side (\"x^2\", not \"y = x^2\")", i)
        throw new ParseError("\"" + c + "\" is not part of a formula", i)
    }
    return out
}

/// A run of letters as names: known ones (longest first), the variable and constants, else single letters
/// (parameters). A run that is a typo of a function is an error.
function splitName(token, variable) {
    const word = token.v
    const lower = word.toLowerCase()
    if (FUNCTIONS[lower] || lower === "pi" || word === "π" || word === variable || word.length === 1) {
        return [{ t: "name", v: FUNCTIONS[lower] || lower === "pi" ? lower : word, p: token.p }]
    }
    const parts = []
    let i = 0
    while (i < word.length) {
        let found = ""
        for (const n of NAMES) {
            if (lower.startsWith(n, i) && n.length > found.length) found = n
        }
        if (found !== "") {
            parts.push({ t: "name", v: found, p: token.p + i })
            i += found.length
        } else {
            parts.push({ t: "name", v: word[i], p: token.p + i })
            ++i
        }
    }
    // A run of letters with no known name in it that looks like a function misspelled
    const knownInside = parts.some(function (q) { return q.v.length > 1 })
    if (!knownInside && word.length >= 3) {
        const near = closest(word)
        if (near !== "") throw new ParseError("unknown function \"" + word + "\" - did you mean " + near + "?", token.p)
    }
    return parts
}

// --- the parser -----------------------------------------------------------------------------------------------------

function Parser(tokens, options) {
    this.tokens = tokens
    this.i = 0
    this.variable = options.variable || "x"
    this.params = {}
    this.absDepth = 0
}
Parser.prototype.peek = function () { return this.tokens[this.i] }
Parser.prototype.next = function () { return this.tokens[this.i++] }
Parser.prototype.at = function (t, v) {
    const k = this.tokens[this.i]
    return k !== undefined && k.t === t && (v === undefined || k.v === v)
}
Parser.prototype.position = function () {
    const k = this.tokens[this.i]
    return k ? k.p : (this.tokens.length > 0 ? this.tokens[this.tokens.length - 1].p + 1 : 0)
}
/// Whether the next token can start a factor (for implicit multiplication)
Parser.prototype.startsFactor = function () {
    const k = this.peek()
    if (!k) return false
    if (k.t === "num" || k.t === "name" || k.t === "(") return true
    if (k.t === "|") return this.absDepth === 0  // (an opening bar; inside |…| a bar closes)
    return false
}

Parser.prototype.expression = function () {
    let left = this.term()
    while (this.at("op", "+") || this.at("op", "-")) {
        const op = this.next().v
        if (!this.peek() || this.at(")") || this.at(",")) throw new ParseError("\"" + op + "\" needs something after it", this.position())
        left = { k: op === "+" ? "add" : "sub", a: left, b: this.term() }
    }
    return left
}
Parser.prototype.term = function () {
    let left = this.unary()
    for (;;) {
        if (this.at("op", "*") || this.at("op", "/")) {
            const op = this.next().v
            if (!this.peek() || this.at(")") || this.at(",")) throw new ParseError("\"" + op + "\" needs something after it", this.position())
            left = { k: op === "*" ? "mul" : "div", a: left, b: this.unary() }
        } else if (this.startsFactor()) {
            if (left.k === "num" && this.at("num")) {
                throw new ParseError("an operator is missing between two numbers", this.position())
            }
            left = { k: "mul", a: left, b: this.power(), implicit: true }
        } else {
            return left
        }
    }
}
Parser.prototype.unary = function () {
    if (this.at("op", "-")) {
        this.next()
        return { k: "neg", a: this.unary() }
    }
    if (this.at("op", "+")) {
        this.next()
        return this.unary()
    }
    return this.power()
}
Parser.prototype.power = function () {
    const base = this.primary()
    if (this.at("op", "^")) {
        this.next()
        if (!this.peek()) throw new ParseError("\"^\" needs an exponent", this.position())
        return { k: "pow", a: base, b: this.unary() }  // (right to left: 2^3^2 = 2^(3^2); 2^-1)
    }
    return base
}
/// A function's argument without parentheses: "sin 2x" is sin(2x), "sin x cos x" is sin(x)·cos(x)
Parser.prototype.bareArgument = function () {
    let arg = this.power()
    while (this.startsFactor() && !(this.at("name") && FUNCTIONS[this.peek().v])) {
        arg = { k: "mul", a: arg, b: this.power(), implicit: true }
    }
    return arg
}
Parser.prototype.primary = function () {
    const k = this.peek()
    if (!k) throw new ParseError(this.tokens.length === 0 ? "empty" : "the formula ends too early", this.position())
    if (k.t === "num") {
        this.next()
        return { k: "num", v: k.v, src: k.src }
    }
    if (k.t === "(") {
        this.next()
        const inner = this.expression()
        if (!this.at(")")) throw new ParseError(this.at(",") ? "\",\" only separates the arguments of a function" : "\")\" is missing", this.position())
        this.next()
        return { k: "group", a: inner }
    }
    if (k.t === "|") {
        this.next()
        ++this.absDepth
        const inner = this.expression()
        --this.absDepth
        if (!this.at("|")) throw new ParseError("the closing \"|\" is missing", this.position())
        this.next()
        return { k: "call", f: "abs", args: [inner] }
    }
    if (k.t === "name") {
        this.next()
        const name = k.v
        const fn = FUNCTIONS[name]
        if (fn) {
            // sin^2 x
            let outerPower = null
            if (this.at("op", "^")) {
                this.next()
                outerPower = this.unary()
            }
            let args
            if (this.at("(")) {
                this.next()
                args = [this.expression()]
                while (this.at(",")) {
                    this.next()
                    args.push(this.expression())
                }
                if (!this.at(")")) throw new ParseError("\")\" is missing after the arguments of " + name, this.position())
                this.next()
            } else {
                if (!this.startsFactor()) throw new ParseError(name + " needs an argument: " + name + "(x)", this.position())
                args = [this.bareArgument()]
            }
            const range = fn.args || [1, 1]
            if (args.length < range[0] || args.length > range[1]) {
                throw new ParseError(name + " takes " + (range[0] === range[1] ? range[0] : range[0] + " to " + range[1]) +
                                     " argument" + (range[1] > 1 ? "s" : ""), k.p)
            }
            const call = { k: "call", f: name, args: args }
            return outerPower ? { k: "pow", a: call, b: outerPower } : call
        }
        if (name === "pi" || name === "π") return { k: "const", v: "pi" }
        if (name === "e") return { k: "const", v: "e" }
        if (name === this.variable) return { k: "var", v: name }
        if (name.length === 1) {
            this.params[name] = true
            return { k: "param", v: name }
        }
        throw new ParseError("unknown name \"" + name + "\"", k.p)
    }
    if (k.t === ")") throw new ParseError("\")\" without \"(\"", k.p)
    if (k.t === ",") throw new ParseError("\",\" only separates the arguments of a function", k.p)
    if (k.t === "op") throw new ParseError("\"" + k.v + "\" needs something before it", k.p)
    throw new ParseError("unexpected \"" + (k.v || k.t) + "\"", k.p)
}

/// Parses a formula. options: {variable: "x" | "t", decimalComma: true}
export function parse(text, options) {
    options = options || {}
    try {
        const raw = tokenize(String(text), options)
        const tokens = []
        raw.forEach(function (tk) {
            if (tk.t === "name") splitName(tk, options.variable || "x").forEach(function (p) { tokens.push(p) })
            else tokens.push(tk)
        })
        if (tokens.length === 0) return { ok: false, error: "empty", position: 0 }
        const p = new Parser(tokens, options)
        const tree = p.expression()
        if (p.peek()) {
            const k = p.peek()
            throw new ParseError(k.t === ")" ? "\")\" without \"(\"" : "unexpected \"" + (k.v !== undefined ? k.v : k.t) + "\"", k.p)
        }
        return { ok: true, tree: tree, params: Object.keys(p.params).sort() }
    } catch (e) {
        if (e instanceof ParseError) return { ok: false, error: e.message, position: e.position }
        throw e
    }
}

// --- evaluation -------------------------------------------------------------------------------------------------------

/// A function of the variables ({x: …, a: …}) for a tree: closures, built once
export function compile(tree) {
    switch (tree.k) {
    case "num": { const v = tree.v; return function () { return v } }
    case "const": { const v = tree.v === "pi" ? Math.PI : Math.E; return function () { return v } }
    case "var":
    case "param": { const n = tree.v; return function (vars) { const v = vars[n]; return v === undefined ? NaN : v } }
    case "group": return compile(tree.a)
    case "neg": { const a = compile(tree.a); return function (vars) { return -a(vars) } }
    case "add": { const a = compile(tree.a), b = compile(tree.b); return function (vars) { return a(vars) + b(vars) } }
    case "sub": { const a = compile(tree.a), b = compile(tree.b); return function (vars) { return a(vars) - b(vars) } }
    case "mul": { const a = compile(tree.a), b = compile(tree.b); return function (vars) { return a(vars) * b(vars) } }
    case "div": { const a = compile(tree.a), b = compile(tree.b); return function (vars) { return a(vars) / b(vars) } }
    case "pow": {
        const a = compile(tree.a), b = compile(tree.b)
        return function (vars) {
            const x = a(vars), y = b(vars)
            // (a negative base to the power of an odd fraction: the real root, as on a calculator: (-8)^(1/3) = -2)
            if (x < 0 && !Number.isInteger(y)) {
                const inv = 1 / y
                if (Math.abs(inv - Math.round(inv)) < 1e-9 && Math.round(inv) % 2 !== 0) return -Math.pow(-x, y)
            }
            return Math.pow(x, y)
        }
    }
    case "call": {
        const fn = FUNCTIONS[tree.f]
        const args = tree.args.map(compile)
        if (tree.f === "log" && args.length === 2) {
            const b = args[0], x = args[1]
            return function (vars) { return Math.log(x(vars)) / Math.log(b(vars)) }
        }
        if (args.length === 1) { const a = args[0], f = fn.f; return function (vars) { return f(a(vars)) } }
        return function (vars) { return fn.f.apply(null, args.map(function (a) { return a(vars) })) }
    }
    }
    throw new Error("unknown node " + tree.k)
}

// --- LaTeX ------------------------------------------------------------------------------------------------------------

/// A number as LaTeX ("1{,}5" with a decimal comma; "-2")
export function numberTex(v, options) {
    let s = typeof v === "string" ? v : String(Math.round(v * 1e10) / 1e10)
    if (s.indexOf("e") >= 0) {
        const m = /^(-?[\d.]+)e([+-]?\d+)$/.exec(s)
        if (m) s = m[1] + " \\cdot 10^{" + Number(m[2]) + "}"
    }
    if (options && options.decimalComma) s = s.replace(".", "{,}")
    return s
}

const PREC = { add: 1, sub: 1, neg: 2, mul: 3, div: 3, pow: 5 }
function prec(t) { return PREC[t.k] !== undefined ? PREC[t.k] : 9 }

/// Starts with a digit (2x: the number goes first)
function startsWithNumber(t) {
    if (t.k === "num") return true
    if (t.k === "mul" || t.k === "pow") return startsWithNumber(t.a)
    return false
}

/// The tree as LaTeX. options: {decimalComma}
export function toLatex(t, options) {
    options = options || {}
    function wrap(s) { return "\\left(" + s + "\\right)" }
    function tex(t, parent, side) {
        switch (t.k) {
        case "num": return numberTex(t.v, options)
        case "const": return t.v === "pi" ? "\\pi" : "e"
        case "var":
        case "param": return t.v
        case "group": {
            // (the user's parentheses: kept where they matter, dropped where the LaTeX shows the grouping anyway)
            const inner = tex(t.a, parent, side)
            if (parent === "div" || parent === "sqrt" || parent === "exp" || parent === "abs" || parent === "top") return tex(t.a, null)
            if (prec(t.a) >= 9) return inner
            return wrap(tex(t.a, null))
        }
        case "neg": {
            const a = tex(t.a, "neg")
            return "-" + (prec(t.a) <= 2 ? wrap(a) : a)
        }
        case "add": return tex(t.a, "add", "a") + " + " + tex(t.b, "add", "b")
        case "sub": {
            const b = tex(t.b, "sub", "b")
            return tex(t.a, "sub", "a") + " - " + (prec(t.b) <= 1 && t.b.k !== "group" ? wrap(b) : b)
        }
        case "mul": {
            let a = tex(t.a, "mul", "a"), b = tex(t.b, "mul", "b")
            if (prec(t.a) < 3 && t.a.k !== "group") a = wrap(a)
            if (prec(t.b) < 3 && t.b.k !== "group") b = wrap(b)
            if (t.b.k === "neg") b = wrap(b)
            // 2x, 2\sin(x), ab, 2(x + 1): juxtaposed; 2 \cdot 3 and x \cdot 2: a dot
            const juxtapose = !startsWithNumber(t.b) && t.b.k !== "neg"
            return a + (juxtapose ? (/[A-Za-z]$/.test(a) && /^[A-Za-z]/.test(b) ? " " : "") : " \\cdot ") + b
        }
        case "div": return "\\frac{" + tex(t.a, "div") + "}{" + tex(t.b, "div") + "}"
        case "pow": {
            let a = tex(t.a, "pow", "a")
            if (prec(t.a) <= 5 || t.a.k === "num" && t.a.v < 0) a = t.a.k === "group" ? a : wrap(a)
            if (t.a.k === "call" && FUNCTIONS[t.a.f].tex && t.a.args.length === 1) {
                // sin(x)^2 as \sin^{2}(x)
                return FUNCTIONS[t.a.f].tex + "^{" + tex(t.b, "top") + "}" + wrap(tex(t.a.args[0], null))
            }
            return a + "^{" + tex(t.b, "top") + "}"
        }
        case "call": {
            const f = t.f
            const args = t.args
            const one = function () { return tex(args[0], f === "sqrt" || f === "abs" || f === "exp" ? f : null) }
            switch (f) {
            case "sqrt": return "\\sqrt{" + one() + "}"
            case "cbrt": return "\\sqrt[3]{" + one() + "}"
            case "root": return "\\sqrt[" + tex(args[1], "top") + "]{" + tex(args[0], "sqrt") + "}"
            case "abs": return "\\left|" + one() + "\\right|"
            case "exp": return "e^{" + one() + "}"
            case "floor": return "\\lfloor " + one() + " \\rfloor"
            case "ceil": return "\\lceil " + one() + " \\rceil"
            case "log":
                if (args.length === 2) return "\\log_{" + tex(args[0], "top") + "}" + wrap(tex(args[1], null))
                break
            }
            const name = FUNCTIONS[f].tex || "\\operatorname{" + f + "}"
            return name + wrap(args.map(function (a) { return tex(a, null) }).join(", "))
        }
        }
        return "?"
    }
    return tex(t, null)
}
