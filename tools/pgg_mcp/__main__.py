"""``python -m tools.pgg_mcp`` — stdio MCP server (venv already prepared).

Bootstrap (venv + deps) is ``python -m tools.pgg_mcp.launch``.
"""

from tools.pgg_mcp.server import mcp


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
