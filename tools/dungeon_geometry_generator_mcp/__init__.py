"""DungeonGeometryGenerator MCP — agent tooling over the DungeonGeometryGeneratorServe RPC.

Same Python session on every OS: ``DungeonGeometryGeneratorSession`` starts DungeonGeometryGeneratorServe or returns
``need_build``. Entry points: ``python -m tools.dungeon_geometry_generator_mcp.launch`` (venv) then
``python -m tools.dungeon_geometry_generator_mcp`` (stdio server).
"""
