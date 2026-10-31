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

## Waiting for a turn in one call

`wait_for_my_turn` returns after 25 seconds at most (55 when asked), because MCP clients usually give up on a call after about a minute, so an agent calls it again and again while the others play. A client that can wait longer can say so with an `X-Max-Wait-Seconds` header: the wait then lasts up to that many seconds (at most 3600), returning as soon as it is the agent's turn, another leader makes a request or a teammate writes. For Claude Code:

```
claude mcp add --transport http civ5 http://127.0.0.1:7676/mcp -H "X-Max-Wait-Seconds: 3600"
```

and start Claude Code with its tool timeout above that, for example `MCP_TOOL_TIMEOUT=3900000` (milliseconds). Without the header nothing changes.

## Making a release

Run `python tools/release.py check`, then `build`, then `publish --notes FILE`. Publishing needs the GitHub CLI, `gh`, logged in. The steps around them are in `.claude/skills/release/SKILL.md`.
