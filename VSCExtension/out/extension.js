"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const path = __importStar(require("path"));
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
let client;
const typeDecoration = vscode.window.createTextEditorDecorationType({
    color: '#D19A66',
});
const variableDecoration = vscode.window.createTextEditorDecorationType({
    color: '#56B6C2',
});
const asmFunctionToSymbol = new Map();
const asmSymbolToLocation = new Map();
let navigationIndexRefreshTimer;
let decorationRefreshTimer;
const keywordDocs = {
    data: 'Declares top-level data that the compiler emits into the object file.',
    section: 'Adds an explicit output section to a data declaration.',
    struct: 'Declares a structure type.',
    packed: 'Marks a structure as packed so the backend omits padding.',
    function: 'Declares a function.',
    extern: 'Marks a declaration as external.',
    asm: 'Used with extern for external assembly symbols.',
    local: 'Declares a local variable inside a function body.',
    return: 'Returns a value from the current function.',
    if: 'Starts a conditional block.',
    then: 'Completes the if header.',
    while: 'Starts a loop block.',
    do: 'Completes the while header.',
    end: 'Closes a block.',
    true: 'Boolean true literal.',
    false: 'Boolean false literal.',
    nil: 'Null-like literal used for absent values.',
    '~=': 'Lua inequality operator supported by the custom parser.',
};
function findTypeAnnotationSpan(text, startIndex) {
    let index = startIndex;
    while (index < text.length && /\s/.test(text[index])) {
        index++;
    }
    const typeStart = index;
    let parenDepth = 0;
    let angleDepth = 0;
    while (index < text.length) {
        const ch = text[index];
        if (parenDepth === 0 && angleDepth === 0 && (ch === '=' || ch === ',' || ch === ')' || ch === ';')) {
            break;
        }
        if (parenDepth === 0 && angleDepth === 0 && /\s/.test(ch)) {
            break;
        }
        if (ch === '(') {
            parenDepth++;
            index++;
            continue;
        }
        if (ch === ')') {
            if (parenDepth === 0) {
                break;
            }
            parenDepth--;
            index++;
            continue;
        }
        if (ch === '<') {
            angleDepth++;
            index++;
            continue;
        }
        if (ch === '>') {
            if (angleDepth === 0) {
                break;
            }
            angleDepth--;
            index++;
            continue;
        }
        index++;
    }
    if (index <= typeStart) {
        return undefined;
    }
    return { start: typeStart, end: index };
}
function stripComment(line) {
    const commentIndex = line.indexOf('--');
    if (commentIndex < 0) {
        return line;
    }
    return line.slice(0, commentIndex);
}
function collectTypeDecorations(document) {
    const decorations = [];
    for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
        const line = stripComment(document.lineAt(lineNumber).text);
        for (let colonIndex = line.indexOf(':'); colonIndex >= 0; colonIndex = line.indexOf(':', colonIndex + 1)) {
            const span = findTypeAnnotationSpan(line, colonIndex + 1);
            if (span) {
                decorations.push({
                    range: new vscode.Range(lineNumber, span.start, lineNumber, span.end),
                });
            }
        }
    }
    return decorations;
}
function collectVariableDecorations(document) {
    const decorations = [];
    const variablePatterns = [
        /^\s*data\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
        /^\s*local\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
        /^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
        /^\s*asm\s+function\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
        /^\s*(?:packed\s+)?struct\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
    ];
    for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
        const line = document.lineAt(lineNumber).text;
        for (const pattern of variablePatterns) {
            pattern.lastIndex = 0;
            for (let match = pattern.exec(line); match !== null; match = pattern.exec(line)) {
                const variableText = match[1];
                const start = line.indexOf(variableText, match.index);
                if (start >= 0) {
                    decorations.push({
                        range: new vscode.Range(lineNumber, start, lineNumber, start + variableText.length),
                    });
                }
            }
        }
    }
    return decorations;
}
function refreshDecorations(editor) {
    const activeEditors = editor ? [editor] : vscode.window.visibleTextEditors;
    for (const visibleEditor of activeEditors) {
        if (visibleEditor.document.languageId === 'lua') {
            visibleEditor.setDecorations(variableDecoration, collectVariableDecorations(visibleEditor.document));
            visibleEditor.setDecorations(typeDecoration, collectTypeDecorations(visibleEditor.document));
        }
    }
}
function scheduleDecorationRefresh(editor) {
    if (decorationRefreshTimer) {
        clearTimeout(decorationRefreshTimer);
    }
    decorationRefreshTimer = setTimeout(() => {
        refreshDecorations(editor);
    }, 120);
}
function hoverTextForWord(document, word, position) {
    if (keywordDocs[word]) {
        return keywordDocs[word];
    }
    const line = stripComment(document.lineAt(position.line).text);
    if (/^\s*(?:packed\s+)?struct\s+/.test(line) && /\b[A-Za-z_][A-Za-z0-9_]*\b/.test(word)) {
        return 'Structure name declared by a Lunaris `struct` or `packed struct` form.';
    }
    if (/^\s*data\s+/.test(line) && /\b[A-Za-z_][A-Za-z0-9_]*\b/.test(word)) {
        return 'Top-level data name declared by Lunaris `data`.';
    }
    if (/^\s*(?:asm\s+)?function\s+/.test(line) && /\b[A-Za-z_][A-Za-z0-9_]*\b/.test(word)) {
        return 'Function name declared by Lunaris.';
    }
    for (let colonIndex = line.indexOf(':'); colonIndex >= 0; colonIndex = line.indexOf(':', colonIndex + 1)) {
        const span = findTypeAnnotationSpan(line, colonIndex + 1);
        if (span && position.character >= span.start && position.character <= span.end) {
            return 'Lunaris type annotation.';
        }
    }
    return undefined;
}
function extractAsmFunctionReference(line) {
    const match = /^\s*asm\s+function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*:\s*[^=]+?=\s*([A-Za-z_][A-Za-z0-9_]*)\s*;?\s*$/.exec(line);
    if (!match) {
        return undefined;
    }
    return {
        functionName: match[1],
        symbolName: match[2],
    };
}
async function findAsmFunctionSymbol(functionName) {
    const asmFunctionPattern = new RegExp(`^\\s*asm\\s+function\\s+(${escapeRegExp(functionName)})\\s*\\([^)]*\\)\\s*:\\s*[^=]+?=\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*;?\\s*$`);
    const luaFiles = await vscode.workspace.findFiles('**/*.lua');
    for (const file of luaFiles) {
        const document = await vscode.workspace.openTextDocument(file);
        for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
            const line = document.lineAt(lineNumber).text;
            const match = asmFunctionPattern.exec(line);
            asmFunctionPattern.lastIndex = 0;
            if (match) {
                return match[2];
            }
        }
    }
    return undefined;
}
function escapeRegExp(value) {
    return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
function lineText(document, line) {
    return document.lineAt(line).text;
}
function findDeclarationRange(document, name) {
    const escapedName = escapeRegExp(name);
    const searchPatterns = [
        new RegExp(`^\\s*asm\\s+function\\s+(${escapedName})\\b`),
        new RegExp(`^\\s*data\\s+(${escapedName})\\b`),
        new RegExp(`^\\s*(?:packed\\s+)?struct\\s+(${escapedName})\\b`),
        new RegExp(`^\\s*function\\s+(${escapedName})\\b`),
    ];
    for (const pattern of searchPatterns) {
        for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
            const line = lineText(document, lineNumber);
            const match = pattern.exec(line);
            pattern.lastIndex = 0;
            if (!match || match.index === undefined) {
                continue;
            }
            const startCharacter = line.indexOf(match[1], match.index);
            if (startCharacter >= 0) {
                return new vscode.Range(lineNumber, startCharacter, lineNumber, startCharacter + match[1].length);
            }
        }
    }
    for (let lineNumber = document.lineCount - 1; lineNumber >= 0; lineNumber--) {
        const line = lineText(document, lineNumber);
        const localPattern = new RegExp(`^\s*local\s+(${escapedName})\b`);
        const localMatch = localPattern.exec(line);
        if (localMatch && localMatch.index !== undefined) {
            const startCharacter = line.indexOf(localMatch[1], localMatch.index);
            if (startCharacter >= 0) {
                return new vscode.Range(lineNumber, startCharacter, lineNumber, startCharacter + localMatch[1].length);
            }
        }
    }
    return undefined;
}
async function findAsmLabelLocation(symbolName) {
    const labelPattern = new RegExp(`^\\s*${escapeRegExp(symbolName)}:\\s*$`);
    const assemblyFiles = [
        ...(await vscode.workspace.findFiles('**/*.S')),
        ...(await vscode.workspace.findFiles('**/*.s')),
        ...(await vscode.workspace.findFiles('**/*.asm')),
    ];
    for (const file of assemblyFiles) {
        const document = await vscode.workspace.openTextDocument(file);
        for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
            const line = lineText(document, lineNumber);
            if (!labelPattern.test(line)) {
                continue;
            }
            const startCharacter = line.indexOf(symbolName);
            if (startCharacter >= 0) {
                return new vscode.Location(file, new vscode.Range(lineNumber, startCharacter, lineNumber, startCharacter + symbolName.length));
            }
        }
    }
    return undefined;
}
async function refreshNavigationIndex() {
    asmFunctionToSymbol.clear();
    asmSymbolToLocation.clear();
    const luaFiles = await vscode.workspace.findFiles('**/*.lua');
    for (const file of luaFiles) {
        const document = await vscode.workspace.openTextDocument(file);
        for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
            const line = document.lineAt(lineNumber).text;
            const match = /^\s*asm\s+function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*:\s*[^=]+?=\s*([A-Za-z_][A-Za-z0-9_]*)\s*;?\s*$/.exec(line);
            if (match) {
                asmFunctionToSymbol.set(match[1], match[2]);
            }
        }
    }
    const assemblyFiles = [
        ...(await vscode.workspace.findFiles('**/*.S')),
        ...(await vscode.workspace.findFiles('**/*.s')),
        ...(await vscode.workspace.findFiles('**/*.asm')),
    ];
    for (const file of assemblyFiles) {
        const document = await vscode.workspace.openTextDocument(file);
        for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
            const line = document.lineAt(lineNumber).text;
            const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*$/.exec(line);
            if (!match || asmSymbolToLocation.has(match[1])) {
                continue;
            }
            const startCharacter = line.indexOf(match[1]);
            if (startCharacter >= 0) {
                asmSymbolToLocation.set(match[1], new vscode.Location(file, new vscode.Range(lineNumber, startCharacter, lineNumber, startCharacter + match[1].length)));
            }
        }
    }
}
function scheduleNavigationIndexRefresh() {
    if (navigationIndexRefreshTimer) {
        clearTimeout(navigationIndexRefreshTimer);
    }
    navigationIndexRefreshTimer = setTimeout(() => {
        void refreshNavigationIndex();
    }, 250);
}
function activate(context) {
    const serverModule = context.asAbsolutePath(path.join('out', 'server.js'));
    const serverOptions = {
        run: {
            module: serverModule,
            transport: node_1.TransportKind.ipc,
        },
        debug: {
            module: serverModule,
            transport: node_1.TransportKind.ipc,
            options: {
                execArgv: ['--nolazy', '--inspect=6009'],
            },
        },
    };
    const clientOptions = {
        documentSelector: [
            {
                scheme: 'file',
                language: 'lua',
            },
        ],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.lua'),
        },
    };
    client = new node_1.LanguageClient('lunarisLuaLanguageServer', 'Lunaris Lua Language Server', serverOptions, clientOptions);
    void client.start();
    context.subscriptions.push(client);
    context.subscriptions.push(typeDecoration);
    context.subscriptions.push(variableDecoration);
    const hoverProvider = vscode.languages.registerHoverProvider('lua', {
        provideHover(document, position) {
            if (document.languageId !== 'lua') {
                return undefined;
            }
            const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
            if (!range) {
                return undefined;
            }
            const word = document.getText(range);
            const documentation = hoverTextForWord(document, word, position);
            if (!documentation) {
                return undefined;
            }
            const markdown = new vscode.MarkdownString();
            markdown.appendMarkdown(`**${word}**\n\n${documentation}`);
            markdown.isTrusted = true;
            return new vscode.Hover(markdown, range);
        },
    });
    context.subscriptions.push(hoverProvider);
    const navigationProvider = {
        async provideDefinition(document, position) {
            const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
            if (!range) {
                return undefined;
            }
            const name = document.getText(range);
            const asmSymbol = asmFunctionToSymbol.get(name);
            if (asmSymbol) {
                const asmLocation = asmSymbolToLocation.get(asmSymbol) ?? (await findAsmLabelLocation(asmSymbol));
                if (asmLocation) {
                    return asmLocation;
                }
            }
            const asmReference = extractAsmFunctionReference(document.lineAt(position.line).text);
            if (asmReference && (name === asmReference.functionName || name === asmReference.symbolName)) {
                const asmLocation = await findAsmLabelLocation(asmReference.symbolName);
                if (asmLocation) {
                    return asmLocation;
                }
            }
            const declarationRange = findDeclarationRange(document, name);
            if (!declarationRange) {
                return undefined;
            }
            return new vscode.Location(document.uri, declarationRange);
        },
        async provideDeclaration(document, position) {
            const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
            if (!range) {
                return undefined;
            }
            const name = document.getText(range);
            const asmSymbol = asmFunctionToSymbol.get(name);
            if (asmSymbol) {
                const asmLocation = asmSymbolToLocation.get(asmSymbol) ?? (await findAsmLabelLocation(asmSymbol));
                if (asmLocation) {
                    return asmLocation;
                }
            }
            const asmReference = extractAsmFunctionReference(document.lineAt(position.line).text);
            if (asmReference && (name === asmReference.functionName || name === asmReference.symbolName)) {
                const asmLocation = await findAsmLabelLocation(asmReference.symbolName);
                if (asmLocation) {
                    return asmLocation;
                }
            }
            const declarationRange = findDeclarationRange(document, name);
            if (!declarationRange) {
                return undefined;
            }
            return new vscode.Location(document.uri, declarationRange);
        },
    };
    context.subscriptions.push(vscode.languages.registerDefinitionProvider('lua', navigationProvider));
    context.subscriptions.push(vscode.languages.registerDeclarationProvider('lua', navigationProvider));
    context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor((editor) => refreshDecorations(editor ?? undefined)));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((event) => {
        const editor = vscode.window.visibleTextEditors.find((visibleEditor) => visibleEditor.document.uri.toString() === event.document.uri.toString());
        if (editor) {
            scheduleDecorationRefresh(editor);
        }
        if (event.document.languageId === 'lua' || /\.(?:S|s|asm)$/i.test(event.document.fileName)) {
            scheduleNavigationIndexRefresh();
        }
    }));
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument((document) => {
        const editor = vscode.window.visibleTextEditors.find((visibleEditor) => visibleEditor.document.uri.toString() === document.uri.toString());
        if (editor) {
            scheduleDecorationRefresh(editor);
        }
    }));
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((document) => {
        if (document.languageId === 'lua' || /\.(?:S|s|asm)$/i.test(document.fileName)) {
            scheduleNavigationIndexRefresh();
        }
    }));
    void refreshNavigationIndex();
    refreshDecorations();
}
function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
//# sourceMappingURL=extension.js.map