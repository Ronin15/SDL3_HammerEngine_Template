#!/usr/bin/env python3
"""Tests for atlas mapper state and rename helpers."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
ATLAS_TOOL_PATH = REPO_ROOT / "tools" / "atlas_tool.py"

spec = importlib.util.spec_from_file_location("atlas_tool", ATLAS_TOOL_PATH)
atlas_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(atlas_tool)


def write_png(path: Path, color):
    Image.new("RGBA", (2, 2), color).save(path)


def read_pixel(path: Path):
    with Image.open(path) as img:
        return img.getpixel((0, 0))


class AtlasToolMapperTests(unittest.TestCase):
    def make_paths(self, root: Path) -> dict:
        sprites_dir = root / "res" / "sprites"
        sprites_dir.mkdir(parents=True)
        return {
            "project_root": root,
            "sprites_dir": sprites_dir,
        }

    def test_apply_sprite_renames_handles_cycles(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = self.make_paths(Path(tmp))
            sprites_dir = paths["sprites_dir"]

            red = (255, 0, 0, 255)
            green = (0, 255, 0, 255)
            blue = (0, 0, 255, 255)

            write_png(sprites_dir / "alpha.png", red)
            write_png(sprites_dir / "beta.png", green)
            write_png(sprites_dir / "gamma.png", blue)

            renamed = atlas_tool.apply_sprite_renames(
                sprites_dir,
                {
                    "alpha": "beta",
                    "beta": "gamma",
                    "gamma": "alpha",
                },
            )

            self.assertEqual(len(renamed), 3)
            self.assertEqual(read_pixel(sprites_dir / "alpha.png"), blue)
            self.assertEqual(read_pixel(sprites_dir / "beta.png"), red)
            self.assertEqual(read_pixel(sprites_dir / "gamma.png"), green)
            self.assertFalse(list(sprites_dir.glob("__atlas_tmp_*.png")))

    def test_apply_sprite_renames_rejects_existing_target_conflict(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = self.make_paths(Path(tmp))
            sprites_dir = paths["sprites_dir"]

            write_png(sprites_dir / "sprite_001.png", (255, 0, 0, 255))
            write_png(sprites_dir / "existing.png", (0, 255, 0, 255))

            with self.assertRaises(ValueError):
                atlas_tool.apply_sprite_renames(
                    sprites_dir,
                    {"sprite_001": "existing"},
                )

            self.assertTrue((sprites_dir / "sprite_001.png").exists())
            self.assertTrue((sprites_dir / "existing.png").exists())

    def test_build_mapper_state_returns_fresh_sprites_and_missing_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = self.make_paths(Path(tmp))
            sprites_dir = paths["sprites_dir"]

            write_png(sprites_dir / "sprite_001.png", (255, 0, 0, 255))
            write_png(sprites_dir / "sword.png", (0, 255, 0, 255))

            expected_ids = {
                "Items": [
                    {"id": "sword", "name": "Sword", "source": "test"},
                    {"id": "potion", "name": "Potion", "source": "test"},
                ]
            }

            original_collector = atlas_tool.collect_expected_texture_ids
            atlas_tool.collect_expected_texture_ids = lambda _: expected_ids
            try:
                state = atlas_tool.build_mapper_state(paths)
            finally:
                atlas_tool.collect_expected_texture_ids = original_collector

            filenames = {sprite["filename"] for sprite in state["sprites"]}
            mapped_by_name = {
                sprite["filename"]: sprite["mapped"]
                for sprite in state["sprites"]
            }

            self.assertEqual(filenames, {"sprite_001", "sword"})
            self.assertFalse(mapped_by_name["sprite_001"])
            self.assertTrue(mapped_by_name["sword"])
            self.assertEqual(state["expectedIds"], expected_ids)
            self.assertEqual(
                state["missingIds"],
                {"Items": [{"id": "potion", "name": "Potion", "source": "test"}]},
            )

    def test_collect_expected_texture_ids_includes_split_resource_catalogs(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = self.make_paths(Path(tmp))
            data_dir = Path(tmp) / "res" / "data"
            data_dir.mkdir()

            (data_dir / "items.json").write_text(
                json.dumps({
                    "resources": [
                        {
                            "name": "Health Potion",
                            "category": "Item",
                            "textureId": "health_potion_world",
                        }
                    ]
                }),
                encoding="utf-8",
            )
            (data_dir / "weapons.json").write_text(
                json.dumps({
                    "resources": [
                        {
                            "name": "Iron Sword",
                            "category": "Item",
                            "worldTextureId": "iron_sword_world",
                            "iconTextureId": "iron_sword_icon",
                        }
                    ]
                }),
                encoding="utf-8",
            )
            (data_dir / "equipment.json").write_text(
                json.dumps({
                    "resources": [
                        {
                            "name": "Iron Shield",
                            "category": "Item",
                            "textureId": "iron_shield_icon",
                        }
                    ]
                }),
                encoding="utf-8",
            )

            expected_ids = atlas_tool.collect_expected_texture_ids(paths)

            self.assertEqual(
                expected_ids["Items"],
                [
                    {
                        "id": "health_potion_world",
                        "name": "Health Potion",
                        "source": "items.json",
                    },
                    {
                        "id": "iron_sword_world",
                        "name": "Iron Sword",
                        "source": "weapons.json",
                    },
                    {
                        "id": "iron_sword_icon",
                        "name": "Iron Sword",
                        "source": "weapons.json",
                    },
                    {
                        "id": "iron_shield_icon",
                        "name": "Iron Shield",
                        "source": "equipment.json",
                    },
                ],
            )

    def test_scan_for_texture_ids_includes_nested_icons_once(self):
        data = {
            "name": "Root",
            "groups": [
                {
                    "name": "Potion",
                    "iconTextureId": "potion_icon",
                    "variants": [
                        {
                            "name": "Potion World",
                            "worldTextureId": "potion_world",
                        },
                        {
                            "name": "Duplicate Icon",
                            "iconTextureId": "potion_icon",
                        },
                    ],
                }
            ],
        }

        self.assertEqual(
            atlas_tool.scan_for_texture_ids(data, "items.json"),
            [
                {
                    "id": "potion_icon",
                    "name": "Potion",
                    "source": "items.json",
                },
                {
                    "id": "potion_world",
                    "name": "Potion World",
                    "source": "items.json",
                },
            ],
        )


if __name__ == "__main__":
    unittest.main()
