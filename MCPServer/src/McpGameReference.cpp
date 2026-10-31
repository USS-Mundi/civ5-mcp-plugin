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
		"AmountMinor", "AmountMajor", "Area", "Hills", "Flatlands", "NoRiverSide", "Unique", "GroupSize",
		"GroupRange", "CombatLimit", "RangedCombatLimit", "MilitarySupport", "MilitaryProduction", "Pillage", "Conscription",
		"RequiresFaithPurchaseEnabled", "Sellable", "UnlockedByBelief", "ResourceUsage", "SeeFrom", "SeeThrough",
		"InfluenceCost", "ShowInPedia", "PrereqTech", "Mechanized", "IconString", "TechCityTrade",
		"CivilianAttackPriority", "MaxStartEra", "PlacementOrder", "GroupRand", "Normalize", "WorkerSpeed",
		"ExtraMaintenanceCost", "OccurrenceFrequency", "MinLandPercent", "Visible", "GreatPeopleTitle", "DontShowYields",
		"ThemingBonusHelp", "GraphicalOnly", "NoRegional", "Growth", "Recommendation", "CtrlDown", "AltDown", "ShiftDown",
		"CtrlDownAlt", "AltDownAlt", "ShiftDownAlt", "CityWall"};
	const char* const CHILD_SKIP[] = {"Flavor", "AI", "Art", "Audio", "UniqueNames", "Sound", "Booleans", "Gameplay2DScripts"};
	const char* const BELIEF_KINDS[] = {"Pantheon", "Founder", "Follower", "Enhancer", "Reformation"};

	// Related tables written under a shorter label, and what the legend says the label means.
	const char* const CHILD_SHORT[][3] = {
		{"FreePromotions", "promos", "free promotions"}, {"ClassUpgrades", "upgrades", "upgrades to"},
		{"ClassesNeededInCity", "needs", "buildings the city needs first"}, {"PrereqBuildingClasses", "needs in all cities", ""},
		{"ResourceQuantityRequirements", "uses", "strategic resources it uses"}, {"ResourceTypes", "connects", "resources it connects"},
		{"ValidTerrains", "terrains", "terrains it can be built on"}, {"ValidFeatures", "features", "features it can be built on"},
		{"ResourceType_Yields", "on resources", "yields on these resources"}};

	// Capitalized words in a description that say nothing about which field it repeats.
	const char* const HELP_STOP[] = {"City", "Cities", "Unit", "Units", "Empire", "I", "II", "III"};

	const char* const ROMAN[] = {"I", "II", "III", "IV", "V", "VI"};

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

	// A type no table names, as words: SPECIALUNIT_FIGHTER -> fighter, GREAT_WORK_SLOT_MUSIC -> music.
	std::string Pretty(const std::string& type)
	{
		size_t under = type.find('_');
		if(under == std::string::npos || type.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) return type;
		std::string rest = StartsWith(type, "GREAT_WORK_SLOT_") ? type.substr(16) : type.substr(under + 1);
		return Lower(Replace(rest, "_", " "));
	}

	// A field's name without its value: kind:luxury -> kind, H4 -> H, Trade(x) -> Trade.
	std::string Label(const std::string& f) { return f.substr(0, f.find_first_of("(:0123456789")); }

	// The words of a text in lower case, column names split at their capitals (FreeBuilding -> free, building).
	StringSet WordsOf(const std::string& s)
	{
		StringSet words;
		std::string w;
		for(size_t i = 0; i <= s.size(); ++i)
		{
			char c = i < s.size() ? s[i] : ' ';
			bool bLetter = isalpha((unsigned char)c) != 0;
			if(bLetter && isupper((unsigned char)c) && !w.empty() && islower((unsigned char)w[w.size() - 1]))
			{
				words.insert(Lower(w));
				w.clear();
			}
			if(bLetter) w += c;
			else if(!w.empty())
			{
				words.insert(Lower(w));
				w.clear();
			}
		}
		return words;
	}

	StringSet NumbersOf(const std::string& s)
	{
		StringSet numbers;
		for(size_t i = 0; i < s.size();)
		{
			size_t j = i;
			while(j < s.size() && isdigit((unsigned char)s[j])) ++j;
			if(j > i) numbers.insert(s.substr(i, j - i));
			i = j > i ? j : i + 1;
		}
		return numbers;
	}

	bool HasWord(const StringSet& words, const std::string& word)
	{
		std::string w = Lower(word);
		if(words.count(w)) return true;
		if(w.size() < 4) return false;
		for(StringSet::const_iterator it = words.begin(); it != words.end(); ++it)
			if(it->size() >= 4 && it->compare(0, 4, w, 0, 4) == 0) return true;
		return false;
	}

	// A description without the sentences that only repeat its line: a sentence goes when every
	// number in it, and every capitalized word after its first, is already among the fields. One
	// with no such word needs numbers of two digits or more, since a lone 1 or 2 is on most lines.
	std::string WithoutRepeats(const std::string& help, const std::string& fields)
	{
		StringSet fieldWords = WordsOf(fields), fieldNumbers = NumbersOf(fields);
		Strings sentences;
		size_t from = 0;
		for(size_t i = 0; i <= help.size(); ++i)
		{
			bool bEnd = i == help.size() || (help[i] == ';' && i + 1 < help.size() && help[i + 1] == ' ');
			bool bStop = !bEnd && help[i] == '.' && i + 1 < help.size() && help[i + 1] == ' ';
			if(!bEnd && !bStop) continue;
			std::string s = Strip(help.substr(from, bStop ? i + 1 - from : i - from));
			if(!s.empty()) sentences.push_back(s);
			from = i + 1;
		}
		Strings kept;
		for(size_t n = 0; n < sentences.size(); ++n)
		{
			const std::string& s = sentences[n];
			StringSet numbers = NumbersOf(s);
			Strings names;
			bool bFirst = true;
			std::string w;
			for(size_t i = 0; i <= s.size(); ++i)
			{
				char c = i < s.size() ? s[i] : ' ';
				if(isalpha((unsigned char)c))
				{
					w += c;
					continue;
				}
				if(w.empty()) continue;
				if(!bFirst && isupper((unsigned char)w[0]) && !InList(w, HELP_STOP, MCP_REF_COUNT(HELP_STOP))) names.push_back(w);
				bFirst = false;
				w.clear();
			}
			bool bRepeat = !numbers.empty() || !names.empty();
			for(StringSet::const_iterator it = numbers.begin(); it != numbers.end() && bRepeat && names.empty(); ++it) bRepeat = it->size() >= 2;
			for(StringSet::const_iterator it = numbers.begin(); it != numbers.end() && bRepeat; ++it) bRepeat = fieldNumbers.count(*it) > 0;
			for(size_t i = 0; i < names.size() && bRepeat; ++i) bRepeat = HasWord(fieldWords, names[i]);
			if(!bRepeat) kept.push_back(s);
		}
		return Join(kept, " ");
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
		for(std::string before; before != t;)
		{
			before = t;
			t = Replace(t, "; ; ", "; ");
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
		Builder(McpRefSource& source, const Strings& civs, const McpRefSettings& settings)
			: m_src(source), m_civs(civs), m_civSet(civs.begin(), civs.end()), m_settings(settings), m_lastEra(NONE_KEY) {}

		std::string Build();

	private:
		enum Kind { PLAIN, TECHS, BUILDINGS, UNITS, BUILDS, TERRAIN, FEATURES, NATURAL_WONDERS, RESOURCES };
		typedef std::map<std::string, StringSet> Owners;
		struct Defaults
		{
			StringMap values;
			StringSet booleans;
		};

		McpRefSource& m_src;
		Strings m_civs;
		StringSet m_civSet;
		McpRefSettings m_settings;
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
		Strings m_childLabels; // short related-table labels in use, for the legend
		std::map<std::string, Strings> m_extra; // more fields for a section's rows, by type

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
				else if(!Lookup(v.s, s)) s = Pretty(v.s);
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
				if(c == "HurryCostModifier" && !compact && !skip.count(c))
				{
					// -1 is the game's mark for what gold cannot buy; wonders never can, so they do not say it
					if(Number(&v) == -1 && Number(Col(r, "Cost")) > 0 && !m_wonderClasses.count(ColText(r, "BuildingClass")))
						out.push_back("no gold purchase");
					continue;
				}
				if(SkipCol(c) || skip.count(c) || c == "YieldType") continue;
				if(!compact && c == "GreatWorkCount" && Truthy(Col(r, "GreatWorkSlotType")) && Number(&v) > 0) continue; // said with its slot
				if(!compact && c == "GreatWorkSlotType" && Number(Col(r, "GreatWorkCount")) > 0)
				{
					std::string slot;
					if(Val(v, slot)) out.push_back(Abbr("GreatWorkCount") + ":" + Dec((long long)Number(Col(r, "GreatWorkCount"))) + " " + slot);
					continue;
				}
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
				else if(compact) out.push_back(Replace(s, " Units", ""));
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

		// One row of a related table: its values, and when the first one names a type, that type
		// and the values after it.
		struct Entry
		{
			std::string table, full, target, targetName, rest;
		};

		struct PromotionLine
		{
			std::string name, who, after, what, text;
		};

		// The first value of a row, as Cols reads it, when it is a type some table names.
		std::string FirstNamed(const McpRefRow& r, const StringSet& skip)
		{
			for(size_t i = 0; i < r.size(); ++i)
			{
				const std::string& c = r[i].first;
				if(SkipCol(c) || skip.count(c) || c == "YieldType" || r[i].second.kind == McpRefValue::NUL) continue;
				std::string name;
				return (r[i].second.kind == McpRefValue::TEXT && Lookup(r[i].second.s, name)) ? r[i].second.s : "";
			}
			return "";
		}

		// The names of the types left out of a group; one named like a type in the group goes by its type word.
		Strings Distinct(const Strings& missing, const Strings& names) const
		{
			Strings out;
			for(size_t i = 0; i < missing.size(); ++i)
			{
				std::string name = Replace(Name(missing[i]), " Units", "");
				out.push_back(std::find(names.begin(), names.end(), name) == names.end() ? name : Pretty(missing[i]));
			}
			return out;
		}

		// Targets that share a value: all of their kind, all but a few, or each by name.
		std::string Targets(const Strings& types, const Strings& names) const
		{
			std::string family = types[0].substr(0, types[0].find('_') + 1);
			bool bSameFamily = family.size() > 1;
			for(size_t i = 0; i < types.size() && bSameFamily; ++i) bSameFamily = StartsWith(types[i], family.c_str());
			if(bSameFamily)
			{
				StringSet have(types.begin(), types.end());
				Strings missing;
				for(StringMap::const_iterator it = m_names.lower_bound(family); it != m_names.end() && StartsWith(it->first, family.c_str()); ++it)
					if(!have.count(it->first) && !m_absent.count(it->first)) missing.push_back(it->first);
				if(missing.empty()) return "all";
				if(missing.size() <= 3 && types.size() > missing.size()) return "all but " + Join(Distinct(missing, names), ", ");
			}
			return Join(names, "/");
		}

		// A related table's rows for one object: yields of one target together (Spices 2g1f), and a
		// value three or more targets share written once (Mt. Fuji/Krakatoa/Uluru: 5s).
		std::string Group(const std::string& table, const std::vector<Entry>& entries)
		{
			bool bBare = table == "yield";
			for(size_t i = 0; i < entries.size() && bBare; ++i) bBare = IsBareYield(entries[i].full);
			if(bBare)
			{
				std::string s;
				for(size_t i = 0; i < entries.size(); ++i) s += entries[i].full;
				return s;
			}
			std::vector<Entry> items;
			for(size_t i = 0; i < entries.size(); ++i)
			{
				const Entry& e = entries[i];
				size_t j = 0;
				if(!e.target.empty() && IsBareYield(e.rest))
					while(j < items.size() && !(items[j].target == e.target && IsBareYield(items[j].rest))) ++j;
				else j = items.size();
				if(j < items.size()) items[j].rest += e.rest;
				else items.push_back(e);
			}
			Strings parts;
			std::vector<bool> done(items.size(), false);
			for(size_t i = 0; i < items.size(); ++i)
			{
				if(done[i]) continue;
				done[i] = true;
				if(items[i].target.empty())
				{
					parts.push_back(items[i].full);
					continue;
				}
				std::vector<size_t> same(1, i);
				for(size_t j = i + 1; j < items.size(); ++j)
					if(!done[j] && !items[j].target.empty() && items[j].rest == items[i].rest) same.push_back(j);
				if(same.size() < 3)
				{
					parts.push_back(items[i].targetName + " " + items[i].rest);
					continue;
				}
				Strings types, names;
				for(size_t k = 0; k < same.size(); ++k)
				{
					done[same[k]] = true;
					types.push_back(items[same[k]].target);
					names.push_back(items[same[k]].targetName);
				}
				parts.push_back(Targets(types, names) + ": " + items[i].rest);
			}
			std::string label = table;
			for(size_t i = 0; i < MCP_REF_COUNT(CHILD_SHORT); ++i)
			{
				if(table != CHILD_SHORT[i][0]) continue;
				label = CHILD_SHORT[i][1];
				if(*CHILD_SHORT[i][2] && std::find(m_childLabels.begin(), m_childLabels.end(), label) == m_childLabels.end()) m_childLabels.push_back(label);
			}
			return label + "(" + Join(parts, ", ") + ")";
		}

		// The rows of every table named prefix..., by the object they belong to, written
		// TableName(values, values) or, for plain yields, 2g1f.
		std::map<std::string, Strings> Children(const std::string& prefix, const std::string& key, const StringSet& skipTables = StringSet())
		{
			std::map<std::string, std::vector<Entry> > by;
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
					if(body.empty()) continue;
					Entry e;
					e.table = name;
					e.full = Join(body, " ");
					std::string first = FirstNamed(rows[i], skip);
					if(body.size() >= 2 && !first.empty() && Replace(Name(first), " Units", "") == body[0])
					{
						e.target = first;
						e.targetName = body[0];
						e.rest = Join(Strings(body.begin() + 1, body.end()), " ");
					}
					by[Key(Col(rows[i], key.c_str()))].push_back(e);
				}
			}
			std::map<std::string, Strings> merged;
			for(std::map<std::string, std::vector<Entry> >::const_iterator it = by.begin(); it != by.end(); ++it)
			{
				std::vector<std::pair<std::string, std::vector<Entry> > > groups;
				for(size_t i = 0; i < it->second.size(); ++i)
				{
					const Entry& e = it->second[i];
					size_t g = 0;
					while(g < groups.size() && groups[g].first != e.table) ++g;
					if(g == groups.size()) groups.push_back(std::make_pair(e.table, std::vector<Entry>()));
					std::vector<Entry>& entries = groups[g].second;
					bool bSeen = false;
					for(size_t k = 0; k < entries.size() && !bSeen; ++k) bSeen = entries[k].full == e.full;
					if(!bSeen) entries.push_back(e);
				}
				// a building needed in every city is needed in this one, so it is said once
				StringSet everywhere;
				for(size_t g = 0; g < groups.size(); ++g)
					if(groups[g].first == "PrereqBuildingClasses")
						for(size_t k = 0; k < groups[g].second.size(); ++k) everywhere.insert(groups[g].second[k].full);
				Strings& lines = merged[it->first];
				for(size_t g = 0; g < groups.size(); ++g)
				{
					std::vector<Entry> entries;
					for(size_t k = 0; k < groups[g].second.size(); ++k)
						if(groups[g].first != "ClassesNeededInCity" || !everywhere.count(groups[g].second[k].full)) entries.push_back(groups[g].second[k]);
					if(!entries.empty()) lines.push_back(Group(groups[g].first, entries));
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
				return bCost || Truthy(Col(r, "Special")) || Referenced(type) || Referenced(ColText(r, "Class"));
			case BUILDS:
				return !Truthy(Col(r, "ImprovementType")); // an improvement's build is written on the improvement
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
		// special units it says what the fields cannot. An aircraft's says only what it is, and a
		// resource's only what uses it.
		bool HelpWanted(Kind kind, const McpRefRow& r) const
		{
			switch(kind)
			{
			case TECHS:
			case BUILDS: return false;
			case RESOURCES: return Truthy(Col(r, "OnlyMinorCivs")); // where a resource the map never places comes from
			case BUILDINGS: return m_wonderClasses.count(ColText(r, "BuildingClass")) || m_uniqueBuildings.count(ColText(r, "Type")) || Number(Col(r, "Cost")) <= 0;
			case UNITS:
				return m_uniqueUnits.count(ColText(r, "Type")) || (Truthy(Col(r, "Special")) && ColText(r, "Special") != "SPECIALUNIT_FIGHTER")
					|| (!Truthy(Col(r, "Combat")) && !Truthy(Col(r, "RangedCombat")));
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
				it = m_extra.find(ColText(items[i], "Type"));
				if(it != m_extra.end()) body.insert(body.end(), it->second.begin(), it->second.end());
				bodies.push_back(body);
			}

			// a field most rows share is said once, and a row without it says so, when that is shorter
			std::map<std::string, int> freq;
			for(size_t i = 0; i < bodies.size(); ++i)
			{
				StringSet seen(bodies[i].begin(), bodies[i].end());
				for(StringSet::const_iterator p = seen.begin(); p != seen.end(); ++p) ++freq[*p];
			}
			Strings common;
			for(std::map<std::string, int>::const_iterator it = freq.begin(); it != freq.end(); ++it)
			{
				double n = it->second, total = (double)items.size(), len = (double)it->first.size() + 2;
				double saved = n * len - len - (total - n) * ((double)Label(it->first).size() + 10);
				if(items.size() >= 10 && n >= 0.6 * total && saved > 0) common.push_back(it->first);
			}
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
					if(!has.count(common[j]) && !stems.count(Stem(common[j]))) body.push_back("without " + Label(common[j]));
				Strings extras = Extras(kind, r);
				body.insert(body.end(), extras.begin(), extras.end());
				std::string help = (Col(r, "Help") && HelpWanted(kind, r)) ? Text(Col(r, "Help")) : "";
				std::string name = Name(ColText(r, "Type"));
				help = StripName(help, name);
				if(!help.empty()) help = WithoutRepeats(help, Join(b, "; ") + (kind == BUILDINGS ? Note(kind, r) : ""));
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
		void FoldBuilds();
		void Civilizations();
		void Rules();
		Strings DefaultsOf(const char* sql);
		void TechnologyLinks();
		void Policies();
		void Beliefs();
		void Promotions();
	};

	// What cannot appear in this game: units and buildings no civilization here can have, and
	// the improvements, builds and resources of civilizations not playing.
	void Builder::FindAbsent()
	{
		// a class's default only counts for its own class: a mod may name one row the default of a second, unused class
		const char* const CLASSES[][4] = {{"BuildingClasses", "DefaultBuilding", "Buildings", "BuildingClass"}, {"UnitClasses", "DefaultUnit", "Units", "Class"}};
		for(size_t t = 0; t < MCP_REF_COUNT(CLASSES); ++t)
		{
			Rows rows = Select(std::string("select o.Type as t from ") + CLASSES[t][2] + " o join " + CLASSES[t][0] + " c on c.Type=o."
				+ CLASSES[t][3] + " where c." + CLASSES[t][1] + "=o.Type");
			for(size_t i = 0; i < rows.size(); ++i) m_classDefaults.insert(ColText(rows[i], "t"));
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
		// and a build no unit in this game can do
		Rows builders = Select("select UnitType, BuildType from Unit_Builds");
		if(!builders.empty())
		{
			StringSet doable;
			for(size_t i = 0; i < builders.size(); ++i)
				if(!m_absent.count(ColText(builders[i], "UnitType"))) doable.insert(ColText(builders[i], "BuildType"));
			for(size_t i = 0; i < rows.size(); ++i)
				if(!doable.count(ColText(rows[i], "Type"))) m_absent.insert(ColText(rows[i], "Type"));
		}
		for(size_t c = 0; c < m_civs.size(); ++c)
		{
			rows = Select("select lt.TraitType from Civilization_Leaders cl join Leader_Traits lt on lt.LeaderType=cl.LeaderheadType where cl.CivilizationType=" + Quote(m_civs[c]));
			for(size_t i = 0; i < rows.size(); ++i) m_gameTraits.insert(ColText(rows[i], "TraitType"));
		}
	}

	// Every name another row mentions, so something the game grants (a free building, a
	// promotion a wonder gives) is listed even though it cannot be built or chosen. A row's own
	// name, rows about absent things or for civilizations not playing, and their traits do not count.
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
				const McpRefValue* civ = Col(r, "CivilizationType");
				if(Truthy(civ) && !m_civSet.count(PyStr(*civ))) continue;
				if(table.find('_') != std::string::npos && MentionsAbsent(r)) continue; // a related row of something absent
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

	// Each improvement's builds, written on the improvement: build(Time700, clears features). Building on
	// a feature that the feature's own clearing build would remove the same way just says it clears it.
	void Builder::FoldBuilds()
	{
		std::map<std::string, Strings> features = Children("Build", "BuildType");
		StringSet clearing;
		Rows builds = Select("select * from Builds");
		for(size_t i = 0; i < builds.size(); ++i)
		{
			const McpRefRow& r = builds[i];
			if(Truthy(Col(r, "ImprovementType")) || Truthy(Col(r, "RouteType")) || Truthy(Col(r, "Repair")) || Truthy(Col(r, "RemoveRoute"))) continue;
			std::map<std::string, Strings>::const_iterator it = features.find(ColText(r, "Type"));
			if(it == features.end() || it->second.size() != 1 || !StartsWith(it->second[0], "Features(")) continue;
			std::string inner = it->second[0].substr(9, it->second[0].size() - 10);
			if(inner.find(", ") == std::string::npos) clearing.insert(inner);
		}
		StringSet skip;
		skip.insert("ImprovementType");
		for(size_t i = 0; i < builds.size(); ++i)
		{
			const McpRefRow& r = builds[i];
			std::string type = ColText(r, "Type"), improvement = ColText(r, "ImprovementType");
			if(improvement.empty() || m_absent.count(type)) continue;
			Strings parts = Cols(r, skip, "Builds", false);
			std::map<std::string, Strings>::const_iterator it = features.find(type);
			for(size_t k = 0; it != features.end() && k < it->second.size(); ++k)
			{
				std::string f = it->second[k];
				if(StartsWith(f, "Features("))
				{
					std::string inner = f.substr(9, f.size() - 10);
					bool bClears = !inner.empty();
					for(size_t from = 0, at; from < inner.size() && bClears; from = at + 2)
					{
						at = inner.find(", ", from);
						if(at == std::string::npos) at = inner.size();
						bClears = clearing.count(inner.substr(from, at - from)) > 0;
					}
					if(bClears) f = "clears features";
				}
				parts.push_back(f);
			}
			m_extra[improvement].push_back("build(" + Join(parts, ", ") + ")");
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

	// The first row's number in a column, or the fallback when there is no row or no number.
	double FieldOr(const Rows& rows, const char* col, double fallback)
	{
		const McpRefValue* v = rows.empty() ? NULL : Col(rows[0], col);
		return v && IsNumber(*v) ? Number(v) : fallback;
	}

	// Whether every space-separated name has a value.
	bool HasAll(const std::map<std::string, double>& values, const std::string& names)
	{
		for(size_t start = 0, end; start < names.size(); start = end + 1)
		{
			end = names.find(' ', start);
			if(end == std::string::npos) end = names.size();
			if(end > start && !values.count(names.substr(start, end - start))) return false;
		}
		return true;
	}

	// "a, b and c"
	std::string JoinAnd(const Strings& v)
	{
		if(v.size() < 2) return Join(v, "");
		return Join(Strings(v.begin(), v.end() - 1), ", ") + " and " + v.back();
	}

	std::string Count(double n, const char* one, const char* many) { return PyG(n) + " " + (n == 1 ? one : many); }

	// The names of the class defaults a query returns as t, leaving out those of civilizations not in the game.
	Strings Builder::DefaultsOf(const char* sql)
	{
		Strings names;
		Rows rows = Select(sql);
		for(size_t i = 0; i < rows.size(); ++i)
			if(!m_absent.count(ColText(rows[i], "t"))) names.push_back(Name(ColText(rows[i], "t")));
		return names;
	}

	// What the numbers do: happiness, growth, gold and research, and what the computer players are spared.
	// A line whose values this game does not have is left out.
	void Builder::Rules()
	{
		std::map<std::string, double> def;
		Rows rows = Select("select Name, Value from Defines");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefValue* v = Col(rows[i], "Value");
			if(v && IsNumber(*v)) def[ColText(rows[i], "Name")] = Number(v);
		}
		Rows handicap = Select("select * from HandicapInfos where Type=" + Quote(m_settings.handicap));
		Rows world = Select("select * from Worlds where Type=" + Quote(m_settings.worldSize));
		Rows ai;
		if(def.count("AI_HANDICAP")) ai = Select("select * from HandicapInfos where ID=" + Dec((long long)def["AI_HANDICAP"]));
		if(ai.empty()) ai = Select("select * from HandicapInfos where Type='HANDICAP_AI_DEFAULT'");

		m_out.push_back("\n## Rules of this game");
		Strings setting;
		if(!handicap.empty()) setting.push_back("difficulty " + Text(Col(handicap[0], "Description")));
		if(!world.empty()) setting.push_back("map size " + Text(Col(world[0], "Description")));
		if(!setting.empty()) m_out.push_back("This game: " + Join(setting, ", ") + ".");

		if(HasAll(def, "UNHAPPINESS_PER_CITY UNHAPPINESS_PER_POPULATION UNHAPPINESS_PER_CAPTURED_CITY UNHAPPINESS_PER_OCCUPIED_POPULATION HAPPINESS_PER_EXTRA_LUXURY HAPPINESS_PER_NATURAL_WONDER"))
		{
			double city = def["UNHAPPINESS_PER_CITY"] * FieldOr(handicap, "NumCitiesUnhappinessMod", 100) / 100 * FieldOr(world, "NumCitiesUnhappinessPercent", 100) / 100;
			double citizen = def["UNHAPPINESS_PER_POPULATION"] * FieldOr(handicap, "PopulationUnhappinessMod", 100) / 100;
			std::string line = "Happiness: ";
			if(!handicap.empty()) line += "you start with " + PyG(FieldOr(handicap, "HappinessDefault", 0)) + ". ";
			line += "Each city costs " + PyG(city) + " and each citizen " + PyG(citizen) + "; an occupied city costs "
				+ PyG(def["UNHAPPINESS_PER_CAPTURED_CITY"]) + " and " + PyG(def["UNHAPPINESS_PER_OCCUPIED_POPULATION"]) + " per citizen";
			Strings courts = DefaultsOf("select b.Type as t from Buildings b join BuildingClasses c on c.Type=b.BuildingClass and c.DefaultBuilding=b.Type where b.NoOccupiedUnhappiness=1");
			if(!courts.empty()) line += " until it has a " + Join(courts, " or ");
			line += ". ";
			Rows lux = Select("select Happiness as h, count(*) as n from Resources where ResourceClassType='RESOURCECLASS_LUXURY' group by Happiness order by n desc");
			if(!lux.empty()) line += "Each kind of luxury gives " + PyG(Number(Col(lux[0], "h"))) + " once it is improved or traded for; more copies give "
				+ PyG(def["HAPPINESS_PER_EXTRA_LUXURY"]) + ". ";
			line += "Each natural wonder gives " + PyG(def["HAPPINESS_PER_NATURAL_WONDER"]) + ". A city's happiness from its own buildings is at most its population.";
			m_out.push_back(line);
		}
		if(HasAll(def, "UNHAPPY_GROWTH_PENALTY VERY_UNHAPPY_PRODUCTION_PENALTY_PER_UNHAPPY VERY_UNHAPPY_MAX_PRODUCTION_PENALTY VERY_UNHAPPY_GOLD_PENALTY_PER_UNHAPPY VERY_UNHAPPY_MAX_GOLD_PENALTY VERY_UNHAPPY_COMBAT_PENALTY_PER_UNHAPPY VERY_UNHAPPY_MAX_COMBAT_PENALTY"))
		{
			std::string production = PyG(def["VERY_UNHAPPY_PRODUCTION_PENALTY_PER_UNHAPPY"]) + "% per point below 0 (at most " + PyG(def["VERY_UNHAPPY_MAX_PRODUCTION_PENALTY"]) + "%)";
			std::string gold = PyG(def["VERY_UNHAPPY_GOLD_PENALTY_PER_UNHAPPY"]) + "% per point below 0 (at most " + PyG(def["VERY_UNHAPPY_MAX_GOLD_PENALTY"]) + "%)";
			m_out.push_back("Below 0: growth " + PyG(def["UNHAPPY_GROWTH_PENALTY"]) + "% in every city; "
				+ (production == gold ? "production and gold " + production : "production " + production + ", gold " + gold)
				+ "; combat strength " + PyG(def["VERY_UNHAPPY_COMBAT_PENALTY_PER_UNHAPPY"]) + "% per point (at most " + PyG(def["VERY_UNHAPPY_MAX_COMBAT_PENALTY"]) + "%).");
		}
		if(HasAll(def, "VERY_UNHAPPY_THRESHOLD VERY_UNHAPPY_GROWTH_PENALTY VERY_UNHAPPY_CANT_TRAIN_SETTLERS"))
			m_out.push_back(PyG(def["VERY_UNHAPPY_THRESHOLD"]) + " or lower: growth " + PyG(def["VERY_UNHAPPY_GROWTH_PENALTY"]) + "%"
				+ (def["VERY_UNHAPPY_CANT_TRAIN_SETTLERS"] == 1 ? ", no city can train a unit that founds cities," : "") + " and rebels appear after a countdown.");
		if(HasAll(def, "SUPER_UNHAPPY_THRESHOLD"))
			m_out.push_back(PyG(def["SUPER_UNHAPPY_THRESHOLD"]) + " or lower: cities discontented with your ideology can revolt.");
		if(HasAll(def, "GOLDEN_AGE_BASE_THRESHOLD_HAPPINESS GOLDEN_AGE_EACH_GA_ADDITIONAL_HAPPINESS GOLDEN_AGE_THRESHOLD_CITY_MULTIPLIER GOLDEN_AGE_LENGTH"))
			m_out.push_back("Golden age: " + PyG(def["GOLDEN_AGE_BASE_THRESHOLD_HAPPINESS"]) + " surplus happiness, +" + PyG(def["GOLDEN_AGE_EACH_GA_ADDITIONAL_HAPPINESS"])
				+ " for each earlier one and +" + PyG(def["GOLDEN_AGE_THRESHOLD_CITY_MULTIPLIER"] * 100) + "% per city; it lasts " + PyG(def["GOLDEN_AGE_LENGTH"]) + " turns"
				+ (def.count("GOLDEN_AGE_CULTURE_MODIFIER") ? " and adds " + PyG(def["GOLDEN_AGE_CULTURE_MODIFIER"]) + "% culture." : "."));
		if(HasAll(def, "FOOD_CONSUMPTION_PER_POPULATION"))
		{
			Strings settlers = DefaultsOf("select u.Type as t from Units u join UnitClasses c on c.Type=u.Class and c.DefaultUnit=u.Type where u.Food=1");
			m_out.push_back("Food: each citizen eats " + PyG(def["FOOD_CONSUMPTION_PER_POPULATION"]) + "."
				+ (settlers.empty() ? "" : " A city training a " + Join(settlers, " or ") + " does not grow; its surplus food goes into the unit.")
				+ " A city short of food with an empty store loses a citizen.");
		}
		if(HasAll(def, "DEFICIT_UNIT_DISBANDING_THRESHOLD"))
			m_out.push_back("Gold: when the treasury plus income would fall below 0, the treasury stays at 0 and the shortfall comes out of science; a shortfall of "
				+ PyG(-def["DEFICIT_UNIT_DISBANDING_THRESHOLD"]) + " or more disbands units.");
		if(HasAll(def, "SCIENCE_PER_POPULATION TECH_COST_TOTAL_KNOWN_TEAM_MODIFIER"))
		{
			std::string line = "Science: each citizen makes " + PyG(def["SCIENCE_PER_POPULATION"]) + ". Research on a technology runs up to "
				+ PyG(def["TECH_COST_TOTAL_KNOWN_TEAM_MODIFIER"]) + "% faster in proportion to how many civilizations you have met already know it.";
			if(!world.empty())
				line += " Each city raises technology costs " + PyG(FieldOr(world, "NumCitiesTechCostMod", 0)) + "%; each city after the first raises policy costs "
					+ PyG(FieldOr(world, "NumCitiesPolicyCostMod", 0)) + "%.";
			m_out.push_back(line);
		}

		// what the difficulty gives the computer players: the game difficulty's AI columns, and the computer players' own difficulty
		if(handicap.empty()) return;
		Strings parts;
		if(!ai.empty()) parts.push_back("they start with " + PyG(FieldOr(ai, "HappinessDefault", 0)) + " happiness");
		double own = FieldOr(ai, "NumCitiesUnhappinessMod", 100), all = FieldOr(handicap, "AIUnhappinessPercent", 100);
		if(own != 100 || all != 100)
			parts.push_back(own == 100 ? "unhappiness x" + PyG(all / 100)
				: "city and citizen unhappiness x" + PyG(own / 100) + (all == 100 ? "" : ", then all unhappiness x" + PyG(all / 100)));
		if(FieldOr(handicap, "AIGrowthPercent", 100) != 100) parts.push_back("food to grow x" + PyG(FieldOr(handicap, "AIGrowthPercent", 100) / 100));
		if(FieldOr(ai, "ResearchPercent", 100) != 100) parts.push_back("technologies x" + PyG(FieldOr(ai, "ResearchPercent", 100) / 100));
		const char* const BUILDS[][2] = {{"AITrainPercent", "units"}, {"AIConstructPercent", "buildings"}, {"AICreatePercent", "projects"}};
		Strings costs;
		for(size_t i = 0; i < MCP_REF_COUNT(BUILDS); ++i)
			if(FieldOr(handicap, BUILDS[i][0], 100) != 100) costs.push_back(std::string(BUILDS[i][1]) + " x" + PyG(FieldOr(handicap, BUILDS[i][0], 100) / 100));
		double era = FieldOr(handicap, "AIPerEraModifier", 0);
		if(!costs.empty())
			parts.push_back("production for " + Join(costs, ", ")
				+ (era == 0 ? "" : ", and these and growth " + PyG(era < 0 ? -era : era) + "% " + (era < 0 ? "less" : "more") + " each era"));
		Strings upkeep;
		if(FieldOr(ai, "UnitCostPercent", 100) != 100) upkeep.push_back("unit upkeep x" + PyG(FieldOr(ai, "UnitCostPercent", 100) / 100));
		if(FieldOr(ai, "BuildingCostPercent", 100) != 100) upkeep.push_back("building upkeep x" + PyG(FieldOr(ai, "BuildingCostPercent", 100) / 100));
		if(!upkeep.empty()) parts.push_back(Join(upkeep, ", "));
		if(FieldOr(handicap, "AIWorkRateModifier", 0) != 0) parts.push_back("workers +" + PyG(FieldOr(handicap, "AIWorkRateModifier", 0)) + "%");
		Strings extra;
		if(FieldOr(handicap, "AIStartingDefenseUnits", 0) > 0) extra.push_back(PyG(FieldOr(handicap, "AIStartingDefenseUnits", 0)) + " defence");
		if(FieldOr(handicap, "AIStartingWorkerUnits", 0) > 0) extra.push_back(Count(FieldOr(handicap, "AIStartingWorkerUnits", 0), "worker", "workers"));
		if(FieldOr(handicap, "AIStartingExploreUnits", 0) > 0) extra.push_back(Count(FieldOr(handicap, "AIStartingExploreUnits", 0), "explorer", "explorers"));
		double copies = FieldOr(handicap, "AIStartingUnitMultiplier", 0);
		if(copies > 0) parts.push_back(Count(copies, "extra copy", "extra copies") + " of each starting unit" + (extra.empty() ? "" : " plus " + JoinAnd(extra)));
		else if(!extra.empty()) parts.push_back("extra starting units: " + JoinAnd(extra));
		if(FieldOr(handicap, "AIFreeXP", 0) > 0) parts.push_back("new units +" + PyG(FieldOr(handicap, "AIFreeXP", 0)) + " experience");
		if(!parts.empty()) m_out.push_back("Computer players at this difficulty: " + Join(parts, "; ") + ". Human seats, agents included, get none of this.");
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

	// The policies under a heading per branch: the branch's own fields, what adopting and
	// completing it give, then its policies. A policy outside every branch is a mod's hidden marker.
	void Builder::Policies()
	{
		m_out.push_back("\n## Policies");
		std::map<std::string, Strings> needsAll, needsAny;
		Rows rows = Select("select PolicyType, PrereqPolicy from Policy_PrereqPolicies");
		for(size_t i = 0; i < rows.size(); ++i) needsAll[ColText(rows[i], "PolicyType")].push_back(Name(ColText(rows[i], "PrereqPolicy")));
		rows = Select("select PolicyType, PrereqPolicy from Policy_PrereqORPolicies");
		for(size_t i = 0; i < rows.size(); ++i) needsAny[ColText(rows[i], "PolicyType")].push_back(Name(ColText(rows[i], "PrereqPolicy")));
		Rows policies = Select("select * from Policies");
		Rows branches = Select("select * from PolicyBranchTypes");
		StringSet branchSkip;
		branchSkip.insert("FreePolicy");
		branchSkip.insert("FreeFinishingPolicy");
		for(size_t b = 0; b < branches.size(); ++b)
		{
			const McpRefRow& br = branches[b];
			std::string branch = ColText(br, "Type");
			Strings fields = Cols(br, branchSkip, "", false);
			m_out.push_back("### " + Name(branch) + (fields.empty() ? "" : ": " + Join(fields, "; ")));
			std::string opener;
			for(size_t i = 0; i < policies.size(); ++i)
			{
				std::string type = ColText(policies[i], "Type");
				bool bOpener = type == ColText(br, "FreePolicy");
				if(!bOpener && type != ColText(br, "FreeFinishingPolicy")) continue;
				// the text opens with a line about who the branch suits, then what adopting and completing it give
				std::string help = Text(Col(policies[i], "Help"));
				Strings pieces;
				for(size_t from = 0, at; from < help.size(); from = at + 2)
				{
					at = help.find("; ", from);
					if(at == std::string::npos) at = help.size();
					pieces.push_back(help.substr(from, at - from));
				}
				size_t first = 0;
				while(first < pieces.size() && !StartsWith(pieces[first], "Adopting")) ++first;
				if(first == pieces.size()) first = 0;
				std::string text = Join(Strings(pieces.begin() + first, pieces.end()), " ");
				if(text.empty() || text == opener) continue;
				if(bOpener) opener = text;
				m_out.push_back(bOpener ? text : "Completing it: " + text);
			}
			for(size_t i = 0; i < policies.size(); ++i)
			{
				const McpRefRow& p = policies[i];
				if(ColText(p, "PolicyBranchType") != branch) continue;
				std::string type = ColText(p, "Type"), name;
				bool bNamed = Lookup(type, name);
				std::string help = StripName(Text(Col(p, "Help")), bNamed ? name : "");
				if(help.empty()) continue;
				Strings notes;
				if(Number(Col(p, "Level")) > 0) notes.push_back("level " + ColText(p, "Level"));
				if(!needsAll[type].empty()) notes.push_back("after " + Join(needsAll[type], " and "));
				if(!needsAny[type].empty()) notes.push_back("after " + Join(needsAny[type], " or "));
				m_out.push_back((bNamed ? name : "None") + (notes.empty() ? "" : " (" + Join(notes, "; ") + ")") + ": " + help);
			}
		}
	}

	// The beliefs under a heading per kind, in the order the kinds come in a game.
	void Builder::Beliefs()
	{
		m_out.push_back("\n## Beliefs");
		Rows rows = Select("select * from Beliefs");
		Strings headings;
		std::map<std::string, Strings> lines;
		for(size_t i = 0; i < rows.size(); ++i)
		{
			Strings kinds;
			for(size_t k = 0; k < MCP_REF_COUNT(BELIEF_KINDS); ++k)
				if(Truthy(Col(rows[i], BELIEF_KINDS[k]))) kinds.push_back(BELIEF_KINDS[k]);
			std::string heading = kinds.empty() ? "Other" : Join(kinds, ", ");
			if(!lines.count(heading)) headings.push_back(heading);
			lines[heading].push_back(Text(Col(rows[i], "ShortDescription")) + ": " + Text(Col(rows[i], "Description")));
		}
		Strings ordered;
		for(size_t k = 0; k < MCP_REF_COUNT(BELIEF_KINDS); ++k)
			if(lines.count(BELIEF_KINDS[k])) ordered.push_back(BELIEF_KINDS[k]);
		for(size_t h = 0; h < headings.size(); ++h)
			if(std::find(ordered.begin(), ordered.end(), headings[h]) == ordered.end()) ordered.push_back(headings[h]);
		for(size_t h = 0; h < ordered.size(); ++h)
		{
			m_out.push_back("### " + ordered[h]);
			m_out.insert(m_out.end(), lines[ordered[h]].begin(), lines[ordered[h]].end());
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
		// the unit types each promotion is for, and every type any promotion is for, in order
		std::map<std::string, Strings> combats, needs;
		Strings allCombats;
		rows = Select("select PromotionType, UnitCombatType from UnitPromotions_UnitCombats");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			Strings& who = combats[ColText(rows[i], "PromotionType")];
			std::string combat = ColText(rows[i], "UnitCombatType");
			if(std::find(who.begin(), who.end(), combat) == who.end()) who.push_back(combat);
			if(std::find(allCombats.begin(), allCombats.end(), combat) == allCombats.end()) allCombats.push_back(combat);
		}
		StringSet fieldSkip, shown;
		fieldSkip.insert("CannotBeChosen");
		rows = Select("select * from UnitPromotions");
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefRow& r = rows[i];
			std::string type = ColText(r, "Type");
			Strings& after = needs[type];
			for(size_t c = 0; c < r.size(); ++c)
			{
				if(!StartsWith(r[c].first, "PromotionPrereq")) continue;
				fieldSkip.insert(r[c].first);
				if(Truthy(&r[c].second)) after.push_back(PyStr(r[c].second));
			}
			if(!Truthy(Col(r, "CannotBeChosen")) || free.count(type) || Referenced(type)) shown.insert(type);
		}
		Rows ors = Select("select * from UnitPromotions_PromotionPrereqOrs");
		for(size_t i = 0; i < ors.size(); ++i)
			if(ors[i].size() >= 2) needs[PyStr(ors[i][0].second)].push_back(PyStr(ors[i][1].second));
		// a promotion that needs others is listed only when one of them can be had in this game
		for(bool bChanged = true; bChanged;)
		{
			bChanged = false;
			for(StringSet::iterator it = shown.begin(); it != shown.end();)
			{
				const Strings& after = needs[*it];
				bool bReachable = after.empty() || free.count(*it) || Referenced(*it);
				for(size_t k = 0; k < after.size() && !bReachable; ++k) bReachable = shown.count(after[k]) > 0;
				if(bReachable) ++it;
				else
				{
					shown.erase(it++);
					bChanged = true;
				}
			}
		}

		std::vector<PromotionLine> lines;
		StringMap said; // what a promotion does, and whose it was first
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const McpRefRow& r = rows[i];
			std::string type = ColText(r, "Type");
			if(!shown.count(type)) continue;
			Strings after;
			for(size_t k = 0; k < needs[type].size(); ++k)
			{
				std::string n = Name(needs[type][k]);
				if(std::find(after.begin(), after.end(), n) == after.end()) after.push_back(n);
			}
			PromotionLine l;
			bool bNamed = Lookup(type, l.name);
			if(!bNamed) l.name = "None";
			l.what = StripName(Text(Col(r, "Help")), bNamed ? l.name : "");
			if(l.what.empty())
			{
				Strings fields = Cols(r, fieldSkip, "UnitPromotions", false);
				fields.insert(fields.end(), related[type].begin(), related[type].end());
				l.what = Join(fields, "; ");
			}
			const Strings& types = combats[type];
			Strings names, missing;
			for(size_t k = 0; k < types.size(); ++k) names.push_back(Replace(Name(types[k]), " Units", ""));
			for(size_t k = 0; k < allCombats.size(); ++k)
				if(std::find(types.begin(), types.end(), allCombats[k]) == types.end()) missing.push_back(allCombats[k]);
			if(types.empty()) l.who = "";
			else if(missing.empty()) l.who = "all";
			else if(missing.size() <= 3 && types.size() > missing.size()) l.who = "all but " + Join(Distinct(missing, names), ", ");
			else l.who = Join(names, ", ");
			l.after = Join(after, " or ");
			std::string key = l.who + "|" + l.what;
			StringMap::const_iterator it = said.find(key);
			std::string does = l.what;
			if(it != said.end())
			{
				if(it->second == l.name) continue; // the same promotion defined twice
				does = "same as " + it->second;
			}
			else if(!l.what.empty()) said[key] = l.name;
			l.text = l.name + (l.who.empty() ? "" : " [" + l.who + "]") + (after.empty() ? "" : " (after " + l.after + ")") + ": " + does;
			lines.push_back(l);
		}
		// tiers of one promotion that each need only the one before and do the same go on one line
		for(size_t i = 0; i < lines.size(); ++i)
		{
			const PromotionLine& l = lines[i];
			size_t tiers = 1;
			std::string stem = l.name.size() > 2 ? l.name.substr(0, l.name.size() - 2) : "";
			if(l.name.size() > 2 && l.name.compare(l.name.size() - 2, 2, " I") == 0)
				while(i + tiers < lines.size() && tiers < MCP_REF_COUNT(ROMAN))
				{
					const PromotionLine& n = lines[i + tiers];
					if(n.name != stem + " " + ROMAN[tiers] || n.who != l.who || n.after != stem + " " + ROMAN[tiers - 1] || n.what != l.what) break;
					++tiers;
				}
			if(tiers == 1)
			{
				m_out.push_back(l.text);
				continue;
			}
			Strings levels(ROMAN, ROMAN + tiers);
			m_out.push_back(stem + " " + Join(levels, ", ") + (l.who.empty() ? "" : " [" + l.who + "]") + " ("
				+ (l.after.empty() ? "" : "after " + l.after + "; ") + "each after the one before): " + l.what);
			i += tiers - 1;
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
				if(name.empty()) name = Pretty(ColText(rows[i], "Type"));
				name = name.substr(0, name.find('|'));
				if(std::string(NAME_TABLES[t]) == "Builds")
				{
					// a build is named by what it makes: Construct a Farm -> Farm
					const char* const VERBS[] = {"Construct an ", "Construct a ", "Create "};
					for(size_t v = 0; v < MCP_REF_COUNT(VERBS); ++v)
						if(StartsWith(name, VERBS[v]))
						{
							name = name.substr(strlen(VERBS[v]));
							break;
						}
				}
				m_names[ColText(rows[i], "Type")] = name;
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
		Rules();

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
		FoldBuilds();
		Section("Improvements", "Improvements", "Improvement_", "ImprovementType", PLAIN, StringSet());
		m_extra.clear();
		Section("Worker builds", "Builds", "Build", "BuildType", BUILDS, StringSet());
		Section("Resources", "Resources", "Resource_", "ResourceType", RESOURCES, StringSet());
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
		Strings labels;
		for(size_t i = 0; i < m_childLabels.size(); ++i)
			for(size_t j = 0; j < MCP_REF_COUNT(CHILD_SHORT); ++j)
				if(m_childLabels[i] == CHILD_SHORT[j][1]) labels.push_back(m_childLabels[i] + " " + CHILD_SHORT[j][2]);
		if(!labels.empty()) legend += " Short table names: " + Join(labels, ", ") + ". Targets sharing a value are written together: A/B/C: 5s, all: 15, all but X: 15.";
		m_out.insert(m_out.begin() + 2, legend);
		return Join(m_out, "\n");
	}
}

std::string McpBuildGameReference(McpRefSource& source, const std::vector<std::string>& civs, const McpRefSettings& settings)
{
	Builder builder(source, civs, settings);
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
	McpRefSettings settings;
	CvHandicapInfo* pHandicap = GC.getHandicapInfo(GC.getGame().getHandicapType());
	if(pHandicap) settings.handicap = pHandicap->GetType();
	settings.worldSize = GC.getMap().getWorldInfo().GetType();
	GameDatabaseSource source;
	return McpBuildGameReference(source, civs, settings);
}
#endif
