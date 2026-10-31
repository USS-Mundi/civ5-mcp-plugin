# civ5-mcp-plugin
A DLL Bazaar Plugin for Civilization 5 that allows an LLM with MCP functionality to play the game with you as a teammate, against you as an opponent, or play your turns for you.

## What you need

- Civilization V with Brave New World, on Windows.
- Bazaar, the multi-DLL mod loader, with its two mods: Bazaar Multi-DLL Mod Loader and Bazaar - VMC Bootstrap (required). Each release of this plugin says which Bazaar version it requires.
- An MCP client on the same PC, such as Claude Code.

## Installing

1. Install Bazaar's two mods.
2. Download the `.civ5mod` file from this plugin's release on GitHub, and put it in `Documents\My Games\Sid Meier's Civilization 5\MODS`.
3. In the game's Mods menu, turn on Bazaar - VMC Bootstrap (required), Bazaar Multi-DLL Mod Loader and MCP Server for Bazaar.
4. Start or load a single-player game. While it runs, the server is at `http://127.0.0.1:7676/mcp`.
5. Connect your MCP client to that address. For Claude Code: `claude mcp add --transport http civ5 http://127.0.0.1:7676/mcp`

This plugin replaces the Local MCP Server DLL mod, so turn that one off.

## Giving the model the game's rules

While a game is loaded, `http://127.0.0.1:7676/game-reference` returns the rules of that game as plain text: technologies, buildings, wonders, units, policies, beliefs, promotions, improvements, resources, terrain and specialists, with their costs and effects, and the abilities and unique units of the civilizations in the game. It also gives the rules at this game's difficulty and map size: what happiness, food and gold below zero cost, and what the computer players get. It is read from the game's own database, so mods are included. For an 8-civilization Brave New World game it is about 42,000 tokens.

With it in the system prompt, the model plans from the game's numbers rather than from memory, which gets many of them wrong. Load your game first, then start Claude Code with it (PowerShell):

```
curl.exe -s http://127.0.0.1:7676/game-reference -o game-reference.txt
claude --append-system-prompt-file game-reference.txt
```

Fetch it again for each new game, since it depends on the civilizations in it.

## Making a release

Run `python tools/release.py check`, then `build`, then `publish --notes FILE`. Publishing needs the GitHub CLI, `gh`, logged in. The steps around them are in `.claude/skills/release/SKILL.md`.
