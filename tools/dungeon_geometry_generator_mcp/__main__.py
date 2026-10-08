"""``python -m tools.dungeon_geometry_generator_mcp`` — stdio MCP server (venv already prepared).

Bootstrap (venv + deps) is ``python -m tools.dungeon_geometry_generator_mcp.launch``.
"""

from tools.dungeon_geometry_generator_mcp.server import mcp


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
