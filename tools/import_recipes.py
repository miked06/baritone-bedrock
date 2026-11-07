"""Import local Bedrock shaped/shapeless recipes into a Baritone planning catalog.

Reads a matching game/behavior-pack recipes directory, never downloads or changes
Minecraft files. Unsupported metadata, unlock conditions, container returns and
unresolved tags are reported instead of guessed. Native crafting remains authoritative.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

MAX_OUTPUTS = 2048
ID = re.compile(r"[a-z0-9_]+:[a-z0-9_]+(?:[./-][a-z0-9_]+)*\Z")
WOODS = ("oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak", "crimson", "warped", "bamboo")
DEFAULT_TAGS = {
    "minecraft:planks": [f"minecraft:{wood}_planks" for wood in WOODS],
    "minecraft:stone_crafting_materials": ["minecraft:cobblestone", "minecraft:cobbled_deepslate", "minecraft:blackstone"],
}


def identifier(value):
    if not isinstance(value, str):
        raise ValueError("Expected an item/tag identifier")
    value = value if ":" in value else "minecraft:" + value
    if len(value) > 128 or not ID.fullmatch(value) or "/" in value or "." in value or "-" in value:
        raise ValueError("Unsupported identifier")
    return value


def jsonc(text):
    # Remove comments only outside strings; preserve URLs and escaped quotes.
    token = re.compile(r'"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/')
    return json.loads(token.sub(lambda m: m[0] if m[0].startswith('"') else " ", text))


def quantity(value):
    if type(value) is not int or not 1 <= value <= 64:
        raise ValueError("Quantity must be an integer 1..64")
    return value


def ingredient(spec, tags):
    if not isinstance(spec, dict) or set(spec) - {"item", "tag", "count"} or ("item" in spec) == ("tag" in spec):
        raise ValueError("Unsupported ingredient metadata or descriptor")
    count = quantity(spec.get("count", 1))
    if "item" in spec:
        return count, (identifier(spec["item"]),)
    tag = identifier(spec["tag"])
    choices = tags.get(tag)
    if not isinstance(choices, list) or not 1 <= len(choices) <= 16:
        raise ValueError(f"Unresolved or oversized ingredient tag: {tag}")
    choices = tuple(dict.fromkeys(identifier(item) for item in choices))
    return count, choices


def parse_recipe(document, tags):
    if not isinstance(document, dict):
        raise ValueError("Recipe document must be an object")
    kinds = [kind for kind in ("minecraft:recipe_shaped", "minecraft:recipe_shapeless") if kind in document]
    if len(kinds) != 1:
        raise ValueError("Only shaped/shapeless crafting recipes are supported")
    kind = kinds[0]
    recipe = document[kind]
    if not isinstance(recipe, dict) or recipe.get("tags") != ["crafting_table"]:
        raise ValueError("Recipe requires an unsupported workstation or workstation combination")
    # Recipes consuming tools/containers must not silently discard return items.
    if set(recipe) - {"description", "tags", "pattern", "key", "ingredients", "result", "priority", "assume_symmetry", "unlock"}:
        raise ValueError("Unsupported recipe fields")
    result = recipe.get("result")
    if isinstance(result, list) and len(result) == 1:
        result = result[0]
    if not isinstance(result, dict) or set(result) - {"item", "count"}:
        raise ValueError("Unsupported output metadata or multiple results")
    output = identifier(result.get("item"))
    output_count = quantity(result.get("count", 1))
    groups = Counter()
    if kind.endswith("recipe_shaped"):
        pattern, key = recipe.get("pattern"), recipe.get("key")
        if not isinstance(pattern, list) or not 1 <= len(pattern) <= 3 or any(not isinstance(row, str) or not 1 <= len(row) <= 3 for row in pattern) or len({len(row) for row in pattern}) != 1 or not isinstance(key, dict):
            raise ValueError("Invalid crafting pattern")
        counts = Counter("".join(pattern).replace(" ", ""))
        if set(counts) != set(key):
            raise ValueError("Pattern and ingredient keys differ")
        for char, instances in counts.items():
            count, alternatives = ingredient(key[char], tags)
            groups[alternatives] += count * instances
    else:
        ingredients = recipe.get("ingredients")
        if not isinstance(ingredients, list) or not 1 <= len(ingredients) <= 9:
            raise ValueError("Expected 1..9 ingredients")
        for entry in ingredients:
            count, alternatives = ingredient(entry, tags)
            groups[alternatives] += count
    if not groups or sum(groups.values()) > 9:
        raise ValueError("Recipe does not fit a crafting grid")
    for alternatives in groups:
        if any(item.endswith(("_bucket", "_bottle")) or item == "minecraft:bucket" for item in alternatives):
            raise ValueError("Ingredient container returns require native dependency support")
    if any(output in alternatives for alternatives in groups):
        raise ValueError("Self-dependent recipe")
    priority = recipe.get("priority", 0)
    if type(priority) is not int:
        raise ValueError("Invalid recipe priority")
    return output, output_count, sorted((count, choices) for choices, count in groups.items()), priority, "unlock" in recipe


def import_directory(directory, tags=None):
    mapping = dict(DEFAULT_TAGS)
    if tags:
        for tag, choices in tags.items():
            mapping[identifier(tag)] = choices
    selected, origins = {}, {}
    report = {"imported_outputs": 0, "skipped": [], "alternative_recipes": [], "unlock_recipes": [], "source": str(directory.resolve())}
    files = sorted(directory.rglob("*.json"))
    if len(files) > 10000:
        raise ValueError("Recipe directory exceeds 10000 files")
    for file in files:
        try:
            if file.is_symlink() or file.stat().st_size > 256 * 1024:
                raise ValueError("Recipe file is linked or exceeds 256 KiB")
            output, count, groups, priority, unlock = parse_recipe(jsonc(file.read_text(encoding="utf-8-sig")), mapping)
            candidate = (priority, sum(g[0] for g in groups), str(file.relative_to(directory)), count, groups)
            if output in selected:
                report["alternative_recipes"].append({"output": output, "file": str(file.relative_to(directory)), "policy": "one deterministic recipe per output: priority, ingredient count, path"})
            if output not in selected or candidate[:3] < selected[output][:3]:
                selected[output] = candidate
                origins[output] = str(file.relative_to(directory))
            if unlock:
                report["unlock_recipes"].append(str(file.relative_to(directory)))
        except (OSError, ValueError, TypeError, KeyError) as error:
            report["skipped"].append({"file": str(file.relative_to(directory)), "reason": str(error)})
    if not selected or len(selected) > MAX_OUTPUTS:
        raise ValueError(f"Expected 1..{MAX_OUTPUTS} supported output recipes")
    lines = ["BARITONE_RECIPES 1"]
    quote = lambda value: '"' + value + '"'  # Validated identifiers contain no quotes/backslashes.
    for output, (_, _, _, count, groups) in sorted(selected.items()):
        fields = [quote(output), str(count), str(len(groups))]
        for amount, alternatives in groups:
            fields += [str(amount), str(len(alternatives)), *(quote(item) for item in alternatives)]
        lines.append(" ".join(fields))
    report["imported_outputs"] = len(selected)
    report["selected_recipes"] = origins
    report["limits"] = "Planning descriptions only. Alternative recipes use one selected variant; dependency cycles are refused by the planner. Unlocks and server recipes are checked during native crafting."
    return "\n".join(lines) + "\n", report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--tags", type=Path, help="Explicit JSON mapping from ingredient tags to exact item IDs")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("Choose a local recipes directory matching your game/behavior pack")
    report_path = args.output.with_suffix(args.output.suffix + ".report.json")
    if args.output.exists() or report_path.exists():
        parser.error("Choose new output/report paths; existing files are preserved")
    try:
        tags = json.loads(args.tags.read_text(encoding="utf-8-sig")) if args.tags else None
        if tags is not None and not isinstance(tags, dict):
            raise ValueError("Tags file must be an object")
        catalog, report = import_directory(args.directory, tags)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8", newline="\n") as file:
            file.write(catalog)
        with report_path.open("x", encoding="utf-8") as file:
            json.dump(report, file, indent=2)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Imported {report['imported_outputs']} outputs; skipped {len(report['skipped'])} files. Report: {report_path}")
    print(f'In Minecraft: #recipe import "{args.output.resolve()}"')


if __name__ == "__main__":
    main()
