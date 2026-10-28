#ifndef MCP_REF_STANDALONE
#include "CvGameCoreDLLPCH.h"
#endif
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include "McpGameReference.h"
#ifndef MCP_REF_STANDALONE
#include "CvGlobals.h"
#include "CvPlayer.h"
#include "McpToolSupport.h"
#endif

// The reference is read from the database alone, so the base game and any mod come out
// the same way: every table about an object is read, fields still at their default are
// left out, and the rest are written in the short codes the legend explains.

#define MCP_REF_COUNT(a) (sizeof(a) / sizeof((a)[0]))

namespace
{
	typedef std::vector<McpRefRow> Rows;
	typedef std::vector<std::string> Strings;
	typedef std::map<std::string, std::string> StringMap;
	typedef std::set<std::string> StringSet;

	const char* const ICONS[][2] = {
		{"FOOD", "food"}, {"PRODUCTION", "production"}, {"GOLD", "gold"}, {"RESEARCH", "science"}, {"CULTURE", "culture"},
		{"PEACE", "faith"}, {"HAPPINESS_1", "happiness"}, {"HAPPINESS_3", "unhappiness"}, {"HAPPINESS_4", "unhappiness"},
		{"STRENGTH", "combat strength"}, {"RANGE_STRENGTH", "ranged strength"}, {"MOVES", "moves"},
		{"GREAT_PEOPLE", "great person points"}, {"TOURISM", "tourism"}, {"GOLDEN_AGE", "golden age points"},
		{"INFLUENCE", "influence"}, {"CITIZEN", "citizen"}};

	// Yield words in the game's text, in the order they are tried, and their codes.
	const char* const PROSE[][2] = {
		{"food", "f"}, {"production", "p"}, {"gold", "g"}, {"science", "s"}, {"culture", "c"}, {"faith", "fa"},
		{"tourism", "t"}, {"golden age points", "ga"}};

	const char* const YIELD_CODES[][2] = {
		{"YIELD_FOOD", "f"}, {"YIELD_PRODUCTION", "p"}, {"YIELD_GOLD", "g"}, {"YIELD_SCIENCE", "s"}, {"YIELD_CULTURE", "c"},
		{"YIELD_FAITH", "fa"}, {"YIELD_GOLDEN_AGE_POINTS", "ga"}, {"YIELD_TOURISM", "t"}};

	// The column that holds the amount in a row with a YieldType.
	const char* const YIELD_AMOUNTS[] = {"Yield", "YieldTimes100", "YieldChange", "YieldModifier", "YieldMod", "Modifier"};

	// Columns written in short form; the legend lists the ones a reference uses.
	const char* const SHORT[][2] = {
		{"Cost", "c"}, {"FaithCost", "fc"}, {"Yield", ""}, {"YieldTimes100", ""}, {"GoldMaintenance", "up"}, {"Combat", "str"},
		{"RangedCombat", "rs"}, {"Range", "rng"}, {"Moves", "mv"}, {"ObsoleteTech", "obs"}, {"CombatClass", "cls"},
		{"Domain", "dom"}, {"Happiness", "H"}, {"UnmoddedHappiness", "H*"}, {"Defense", "def"}, {"ExtraCityHitPoints", "hp"},
		{"GreatPeopleRateChange", "gpp"}, {"SpecialistType", "spec"}, {"SpecialistCount", "specs"}, {"Era", "era"},
		{"TechReveal", "revealed"}, {"ResourceClassType", "kind"}, {"GreatWorkCount", "gw"}, {"GreatWorkSlotType", "gwtype"},
		{"FreePolicies", "freepol"}};

	// Tables whose names name things elsewhere.
	const char* const NAME_TABLES[] = {
		"Technologies", "Buildings", "BuildingClasses", "Units", "UnitClasses", "Resources", "Terrains", "Features", "Policies",
		"PolicyBranchTypes", "Eras", "Specialists", "Yields", "UnitCombatInfos", "Domains", "Improvements", "Builds",
		"UnitPromotions", "GreatWorkSlots", "Beliefs", "Projects", "Processes", "Civilizations", "Leaders"};

	// Columns about art, sound, the AI, the interface and map generation, which say nothing about play.
	const char* const SKIP_SUB[] = {
		"Art", "Icon", "Audio", "Sound", "Movie", "Portrait", "Splash", "Flavor", "Quote", "Civilopedia", "Strategy", "Pedia",
		"Tooltip", "Grid", "Advisor", "Ambience", "Color", "Image", "Atlas", "Button", "Latitude", "RandApp", "Appearance",
		"AdvancedStart", "Goody", "ConquestProb", "MinAreaSize", "EffectType", "TilesPer", "StartingLocation", "Probability", "HotKey",
		"Priority", "EntityEvent"};
	const char* const SKIP_EXACT[] = {
		"Title", "ID", "Type", "Description", "Help", "ShortDescription", "Adjective", "Class", "BuildingClass", "DisplayPosition",
		"XPValueAttack", "XPValueDefense", "DefaultUnitAI", "MoveRate", "FreeStartEra", "Player",
		"AmountMinor", "AmountMajor", "Area", "Hills", "Flatlands", "NoRiverSide", "OnlyMinorCivs", "Unique", "GroupSize",
		"GroupRange", "CombatLimit", "RangedCombatLimit", "MilitarySupport", "MilitaryProduction", "Pillage", "Conscription",
		"RequiresFaithPurchaseEnabled", "Sellable", "UnlockedByBelief", "ResourceUsage", "SeeFrom", "SeeThrough",
		"InfluenceCost", "ShowInPedia", "PrereqTech", "Mechanized", "IconString", "TechCityTrade",
		"CivilianAttackPriority", "MaxStartEra", "PlacementOrder", "GroupRand", "Normalize", "WorkerSpeed",
		"ExtraMaintenanceCost", "OccurrenceFrequency", "MinLandPercent", "Visible", "GreatPeopleTitle", "DontShowYields",
		"ThemingBonusHelp", "GraphicalOnly", "NoRegional", "Growth", "Recommendation", "CtrlDown", "AltDown", "ShiftDown",
		"CtrlDownAlt", "AltDownAlt", "ShiftDownAlt"};
	const char* const CHILD_SKIP[] = {"Flavor", "AI", "Art", "Audio", "UniqueNames", "Sound", "Booleans", "Gameplay2DScripts"};
	const char* const BELIEF_KINDS[] = {"Pantheon", "Founder", "Follower", "Enhancer", "Reformation"};

	const std::string NONE_KEY = "\x01None";

	bool StartsWith(const std::string& s, const char* p)
	{
		size_t n = strlen(p);
		return s.size() >= n && s.compare(0, n, p) == 0;
	}

	bool Contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

	std::string Lower(std::string s)
	{
		for(size_t i = 0; i < s.size(); ++i) s[i] = (char)tolower((unsigned char)s[i]);
		return s;
	}

	std::string Join(const Strings& v, const char* sep)
	{
		std::string s;
		for(size_t i = 0; i < v.size(); ++i)
		{
			if(i) s += sep;
			s += v[i];
		}
		return s;
	}

	std::string Replace(std::string s, const std::string& from, const std::string& to)
	{
		for(size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
			s.replace(pos, from.size(), to);
		return s;
	}

	std::string Dec(long long n)
	{
		char buf[32];
		char* p = buf + sizeof(buf) - 1;
		*p = 0;
		unsigned long long u = n < 0 ? 0ULL - (unsigned long long)n : (unsigned long long)n;
		do { *--p = (char)('0' + u % 10); u /= 10; } while(u);
		if(n < 0) *--p = '-';
		return p;
	}

	// Bytes of the whitespace character at s[i], as Python counts whitespace; 0 when there is none.
	size_t SpaceAt(const std::string& s, size_t i)
	{
		if(i >= s.size()) return 0;
		unsigned char c = (unsigned char)s[i];
		if(c == ' ' || (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x1f)) return 1;
		unsigned char d = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
		unsigned char e = i + 2 < s.size() ? (unsigned char)s[i + 2] : 0;
		if(c == 0xc2) return (d == 0x85 || d == 0xa0) ? 2 : 0;
		if(c == 0xe1) return (d == 0x9a && e == 0x80) ? 3 : 0;
		if(c == 0xe2) return ((d == 0x80 && ((e >= 0x80 && e <= 0x8a) || e == 0xa8 || e == 0xa9 || e == 0xaf)) || (d == 0x81 && e == 0x9f)) ? 3 : 0;
		if(c == 0xe3) return (d == 0x80 && e == 0x80) ? 3 : 0;
		return 0;
	}

	// Whether the character at s[i] counts as part of a word, as Python's \b sees it.
	bool WordCharAt(const std::string& s, size_t i)
	{
		unsigned char c = (unsigned char)s[i];
		if(c < 0x80) return isalnum(c) || c == '_';
		unsigned char d = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
		if(c == 0xc2) return d == 0xaa || d == 0xb2 || d == 0xb3 || d == 0xb5 || d == 0xb9 || d == 0xba || (d >= 0xbc && d <= 0xbe);
		if(c == 0xc3) return d != 0x97 && d != 0xb7;
		if(c == 0xe2 && (d >= 0x80 && d <= 0x81)) return false;
		if(c == 0xe3 && d == 0x80) return false;
		return true;
	}

	std::string Strip(const std::string& s)
	{
		size_t b = 0, e = s.size();
		while(b < e)
		{
			size_t n = SpaceAt(s, b);
			if(!n) break;
			b += n;
		}
		while(e > b)
		{
			if(SpaceAt(s, e - 1) == 1) e -= 1;
			else if(e - b >= 2 && SpaceAt(s, e - 2) == 2) e -= 2;
			else if(e - b >= 3 && SpaceAt(s, e - 3) == 3) e -= 3;
			else break;
		}
		return s.substr(b, e - b);
	}

	std::string StripChars(const std::string& s, const char* chars, bool left, bool right)
	{
		size_t b = 0, e = s.size();
		while(left && b < e && strchr(chars, s[b])) ++b;
		while(right && e > b && strchr(chars, s[e - 1])) --e;
		return s.substr(b, e - b);
	}

	// The shortest digits that read back as d, and the power of ten of the first one.
	void ShortestDigits(double d, std::string& digits, int& exp10)
	{
		char buf[64];
		for(int prec = 1; prec <= 17; ++prec)
		{
			sprintf(buf, "%.*e", prec - 1, d);
			if(strtod(buf, NULL) == d) break;
		}
		const char* p = buf;
		if(*p == '-') ++p;
		digits.clear();
		for(; *p && *p != 'e' && *p != 'E'; ++p)
			if(*p != '.') digits += *p;
		exp10 = *p ? atoi(p + 1) : 0;
	}

	std::string Exponent(int e)
	{
		char buf[16];
		sprintf(buf, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
		return buf;
	}

	// Python's str() of a float.
	std::string PyRepr(double d)
	{
		if(d != d) return "nan";
		if(d > DBL_MAX) return "inf";
		if(d < -DBL_MAX) return "-inf";
		if(d == 0) return "0.0";
		std::string digits;
		int e;
		ShortestDigits(d, digits, e);
		std::string sign = d < 0 ? "-" : "";
		if(e >= 16 || e < -4)
			return sign + digits.substr(0, 1) + (digits.size() > 1 ? "." + digits.substr(1) : "") + Exponent(e);
		if(e < 0) return sign + "0." + std::string(-e - 1, '0') + digits;
		std::string whole = digits.substr(0, std::min(digits.size(), (size_t)e + 1));
		whole.append((size_t)e + 1 - whole.size(), '0');
		return sign + whole + "." + (digits.size() > (size_t)e + 1 ? digits.substr(e + 1) : "0");
	}

	std::string TrimZeros(std::string m)
	{
		if(m.find('.') == std::string::npos) return m;
		m = StripChars(m, "0", false, true);
		return StripChars(m, ".", false, true);
	}

	// Python's "%g" % d.
	std::string PyG(double d)
	{
		if(d == 0) return "0";
		if(d != d || d > DBL_MAX || d < -DBL_MAX) return PyRepr(d);
		char buf[512];
		sprintf(buf, "%.5e", d);
		const char* pe = strchr(buf, 'e');
		int e = atoi(pe + 1);
		if(e < -4 || e >= 6) return TrimZeros(std::string(buf, pe - buf)) + Exponent(e);
		sprintf(buf, "%.*f", 5 - e, d);
		return TrimZeros(buf);
	}

	bool IsNumber(const McpRefValue& v) { return v.kind == McpRefValue::INT || v.kind == McpRefValue::REAL; }

	double Number(const McpRefValue* v)
	{
		if(!v) return 0;
		if(v->kind == McpRefValue::INT) return (double)v->i;
		if(v->kind == McpRefValue::REAL) return v->d;
		return 0;
	}

	bool Truthy(const McpRefValue* v)
	{
		if(!v) return false;
		switch(v->kind)
		{
		case McpRefValue::INT: return v->i != 0;
		case McpRefValue::REAL: return v->d != 0;
		case McpRefValue::TEXT: return !v->s.empty();
		default: return false;
		}
	}

	// Python's str() of a value.
	std::string PyStr(const McpRefValue& v)
	{
		switch(v.kind)
		{
		case McpRefValue::INT: return Dec(v.i);
		case McpRefValue::REAL: return PyRepr(v.d);
		case McpRefValue::TEXT: return v.s;
		default: return "None";
		}
	}

	// A value as a map key; null and absent values share one key that no text can be.
	std::string Key(const McpRefValue* v)
	{
		if(!v || v->kind == McpRefValue::NUL) return NONE_KEY;
		return v->kind == McpRefValue::TEXT ? v->s : "\x02" + PyStr(*v);
	}

	const McpRefValue* Col(const McpRefRow& r, const char* name)
	{
		for(size_t i = 0; i < r.size(); ++i)
			if(r[i].first == name) return &r[i].second;
		return NULL;
	}

	std::string ColText(const McpRefRow& r, const char* name)
	{
		const McpRefValue* v = Col(r, name);
		return (v && v->kind != McpRefValue::NUL) ? PyStr(*v) : "";
	}

	std::string Quote(const std::string& s) { return "'" + Replace(s, "'", "''") + "'"; }

	// Text up to its first digit or colon: what a field is, without its amount.
	std::string Stem(const std::string& p) { return p.substr(0, p.find_first_of("0123456789:")); }

	// Python's re.sub(r"YieldChanges|Changes", "", s).
	std::string RemoveChanges(const std::string& s)
	{
		std::string out;
		for(size_t i = 0; i < s.size();)
		{
			if(s.compare(i, 12, "YieldChanges") == 0) i += 12;
			else if(s.compare(i, 7, "Changes") == 0) i += 7;
			else out += s[i++];
		}
		return out;
	}

	// A yield alone, as in 2g or 1.5f.
	bool IsBareYield(const std::string& s)
	{
		size_t i = 0;
		while(i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;
		if(i == 0) return false;
		size_t j = i;
		while(j < s.size() && s[j] >= 'a' && s[j] <= 'z') ++j;
		return j > i && j == s.size();
	}

	// A column name as words: GoldMaintenance -> gold maintenance.
	std::string Words(const std::string& c)
	{
		std::string out;
		for(size_t i = 0; i < c.size(); ++i)
		{
			if(i && c[i] >= 'A' && c[i] <= 'Z') out += ' ';
			out += c[i];
		}
		return Lower(out);
	}

	bool InList(const std::string& s, const char* const* list, size_t n)
	{
		for(size_t i = 0; i < n; ++i)
			if(s == list[i]) return true;
		return false;
	}

	bool SkipCol(const std::string& c)
	{
		if(InList(c, SKIP_EXACT, MCP_REF_COUNT(SKIP_EXACT)) || StartsWith(c, "AI")) return true;
		for(size_t i = 0; i < MCP_REF_COUNT(SKIP_SUB); ++i)
			if(Contains(c, SKIP_SUB[i])) return true;
		return false;
	}

	// Whether one of the next three words of s, from i, starts like a word of word does.
	bool NamedAhead(const std::string& s, size_t i, const std::string& word)
	{
		Strings stems;
		for(size_t b = 0, e; b < word.size(); b = e + 1)
		{
			e = word.find(' ', b);
			if(e == std::string::npos) e = word.size();
			stems.push_back(word.substr(b, std::min(e - b, (size_t)4)));
		}
		for(int w = 0; w < 3 && i < s.size(); ++w)
		{
			size_t end = s.find_first_of(" ;,.", i);
			if(end == std::string::npos) end = s.size();
			for(size_t k = 0; k < stems.size(); ++k)
				if(!stems[k].empty() && Lower(s.substr(i, stems[k].size())) == stems[k]) return true;
			i = s.find_first_not_of(" ;,.", end);
			if(i == std::string::npos) break;
		}
		return false;
	}

	// The game's text as plain words, with yields in short codes: icons named, markup and
	// line breaks dropped, "+2 [ICON_GOLD] Gold" written 2g.
	std::string CleanText(const std::string& raw)
	{
		std::string t;
		size_t from = 0, pos = 0;
		while((pos = raw.find("[ICON_", pos)) != std::string::npos)
		{
			size_t j = pos + 6;
			while(j < raw.size() && (isupper((unsigned char)raw[j]) || isdigit((unsigned char)raw[j]) || raw[j] == '_')) ++j;
			if(j == pos + 6 || j >= raw.size() || raw[j] != ']')
			{
				++pos;
				continue;
			}
			std::string icon = raw.substr(pos + 6, j - pos - 6);
			size_t end = j + 1;
			for(size_t n; (n = SpaceAt(raw, end)) != 0;) end += n;
			t += raw.substr(from, pos - from);
			// A yield or resource icon becomes its word, unless the text names it within the next
			// few words; any other icon (a building's, a unit's) is dropped, since the text names it.
			std::string word;
			for(size_t i = 0; i < MCP_REF_COUNT(ICONS); ++i)
				if(icon == ICONS[i][0]) word = ICONS[i][1];
			if(word.empty() && icon.compare(0, 4, "RES_") == 0) word = Replace(Lower(icon.substr(4)), "_", " ");
			if(!word.empty() && !NamedAhead(raw, end, word)) t += word + " ";
			from = pos = end;
		}
		t += raw.substr(from);

		t = Replace(t, "[NEWLINE]", "; ");
		std::string u;
		for(size_t i = 0; i < t.size(); ++i)
		{
			size_t close = t[i] == '[' ? t.find(']', i + 1) : std::string::npos;
			if(close != std::string::npos) i = close;
			else u += t[i];
		}
		t.clear();
		for(size_t i = 0; i < u.size();)
		{
			size_t n = SpaceAt(u, i);
			if(!n)
			{
				t += u[i++];
				continue;
			}
			while((n = SpaceAt(u, i)) != 0) i += n;
			t += ' ';
		}

		// [+-]?\d+%?\s+(word)\b(\s+\2\b)?, case-insensitively, becomes the number and the word's code
		u.clear();
		for(size_t i = 0; i < t.size();)
		{
			size_t j = i;
			if(t[j] == '+' || t[j] == '-') ++j;
			size_t digits = j;
			while(j < t.size() && isdigit((unsigned char)t[j])) ++j;
			bool matched = false;
			if(j > digits)
			{
				if(j < t.size() && t[j] == '%') ++j;
				size_t numberEnd = j;
				for(size_t n; (n = SpaceAt(t, j)) != 0;) j += n;
				for(size_t w = 0; j > numberEnd && w < MCP_REF_COUNT(PROSE) && !matched; ++w)
				{
					size_t len = strlen(PROSE[w][0]);
					if(Lower(t.substr(j, len)) != PROSE[w][0] || (j + len < t.size() && WordCharAt(t, j + len))) continue;
					matched = true;
					size_t end = j + len, k = end;
					for(size_t n; (n = SpaceAt(t, k)) != 0;) k += n;
					if(k > end && Lower(t.substr(k, len)) == PROSE[w][0] && !(k + len < t.size() && WordCharAt(t, k + len)))
						end = k + len;
					u += t.substr(i, numberEnd - i) + PROSE[w][1];
					i = end;
				}
			}
			if(!matched) u += t[i++];
		}
		return StripChars(u, " ;", true, true);
	}

	class Builder
	{
	public:
		Builder(McpRefSource& source, const Strings& civs)
			: m_src(source), m_civs(civs), m_civSet(civs.begin(), civs.end()), m_lastEra(NONE_KEY) {}

		std::string Build();

	private:
		enum Kind { PLAIN, TECHS, BUILDINGS, UNITS, BUILDS, TERRAIN, FEATURES, NATURAL_WONDERS };
		typedef std::map<std::string, StringSet> Owners;
		struct Defaults
		{
			StringMap values;
			StringSet booleans;
		};

		McpRefSource& m_src;
		Strings m_civs;
		StringSet m_civSet;
		StringMap m_names;
		std::vector<std::pair<std::string, std::string> > m_used; // short code, column, in order of first use
		std::map<std::string, Defaults> m_defaults;
		Strings m_tables;
		Strings m_out;
		Owners m_uniqueBuildings, m_uniqueUnits;
		std::map<std::string, Strings> m_unlocks, m_needs;
		StringMap m_eraOf;
		std::string m_lastEra;
		StringMap m_wonderClasses; // class, what kind of wonder
		StringSet m_classDefaults, m_absent, m_referenced, m_gameTraits;

		Rows Select(const std::string& sql)
		{
			Rows rows;
			if(!m_src.Query(sql, rows)) rows.clear();
			return rows;
		}

		bool Lookup(const std::string& type, std::string& name) const
		{
			StringMap::const_iterator it = m_names.find(type);
			if(it == m_names.end()) return false;
			name = it->second;
			return true;
		}

		std::string Name(const std::string& type) const
		{
			std::string name;
			return Lookup(type, name) ? name : type;
		}

		std::string Text(const std::string& key)
		{
			if(key.empty()) return "";
			std::string t;
			if(!m_src.Localize(key, t)) t = StartsWith(key, "TXT_KEY_") ? "" : key;
			return t.empty() ? "" : CleanText(t);
		}

		std::string Text(const McpRefValue* v)
		{
			return (Truthy(v) && v->kind == McpRefValue::TEXT) ? Text(v->s) : "";
		}

		std::string StripName(std::string t, const std::string& name)
		{
			t = Strip(t);
			if(!name.empty() && t.size() >= name.size() && Lower(t.substr(0, name.size())) == Lower(name)
				&& (t.size() == name.size() || !isalnum((unsigned char)t[name.size()])))
				t = StripChars(t.substr(name.size()), " :.-;", true, false);
			return t;
		}

		std::string Abbr(const std::string& c)
		{
			for(size_t i = 0; i < MCP_REF_COUNT(SHORT); ++i)
			{
				if(c != SHORT[i][0]) continue;
				bool bSeen = false;
				for(size_t j = 0; j < m_used.size() && !bSeen; ++j) bSeen = m_used[j].first == SHORT[i][1];
				if(*SHORT[i][1] && !bSeen) m_used.push_back(std::make_pair(std::string(SHORT[i][1]), c));
				return SHORT[i][1];
			}
			return c;
		}

		const Defaults& DefaultsOf(const std::string& table)
		{
			std::map<std::string, Defaults>::iterator it = m_defaults.find(table);
			if(it != m_defaults.end()) return it->second;
			Defaults& d = m_defaults[table];
			Rows cols = Select("pragma table_info(" + table + ")");
			for(size_t i = 0; i < cols.size(); ++i)
			{
				std::string name = ColText(cols[i], "name");
				const McpRefValue* dflt = Col(cols[i], "dflt_value");
				if(dflt && dflt->kind != McpRefValue::NUL) d.values[name] = StripChars(StripChars(PyStr(*dflt), "'", true, true), "\"", true, true);
				if(Lower(ColText(cols[i], "type")) == "boolean") d.booleans.insert(name);
			}
			return d;
		}

		// A value as the reference writes it; false when it says nothing (null, zero, -1, NONE).
		bool Val(const McpRefValue& v, std::string& s)
		{
			switch(v.kind)
			{
			case McpRefValue::INT:
				if(v.i == 0 || v.i == -1) return false;
				s = Dec(v.i);
				return true;
			case McpRefValue::REAL:
				if(v.d == 0 || v.d == -1) return false;
				s = PyG(v.d);
				return true;
			case McpRefValue::TEXT:
				if(v.s.empty() || v.s == "NONE" || v.s == "NULL" || v.s == "false") return false;
				if(StartsWith(v.s, "TXT_KEY_"))
				{
					std::string t;
					if(!m_src.Localize(v.s, t)) return false;
					s = t.empty() ? "" : CleanText(t);
				}
				else if(StartsWith(v.s, "TRADE_CONNECTION_")) s = Lower(v.s.substr(17));
				else if(StartsWith(v.s, "RESOURCECLASS_"))
				{
					std::string k = v.s.substr(14);
					s = (k == "RUSH" || k == "MODERN") ? "strategic" : k == "LUXURY" ? "luxury" : k == "BONUS" ? "bonus" : Lower(k);
				}
				else s = Name(v.s);
				return true;
			default:
				return false;
			}
		}

		std::string YieldCode(const std::string& type) const
		{
			for(size_t y = 0; y < MCP_REF_COUNT(YIELD_CODES); ++y)
				if(type == YIELD_CODES[y][0]) return YIELD_CODES[y][1];
			return Name(type);
		}

		// A row's fields that differ from their defaults: short code and amount (c120),
		// short code and name (cls:Melee), a flag's name, a yield as 2g. compact writes the
		// values alone, for rows of a related table, and names the numbers when there are
		// several. A row whose yield comes to nothing comes back empty.
		Strings Cols(const McpRefRow& r, const StringSet& skip, const std::string& table, bool compact)
		{
			static const Defaults none;
			const Defaults& d = table.empty() ? none : DefaultsOf(table);
			const McpRefValue* yieldType = Col(r, "YieldType");
			const char* amountCol = NULL;
			for(size_t a = 0; yieldType && a < MCP_REF_COUNT(YIELD_AMOUNTS) && !amountCol; ++a)
				if(Col(r, YIELD_AMOUNTS[a])) amountCol = YIELD_AMOUNTS[a];
			Strings out;
			std::vector<size_t> numberAt;
			std::vector<std::string> numberCol;
			std::string amount;
			for(size_t i = 0; i < r.size(); ++i)
			{
				const std::string& c = r[i].first;
				const McpRefValue& v = r[i].second;
				if(SkipCol(c) || skip.count(c) || c == "YieldType") continue;
				if(amountCol && c == amountCol)
				{
					if(IsNumber(v) && Number(&v) != 0) amount = PyG(c == "YieldTimes100" ? Number(&v) / 100.0 : Number(&v));
					continue;
				}
				StringMap::const_iterator itDefault = d.values.find(c);
				if(itDefault != d.values.end() && PyStr(v) == itDefault->second) continue;
				bool bFlagText = v.kind == McpRefValue::TEXT && v.s == "true";
				if(d.booleans.count(c) || bFlagText)
				{
					bool bOn = bFlagText || (IsNumber(v) && Number(&v) == 1) || (v.kind == McpRefValue::TEXT && v.s == "1");
					bool bDefaultOff = itDefault == d.values.end() || itDefault->second == "false" || itDefault->second == "0";
					if(bOn && bDefaultOff) out.push_back(Abbr(c));
					continue;
				}
				std::string s;
				if(!Val(v, s)) continue;
				if(compact && IsNumber(v))
				{
					numberAt.push_back(out.size());
					numberCol.push_back(c);
					out.push_back(s);
				}
				else if(compact) out.push_back(s);
				else if(IsNumber(v)) out.push_back(Abbr(c) + s);
				else out.push_back(Abbr(c) + ":" + Replace(s, " Units", ""));
			}
			if(numberAt.size() > 1)
				for(size_t n = 0; n < numberAt.size(); ++n) out[numberAt[n]] = Abbr(numberCol[n]) + out[numberAt[n]];
			if(yieldType && yieldType->kind != McpRefValue::NUL)
			{
				if(!amountCol) out.push_back(Name(PyStr(*yieldType)));
				else if(amount.empty()) return Strings();
				else out.push_back(amount + YieldCode(PyStr(*yieldType)));
			}
			return out;
		}

		// The rows of every table named prefix..., by the object they belong to, written
		// TableName(values, values) or, for plain yields, 2g1f.
		std::map<std::string, Strings> Children(const std::string& prefix, const std::string& key, const StringSet& skipTables = StringSet())
		{
			typedef std::vector<std::pair<std::string, std::string> > Items;
			std::map<std::string, Items> by;
			StringSet skip;
			skip.insert(key);
			for(size_t t = 0; t < m_tables.size(); ++t)
			{
				const std::string& table = m_tables[t];
				if(!StartsWith(table, prefix.c_str())) continue;
				bool bSkip = false;
				for(size_t i = 0; i < MCP_REF_COUNT(CHILD_SKIP) && !bSkip; ++i) bSkip = Contains(table, CHILD_SKIP[i]);
				if(bSkip || skipTables.count(table)) continue;
				Rows rows;
				if(!m_src.Query("select * from " + table, rows) || rows.empty() || !Col(rows[0], key.c_str())) continue;
				std::string name = RemoveChanges(table.substr(prefix.size()));
				if(!name.empty() && name[0] == '_') name.erase(0, 1);
				if(name.empty() || name == "Yields" || name == "Yield") name = "yield";
				for(size_t i = 0; i < rows.size(); ++i)
				{
					const McpRefValue* civ = Col(rows[i], "CivilizationType");
					if(Truthy(civ) && !m_civSet.count(PyStr(*civ))) continue;
					if(MentionsAbsent(rows[i])) continue;
					Strings body = Cols(rows[i], skip, table, true);
					if(!body.empty()) by[Key(Col(rows[i], key.c_str()))].push_back(std::make_pair(name, Join(body, " ")));
				}
			}
			std::map<std::string, Strings> merged;
			for(std::map<std::string, Items>::const_iterator it = by.begin(); it != by.end(); ++it)
			{
				std::vector<std::pair<std::string, Strings> > groups;
				for(size_t i = 0; i < it->second.size(); ++i)
				{
					size_t g = 0;
					while(g < groups.size() && groups[g].first != it->second[i].first) ++g;
					if(g == groups.size()) groups.push_back(std::make_pair(it->second[i].first, Strings()));
					Strings& bodies = groups[g].second;
					if(std::find(bodies.begin(), bodies.end(), it->second[i].second) == bodies.end()) bodies.push_back(it->second[i].second);
				}
				Strings& lines = merged[it->first];
				for(size_t g = 0; g < groups.size(); ++g)
				{
					bool bBare = groups[g].first == "yield";
					for(size_t i = 0; i < groups[g].second.size() && bBare; ++i) bBare = IsBareYield(groups[g].second[i]);
					lines.push_back(bBare ? Join(groups[g].second, "") : groups[g].first + "(" + Join(groups[g].second, ", ") + ")");
				}
			}
			return merged;
		}

		Owners OwnersOf(const std::string& table, const std::string& col)
		{
			Owners owners;
			Rows rows = Select("select CivilizationType, " + col + " as x from " + table + " where " + col + " is not null");
			for(size_t i = 0; i < rows.size(); ++i) owners[ColText(rows[i], "x")].insert(ColText(rows[i], "CivilizationType"));
			return owners;
		}

		// Whether a unit or building can exist in this game: it is its class's default, or the
		// unique of a civilization in the game. Mods leave other rows behind that nothing uses.
		bool Available(const Owners& owners, const std::string& type) const
		{
			Owners::const_iterator it = owners.find(type);
			if(it == owners.end()) return m_classDefaults.count(type) > 0;
			for(StringSet::const_iterator c = it->second.begin(); c != it->second.end(); ++c)
				if(m_civSet.count(*c)) return true;
			return false;
		}

		std::string UniqueNote(const Owners& owners, const std::string& type) const
		{
			Owners::const_iterator it = owners.find(type);
			if(it == owners.end()) return "";
			Strings civs;
			for(StringSet::const_iterator c = it->second.begin(); c != it->second.end(); ++c)
				if(m_civSet.count(*c)) civs.push_back(Name(*c));
			return " [unique: " + Join(civs, ", ") + "]";
		}

		bool MentionsAbsent(const McpRefRow& r) const
		{
			for(size_t c = 0; c < r.size(); ++c)
				if(r[c].second.kind == McpRefValue::TEXT && m_absent.count(r[c].second.s)) return true;
			return false;
		}

		bool Referenced(const std::string& type) const { return !type.empty() && m_referenced.count(type) > 0; }

		// A building or unit with no cost is listed when the game hands it out: the palace,
		// the settler, or one another row grants.
		bool Include(Kind kind, const McpRefRow& r) const
		{
			std::string type = ColText(r, "Type");
			if(m_absent.count(type)) return false;
			const McpRefValue* civ = Col(r, "CivilizationType");
			if(Truthy(civ) && !m_civSet.count(PyStr(*civ))) return false;
			bool bCost = Number(Col(r, "Cost")) > 0 || Number(Col(r, "FaithCost")) > 0;
			switch(kind)
			{
			case BUILDINGS:
				return bCost || Truthy(Col(r, "Capital")) || Referenced(type) || Referenced(ColText(r, "BuildingClass"));
			case UNITS:
				return bCost || Truthy(Col(r, "Special")) || Truthy(Col(r, "Found")) || Referenced(type) || Referenced(ColText(r, "Class"));
			case TERRAIN:
				return !Truthy(Col(r, "GraphicalOnly"));
			case FEATURES:
				return !Truthy(Col(r, "NaturalWonder"));
			case NATURAL_WONDERS:
				return Truthy(Col(r, "NaturalWonder"));
			default:
				return true;
			}
		}

		// Whether the game's own description is worth its length: for wonders, uniques and
		// special units it says what the fields cannot.
		bool HelpWanted(Kind kind, const McpRefRow& r) const
		{
			switch(kind)
			{
			case TECHS:
			case BUILDS: return false;
			case BUILDINGS: return m_wonderClasses.count(ColText(r, "BuildingClass")) || m_uniqueBuildings.count(ColText(r, "Type")) || Number(Col(r, "Cost")) <= 0;
			case UNITS: return m_uniqueUnits.count(ColText(r, "Type")) || Truthy(Col(r, "Special")) || !Truthy(Col(r, "Combat"));
			default: return true;
			}
		}

		// Written after the name; for technologies, an era heading goes out first when the era changes.
		std::string Note(Kind kind, const McpRefRow& r)
		{
			if(kind == BUILDINGS)
			{
				StringMap::const_iterator it = m_wonderClasses.find(ColText(r, "BuildingClass"));
				return UniqueNote(m_uniqueBuildings, ColText(r, "Type")) + (it != m_wonderClasses.end() ? " [" + it->second + "]" : "");
			}
			if(kind == UNITS) return UniqueNote(m_uniqueUnits, ColText(r, "Type"));
			if(kind == TECHS)
			{
				StringMap::const_iterator it = m_eraOf.find(ColText(r, "Type"));
				std::string era = it != m_eraOf.end() ? it->second : NONE_KEY;
				if(era != m_lastEra)
				{
					m_lastEra = era;
					m_out.push_back("### " + (era == NONE_KEY ? std::string() : Name(era)));
				}
			}
			return "";
		}

		Strings Extras(Kind kind, const McpRefRow& r)
		{
			Strings extras;
			if(kind != TECHS) return extras;
			std::string type = ColText(r, "Type");
			const Strings& needs = m_needs[type];
			const Strings& unlocks = m_unlocks[type];
			if(!needs.empty()) extras.push_back("needs " + Join(needs, "+"));
			if(!unlocks.empty()) extras.push_back("unlocks " + Join(unlocks, ", "));
			return extras;
		}

		void Section(const std::string& title, const std::string& table, const std::string& prefix, const std::string& key, Kind kind, const StringSet& skip)
		{
			std::map<std::string, Strings> children = Children(prefix, key);
			m_out.push_back("\n## " + title);
			Rows all = Select("select * from " + table);
			Rows items;
			for(size_t i = 0; i < all.size(); ++i)
				if(Include(kind, all[i])) items.push_back(all[i]);
			std::vector<Strings> bodies;
			for(size_t i = 0; i < items.size(); ++i)
			{
				Strings body = Cols(items[i], skip, table, false);
				std::map<std::string, Strings>::const_iterator it = children.find(ColText(items[i], "Type"));
				if(it != children.end()) body.insert(body.end(), it->second.begin(), it->second.end());
				bodies.push_back(body);
			}

			// a field most rows share is said once, and a row without it says so
			std::map<std::string, int> freq;
			for(size_t i = 0; i < bodies.size(); ++i)
			{
				StringSet seen(bodies[i].begin(), bodies[i].end());
				for(StringSet::const_iterator p = seen.begin(); p != seen.end(); ++p) ++freq[*p];
			}
			Strings common;
			for(std::map<std::string, int>::const_iterator it = freq.begin(); it != freq.end(); ++it)
				if(items.size() >= 10 && it->second >= 0.6 * (double)items.size()) common.push_back(it->first);
			StringSet commonSet(common.begin(), common.end());
			if(!common.empty()) m_out.push_back("Shared by most below (a line says 'without' when not): " + Join(common, "; "));

			for(size_t i = 0; i < items.size(); ++i)
			{
				const McpRefRow& r = items[i];
				const Strings& b = bodies[i];
				Strings body;
				for(size_t j = 0; j < b.size(); ++j)
					if(!commonSet.count(b[j])) body.push_back(b[j]);
				StringSet has(b.begin(), b.end()), stems;
				for(size_t j = 0; j < b.size(); ++j) stems.insert(Stem(b[j]));
				for(size_t j = 0; j < common.size(); ++j)
					if(!has.count(common[j]) && !stems.count(Stem(common[j]))) body.push_back("without " + common[j]);
				Strings extras = Extras(kind, r);
				body.insert(body.end(), extras.begin(), extras.end());
				std::string help = (Col(r, "Help") && HelpWanted(kind, r)) ? Text(Col(r, "Help")) : "";
				std::string name = Name(ColText(r, "Type"));
				help = StripName(help, name);
				if(body.empty() && help.empty())
				{
					if(kind != TERRAIN) continue;
					body.push_back("no yield");
				}
				std::string note = Note(kind, r);
				m_out.push_back(name + note + ": " + Join(body, "; ") + (help.empty() ? "" : " | " + help));
			}
		}

		void FindAbsent();
		void FindReferenced();
		void Civilizations();
		void TechnologyLinks();
		void Policies();
		void Beliefs();
		void Promotions();
	};

	// What cannot appear in this game: units and buildings no civilization here can have, and
	// the improvements, builds and resources of civilizations not playing.
	void Builder::FindAbsent()
	{
		const char* const CLASSES[][2] = {{"BuildingClasses", "DefaultBuilding"}, {"UnitClasses", "DefaultUnit"}};
		for(size_t t = 0; t < MCP_REF_COUNT(CLASSES); ++t)
		{
			Rows rows = Select(std::string("select ") + CLASSES[t][1] + " as d from " + CLASSES[t][0]);
			for(size_t i = 0; i < rows.size(); ++i) m_classDefaults.insert(ColText(rows[i], "d"));
		}
		Rows rows = Select("select Type from Buildings");
		for(size_t i = 0; i < rows.size(); ++i)
			if(!Available(m_uniqueBuildings, ColText(rows[i], "Type"))) m_absent.insert(ColText(rows[i], "Type"));
		rows = Select("select Type from Units");
		for(size_t i = 0; i < rows.size(); ++i)
			if(!Available(m_uniqueUnits, ColText(rows[i], "Type"))) m_absent.insert(ColText(rows[i], "Type"));
		const char* const OWNED[] = {"Improvements", "Resources"};
		for(size_t t = 0; t < MCP_REF_COUNT(OWNED); ++t)
		{
			rows = Select(std::string("select Type, CivilizationType from ") + OWNED[t]);
			for(size_t i = 0; i < rows.size(); ++i)
			{
				const McpRefValue* civ = Col(rows[i], "CivilizationType");
				if(Truthy(civ) && !m_civSet.count(PyStr(*civ))) m_absent.insert(ColText(rows[i], "Type"));
			}
		}
		rows = Select("select Type, ImprovementType from Builds");
		for(size_t i = 0; i < rows.size(); ++i)
			if(m_absent.count(ColText(rows[i], "ImprovementType"))) m_absent.insert(ColText(rows[i], "Type"));
		for(size_t c = 0; c < m_civs.size(); ++c)
		{
			rows = Select("select lt.TraitType from Civilization_Leaders cl join Leader_Traits lt on lt.LeaderType=cl.LeaderheadType where cl.CivilizationType=" + Quote(m_civs[c]));
			for(size_t i = 0; i < rows.size(); ++i) m_gameTraits.insert(ColText(rows[i], "TraitType"));
		}
	}

	// Every name another row mentions, so something the game grants (a free building, a
	// promotion a wonder gives) is listed even though it cannot be built or chosen. A row's own
	// name, rows about absent things and the traits of civilizations not playing do not count.
	void Builder::FindReferenced()
	{
		for(size_t t = 0; t < m_tables.size(); ++t)
		{
			const std::string& table = m_tables[t];
			bool bSkip = table == "BuildingClasses" || table == "UnitClasses" || table == "Unit_FreePromotions"
				|| StartsWith(table, "Civilization_") || StartsWith(table, "UnitPromotions");
			for(size_t i = 0; i < MCP_REF_COUNT(CHILD_SKIP) && !bSkip; ++i) bSkip = Contains(table, CHILD_SKIP[i]);
			if(bSkip) continue;
			bool bTraits = table == "Traits" || StartsWith(table, "Trait_");
			Rows rows;
			if(!m_src.Query("select * from " + table, rows)) continue;
			for(size_t i = 0; i < rows.size(); ++i)
			{
				const McpRefRow& r = rows[i];
				if(m_absent.count(ColText(r, "Type"))) continue;
				if(bTraits && !m_gameTraits.count(ColText(r, table == "Traits" ? "Type" : "TraitType"))) continue;
				for(size_t c = 0; c < r.size(); ++c)
				{
					const std::string& col = r[c].first;
					if(r[c].second.kind != McpRefValue::TEXT || col == "Type") continue;
					if((table == "Buildings" && col == "BuildingClass") || (table == "Units" && col == "Class")) continue;
					if((StartsWith(table, "Building_") && col == "BuildingType") || (StartsWith(table, "Unit_") && col == "UnitType")) continue;
					m_referenced.insert(r[c].second.s);
				}
			}
		}
	}

	void Builder::Civilizations()
	{
		m_out.push_back("\n## Civilizations in this game");
		for(size_t c = 0; c < m_civs.size(); ++c)
		{
			const std::string& civ = m_civs[c];
			Rows leaders = Select("select LeaderheadType from Civilization_Leaders where CivilizationType=" + Quote(civ));
			std::string leader = leaders.empty() ? "" : ColText(leaders[0], "LeaderheadType");
			Strings traits, uniques;
			Rows rows = Select("select t.Description from Leader_Traits lt join Traits t on t.Type=lt.TraitType where lt.LeaderType=" + Quote(leader));
			for(size_t i = 0; i < rows.size(); ++i) traits.push_back(Text(Col(rows[i], "Description")));
			rows = Select("select BuildingType, BuildingClassType from Civilization_BuildingClassOverrides where CivilizationType=" + Quote(civ) + " and BuildingType is not null");
			for(size_t i = 0; i < rows.size(); ++i) uniques.push_back(Name(ColText(rows[i], "BuildingType")) + " replaces " + Name(ColText(rows[i], "BuildingClassType")));
			rows = Select("select UnitType, UnitClassType from Civilization_UnitClassOverrides where CivilizationType=" + Quote(civ) + " and UnitType is not null");
			for(size_t i = 0; i < rows.size(); ++i) uniques.push_back(Name(ColText(rows[i], "UnitType")) + " replaces " + Name(ColText(rows[i], "UnitClassType")));
			rows = Select("select Type from Improvements where CivilizationType=" + Quote(civ));
			for(size_t i = 0; i < rows.size(); ++i) uniques.push_back(Name(ColText(rows[i], "Type")));
			m_out.push_back(Name(civ) + " (" + Name(leader) + "): " + Join(traits, " ") + " | uniques: " + Join(uniques, "; "));
		}
	}

	// What each technology needs and what it unlocks, for the civilizations in the game.
	void Builder::TechnologyLinks()
	{
		Rows rows = Select("select Type, PrereqTech from Buildings where Cost>0 or FaithCost>0");
		for(size_t i = 0; i < rows.size(); ++i)
			if(Available(m_uniqueBuildings, ColText(rows[i], "Type")))
				m_unlocks[Key(Col(rows[i], "PrereqTech"))].push_back(Name(ColText(rows[i], "Type")));
		rows = Select("select Type, PrereqTech, Cost, FaithCost, Special from Units");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefRow& r = rows[i];
			if(Available(m_uniqueUnits, ColText(r, "Type")) && (Number(Col(r, "Cost")) > 0 || Number(Col(r, "FaithCost")) > 0 || Truthy(Col(r, "Special"))))
				m_unlocks[Key(Col(r, "PrereqTech"))].push_back(Name(ColText(r, "Type")));
		}
		rows = Select("select Type, PrereqTech from Builds where PrereqTech is not null");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefRow& r = rows[i];
			if(m_absent.count(ColText(r, "Type"))) continue;
			std::string build = "build " + Name(ColText(r, "Type"));
			Strings& unlocks = m_unlocks[Key(Col(r, "PrereqTech"))];
			if(std::find(unlocks.begin(), unlocks.end(), build) == unlocks.end()) unlocks.push_back(build);
		}
		rows = Select("select Type, TechReveal from Resources where TechReveal is not null");
		for(size_t i = 0; i < rows.size(); ++i)
			if(!m_absent.count(ColText(rows[i], "Type"))) m_unlocks[Key(Col(rows[i], "TechReveal"))].push_back("reveals " + Name(ColText(rows[i], "Type")));
		rows = Select("select TechType, PrereqTech from Technology_PrereqTechs");
		for(size_t i = 0; i < rows.size(); ++i) m_needs[ColText(rows[i], "TechType")].push_back(Name(ColText(rows[i], "PrereqTech")));
		rows = Select("select Type, Era from Technologies");
		for(size_t i = 0; i < rows.size(); ++i) m_eraOf[ColText(rows[i], "Type")] = Key(Col(rows[i], "Era"));
	}

	void Builder::Policies()
	{
		m_out.push_back("\n## Policies");
		StringSet branchPolicies; // a branch's opener and finisher; any other policy outside a branch is a mod's hidden marker
		Rows rows = Select("select * from PolicyBranchTypes");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			branchPolicies.insert(ColText(rows[i], "FreePolicy"));
			branchPolicies.insert(ColText(rows[i], "FreeFinishingPolicy"));
			m_out.push_back("Branch " + Name(ColText(rows[i], "Type")) + ": " + Join(Cols(rows[i], StringSet(), "", false), "; "));
		}
		std::map<std::string, Strings> needsAll, needsAny;
		rows = Select("select PolicyType, PrereqPolicy from Policy_PrereqPolicies");
		for(size_t i = 0; i < rows.size(); ++i) needsAll[ColText(rows[i], "PolicyType")].push_back(Name(ColText(rows[i], "PrereqPolicy")));
		rows = Select("select PolicyType, PrereqPolicy from Policy_PrereqORPolicies");
		for(size_t i = 0; i < rows.size(); ++i) needsAny[ColText(rows[i], "PolicyType")].push_back(Name(ColText(rows[i], "PrereqPolicy")));
		rows = Select("select * from Policies");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			std::string type = ColText(rows[i], "Type"), name, branch;
			bool bNamed = Lookup(type, name);
			std::string help = StripName(Text(Col(rows[i], "Help")), bNamed ? name : "");
			if(help.empty()) continue; // a branch's finisher repeats its opener's text
			const McpRefValue* b = Col(rows[i], "PolicyBranchType");
			bool bInBranch = b && b->kind == McpRefValue::TEXT && !b->s.empty();
			if(!bInBranch && !branchPolicies.count(type)) continue;
			if(!(bInBranch && Lookup(b->s, branch))) branch = "-";
			if(Number(Col(rows[i], "Level")) > 0) branch += ", level " + ColText(rows[i], "Level");
			if(!needsAll[type].empty()) branch += "; after " + Join(needsAll[type], " and ");
			if(!needsAny[type].empty()) branch += "; after " + Join(needsAny[type], " or ");
			m_out.push_back((bNamed ? name : "None") + " (" + branch + "): " + help);
		}
	}

	void Builder::Beliefs()
	{
		m_out.push_back("\n## Beliefs");
		Rows rows = Select("select * from Beliefs");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			Strings kinds;
			for(size_t k = 0; k < MCP_REF_COUNT(BELIEF_KINDS); ++k)
				if(Truthy(Col(rows[i], BELIEF_KINDS[k]))) kinds.push_back(BELIEF_KINDS[k]);
			m_out.push_back(Text(Col(rows[i], "ShortDescription")) + " (" + Join(kinds, ",") + "): " + Text(Col(rows[i], "Description")));
		}
	}

	void Builder::Promotions()
	{
		StringSet relatedSkip;
		relatedSkip.insert("UnitPromotions_UnitCombats");
		relatedSkip.insert("UnitPromotions_PromotionPrereqOrs");
		std::map<std::string, Strings> related = Children("UnitPromotions_", "PromotionType", relatedSkip);
		m_out.push_back("\n## Promotions");
		StringSet free;
		Rows rows = Select("select PromotionType, UnitType from Unit_FreePromotions");
		for(size_t i = 0; i < rows.size(); ++i)
			if(Available(m_uniqueUnits, ColText(rows[i], "UnitType"))) free.insert(ColText(rows[i], "PromotionType"));
		std::map<std::string, Strings> combats, afterAny;
		size_t allCombats = Select("select Type from UnitCombatInfos").size();
		rows = Select("select PromotionType, UnitCombatType from UnitPromotions_UnitCombats");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			Strings& who = combats[ColText(rows[i], "PromotionType")];
			std::string combat = Replace(Name(ColText(rows[i], "UnitCombatType")), " Units", "");
			if(std::find(who.begin(), who.end(), combat) == who.end()) who.push_back(combat);
		}
		rows = Select("select * from UnitPromotions_PromotionPrereqOrs");
		for(size_t i = 0; i < rows.size(); ++i)
			if(rows[i].size() >= 2) afterAny[PyStr(rows[i][0].second)].push_back(Name(PyStr(rows[i][1].second)));
		StringSet fieldSkip;
		fieldSkip.insert("CannotBeChosen");
		StringMap said; // what a promotion does, and whose it was first
		rows = Select("select * from UnitPromotions");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefRow& r = rows[i];
			std::string type = ColText(r, "Type");
			if(Truthy(Col(r, "CannotBeChosen")) && !free.count(type) && !Referenced(type)) continue;
			Strings after;
			for(size_t c = 0; c < r.size(); ++c)
			{
				if(!StartsWith(r[c].first, "PromotionPrereq")) continue;
				fieldSkip.insert(r[c].first);
				if(Truthy(&r[c].second)) after.push_back(Name(PyStr(r[c].second)));
			}
			after.insert(after.end(), afterAny[type].begin(), afterAny[type].end());
			std::string name;
			bool bNamed = Lookup(type, name);
			if(!bNamed) name = "None";
			std::string what = StripName(Text(Col(r, "Help")), bNamed ? name : "");
			if(what.empty())
			{
				Strings fields = Cols(r, fieldSkip, "UnitPromotions", false);
				fields.insert(fields.end(), related[type].begin(), related[type].end());
				what = Join(fields, "; ");
			}
			std::string who = (allCombats > 0 && combats[type].size() >= allCombats) ? "all" : Join(combats[type], ", ");
			std::string line = name + (who.empty() ? "" : " [" + who + "]") + (after.empty() ? "" : " (after " + Join(after, " or ") + ")") + ": ";
			std::string key = who + "|" + what;
			StringMap::const_iterator it = said.find(key);
			if(it != said.end())
			{
				if(it->second == name) continue; // the same promotion defined twice
				line += "same as " + it->second;
			}
			else
			{
				if(!what.empty()) said[key] = name;
				line += what;
			}
			m_out.push_back(line);
		}
	}

	std::string Builder::Build()
	{
		for(size_t t = 0; t < MCP_REF_COUNT(NAME_TABLES); ++t)
		{
			Rows rows;
			if(!m_src.Query(std::string("select Type, Description from ") + NAME_TABLES[t], rows)) continue;
			for(size_t i = 0; i < rows.size(); ++i)
			{
				std::string name = Text(Col(rows[i], "Description"));
				if(name.empty()) name = ColText(rows[i], "Type");
				m_names[ColText(rows[i], "Type")] = name.substr(0, name.find('|'));
			}
		}
		const char* const CLASSES[][2] = {{"BuildingClasses", "DefaultBuilding"}, {"UnitClasses", "DefaultUnit"}};
		for(size_t t = 0; t < MCP_REF_COUNT(CLASSES); ++t)
		{
			Rows rows;
			if(!m_src.Query(std::string("select Type, ") + CLASSES[t][1] + " as d from " + CLASSES[t][0], rows)) continue;
			for(size_t i = 0; i < rows.size(); ++i)
			{
				StringMap::const_iterator it = m_names.find(ColText(rows[i], "d"));
				if(it != m_names.end()) m_names[ColText(rows[i], "Type")] = it->second;
			}
		}
		Rows tables = Select("select name from sqlite_master where type='table'");
		for(size_t i = 0; i < tables.size(); ++i) m_tables.push_back(ColText(tables[i], "name"));
		m_uniqueBuildings = OwnersOf("Civilization_BuildingClassOverrides", "BuildingType");
		m_uniqueUnits = OwnersOf("Civilization_UnitClassOverrides", "UnitType");
		FindAbsent();
		FindReferenced();

		m_out.push_back("# Game reference");
		m_out.push_back("The rules of this game as loaded, not its current state. Costs are standard-speed base values. "
			"Where a description's numbers differ from the fields on its line, the fields are what the game uses.");
		Civilizations();

		TechnologyLinks();
		StringSet techSkip;
		techSkip.insert("Era");
		Section("Technologies", "Technologies", "Technology_PrereqTechs_none", "TechType", TECHS, techSkip);

		Rows classes = Select("select * from BuildingClasses");
		for(size_t i = 0; i < classes.size(); ++i)
		{
			const McpRefRow& r = classes[i];
			const char* kind = Number(Col(r, "MaxGlobalInstances")) > 0 ? "world wonder" : Number(Col(r, "MaxTeamInstances")) > 0 ? "team wonder"
				: Number(Col(r, "MaxPlayerInstances")) > 0 ? "national wonder" : NULL;
			if(kind) m_wonderClasses[ColText(r, "Type")] = kind;
		}
		Section("Buildings and wonders", "Buildings", "Building_", "BuildingType", BUILDINGS, StringSet());
		Section("Units", "Units", "Unit_", "UnitType", UNITS, StringSet());
		Policies();
		Beliefs();
		Promotions();
		Section("Improvements", "Improvements", "Improvement_", "ImprovementType", PLAIN, StringSet());
		Section("Worker builds", "Builds", "Build", "BuildType", BUILDS, StringSet());
		Section("Resources", "Resources", "Resource_", "ResourceType", PLAIN, StringSet());
		Section("Terrain", "Terrains", "Terrain_", "TerrainType", TERRAIN, StringSet());
		Section("Features", "Features", "Feature_", "FeatureType", FEATURES, StringSet());
		Section("Natural wonders", "Features", "Feature_", "FeatureType", NATURAL_WONDERS, StringSet());
		Section("Specialists", "Specialists", "Specialist", "SpecialistType", PLAIN, StringSet());
		Section("Projects", "Projects", "Project_", "ProjectType", PLAIN, StringSet());

		Strings fields;
		for(size_t i = 0; i < m_used.size(); ++i) fields.push_back(m_used[i].first + " " + Words(m_used[i].second));
		std::string legend = "Codes: yields f food, p production, g gold, s science, c culture, fa faith, ga golden age points, t tourism; "
			"3g1f means +3 gold +1 food. Fields: " + Join(fields, ", ") + ". Other fields keep their own names. A name before brackets "
			"is a related table: YieldModifiers in percent, PerPop per 100 citizens (PerPop(50s) is +1 science per 2 citizens).";
		m_out.insert(m_out.begin() + 2, legend);
		return Join(m_out, "\n");
	}
}

std::string McpBuildGameReference(McpRefSource& source, const std::vector<std::string>& civs)
{
	Builder builder(source, civs);
	return builder.Build();
}

#ifndef MCP_REF_STANDALONE
namespace
{
	class GameDatabaseSource : public McpRefSource
	{
	public:
		virtual bool Query(const std::string& sql, std::vector<McpRefRow>& rows)
		{
			Database::Results kResults;
			if(!DB.Execute(kResults, sql.c_str())) return false;
			while(kResults.Step())
			{
				McpRefRow row;
				for(int i = 0; i < kResults.ColumnCount(); ++i)
				{
					McpRefValue v;
					Database::ColumnTypes eType = kResults.ColumnType(i);
					const char* szText = kResults.GetText(i);
					if(szText != NULL && eType != Database::COLTYPE_NULL)
					{
						switch(eType)
						{
						case Database::COLTYPE_INT:
						case Database::COLTYPE_BOOL:
							v.kind = McpRefValue::INT;
							v.i = kResults.GetInt64(i);
							break;
						case Database::COLTYPE_FLOAT:
							v.kind = McpRefValue::REAL;
							v.d = strtod(szText, NULL);
							break;
						default:
							v.kind = McpRefValue::TEXT;
							v.s = szText;
							break;
						}
					}
					row.push_back(std::make_pair(std::string(kResults.ColumnName(i)), v));
				}
				rows.push_back(row);
			}
			return true;
		}

		virtual bool Localize(const std::string& key, std::string& text)
		{
			text = McpTool::Localized(key.c_str());
			return !text.empty() && text != key;
		}
	};
}

std::string McpGameReference()
{
	std::vector<std::string> civs;
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
	{
		CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)i);
		if(!kPlayer.isEverAlive()) continue;
		std::string civ = kPlayer.getCivilizationInfo().GetType();
		if(std::find(civs.begin(), civs.end(), civ) == civs.end()) civs.push_back(civ);
	}
	GameDatabaseSource source;
	return McpBuildGameReference(source, civs);
}
#endif
