"""Offline regression tests for the Bedrock recipe importer (standard library)."""
import json
from pathlib import Path
import tempfile
import unittest
from import_recipes import import_directory, jsonc, parse_recipe, DEFAULT_TAGS


def shaped(output="iron_pickaxe"):
    return {"format_version": "1.20", "minecraft:recipe_shaped": {
        "tags": ["crafting_table"], "pattern": ["XXX", " # ", " # "],
        "key": {"X": {"item": "iron_ingot"}, "#": {"item": "stick"}}, "result": {"item": output}}}


class ImportTests(unittest.TestCase):
    def test_shaped_totals_are_counted_from_pattern(self):
        output, count, groups, _, _ = parse_recipe(shaped(), DEFAULT_TAGS)
        self.assertEqual((output, count), ("minecraft:iron_pickaxe", 1))
        self.assertEqual(groups, [(2, ("minecraft:stick",)), (3, ("minecraft:iron_ingot",))])

    def test_tags_shapeless_and_actual_output_count(self):
        doc = {"minecraft:recipe_shapeless": {"tags": ["crafting_table"], "ingredients": [{"tag": "minecraft:planks", "count": 2}], "result": {"item": "stick", "count": 4}}}
        recipe = parse_recipe(doc, DEFAULT_TAGS)
        self.assertEqual(recipe[1], 4)
        self.assertEqual(len(recipe[2][0][1]), 12)
        doc["minecraft:recipe_shapeless"]["ingredients"][0]["tag"] = "custom:unknown"
        with self.assertRaises(ValueError):
            parse_recipe(doc, DEFAULT_TAGS)

    def test_metadata_containers_stations_and_malformed_grid_are_refused(self):
        mutations = [
            lambda r: r["key"]["X"].update(data=0),
            lambda r: r["key"]["X"].update(item="milk_bucket"),
            lambda r: r.update(tags=["stonecutter"]),
            lambda r: r.update(pattern=["XXXX"]),
            lambda r: r["result"].update(data=1),
            lambda r: r["result"].update(count=True),
            lambda r: r["key"].update(Y={"item": "dirt"}),
        ]
        for mutate in mutations:
            doc = shaped()
            mutate(doc["minecraft:recipe_shaped"])
            with self.subTest(doc=doc), self.assertRaises(ValueError):
                parse_recipe(doc, DEFAULT_TAGS)

    def test_comments_keep_string_content(self):
        self.assertEqual(jsonc('{/* comment */"url":"https://example.test/a//b",// line\n"text":"quote\\\"//"}'), {"url": "https://example.test/a//b", "text": 'quote"//'})

    def test_directory_reports_skips_and_selects_priority_deterministically(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "a.json").write_text(json.dumps(shaped()), encoding="utf-8")
            preferred = shaped()
            preferred["minecraft:recipe_shaped"]["priority"] = -1
            (root / "b.json").write_text(json.dumps(preferred), encoding="utf-8")
            (root / "bad.json").write_text('{"broken":true}', encoding="utf-8")
            catalog, report = import_directory(root)
            self.assertTrue(catalog.startswith("BARITONE_RECIPES 1\n"))
            self.assertIn('"minecraft:iron_ingot"', catalog)
            self.assertEqual(report["imported_outputs"], 1)
            self.assertEqual(report["selected_recipes"]["minecraft:iron_pickaxe"], "b.json")
            self.assertEqual(len(report["skipped"]), 1)
            self.assertEqual(len(report["alternative_recipes"]), 1)


if __name__ == "__main__":
    unittest.main()
