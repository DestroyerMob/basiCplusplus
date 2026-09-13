# BasicC for VS Code

Editor support for `.bc` files:

- Syntax highlighting for keywords and aliases, shared comparisons, strings,
  escapes, comments, numbers, functions, types, and checked casts.
- Four-space indentation, indentation-based folding, comment toggling, and
  bracket/quote completion. Press Enter after a block header to indent; use
  Shift+Tab when leaving a block.
- Snippets including `main`, `fn`, `var`, `ifelse`, `for`, `while`, `struct`,
  `unsafe`, `print`, `import`, `externcpp`, and `cast`. Select them from completion
  suggestions (Ctrl+Space) and use Tab to move between placeholders.

This is a declarative language extension. It provides editor syntax support;
compiler diagnostics, go-to-definition, and debugging are not included.

## Install locally

From the BasicC repository root, with Python 3 installed:

```sh
python3 editors/vscode/package.py
code --install-extension build/basicc-language-0.1.0.vsix --force
```

Alternatively, use **Extensions → … → Install from VSIX…** and select that file.
Run **Developer: Reload Window** after installation. Open a `.bc` file; the
language mode at the bottom right should say **BasicC**. If another extension
already claims `.bc`, select **BasicC** with **Change Language Mode**.

To update, edit these files, rerun the packaging/install commands, and reload.
The package needs no network access or npm dependencies. For a temporary preview,
launch `code --extensionDevelopmentPath="$PWD/editors/vscode" .` from the repo.

## Editing the extension

`package.json` registers the language and its defaults. `syntaxes/` contains the
TextMate grammar; `language-configuration.json` controls comments, indentation,
and pairs; `snippets/` contains completion templates. The grammar ends unterminated
strings at the line boundary because BasicC strings cannot span lines. Angle
brackets are not auto-paired because they also serve as comparison operators.

The format follows VS Code's [syntax highlighting](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)
and [language configuration](https://code.visualstudio.com/api/language-extensions/language-configuration-guide) guides.
