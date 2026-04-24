import {
  Diagnostic,
  DiagnosticSeverity,
  CompletionItem,
  CompletionItemKind,
  CompletionList,
  InitializeParams,
  InitializeResult,
  InsertTextFormat,
  Position,
  ProposedFeatures,
  SemanticTokensBuilder,
  TextDocuments,
  TextDocumentSyncKind,
  createConnection,
} from 'vscode-languageserver/node';
import { SemanticTokensLegend, SemanticTokensRequest } from 'vscode-languageserver-protocol';
import { TextDocument } from 'vscode-languageserver-textdocument';

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

const keywordDocs: Record<string, string> = {
  data: 'Declares top-level data that the compiler emits into the object file. Use `section "..."` when the data must live in a named ELF section.',
  section: 'Adds an explicit output section to a `data` declaration.',
  struct: 'Declares a structure type.',
  packed: 'Marks a structure as packed so the backend omits padding.',
  function: 'Declares a function.',
  extern: 'Marks a declaration as external.',
  asm: 'Used with `extern` for external assembly symbols.',
  local: 'Declares a local variable inside a function body.',
  return: 'Returns a value from the current function.',
  if: 'Starts a conditional block.',
  then: 'Completes the `if` header.',
  while: 'Starts a loop block.',
  do: 'Completes the `while` header.',
  end: 'Closes a block.',
  true: 'Boolean true literal.',
  false: 'Boolean false literal.',
  nil: 'Null-like literal used for absent values.',
  '~=': 'Lua inequality operator supported by the custom parser.',
};

function createKeywordItem(label: string, detail: string, documentation: string): CompletionItem {
  return {
    label,
    kind: CompletionItemKind.Keyword,
    detail,
    documentation,
    sortText: `1-${label}`,
  };
}

function createSnippetItem(label: string, detail: string, snippet: string, documentation: string): CompletionItem {
  return {
    label,
    kind: CompletionItemKind.Snippet,
    detail,
    documentation,
    insertText: snippet,
    insertTextFormat: InsertTextFormat.Snippet,
    sortText: `0-${label}`,
  };
}

type BlockKind = 'function' | 'if' | 'while' | 'struct';

interface BlockState {
  kind: BlockKind;
  line: number;
  character: number;
}

interface SemanticTokenEntry {
  line: number;
  character: number;
  length: number;
  tokenType: number;
  tokenModifiers: number;
}

const semanticTokenLegend: SemanticTokensLegend = {
  tokenTypes: ['keyword', 'variable', 'type'],
  tokenModifiers: [],
};

const semanticTokenType = {
  keyword: 0,
  variable: 1,
  type: 2,
} as const;

const keywordPatterns: Array<{ pattern: RegExp; tokenType: number }> = [
  { pattern: /\bdata\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bsection\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bpacked\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bstruct\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\basm\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bfunction\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bif\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bthen\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bwhile\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bdo\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\bend\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /\breturn\b/g, tokenType: semanticTokenType.keyword },
  { pattern: /~/g, tokenType: semanticTokenType.keyword },
];

const variablePatterns: Array<RegExp> = [
  /^\s*data\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
  /^\s*local\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
  /^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
  /^\s*asm\s+function\s+([A-Za-z_][A-Za-z0-9_]*)\b/g,
];

const diagnosticsTimers = new Map<string, NodeJS.Timeout>();

function findTypeAnnotationSpan(text: string, startIndex: number): { start: number; end: number } | undefined {
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

function lineText(document: TextDocument, line: number): string {
  return document.getText({
    start: Position.create(line, 0),
    end: Position.create(line + 1, 0),
  }).replace(/\r?\n$/, '');
}

const completionItems: CompletionItem[] = [
  createSnippetItem(
    'data',
    'Top-level data declaration',
    'data ${1:name}: ${2:type} = ${3:value}',
    'Creates a top-level data object that the compiler emits into the object file.',
  ),
  createSnippetItem(
    'data section',
    'Top-level data declaration with an explicit section',
    'data ${1:name}: ${2:type} section "${3:.limine_requests}" = ${4:value}',
    'Places the data object into a named ELF section.',
  ),
  createSnippetItem(
    'packed struct',
    'Packed structure declaration',
    'packed struct ${1:Name}\n    ${2:field}: ${3:type};\nend',
    'Declares a packed structure without padding between fields.',
  ),
  createSnippetItem(
    'struct',
    'Structure declaration',
    'struct ${1:Name}\n    ${2:field}: ${3:type};\nend',
    'Declares a structure type.',
  ),
  createSnippetItem(
    'function',
    'Function declaration',
    'function ${1:name}(${2:args})\n    ${3:return value}\nend',
    'Declares a function body.',
  ),
  createSnippetItem(
    'extern asm',
    'External assembly declaration',
    'asm function ${1:name}(${2:args}): ${3:type} = ${4:symbol};',
    'Declares a symbol that is implemented outside the Lua source.',
  ),
  createSnippetItem(
    'if',
    'Conditional block',
    'if ${1:condition} then\n    ${2}\nend',
    'Starts an if block.',
  ),
  createSnippetItem(
    'while',
    'Loop block',
    'while ${1:condition} do\n    ${2}\nend',
    'Starts a while loop.',
  ),
  createSnippetItem(
    'return',
    'Return statement',
    'return ${1:value}',
    'Returns from the current function.',
  ),
  createKeywordItem('section', 'Section keyword', keywordDocs.section),
  createKeywordItem('local', 'Local variable keyword', keywordDocs.local),
  createKeywordItem('extern', 'External declaration keyword', keywordDocs.extern),
  createKeywordItem('asm', 'Assembly declaration keyword', keywordDocs.asm),
  createKeywordItem('true', 'Boolean literal', keywordDocs.true),
  createKeywordItem('false', 'Boolean literal', keywordDocs.false),
  createKeywordItem('nil', 'Null-like literal', keywordDocs.nil),
  createKeywordItem('~=', 'Inequality operator', keywordDocs['~=']),
];

function currentPrefix(document: TextDocument, position: Position): string {
  const text = lineText(document, position.line).slice(0, position.character);
  const match = text.match(/[A-Za-z_][A-Za-z0-9_]*$/);
  return match ? match[0] : '';
}

function wordAt(document: TextDocument, position: Position): string | undefined {
  const text = lineText(document, position.line);
  const match = /[A-Za-z_][A-Za-z0-9_]*/g;
  for (let current = match.exec(text); current !== null; current = match.exec(text)) {
    const start = current.index;
    const end = start + current[0].length;
    if (position.character >= start && position.character <= end) {
      return current[0];
    }
  }
  return undefined;
}

function filteredCompletions(prefix: string): CompletionItem[] {
  if (!prefix) {
    return completionItems;
  }

  const lowerPrefix = prefix.toLowerCase();
  return completionItems.filter((item) => {
    return String(item.label).toLowerCase().startsWith(lowerPrefix);
  });
}

function semanticTokensForDocument(document: TextDocument) {
  const builder = new SemanticTokensBuilder();
  const lineTokens: SemanticTokenEntry[] = [];

  function pushMatch(lineNumber: number, lineText: string, pattern: RegExp, tokenType: number, captureGroup = 0): void {
    pattern.lastIndex = 0;
    for (let match = pattern.exec(lineText); match !== null; match = pattern.exec(lineText)) {
      const text = match[captureGroup] ?? match[0];
      const index = captureGroup === 0 ? match.index : match.index + match[0].indexOf(text);
      lineTokens.push({ line: lineNumber, character: index, length: text.length, tokenType, tokenModifiers: 0 });
    }
  }

  for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
    const rawLine = stripComment(lineText(document, lineNumber));
    lineTokens.length = 0;

    for (const { pattern, tokenType } of keywordPatterns) {
      pushMatch(lineNumber, rawLine, pattern, tokenType);
    }

    for (const pattern of variablePatterns) {
      pushMatch(lineNumber, rawLine, pattern, semanticTokenType.variable, 1);
    }

    for (let colonIndex = rawLine.indexOf(':'); colonIndex >= 0; colonIndex = rawLine.indexOf(':', colonIndex + 1)) {
      const span = findTypeAnnotationSpan(rawLine, colonIndex + 1);
      if (span) {
        lineTokens.push({
          line: lineNumber,
          character: span.start,
          length: span.end - span.start,
          tokenType: semanticTokenType.type,
          tokenModifiers: 0,
        });
      }
    }
    lineTokens.sort((left, right) => {
      if (left.character !== right.character) {
        return left.character - right.character;
      }
      if (left.length !== right.length) {
        return left.length - right.length;
      }
      return left.tokenType - right.tokenType;
    });

    let lastEndCharacter = -1;
    for (const token of lineTokens) {
      const tokenEnd = token.character + token.length;
      if (token.length <= 0 || token.character < lastEndCharacter) {
        continue;
      }
      builder.push(token.line, token.character, token.length, token.tokenType, token.tokenModifiers);
      lastEndCharacter = tokenEnd;
    }
  }

  return builder.build();
}

function diagnostic(range: { startLine: number; startCharacter: number; endLine?: number; endCharacter?: number }, message: string): Diagnostic {
  return {
    severity: DiagnosticSeverity.Error,
    range: {
      start: Position.create(range.startLine, range.startCharacter),
      end: Position.create(range.endLine ?? range.startLine, range.endCharacter ?? range.startCharacter + 1),
    },
    message,
    source: 'lunaris-lua',
  };
}

function stripComment(line: string): string {
  const commentIndex = line.indexOf('--');
  if (commentIndex < 0) {
    return line;
  }
  return line.slice(0, commentIndex);
}

function validateDataLine(line: string, lineNumber: number, diagnostics: Diagnostic[]): boolean {
  const dataPattern = /^data\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*.+?(?:\s+section\s+"[^"]+")?\s*=\s*.+$/;
  if (dataPattern.test(line)) {
    return true;
  }

  diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed data declaration'));
  return false;
}

function validateStructHeader(line: string, lineNumber: number, diagnostics: Diagnostic[]): boolean {
  const structPattern = /^(?:packed\s+)?struct\s+[A-Za-z_][A-Za-z0-9_]*$/;
  if (structPattern.test(line)) {
    return true;
  }

  diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed struct declaration'));
  return false;
}

function findMatchingParen(text: string, openIndex: number): number {
  let depth = 0;
  for (let index = openIndex; index < text.length; index++) {
    const ch = text[index];
    if (ch === '(') {
      depth++;
    } else if (ch === ')') {
      depth--;
      if (depth === 0) {
        return index;
      }
    }
  }
  return -1;
}

function validateFunctionHeader(line: string, lineNumber: number, diagnostics: Diagnostic[]): boolean {
  const match = /^function\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/.exec(line);
  if (!match) {
    diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed function declaration'));
    return false;
  }

  const openParenIndex = line.indexOf('(', match.index ?? 0);
  const closeParenIndex = findMatchingParen(line, openParenIndex);
  if (openParenIndex < 0 || closeParenIndex < 0) {
    diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed function declaration'));
    return false;
  }

  const trailing = line.slice(closeParenIndex + 1).trim();
  if (!trailing || trailing.startsWith(':')) {
    return true;
  }

  diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed function declaration'));
  return false;
}

function validateAsmFunction(line: string, lineNumber: number, diagnostics: Diagnostic[]): boolean {
  const match = /^asm\s+function\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/.exec(line);
  if (!match) {
    diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed external asm function declaration'));
    return false;
  }

  const openParenIndex = line.indexOf('(', match.index ?? 0);
  const closeParenIndex = findMatchingParen(line, openParenIndex);
  if (openParenIndex < 0 || closeParenIndex < 0) {
    diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed external asm function declaration'));
    return false;
  }

  const header = line.slice(closeParenIndex + 1).trim();
  const equalsIndex = header.indexOf('=');
  if (equalsIndex > 0) {
    const typePart = header.slice(0, equalsIndex).trim();
    const symbolPart = header.slice(equalsIndex + 1).trim().replace(/;$/, '');
    if (typePart.startsWith(':') && /^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?$/.test(symbolPart)) {
      return true;
    }
  }

  if (/^:\s*.+\s*=\s*[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?;?$/.test(header)) {
    return true;
  }

  diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed external asm function declaration'));
  return false;
}

function validateTextDocument(document: TextDocument): void {
  const diagnostics: Diagnostic[] = [];
  const blocks: BlockState[] = [];

  for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber++) {
    const rawLine = lineText(document, lineNumber);
    const line = stripComment(rawLine).trim();

    if (!line) {
      continue;
    }

    if (line === '}') {
      const top = blocks.pop();
      if (!top || top.kind !== 'struct') {
        diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, "unexpected '}'"));
      }
      continue;
    }

    if (line === 'end') {
      const top = blocks.pop();
      if (!top) {
        diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, "unexpected 'end'"));
      }
      continue;
    }

    if (line.startsWith('data ')) {
      validateDataLine(line, lineNumber, diagnostics);
      continue;
    }

    if (line.startsWith('packed struct ') || line.startsWith('struct ')) {
      if (validateStructHeader(line, lineNumber, diagnostics)) {
        blocks.push({ kind: 'struct', line: lineNumber, character: 0 });
      }
      continue;
    }

    if (line.startsWith('asm function ')) {
      validateAsmFunction(line, lineNumber, diagnostics);
      continue;
    }

    if (line.startsWith('function ')) {
      if (validateFunctionHeader(line, lineNumber, diagnostics)) {
        blocks.push({ kind: 'function', line: lineNumber, character: 0 });
      }
      continue;
    }

    if (line.startsWith('if ')) {
      if (!/\bthen$/.test(line)) {
        diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, "expected 'then' at end of if statement"));
      } else {
        blocks.push({ kind: 'if', line: lineNumber, character: 0 });
      }
      continue;
    }

    if (line.startsWith('while ')) {
      if (!/\bdo$/.test(line)) {
        diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, "expected 'do' at end of while statement"));
      } else {
        blocks.push({ kind: 'while', line: lineNumber, character: 0 });
      }
      continue;
    }

    if (line.startsWith('local ')) {
      const localPattern = /^local\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*:\s*.+)?(?:\s*=\s*.+)?$/;
      if (!localPattern.test(line)) {
        diagnostics.push(diagnostic({ startLine: lineNumber, startCharacter: 0 }, 'malformed local declaration'));
      }
      continue;
    }

    if (line.startsWith('return')) {
      continue;
    }
  }

  while (blocks.length > 0) {
    const block = blocks.pop()!;
    diagnostics.push(diagnostic(
      { startLine: block.line, startCharacter: block.character },
      "block opened here is missing 'end'",
    ));
  }

  connection.sendDiagnostics({ uri: document.uri, diagnostics });
}

function scheduleValidation(document: TextDocument): void {
  const uri = document.uri;
  const existingTimer = diagnosticsTimers.get(uri);
  if (existingTimer) {
    clearTimeout(existingTimer);
  }

  const timer = setTimeout(() => {
    diagnosticsTimers.delete(uri);
    const currentDocument = documents.get(uri);
    if (currentDocument) {
      validateTextDocument(currentDocument);
    }
  }, 120);

  diagnosticsTimers.set(uri, timer);
}

connection.onInitialize((_params: InitializeParams): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: {
        resolveProvider: false,
        triggerCharacters: [' ', '.', ':', '(', '"'],
      },
      semanticTokensProvider: {
        legend: semanticTokenLegend,
        full: true,
        range: false,
      },
    },
  };
});

connection.onRequest(SemanticTokensRequest.type, (params) => {
  const document = documents.get(params.textDocument.uri);
  if (!document) {
    return { data: [] };
  }

  return semanticTokensForDocument(document);
});

connection.onCompletion((params): CompletionList => {
  const document = documents.get(params.textDocument.uri);
  if (!document) {
    return CompletionList.create([]);
  }

  return CompletionList.create(filteredCompletions(currentPrefix(document, params.position)), false);
});

documents.listen(connection);
documents.onDidOpen((event) => scheduleValidation(event.document));
documents.onDidChangeContent((event) => scheduleValidation(event.document));
documents.onDidSave((event) => scheduleValidation(event.document));
documents.onDidClose((event) => {
  const existingTimer = diagnosticsTimers.get(event.document.uri);
  if (existingTimer) {
    clearTimeout(existingTimer);
    diagnosticsTimers.delete(event.document.uri);
  }
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});
connection.listen();