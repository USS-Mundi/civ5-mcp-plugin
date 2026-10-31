"""Quizzes a model on the rules of Civilization V: Brave New World (base game, both expansions, no mods) and scores its
answers against the game's own data. Numbers, names, lists and yields are checked by rule; effect descriptions get a
rough text match, plus an optional meaning review by a model.

    python tools/agent_quiz.py build "<Civilization V>/Assets"   the answer key, from the game's XML
    python tools/agent_quiz.py list                              every field, with a count and an example question
    python tools/agent_quiz.py make NAME --set sampler           or --pick 'unit.combat.* 20' --pick 'tech.cost.*'
    python tools/agent_quiz.py score NAME [--review FILE]

The key and the quizzes go in tools/agent-quiz/. `make` writes runs/NAME/sheet.md there. Give it to the model and save
its reply as answers.txt beside it. With its tools off, Claude can't open the key; run it from an empty folder so no
project instructions load either:

    claude -p --model sonnet --tools "" --strict-mcp-config < sheet.md > answers.txt

Add --append-system-prompt-file game-reference.txt to test with the game reference. Sets: sampler is two questions
from every field and five global rules (166); numbers is five from every field whose answer is a number (113). Costs
are at Standard speed. Scoring reads the current key, so a corrected key also re-scores old quizzes.
"""
import argparse, collections, datetime, difflib, fnmatch, json, os, random, re, sqlite3, sys, unicodedata
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "agent-quiz")
KEY = os.path.join(DATA, "key.json")
RUNS = os.path.join(DATA, "runs")
SETS = {
    # two questions from every field and five global rules
    "sampler": [
        "tech.cost.* 2", "tech.era.* 2", "tech.prereqs.* 2", "tech.leads_to.* 2", "building.cost.* 2",
        "building.tech.* 2", "building.maintenance.* 2", "building.yields.* 2", "building.requires.* 2",
        "building.happiness.* 2", "building.great_works.* 2", "building.replaces.* 2", "building.yield_bonus.* 2",
        "building.specialists.* 2", "wonder.kind.* 2", "wonder.cost.* 2", "wonder.tech.* 2", "wonder.yields.* 2",
        "wonder.policy.* 2", "wonder.effect.* 2", "building.defense.* 2", "building.hit_points.* 2",
        "building.per_citizen.* 2", "building.resources.* 2", "building.faith_cost.* 2",
        "wonder.needs_everywhere.* 2", "unit.cost.* 2", "unit.tech.* 2", "unit.combat.* 2", "unit.moves.* 2",
        "unit.obsolete.* 2", "unit.upgrade.* 2", "unit.type.* 2", "unit.resources.* 2", "unit.replaces.* 2",
        "unit.ranged.* 2", "unit.range.* 2", "unit.faith_cost.* 2", "branch.era.* 2", "branch.opener.* 2",
        "branch.finisher.* 2", "policy.branch.* 2", "policy.prereqs.* 2", "policy.effect.* 2", "policy.level.* 2",
        "belief.kind.* 2", "belief.effect.* 2", "promotion.prereqs.* 2", "promotion.effect.* 2",
        "promotion.units.* 2", "improvement.yields.* 2", "improvement.resources.* 2", "improvement.tech.* 2",
        "improvement.tech_bonus.* 2", "improvement.resource_yields.* 2", "improvement.defense.* 2",
        "resource.class.* 2", "resource.yields.* 2", "resource.reveal.* 2", "resource.improvement.* 2",
        "resource.happiness.* 2", "terrain.yields.* 2", "terrain.moves.* 2", "terrain.defense.* 2",
        "natural.yields.* 2", "natural.happiness.* 2", "feature.yields.* 2", "feature.moves.* 2",
        "feature.defense.* 2", "natural.effect.* 2", "specialist.yields.* 2", "specialist.gpp.* 2",
        "specialist.great_person.* 2", "civ.leader.* 2", "civ.ability_name.* 2", "civ.ability.* 2",
        "civ.units.* 2", "civ.buildings.* 2", "civ.improvements.* 2", "project.cost.* 2", "project.tech.* 2",
        "rule.value.* 5"],
    # five questions from every field whose answer is a number
    "numbers": [
        "tech.cost.* 5", "building.cost.* 5", "building.maintenance.* 5", "building.happiness.* 5",
        "wonder.cost.* 5", "building.defense.* 5", "building.hit_points.* 5", "building.faith_cost.* 5",
        "unit.cost.* 5", "unit.combat.* 5", "unit.moves.* 5", "unit.ranged.* 5", "unit.range.* 5",
        "unit.faith_cost.* 5", "policy.level.* 5", "improvement.defense.* 5", "resource.happiness.* 5",
        "terrain.moves.* 5", "terrain.defense.* 5", "natural.happiness.* 5", "feature.moves.* 5",
        "feature.defense.* 5", "specialist.gpp.* 5", "project.cost.* 5", "rule.value.* 5"]}

ABSTAIN = {"", "?", "unknown", "idk", "dont know", "do not know", "not sure", "no idea"}
NONE = {"none", "no", "nothing", "n a", "na", "zero", "0"}
ALIASES = {"hammers": "production", "hammer": "production", "beakers": "science", "research": "science",
           "literature": "writing", "written": "writing", "artifact": "art", "art artifact": "art",
           "art or artifact": "art", "artwork": "art", "great work of writing": "writing", "great work of art": "art",
           "great work of music": "music"}
STOP = set("the a an of to and or in on for with by from at is are be it its this that each every all your you per as "
           "when while which who than then into their there gives grants provides receive receives can will also".split())

SHEET = """# Civilization V knowledge quiz

Answer every question from what you already know about Civilization V: Brave New World, the base game with both expansions and no mods. Do not search, open files or call tools.

Numbers are the game's own values at Standard speed, before map size, difficulty, policies or other bonuses. If you don't know an answer, write `?`.

## Answer format

Reply with one line per question and nothing else:

Q0001 | <answer>

Each question names the shape of its answer:

- number: a number only, e.g. `75`. A percentage without the % sign; a penalty as a negative number.
- name: one name, or `none`.
- names: names separated by `;` in any order, or `none`.
- amounts: `<number> <name>` pairs separated by `;`, e.g. `2 Food; 1 Gold`, or `none`. Yields are Food, Production, Gold, Science, Culture and Faith.
- text: one line stating the effect, with its numbers.

## Questions ({count})

"""

REVIEW = """# Meaning review

For each item below, compare the answer with the game's text. Judge whether it states the same effect with the same numbers.

Reply with one line per item and nothing else, using `correct`, `partial` or `wrong`:

Q0001 | correct

"""


def load_key():
    if not os.path.exists(KEY):
        sys.exit("no answer key yet; build it first: agent_quiz.py build \"<Civilization V>/Assets\"")
    return json.load(open(KEY, encoding="utf-8"))["questions"]



def load_game(assets):
    """Rebuilds the Brave New World ruleset (base game, Gods and Kings, Brave New World and the civ packs) from the
    game's XML into SQLite, the way the game's loader does: each file name's highest-priority copy wins, then every
    table, row, update and delete is applied. Returns the game database and the English text by tag."""
    A = assets
    if not os.path.isdir(os.path.join(A, "DLC", "Expansion2")):
        sys.exit("%s has no DLC/Expansion2: give the Assets folder of a Brave New World install" % A)
    layers = [(0, ["Gameplay"]),
              (50, ["DLC/%s/Gameplay" % d for d in ("DLC_01", "DLC_02", "DLC_03", "DLC_04", "DLC_05", "DLC_06", "DLC_07", "DLC_Deluxe")] + ["DLC/Shared/Gameplay"]),
              (100, ["DLC/Expansion/Gameplay", "DLC/Expansion/DLC"]),
              (200, ["DLC/Expansion2/Gameplay", "DLC/Expansion2/DLC"])]
    winner = {}
    for prio, dirs in layers:
        for d in dirs:
            root = os.path.join(A, *d.split("/"))
            for r, _, fs in os.walk(root):
                if any(x in r.lower() for x in ("scenario", "tutorial", "worldbuilder")): continue
                parts = r.split(os.sep)
                if any(parts[i - 1].lower() in ("text", "newtext") and parts[i].lower() != "en_us" for i in range(1, len(parts))): continue
                for f in fs:
                    if f.lower().endswith(".xml"): winner[f.lower()] = (prio, os.path.join(r, f))
    files = sorted(winner.values())
    # Text is merged per package, never overridden by file name: every English text file of every layer, in priority order.
    text_files = []
    for prio, dirs in layers:
        for d in dirs:
            for r, _, fs in os.walk(os.path.join(A, *d.split("/"))):
                parts = r.split(os.sep)
                if any(x in r.lower() for x in ("scenario", "tutorial", "worldbuilder")): continue
                if not any(p.lower() == "en_us" for p in parts): continue
                text_files += [(prio, os.path.join(r, f)) for f in sorted(fs) if f.lower().endswith(".xml")]
    db = sqlite3.connect(":memory:"); loc = sqlite3.connect(":memory:"); loc.execute("create table Language_en_US (Tag text primary key, Text text)")
    schema = collections.OrderedDict(); trees = []
    for prio, path in files:
        try: root = ET.parse(path).getroot()
        except ET.ParseError: continue
        if root.tag != "GameData": continue
        trees.append(root)
        for t in root.findall("Table"):
            cols = schema.setdefault(t.get("name"), collections.OrderedDict())
            for c in t.findall("Column"): cols.setdefault(c.get("name"), (c.get("type", "text"), c.get("default")))
    for name, cols in schema.items():
        defs = []
        for c, (typ, dflt) in cols.items():
            d = "" if (dflt is None or dflt.strip().upper() == "NULL") else " default %s" % ({"true": "1", "false": "0"}.get(dflt.lower(), "'%s'" % dflt.replace("'", "''")) if typ != "integer" else dflt)
            defs.append('"%s" %s%s' % (c, typ, d))
        db.execute('create table if not exists "%s" (%s)' % (name, ", ".join(defs)))
        if "Type" in cols: db.execute('create unique index if not exists "u_%s" on "%s" (Type)' % (name, name))
    def conv(table, col, v):
        typ = schema.get(table, {}).get(col, ("text", None))[0]
        if v is None or v.strip().upper() == "NULL": return None
        if typ == "boolean": return 1 if v.strip().lower() in ("true", "1") else 0
        if typ == "integer":
            try: return int(v)
            except ValueError: return v
        return v
    def fields(el):
        d = dict(el.attrib)
        for c in el:
            if len(c) == 0: d[c.tag] = (c.text or "").strip()
        return d
    rows = upd = dele = 0
    failed = collections.Counter()
    for root in trees:
        for t in root:
            name = t.tag
            if name == "Table": continue
            if name.startswith("Language_"):
                if name != "Language_en_US": continue
                for r in t.findall("Row") + t.findall("Replace"):
                    f = fields(r); tag = f.get("Tag"); txt = f.get("Text")
                    if tag: loc.execute("insert or replace into Language_en_US values (?, ?)", (tag, txt))
                continue
            if name not in schema: continue
            cols = schema[name]
            for op in t:
                if op.tag in ("Row", "Replace", "InsertOrAbort"):
                    f = {k: conv(name, k, v) for k, v in fields(op).items() if k in cols}
                    if not f: continue
                    verb = "insert" if op.tag == "InsertOrAbort" else "insert or replace" if (op.tag == "Replace" or "Type" in f) else "insert"
                    try: db.execute('%s into "%s" (%s) values (%s)' % (verb, name, ", ".join('"%s"' % k for k in f), ", ".join("?" * len(f))), list(f.values())); rows += 1
                    except sqlite3.Error: failed[op.tag] += 1
                elif op.tag == "Update":
                    w = next((fields(c) for c in op if c.tag == "Where"), None); s = next((fields(c) for c in op if c.tag == "Set"), None)
                    if not s: continue
                    sets = {k: conv(name, k, v) for k, v in s.items() if k in cols}
                    where = {k: conv(name, k, v) for k, v in (w or {}).items() if k in cols}
                    if not sets: continue
                    sql = 'update "%s" set %s' % (name, ", ".join('"%s" = ?' % k for k in sets))
                    if where: sql += " where " + " and ".join('"%s" = ?' % k for k in where)
                    try: db.execute(sql, list(sets.values()) + list(where.values())); upd += 1
                    except sqlite3.Error: pass
                elif op.tag == "Delete":
                    w = {k: conv(name, k, v) for k, v in fields(op).items() if k in cols}
                    sql = 'delete from "%s"' % name + ((" where " + " and ".join('"%s" = ?' % k for k in w)) if w else "")
                    try: db.execute(sql, list(w.values())); dele += 1
                    except sqlite3.Error: pass
    for prio, path in sorted(text_files):
        try: root = ET.parse(path).getroot()
        except ET.ParseError: continue
        for t in root:
            if t.tag != "Language_en_US": continue
            for r in t.findall("Row") + t.findall("Replace"):
                f = fields(r)
                if f.get("Tag"): loc.execute("insert or replace into Language_en_US values (?, ?)", (f["Tag"], f.get("Text")))
    db.commit()
    print("read %d files: %d rows, %d updates, %d deletes, %d rows rejected" % (len(files), rows, upd, dele, sum(failed.values())))
    if db.execute("select count(*) from Resources where Type = 'RESOURCE_COFFEE'").fetchone()[0]:
        sys.exit("the files did not load as Brave New World: a resource it removes is still there")
    return db, dict(loc.execute("select Tag, Text from Language_en_US"))


def make_key(db, TEXT):
    """The quiz's questions and answers from the game's rules, and the ids dropped because two subjects share a name
    but not an answer."""
    db.row_factory = sqlite3.Row

    def rows(sql, *args):
        return db.execute(sql, args).fetchall()


    def define(name):
        return rows("select Value from Defines where Name = ?", name)[0][0]


    def loc(tag):
        s = TEXT.get(tag, tag) if tag else ""
        for _ in range(3):
            s = re.sub(r"\{(TXT_KEY_[A-Z0-9_]+)\}", lambda m: TEXT.get(m.group(1), m.group(0)), s)
        return s


    ICONS = {"GOLD": "Gold", "PRODUCTION": "Production", "FOOD": "Food", "RESEARCH": "Science", "CULTURE": "Culture",
             "PEACE": "Faith", "HAPPINESS_1": "Happiness", "HAPPINESS_3": "Unhappiness", "HAPPINESS_4": "Unhappiness",
             "STRENGTH": "Combat Strength", "RANGE_STRENGTH": "Ranged Combat Strength", "MOVES": "Movement",
             "GREAT_PEOPLE": "Great Person", "GOLDEN_AGE": "Golden Age", "TOURISM": "Tourism", "INFLUENCE": "Influence",
             "INTERNATIONAL_TRADE": "Trade Route", "TRADE": "Trade Route", "CITIZEN": "Citizen", "GREAT_WORK": "Great Work",
             "SPY": "Spy", "DIPLOMAT": "Diplomat", "CITY_STATE": "City-State", "RELIGION": "Religion",
             "GREAT_GENERAL": "Great General", "GREAT_ADMIRAL": "Great Admiral", "WORKER": "Worker",
             "GREAT_EXPLORER": "Great Explorer", "OCCUPIED": "Occupied", "PUPPET": "Puppet", "CAPITAL": "Capital",
             "RESISTANCE": "Resistance", "RAZING": "Razing", "WAR": "War", "PIRATE": "Plunder", "VP_DIPLOMAT": "Diplomat"}
    RES_ICONS = {r["IconString"].strip("[]")[5:]: loc(r["Description"]) for r in rows("select IconString, Description from Resources") if r["IconString"]}


    def stem(w):
        return w.lower().rstrip("s")


    def clean(tag):
        s = loc(tag).replace("[NEWLINE]", " ").replace("[TAB]", " ")

        def icon(m):
            word = RES_ICONS.get(m.group(1)) or ICONS.get(m.group(1), "")
            nxt = re.match(r"\s*([A-Za-z]+)", m.string[m.end():])
            if not word or (nxt and stem(nxt.group(1))[:4] in {stem(w)[:4] for w in word.split()}):
                return " "
            return " %s " % word

        s = re.sub(r"\[ICON_([A-Z0-9_]+)\]", icon, s)
        s = re.sub(r"(\w)\|\w+", r"\1", s)
        s = re.sub(r"\[[^\]]*\]", " ", s)
        s = re.sub(r"\s+", " ", s).strip()
        s = re.sub(r"\b(\w+) \1\b", r"\1", s)
        return re.sub(r"\s+([.,;:%)])", r"\1", s).replace("( ", "(")


    def slug(s):
        s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()
        return re.sub(r"^the_", "", re.sub(r"[^a-z0-9]+", "_", s).strip("_"))


    YIELDS = {"YIELD_FOOD": "Food", "YIELD_PRODUCTION": "Production", "YIELD_GOLD": "Gold", "YIELD_SCIENCE": "Science",
              "YIELD_CULTURE": "Culture", "YIELD_FAITH": "Faith"}


    def yields(sql, divisor=1):
        out = collections.defaultdict(dict)
        for r in rows(sql):
            if r[2]:
                v = r[2] / divisor
                out[r[0]][YIELDS[r[1]]] = int(v) if v == int(v) else v
        return out


    def named(table, col="Description"):
        return {r["Type"]: clean(r[col]) for r in rows('select Type, "%s" from "%s"' % (col, table))}


    TECH = named("Technologies")
    ERA = named("Eras")
    UNIT = named("Units")
    BUILDING = named("Buildings")
    POLICY = named("Policies")
    PROMOTION = named("UnitPromotions")
    RESOURCE = named("Resources")
    IMPROVEMENT = named("Improvements")
    SPECIALIST = named("Specialists")
    COMBAT = {t: re.sub(r" Units$", "", n) for t, n in named("UnitCombatInfos").items()}
    CLASS_UNIT = {r["Type"]: r["DefaultUnit"] for r in rows("select Type, DefaultUnit from UnitClasses")}
    CLASS_BUILDING = {r["Type"]: r["DefaultBuilding"] for r in rows("select Type, DefaultBuilding from BuildingClasses")}
    GW_SLOT = {"GREAT_WORK_SLOT_ART_ARTIFACT": "Art", "GREAT_WORK_SLOT_LITERATURE": "Writing", "GREAT_WORK_SLOT_MUSIC": "Music"}
    RES_CLASS = {"RESOURCECLASS_BONUS": "Bonus", "RESOURCECLASS_LUXURY": "Luxury", "RESOURCECLASS_RUSH": "Strategic",
                 "RESOURCECLASS_MODERN": "Strategic"}

    MAJORS = [r["Type"] for r in rows("select Type from Civilizations where Playable = 1 order by Type")]
    UU = {}  # unique unit -> civ
    for r in rows("select CivilizationType, UnitClassType, UnitType from Civilization_UnitClassOverrides where UnitType is not null"):
        if r["CivilizationType"] in MAJORS and r["UnitType"] != CLASS_UNIT.get(r["UnitClassType"]):
            UU[r["UnitType"]] = r["CivilizationType"]
    NON_MAJOR_UNITS = {r["UnitType"] for r in rows("select CivilizationType, UnitType from Civilization_UnitClassOverrides where UnitType is not null")
                       if r["CivilizationType"] not in MAJORS}
    UB = {}
    for r in rows("select CivilizationType, BuildingClassType, BuildingType from Civilization_BuildingClassOverrides where BuildingType is not null"):
        if r["CivilizationType"] in MAJORS and r["BuildingType"] != CLASS_BUILDING.get(r["BuildingClassType"]):
            UB[r["BuildingType"]] = r["CivilizationType"]
    UI = {r["Type"]: r["CivilizationType"] for r in rows("select Type, CivilizationType from Improvements where CivilizationType is not null")}

    QUESTIONS = []


    def ask(section, field, subject, prompt, kind, answer, **extra):
        q = dict(id="%s.%s.%s" % (section, field, slug(subject)), section=section, field=field, subject=subject,
                 prompt=prompt.replace("{n}", subject), type=kind, answer=answer)
        q.update(extra)
        QUESTIONS.append(q)


    def names(types, table):
        return sorted({table[t] for t in types if t and t in table})


    # Technologies
    tech_prereqs = collections.defaultdict(set)
    for r in rows("select TechType, PrereqTech from Technology_PrereqTechs"):
        tech_prereqs[r["TechType"]].add(r["PrereqTech"])
    for r in rows("select Type, Cost, Era from Technologies"):
        n = TECH[r["Type"]]
        ask("tech", "cost", n, "{n}: research cost", "number", r["Cost"])
        ask("tech", "era", n, "{n}: era", "name", ERA[r["Era"]])
        ask("tech", "prereqs", n, "{n}: technologies it requires", "names", names(tech_prereqs[r["Type"]], TECH))
        ask("tech", "leads_to", n, "{n}: technologies that require it", "names",
            names([t for t, ps in tech_prereqs.items() if r["Type"] in ps], TECH))

    # Buildings and wonders
    classes = {r["Type"]: r for r in rows("select * from BuildingClasses")}
    b_yields = yields("select BuildingType, YieldType, Yield from Building_YieldChanges")
    b_mods = yields("select BuildingType, YieldType, Yield from Building_YieldModifiers")
    b_perpop = yields("select BuildingType, YieldType, Yield from Building_YieldChangesPerPop", 100)
    b_needed = collections.defaultdict(set)
    for r in rows("select BuildingType, BuildingClassType from Building_ClassesNeededInCity"):
        b_needed[r["BuildingType"]].add(CLASS_BUILDING[r["BuildingClassType"]])
    b_everywhere = collections.defaultdict(set)
    for r in rows("select BuildingType, BuildingClassType from Building_PrereqBuildingClasses where NumBuildingNeeded = -1"):
        b_everywhere[r["BuildingType"]].add(CLASS_BUILDING[r["BuildingClassType"]])
    b_res = collections.defaultdict(dict)
    for r in rows("select BuildingType, ResourceType, Cost from Building_ResourceQuantityRequirements"):
        b_res[r["BuildingType"]][RESOURCE[r["ResourceType"]]] = r["Cost"]
    branch_name = named("PolicyBranchTypes")
    for b in rows("select * from Buildings"):
        t, n, c = b["Type"], BUILDING[b["Type"]], classes[b["BuildingClass"]]
        world, national = c["MaxGlobalInstances"] == 1, c["MaxPlayerInstances"] == 1 or c["MaxTeamInstances"] == 1
        if world or national:
            ask("wonder", "kind", n, "{n}: world wonder or national wonder", "name", "World" if world else "National")
            if b["Cost"] > 0:
                ask("wonder", "cost", n, "{n}: Production cost", "number", b["Cost"])
                ask("wonder", "tech", n, "{n}: technology that unlocks it", "name", TECH.get(b["PrereqTech"], "none"))
            ask("wonder", "yields", n, "{n}: yields it adds to its city", "amounts", b_yields.get(t, {}))
            if b["PolicyBranchType"]:
                ask("wonder", "policy", n, "{n}: policy branch it requires", "name", branch_name[b["PolicyBranchType"]])
            if b_everywhere.get(t):
                ask("wonder", "needs_everywhere", n, "{n}: building every city must have first", "names", names(b_everywhere[t], BUILDING))
            help_text = clean(b["Help"])
            if help_text:
                ask("wonder", "effect", n, "{n}: effect", "text", help_text)
            continue
        if b["Cost"] <= 0 and b["FaithCost"] <= 0:
            continue
        if b["Cost"] > 0:
            ask("building", "cost", n, "{n}: Production cost", "number", b["Cost"])
            ask("building", "tech", n, "{n}: technology that unlocks it", "name", TECH.get(b["PrereqTech"], "none"))
        else:
            ask("building", "faith_cost", n, "{n}: Faith cost", "number", b["FaithCost"])
        ask("building", "maintenance", n, "{n}: Gold maintenance per turn", "number", b["GoldMaintenance"])
        ask("building", "yields", n, "{n}: flat yields it adds to the city", "amounts", b_yields.get(t, {}))
        ask("building", "requires", n, "{n}: buildings the city must already have", "names", names(b_needed[t], BUILDING))
        if t in b_mods:
            ask("building", "yield_bonus", n, "{n}: percentage yield bonuses to the city, as `<percent> <yield>`", "amounts", b_mods[t])
        if t in b_perpop:
            ask("building", "per_citizen", n, "{n}: yield per citizen in the city, as `<amount per citizen> <yield>`", "amounts", b_perpop[t])
        happy = (b["Happiness"] or 0) + (b["UnmoddedHappiness"] or 0)
        if happy:
            ask("building", "happiness", n, "{n}: Happiness it provides", "number", happy)
        if b["Defense"]:
            ask("building", "defense", n, "{n}: city Defense it adds", "number", b["Defense"] // 100 if b["Defense"] % 100 == 0 else b["Defense"] / 100)
        if b["ExtraCityHitPoints"]:
            ask("building", "hit_points", n, "{n}: city hit points it adds", "number", b["ExtraCityHitPoints"])
        if b["SpecialistCount"]:
            ask("building", "specialists", n, "{n}: specialist slots, as `<count> <specialist>`", "amounts",
                {SPECIALIST[b["SpecialistType"]]: b["SpecialistCount"]})
        if b["GreatWorkCount"] and b["GreatWorkCount"] > 0:
            ask("building", "great_works", n, "{n}: Great Work slots, as `<count> <Art|Writing|Music>`", "amounts",
                {GW_SLOT[b["GreatWorkSlotType"]]: b["GreatWorkCount"]})
        if t in b_res:
            ask("building", "resources", n, "{n}: strategic resources it needs, as `<count> <resource>`", "amounts", b_res[t])
        if t in UB:
            ask("building", "replaces", n, "{n} (unique building): the building it replaces", "name",
                BUILDING[CLASS_BUILDING[b["BuildingClass"]]])

    # Units
    u_upgrade = {r["UnitType"]: CLASS_UNIT[r["UnitClassType"]] for r in rows("select UnitType, UnitClassType from Unit_ClassUpgrades")}
    u_res = collections.defaultdict(dict)
    for r in rows("select UnitType, ResourceType, Cost from Unit_ResourceQuantityRequirements"):
        u_res[r["UnitType"]][RESOURCE[r["ResourceType"]]] = r["Cost"]
    for u in rows("select * from Units"):
        t, n = u["Type"], UNIT[u["Type"]]
        if t in NON_MAJOR_UNITS and t not in UU:
            continue
        military = u["Combat"] > 0 or u["RangedCombat"] > 0
        if u["Cost"] > 0:
            ask("unit", "cost", n, "{n}: Production cost", "number", u["Cost"])
            ask("unit", "tech", n, "{n}: technology that unlocks it", "name", TECH.get(u["PrereqTech"], "none"))
        elif u["FaithCost"] > 0 and not u["RequiresFaithPurchaseEnabled"]:
            ask("unit", "faith_cost", n, "{n}: Faith cost", "number", u["FaithCost"])
        if u["Combat"] > 0:
            ask("unit", "combat", n, "{n}: combat strength", "number", u["Combat"])
        if u["RangedCombat"] > 0:
            ask("unit", "ranged", n, "{n}: ranged combat strength", "number", u["RangedCombat"])
            if u["Range"] > 0:
                ask("unit", "range", n, "{n}: attack range in tiles", "number", u["Range"])
        if u["Domain"] != "DOMAIN_AIR":
            ask("unit", "moves", n, "{n}: movement points", "number", u["Moves"])
        if u["ObsoleteTech"]:
            ask("unit", "obsolete", n, "{n}: technology that makes it obsolete", "name", TECH[u["ObsoleteTech"]])
        if military:
            ask("unit", "upgrade", n, "{n}: unit it upgrades to", "name", UNIT.get(u_upgrade.get(t), "none"))
            if u["CombatClass"]:
                ask("unit", "type", n, "{n}: unit type (Melee, Mounted, Archery, ...)", "name", COMBAT[u["CombatClass"]])
            if u["Cost"] > 0:
                ask("unit", "resources", n, "{n}: strategic resources it needs, as `<count> <resource>`", "amounts", u_res.get(t, {}))
        if t in UU:
            ask("unit", "replaces", n, "{n} (unique unit): the unit it replaces", "name", UNIT[CLASS_UNIT[u["Class"]]])

    # Policies
    p_prereqs = collections.defaultdict(set)
    for r in rows("select PolicyType, PrereqPolicy from Policy_PrereqPolicies"):
        p_prereqs[r["PolicyType"]].add(r["PrereqPolicy"])


    def effect(tag, name):
        s = clean(tag)
        return s[len(name):].lstrip(" :.-") if s.lower().startswith(name.lower()) else s


    for br in rows("select * from PolicyBranchTypes"):
        bn = branch_name[br["Type"]]
        if br["PurchaseByLevel"]:
            continue
        ask("branch", "era", bn, "{n} policy branch: era it opens in", "name", ERA.get(br["EraPrereq"], ERA["ERA_ANCIENT"]))
        # The branch's help has a paragraph for adopting the branch and one for completing it.
        paragraphs = loc(br["Help"]).split("[NEWLINE][NEWLINE]")
        opener = [p for p in paragraphs if p.startswith("Adopting") and not p.startswith("Adopting all")]
        finisher = [p for p in paragraphs if p.startswith("Adopting all")]
        if len(opener) != 1 or len(finisher) != 1:
            sys.exit("%s: help has no single opener and finisher paragraph" % br["Type"])
        ask("branch", "opener", bn, "{n} policy branch: effect of adopting it", "text", clean(opener[0]))
        ask("branch", "finisher", bn, "{n} policy branch: effect of completing it", "text", clean(finisher[0]))
    ideology = {r["Type"] for r in rows("select Type from PolicyBranchTypes where PurchaseByLevel = 1")}
    for p in rows("select * from Policies where PolicyBranchType is not null"):
        t, n = p["Type"], POLICY[p["Type"]]
        ask("policy", "branch", n, "{n}: policy branch or ideology it belongs to", "name", branch_name[p["PolicyBranchType"]])
        if p["PolicyBranchType"] in ideology:
            ask("policy", "level", n, "{n}: tenet level", "number", p["Level"])
        else:
            ask("policy", "prereqs", n, "{n}: policies required before it", "names", names(p_prereqs[t], POLICY))
        ask("policy", "effect", n, "{n}: effect", "text", effect(p["Help"], n))

    # Beliefs
    for b in rows("select * from Beliefs"):
        n = clean(b["ShortDescription"])
        kind = next(k for k in ("Pantheon", "Founder", "Follower", "Enhancer", "Reformation") if b[k])
        ask("belief", "kind", n, "{n}: belief type (Pantheon, Founder, Follower, Enhancer or Reformation)", "name", kind)
        ask("belief", "effect", n, "{n}: effect", "text", clean(b["Description"]))

    # Promotions
    p_combats = collections.defaultdict(set)
    for r in rows("select PromotionType, UnitCombatType from UnitPromotions_UnitCombats"):
        p_combats[r["PromotionType"]].add(COMBAT[r["UnitCombatType"]])
    prereq_cols = ["PromotionPrereq"] + ["PromotionPrereqOr%d" % i for i in range(1, 10)]
    for p in rows("select * from UnitPromotions where CannotBeChosen = 0"):
        n = PROMOTION[p["Type"]]
        ask("promotion", "prereqs", n, "{n}: promotions that unlock it (any one of them)", "names",
            names([p[c] for c in prereq_cols], PROMOTION))
        if p_combats[p["Type"]]:
            ask("promotion", "units", n, "{n}: unit types that can take it", "names", sorted(p_combats[p["Type"]]))
        help_text = effect(p["Help"], n)
        if help_text:
            ask("promotion", "effect", n, "{n}: effect", "text", help_text)

    # Improvements
    i_yields = yields("select ImprovementType, YieldType, Yield from Improvement_Yields")
    i_res = collections.defaultdict(set)
    for r in rows("select ImprovementType, ResourceType from Improvement_ResourceTypes where ResourceTrade = 1"):
        i_res[r["ImprovementType"]].add(r["ResourceType"])
    i_per_era = yields("select ImprovementType, YieldType, Yield from Improvement_YieldPerEra")
    i_on_res = collections.defaultdict(dict)
    for r in rows("select ImprovementType, ResourceType, YieldType, Yield from Improvement_ResourceType_Yields"):
        i_on_res[r["ImprovementType"]]["%s on %s" % (YIELDS[r["YieldType"]], RESOURCE[r["ResourceType"]])] = r["Yield"]
    i_tech = collections.defaultdict(dict)
    for r in rows("select ImprovementType, TechType, YieldType, Yield from Improvement_TechYieldChanges"):
        i_tech[r["ImprovementType"]]["%s with %s" % (YIELDS[r["YieldType"]], TECH[r["TechType"]])] = r["Yield"]
    built = {r["ImprovementType"]: r["PrereqTech"] for r in rows("select ImprovementType, PrereqTech from Builds where ImprovementType is not null order by PrereqTech is null")}
    for i in rows("select * from Improvements"):
        t, n = i["Type"], IMPROVEMENT[i["Type"]]
        if t not in built:
            continue
        if t in i_per_era:
            ask("improvement", "yield_per_era", n, "{n}: yield it gains per era", "amounts", i_per_era[t])
        elif t in i_yields or t not in i_on_res:
            ask("improvement", "yields", n, "{n}: yields it adds to any tile it is built on", "amounts", i_yields.get(t, {}))
        if t in i_on_res:
            ask("improvement", "resource_yields", n, "{n}: extra yields on particular resources, as `<number> <yield> on <resource>`",
                "amounts", i_on_res[t])
        if not i["CreatedByGreatPerson"]:
            ask("improvement", "tech", n, "{n}: technology that lets workers build it", "name", TECH.get(built[t], "none"))
        if i_res[t]:
            ask("improvement", "resources", n, "{n}: resources it connects", "names", names(i_res[t], RESOURCE))
        if i_tech[t]:
            ask("improvement", "tech_bonus", n, "{n}: extra yields from later technologies, as `<number> <yield> with <technology>`",
                "amounts", i_tech[t])
        if i["DefenseModifier"]:
            ask("improvement", "defense", n, "{n}: defense bonus % for a unit on it", "number", i["DefenseModifier"])

    # Resources
    r_yields = yields("select ResourceType, YieldType, Yield from Resource_YieldChanges")
    r_improvement = collections.defaultdict(set)
    for imp, res in i_res.items():
        if imp not in UI and not rows("select 1 from Improvements where Type = ? and CreatedByGreatPerson = 1", imp):
            for r in res:
                r_improvement[r].add(imp)
    for r in rows("select * from Resources where Type not in ('RESOURCE_ARTIFACTS', 'RESOURCE_HIDDEN_ARTIFACTS')"):
        t, n = r["Type"], RESOURCE[r["Type"]]
        ask("resource", "class", n, "{n}: resource type (Bonus, Strategic or Luxury)", "name", RES_CLASS[r["ResourceClassType"]])
        ask("resource", "yields", n, "{n}: yields it adds to its tile", "amounts", r_yields.get(t, {}))
        ask("resource", "reveal", n, "{n}: technology that reveals it", "name", TECH.get(r["TechReveal"], "none"))
        if r_improvement[t]:
            ask("resource", "improvement", n, "{n}: improvement that connects it", "names", names(r_improvement[t], IMPROVEMENT))
        if r["Happiness"]:
            ask("resource", "happiness", n, "{n}: Happiness it gives", "number", r["Happiness"])

    # Terrain, features, natural wonders. Hills are a plot type, so their rules come from Defines and the hills yield table.
    t_yields = yields("select TerrainType, YieldType, Yield from Terrain_Yields")
    for t in rows("select * from Terrains where GraphicalOnly = 0 or GraphicalOnly is null"):
        n = clean(t["Description"])
        ask("terrain", "yields", n, "{n}: tile yields", "amounts", t_yields.get(t["Type"], {}))
        ask("terrain", "moves", n, "{n}: movement cost", "number", t["Movement"])
    hills = clean("TXT_KEY_TERRAIN_HILLS") if "TXT_KEY_TERRAIN_HILLS" in TEXT else "Hills"
    ask("terrain", "yields", hills, "{n} (on Grassland, Plains, Desert or Tundra): tile yields", "amounts", {"Production": 2})
    ask("terrain", "moves", hills, "{n}: movement cost", "number", 1 + define("HILLS_EXTRA_MOVEMENT"))
    ask("terrain", "defense", hills, "{n}: defense bonus % for a unit on it", "number", define("HILLS_EXTRA_DEFENSE"))
    f_yields = yields("select FeatureType, YieldType, Yield from Feature_YieldChanges")
    for f in rows("select * from Features"):
        n = clean(f["Description"])
        if f["NaturalWonder"]:
            ask("natural", "yields", n, "{n} (natural wonder): tile yields", "amounts", f_yields.get(f["Type"], {}))
            if f["InBorderHappiness"]:
                ask("natural", "happiness", n, "{n} (natural wonder): Happiness it gives inside your borders", "number", f["InBorderHappiness"])
            help_text = clean(f["Help"])
            if help_text:
                ask("natural", "effect", n, "{n} (natural wonder): effect", "text", help_text)
            continue
        if f["YieldNotAdditive"]:
            ask("feature", "yields", n, "{n}: tile yields (they replace the terrain's)", "amounts", f_yields.get(f["Type"], {}))
        else:
            ask("feature", "yields", n, "{n}: yield change it makes to the terrain's yields", "amounts", f_yields.get(f["Type"], {}))
        if not f["Impassable"]:
            ask("feature", "moves", n, "{n}: movement cost", "number", f["Movement"])
        if f["Defense"]:
            ask("feature", "defense", n, "{n}: defense bonus % for a unit on it (negative for a penalty)", "number", f["Defense"])

    # Specialists
    s_yields = yields("select SpecialistType, YieldType, Yield from SpecialistYields")
    for s in rows("select * from Specialists where GreatPeopleUnitClass is not null"):
        n = SPECIALIST[s["Type"]]
        y = dict(s_yields.get(s["Type"], {}))
        if s["CulturePerTurn"]:
            y["Culture"] = y.get("Culture", 0) + s["CulturePerTurn"]
        ask("specialist", "yields", n, "{n} specialist: yields", "amounts", y)
        ask("specialist", "gpp", n, "{n} specialist: Great Person points per turn", "number", s["GreatPeopleRateChange"])
        ask("specialist", "great_person", n, "{n} specialist: Great Person it generates", "name", UNIT[CLASS_UNIT[s["GreatPeopleUnitClass"]]])

    # Civilizations
    for c in MAJORS:
        n = clean(rows("select ShortDescription from Civilizations where Type = ?", c)[0][0])
        leader = rows("select l.Type, l.Description from Civilization_Leaders cl join Leaders l on l.Type = cl.LeaderheadType where cl.CivilizationType = ?", c)[0]
        leader_name = clean(leader["Description"])
        first = leader_name.split()[0]
        ask("civ", "leader", n, "{n}: leader", "name", leader_name, aliases=[first] if len(first) > 3 else [])
        trait = rows("select t.ShortDescription, t.Description from Leader_Traits lt join Traits t on t.Type = lt.TraitType where lt.LeaderType = ?", leader["Type"])[0]
        ask("civ", "ability_name", n, "{n}: name of its unique ability", "name", clean(trait["ShortDescription"]))
        ask("civ", "ability", n, "{n}: what its unique ability does", "text", clean(trait["Description"]))
        ask("civ", "units", n, "{n}: unique units", "names", names([u for u, cv in UU.items() if cv == c], UNIT))
        ask("civ", "buildings", n, "{n}: unique buildings", "names", names([b for b, cv in UB.items() if cv == c], BUILDING))
        ask("civ", "improvements", n, "{n}: unique improvements", "names", names([i for i, cv in UI.items() if cv == c], IMPROVEMENT))

    # Projects
    for p in rows("select * from Projects where Cost > 0"):
        n = clean(p["Description"])
        ask("project", "cost", n, "{n}: Production cost", "number", p["Cost"])
        ask("project", "tech", n, "{n}: technology that unlocks it", "name", TECH.get(p["TechPrereq"], "none"))

    # Global rules from the Defines table
    RULES = [
        ("UNHAPPINESS_PER_CITY", "Unhappiness each city causes, apart from its citizens"),
        ("UNHAPPINESS_PER_POPULATION", "Unhappiness each citizen causes"),
        ("UNHAPPINESS_PER_CAPTURED_CITY", "Unhappiness an occupied city causes, apart from its citizens"),
        ("UNHAPPINESS_PER_OCCUPIED_POPULATION", "Unhappiness each citizen of an occupied city causes"),
        ("VERY_UNHAPPY_THRESHOLD", "Empire Happiness at which the empire becomes very unhappy"),
        ("SUPER_UNHAPPY_THRESHOLD", "Empire Happiness at which cities can revolt"),
        ("UNHAPPY_GROWTH_PENALTY", "Growth change % while the empire is unhappy"),
        ("GOLDEN_AGE_BASE_THRESHOLD_HAPPINESS", "Happiness needed for the first Golden Age, before city count"),
        ("GOLDEN_AGE_EACH_GA_ADDITIONAL_HAPPINESS", "Extra Happiness needed for each later Golden Age"),
        ("GOLDEN_AGE_LENGTH", "Base length of a Golden Age in turns"),
        ("FOOD_CONSUMPTION_PER_POPULATION", "Food each citizen eats per turn"),
        ("BASE_CITY_GROWTH_THRESHOLD", "Food needed to grow a size 1 city"),
        ("BASE_POLICY_COST", "Culture cost of the first social policy with one city"),
        ("CULTURE_COST_FIRST_PLOT", "Culture a city needs for its first border tile"),
        ("PLOT_BASE_COST", "Base Gold cost to buy a tile"),
        ("MAX_HIT_POINTS", "Hit points of a unit at full health"),
        ("CITY_ATTACK_RANGE", "Range of a city's ranged attack in tiles"),
        ("CITY_HIT_POINTS_HEALED_PER_TURN", "Hit points a damaged city regains per turn"),
        ("ENEMY_HEAL_RATE", "Hit points a resting unit heals per turn in enemy territory"),
        ("NEUTRAL_HEAL_RATE", "Hit points a resting unit heals per turn in neutral territory"),
        ("FRIENDLY_HEAL_RATE", "Hit points a resting unit heals per turn in friendly territory"),
        ("CITY_HEAL_RATE", "Hit points a resting unit heals per turn in a city"),
        ("PILLAGE_HEAL_AMOUNT", "Hit points a unit heals by pillaging"),
        ("INSTA_HEAL_RATE", "Hit points restored by taking the instant heal promotion"),
        ("EXPERIENCE_PER_LEVEL", "Experience a unit needs for its first promotion"),
        ("EXPERIENCE_ATTACKING_UNIT_MELEE", "Experience a melee unit earns for attacking a unit"),
        ("EXPERIENCE_DEFENDING_UNIT_MELEE", "Experience a unit earns for defending against a melee attack"),
        ("BARBARIAN_MAX_XP_VALUE", "Most experience a unit can earn from fighting barbarians"),
        ("GREAT_GENERAL_STRENGTH_MOD", "Combat bonus % from a nearby Great General"),
        ("RIVER_ATTACK_MODIFIER", "Combat change % for attacking across a river"),
        ("AMPHIB_ATTACK_MODIFIER", "Combat change % for attacking from the sea onto land"),
        ("STRATEGIC_RESOURCE_EXHAUSTED_PENALTY", "Combat change % for a unit whose strategic resource is in deficit"),
        ("EMBARKED_UNIT_MOVEMENT", "Movement points of an embarked unit before techs"),
        ("FRIENDSHIP_THRESHOLD_FRIENDS", "Influence needed to be Friends with a city-state"),
        ("FRIENDSHIP_THRESHOLD_ALLIES", "Influence needed to be Allies with a city-state"),
        ("RELIGION_MIN_FAITH_FIRST_PANTHEON", "Faith needed for the first pantheon"),
        ("RELIGION_MIN_FAITH_FIRST_PROPHET", "Faith needed for the first Great Prophet"),
        ("PUPPET_SCIENCE_MODIFIER", "Science change % in a puppet city"),
        ("PUPPET_CULTURE_MODIFIER", "Culture change % in a puppet city"),
        ("CULTURE_LEVEL_INFLUENTIAL", "Your Tourism total as % of a civ's Culture needed to be Influential over it"),
        ("CULTURE_LEVEL_DOMINANT", "Your Tourism total as % of a civ's Culture needed to be Dominant over it"),
        ("TOURISM_MODIFIER_TRADE_ROUTE", "Tourism bonus % with a civ you have a trade route with"),
        ("TOURISM_MODIFIER_OPEN_BORDERS", "Tourism bonus % with a civ that has open borders with you"),
        ("TOURISM_MODIFIER_SHARED_RELIGION", "Tourism bonus % with a civ that shares your religion"),
        ("TOURISM_MODIFIER_DIFFERENT_IDEOLOGIES", "Tourism change % with a civ of a different ideology"),
        ("RESEARCH_AGREEMENT_TIMER", "Turns a research agreement takes"),
    ]
    for name, prompt in RULES:
        v = define(name)
        ask("rule", "value", name.lower(), prompt + " (%s)" % name, "number", int(v) if float(v) == int(float(v)) else v)

    # A name shared by two subjects is kept only when both give the same answer.
    by_id = collections.OrderedDict()
    for q in QUESTIONS:
        by_id.setdefault(q["id"], []).append(q)
    key, dropped = [], []
    for qid, qs in by_id.items():
        if all(json.dumps(q["answer"], sort_keys=True) == json.dumps(qs[0]["answer"], sort_keys=True) for q in qs):
            key.append(qs[0])
        else:
            dropped.append(qid)
    return key, dropped


def norm(s):
    s = unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode().lower().replace("&", " and ")
    s = re.sub(r"['`’]", "", s)
    s = re.sub(r"[^a-z0-9.+-]+", " ", s).strip(" .")
    s = re.sub(r"^the ", "", s)
    s = re.sub(r" (era|units?|policy branch|branch|tree|specialists?|slots?|wonder)$", "", s)
    return ALIASES.get(s, s)


def similar(a, b):
    return difflib.SequenceMatcher(None, a, b).ratio()


def name_key(s):
    return " ".join(re.sub(r"[.+-]", " ", norm(s)).split())


def singular(w):
    return re.sub(r"s$", "", re.sub(r"ies$", "y", re.sub(r"men$", "man", w)))


def same_word(a, b):
    a, b = singular(a), singular(b)
    return a == b or (min(len(a), len(b)) >= 8 and similar(a, b) >= 0.9)


def same_name(a, b):
    """Names match word for word, allowing plurals and a typo in a long word; 'Shock I' is not 'Shock II'."""
    wa, wb = a.split(), b.split()
    if len(wa) == len(wb):
        return all(same_word(x, y) for x, y in zip(wa, wb))
    return similar(a.replace(" ", ""), b.replace(" ", "")) >= 0.95


def number_in(s):
    m = re.search(r"[-+]?\d+(?:\.\d+)?", s.replace(",", ""))
    return float(m.group()) if m else None


def pair_up(given, expected):
    """One-to-one matches between two lists of name keys: [(given index, expected index)]."""
    pairs = sorted(((similar(g, e), i, j) for i, g in enumerate(given) for j, e in enumerate(expected) if same_name(g, e)), reverse=True)
    used_g, used_e, out = set(), set(), []
    for r, i, j in pairs:
        if i not in used_g and j not in used_e:
            used_g.add(i); used_e.add(j); out.append((i, j))
    return out


def split_list(s):
    return [p.strip() for p in re.split(r"[;,\n]", s) if p.strip()]


def is_none(s):
    return norm(s) in NONE


def grade(q, given):
    """Score 0..1 for one answer, by the question's type."""
    kind, want = q["type"], q["answer"]
    if kind == "number":
        got = 0.0 if is_none(given) else number_in(given)
        return float(got is not None and abs(got - want) < 1e-6)
    if kind == "name":
        options = [name_key(want)] + [name_key(a) for a in q.get("aliases", [])]
        g = "none" if is_none(given) else name_key(given)
        return float(any(same_name(g, o) for o in options))
    if kind == "names":
        exp = [name_key(x) for x in want]
        got = [] if is_none(given) else [name_key(x) for x in split_list(given)]
        if not exp and not got:
            return 1.0
        tp = len(pair_up(got, exp))
        if not tp:
            return 0.0
        p, r = tp / len(got), tp / len(exp)
        return 2 * p * r / (p + r)
    if kind == "amounts":
        exp_names = [name_key(k) for k in want]
        exp_vals = list(want.values())
        got_names, got_vals = [], []
        if not is_none(given):
            for piece in split_list(given):
                v = number_in(piece)
                name = name_key(re.sub(r"[-+]?\d+(?:\.\d+)?\s*%?", " ", piece.replace(",", "")))
                got_names.append(name); got_vals.append(v)
        if not exp_names and not got_names:
            return 1.0
        matches = pair_up(got_names, exp_names)
        right = sum(1 for i, j in matches if got_vals[i] is not None and abs(got_vals[i] - exp_vals[j]) < 1e-6)
        return right / (len(exp_names) + len(got_names) - len(matches))
    return soft_text(want, given)


def words(s):
    return {w.rstrip("s") for w in re.findall(r"[a-z]+", norm(s)) if len(w) > 2 and w not in STOP}


def soft_text(want, given):
    """Text answers: share of the game text's numbers and content words that the answer contains."""
    nums_w = {float(n) for n in re.findall(r"\d+(?:\.\d+)?", want)}
    nums_g = {float(n) for n in re.findall(r"\d+(?:\.\d+)?", given)}
    ww = words(want)
    word_recall = len(ww & words(given)) / len(ww) if ww else 1.0
    if nums_w:
        return 0.5 * len(nums_w & nums_g) / len(nums_w) + 0.5 * word_recall
    return word_recall


def run_dir(name):
    return os.path.join(RUNS, name)


def cmd_build(args):
    db, text = load_game(args.assets)
    key, dropped = make_key(db, text)
    os.makedirs(DATA, exist_ok=True)
    with open(KEY, "w", encoding="utf-8") as f:
        json.dump({"source": "Civilization V: Brave New World (base game, Gods and Kings, Brave New World, civ packs; no mods), "
                             "rebuilt from the game's XML by agent_quiz.py build. Costs at Standard speed.",
                   "dropped_ambiguous": dropped, "questions": key}, f, indent=1)
    print("%d questions -> %s (%d dropped: a name two subjects share, with different answers)" % (len(key), KEY, len(dropped)))
    cmd_check(args)


def cmd_list(args):
    key = load_key()
    by = collections.OrderedDict()
    for q in key:
        by.setdefault("%s.%s" % (q["section"], q["field"]), []).append(q)
    print("%d questions. Pick them by id pattern, e.g. 'unit.combat.*', 'tech.*', '*.cost.*', 'civ.*.rome'.\n" % len(key))
    for f, qs in by.items():
        print("  %-26s %4d  %-7s  %s" % (f + ".*", len(qs), qs[0]["type"], qs[0]["prompt"]))


def parse_picks(lines):
    out = []
    for line in lines:
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) > 2 or (len(parts) == 2 and not parts[1].isdigit()):
            sys.exit("bad pick %r: expected 'PATTERN [COUNT]'" % line)
        out.append((parts[0], int(parts[1]) if len(parts) == 2 else None))
    return out


def cmd_make(args):
    key = load_key()
    lines = list(args.pick or [])
    if args.set:
        lines += SETS[args.set] if args.set in SETS else read_text(args.set).splitlines()
    picks = parse_picks(lines)
    if not picks:
        sys.exit("nothing picked: give --set FILE or --pick 'PATTERN [COUNT]' (see: agent_quiz.py list)")
    d = run_dir(args.name)
    if os.path.exists(os.path.join(d, "quiz.json")) and not args.force:
        sys.exit("%s already has a quiz; use another name or --force" % d)
    rng = random.Random(args.seed)
    chosen = set()
    for pattern, count in picks:
        matched = [q["id"] for q in key if fnmatch.fnmatchcase(q["id"], pattern)]
        if not matched:
            sys.exit("pattern %r matches no question (see: agent_quiz.py list)" % pattern)
        chosen.update(rng.sample(matched, count) if count and count < len(matched) else matched)
    questions = []
    for q in key:
        if q["id"] in chosen:
            questions.append(dict(q, q="Q%04d" % (len(questions) + 1)))
    os.makedirs(d, exist_ok=True)
    json.dump({"name": args.name, "created": datetime.datetime.now().isoformat(timespec="seconds"), "seed": args.seed,
               "picks": ["%s %s" % (p, c or "") for p, c in picks], "questions": questions},
              open(os.path.join(d, "quiz.json"), "w", encoding="utf-8"), indent=1)
    with open(os.path.join(d, "sheet.md"), "w", encoding="utf-8") as f:
        f.write(SHEET.format(count=len(questions)))
        for q in questions:
            f.write("%s (%s) %s\n" % (q["q"], q["type"], q["prompt"]))
    print("%d questions -> %s" % (len(questions), os.path.join(d, "sheet.md")))
    print("Save the agent's reply as %s, then: python agent_quiz.py score %s" % (os.path.join(d, "answers.txt"), args.name))


def read_text(path):
    """A text file in UTF-8 or UTF-16, which is what PowerShell's > writes."""
    raw = open(path, "rb").read()
    return raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8-sig", "replace")


def read_lines(path):
    out = {}
    for line in read_text(path).splitlines():
        m = re.match(r"^[\s>*`-]*(Q\d{4})[\s*`]*[|:]\s*(.*?)[\s`*]*$", line)
        if m and m.group(1) not in out:
            out[m.group(1)] = m.group(2).strip()
    return out


def pct(x):
    return "%5.1f%%" % (100 * x)


def cmd_score(args):
    d = run_dir(args.name)
    quiz = json.load(open(os.path.join(d, "quiz.json"), encoding="utf-8"))
    answers = read_lines(args.answers or os.path.join(d, "answers.txt"))
    verdicts = {}
    if args.review:
        marks = {"correct": 1.0, "partial": 0.5, "wrong": 0.0}
        for q, v in read_lines(args.review).items():
            if norm(v) not in marks:
                sys.exit("review %s: %r is not correct, partial or wrong" % (q, v))
            verdicts[q] = marks[norm(v)]
    # Answers come from the current key, so a corrected key re-scores old quizzes; a question since removed is skipped.
    current = {q["id"]: q for q in load_key()}
    retired = [q["q"] for q in quiz["questions"] if q["id"] not in current]
    results = []
    for asked in quiz["questions"]:
        if asked["id"] not in current:
            continue
        q = dict(current[asked["id"]], q=asked["q"])
        given = answers.get(q["q"])
        if given is None:
            status, score = "missing", 0.0
        elif norm(given) in ABSTAIN or given.strip() == "?":
            status, score = "abstained", 0.0
        elif q["type"] == "text":
            score = verdicts.get(q["q"], grade(q, given))
            status = "reviewed" if q["q"] in verdicts else "soft"
        else:
            score = grade(q, given)
            status = "correct" if score == 1 else ("partial" if score > 0 else "wrong")
        results.append(dict(q=q["q"], id=q["id"], section=q["section"], field="%s.%s" % (q["section"], q["field"]),
                            type=q["type"], prompt=q["prompt"], expected=q["answer"], given=given, status=status, score=score))
    json.dump(results, open(os.path.join(d, "results.json"), "w", encoding="utf-8"), indent=1)

    ruled = [r for r in results if r["type"] != "text"]
    text = [r for r in results if r["type"] == "text"]
    count = collections.Counter(r["status"] for r in results)
    print("Quiz %s: %d questions, %d answered, %d abstained, %d missing" % (
        args.name, len(results), len(results) - count["abstained"] - count["missing"], count["abstained"], count["missing"]))
    if retired:
        print("Skipped %d question(s) no longer in the key: %s" % (len(retired), ", ".join(retired)))
    if len(answers) and not any(q["q"] in answers for q in quiz["questions"]):
        sys.exit("no answer line matched a question id; expected lines like 'Q0001 | 75'")
    if ruled:
        print("\nChecked by rule: %s  (%.1f of %d; %d wrong, %d partial)" % (
            pct(sum(r["score"] for r in ruled) / len(ruled)), sum(r["score"] for r in ruled), len(ruled), count["wrong"], count["partial"]))
        by_type = collections.defaultdict(list)
        for r in ruled:
            by_type[r["type"]].append(r["score"])
        print("  " + "   ".join("%s %s (%d)" % (t, pct(sum(v) / len(v)).strip(), len(v)) for t, v in by_type.items()))
    if text:
        reviewed = sum(1 for r in text if r["status"] == "reviewed")
        soft = sum(1 for r in text if r["status"] == "soft")
        how = "meaning review" if not soft else "soft match" if not reviewed else "%d reviewed, %d soft match" % (reviewed, soft)
        print("\nText (%s): %s over %d" % (how, pct(sum(r["score"] for r in text) / len(text)).strip(), len(text)))
        if reviewed and ruled:
            print("All questions: %s" % pct(sum(r["score"] for r in results) / len(results)).strip())
    for label, keyname in (("section", "section"), ("field, worst first", "field")):
        groups = collections.defaultdict(list)
        for r in ruled:
            groups[r[keyname]].append(r["score"])
        if len(groups) > 1:
            print("\nBy %s (checked by rule):" % label)
            items = sorted(groups.items(), key=lambda kv: sum(kv[1]) / len(kv[1])) if keyname == "field" else groups.items()
            for g, v in items:
                print("  %-26s %s  (%d)" % (g, pct(sum(v) / len(v)), len(v)))
    misses = [r for r in ruled if r["status"] in ("wrong", "partial")]
    if misses:
        print("\nMisses (%d), first %d:" % (len(misses), min(len(misses), args.show)))
        for r in misses[:args.show]:
            print("  %s %-34s expected %s | got %s" % (r["q"], r["id"], show(r["expected"]), r["given"]))
    answered_text = [r for r in text if r["status"] in ("soft", "reviewed")]
    if answered_text:
        with open(os.path.join(d, "review.md"), "w", encoding="utf-8") as f:
            f.write(REVIEW)
            for r in answered_text:
                f.write("## %s: %s\nGame: %s\nAnswer: %s\n\n" % (r["q"], r["prompt"], r["expected"], r["given"]))
        print("\nFor a meaning review of the %d text answers, give %s to a model and run:\n  python agent_quiz.py score %s --review <its reply file>" % (
            len(answered_text), os.path.join(d, "review.md"), args.name))
    print("\nPer-question results: %s" % os.path.join(d, "results.json"))


def cmd_check(args):
    """The key must score 100% against its own answers, and no two answers to one field may match each other."""
    key = load_key()
    wrong = [q["id"] for q in key if grade(q, show(q["answer"])) != 1]
    clashes = []
    vocab = collections.defaultdict(set)
    for q in key:
        a = q["answer"]
        for n in ([a] if q["type"] == "name" else a if q["type"] == "names" else list(a) if q["type"] == "amounts" else []):
            vocab["%s.%s" % (q["section"], q["field"])].add(name_key(n))
    for f, ns in vocab.items():
        ns = sorted(ns)
        clashes += ["%s: %s ~ %s" % (f, a, b) for i, a in enumerate(ns) for b in ns[i + 1:] if same_name(a, b)]
    for line in ["does not score its own answer: " + i for i in wrong] + ["answers the matcher cannot tell apart: " + c for c in clashes]:
        print(line)
    if wrong or clashes:
        sys.exit(1)
    print("key ok: %d questions score 100%% against their own answers; no two answers in a field can be confused" % len(key))


def show(v):
    if isinstance(v, dict):
        return "; ".join("%s %s" % (n, k) for k, n in v.items()) or "none"
    if isinstance(v, list):
        return "; ".join(v) or "none"
    return str(v)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="write the answer key from the game's own files")
    b.add_argument("assets", help="the game's Assets folder, e.g. \"C:/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V/Assets\"")
    sub.add_parser("list", help="sections and fields, with counts and an example")
    sub.add_parser("check", help="verify the key against the scorer after rebuilding it")
    m = sub.add_parser("make", help="write runs/NAME/sheet.md for the agent")
    m.add_argument("name")
    m.add_argument("--set", help="sampler, numbers, or a file of picks with one 'PATTERN [COUNT]' per line")
    m.add_argument("--pick", action="append", help="'PATTERN [COUNT]'; repeatable")
    m.add_argument("--seed", type=int, default=1)
    m.add_argument("--force", action="store_true", help="replace an existing quiz of that name")
    s = sub.add_parser("score", help="score runs/NAME/answers.txt")
    s.add_argument("name")
    s.add_argument("--answers", help="answers file (default runs/NAME/answers.txt)")
    s.add_argument("--review", help="meaning-review verdicts for the text answers")
    s.add_argument("--show", type=int, default=40, help="misses to print")
    args = ap.parse_args()
    {"build": cmd_build, "list": cmd_list, "check": cmd_check, "make": cmd_make, "score": cmd_score}[args.cmd](args)


if __name__ == "__main__":
    main()
