#pragma once

#include <string>
#include <vector>

// The job the bridge queues for GET /game-reference. No tool has this name, so a model cannot call it.
#define MCP_JOB_GAME_REFERENCE "game-reference"

// One value read from the game database.
struct McpRefValue
{
	enum Kind { NUL, INT, REAL, TEXT };
	Kind kind;
	long long i;
	double d;
	std::string s;
	McpRefValue() : kind(NUL), i(0), d(0) {}
};

// A row, its columns in the order the query returned them.
typedef std::vector<std::pair<std::string, McpRefValue> > McpRefRow;

// Where the reference reads the rules: the game database and the game's text.
class McpRefSource
{
public:
	virtual ~McpRefSource() {}
	// false when the statement fails, as it does for a table this game does not have
	virtual bool Query(const std::string& sql, std::vector<McpRefRow>& rows) = 0;
	// false when the key has no text
	virtual bool Localize(const std::string& key, std::string& text) = 0;
};

// The rules of the game as loaded, for the given civilizations (CIVILIZATION_ types),
// written compactly for a model's system prompt.
std::string McpBuildGameReference(McpRefSource& source, const std::vector<std::string>& civs);

#ifndef MCP_REF_STANDALONE
// The reference for the game being played, covering every major civilization in it. Game thread only.
std::string McpGameReference();
#endif
