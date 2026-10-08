// Golden parity runner (C# side): reads a scenario JSON from test_data/parity/scenarios,
// generates a layout with the reference Edgar-DotNet GraphBasedGeneratorGrid2D (the same engine
// Edgar.GUI uses) and dumps the logical structure (room outlines/positions/doors) as JSON
// for comparison with the C++ port.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Edgar.Geometry;
using Edgar.GraphBasedGenerator.Grid2D;
using Edgar.Legacy.GeneralAlgorithms.DataStructures.Common;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace LevelSynth.ParityRunner
{
    public static class Program
    {
        public static int Main(string[] args)
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("usage: parity_runner_cs <scenario.json> <output.json>");
                return 2;
            }

            var scenario = JObject.Parse(File.ReadAllText(args[0]));
            var name = scenario.Value<string>("name");
            var seed = scenario.Value<int>("seed");

            var levelDescription = new LevelDescriptionGrid2D<int>();
            if (scenario.Value<string>("schema") == "full_level")
            {
                // Level dumped by the C++ port (benchmark_layout --dump-scenario): identical
                // per-room template pools with polygon outlines and door modes
                var templatesByRoom = scenario["templates_by_room"].ToObject<Dictionary<string, JArray>>();
                foreach (var room in scenario["rooms"])
                {
                    var id = room.Value<int>("id");
                    var isCorridor = room.Value<bool?>("corridor") ?? false;
                    levelDescription.AddRoom(id, new RoomDescriptionGrid2D(isCorridor,
                        LoadFullTemplates(templatesByRoom[id.ToString()])));
                }
            }
            else
            {
                var basicTemplates = LoadTemplates(scenario["templates"]);
                var corridorTemplates = LoadTemplates(scenario["corridor_templates"]);

                foreach (var room in scenario["rooms"])
                {
                    var id = room.Value<int>("id");
                    var isCorridor = room.Value<bool?>("corridor") ?? false;
                    var templates = isCorridor ? corridorTemplates : basicTemplates;
                    if (templates.Count == 0)
                        throw new InvalidOperationException($"No templates for room {id} (corridor={isCorridor})");
                    levelDescription.AddRoom(id, new RoomDescriptionGrid2D(isCorridor, templates));
                }
            }
            foreach (var connection in scenario["connections"])
            {
                levelDescription.AddConnection(connection[0].Value<int>(), connection[1].Value<int>());
            }

            var generator = new GraphBasedGeneratorGrid2D<int>(levelDescription);
            generator.InjectRandomGenerator(new Random(seed));
            var layout = generator.GenerateLayout();
            if (layout == null)
            {
                Console.Error.WriteLine("generation returned null layout");
                return 1;
            }

            var output = new JObject
            {
                ["scenario"] = name,
                ["seed"] = seed,
                ["engine"] = "csharp",
                ["rooms"] = new JArray(layout.Rooms.Select(room => new JObject
                {
                    ["id"] = room.Room,
                    ["is_corridor"] = room.IsCorridor,
                    ["position"] = new JArray(room.Position.X, room.Position.Y),
                    ["outline"] = new JArray(room.Outline.GetPoints()
                        .Select(p => new JArray(p.X + room.Position.X, p.Y + room.Position.Y))),
                    ["doors"] = new JArray((room.Doors ?? new List<LayoutDoorGrid2D<int>>())
                        .Select(d => new JObject
                        {
                            ["from"] = d.FromRoom,
                            ["to"] = d.ToRoom,
                            ["line"] = new JArray(
                                new JArray(d.DoorLine.From.X, d.DoorLine.From.Y),
                                new JArray(d.DoorLine.To.X, d.DoorLine.To.Y)),
                        })),
                })),
            };

            File.WriteAllText(args[1], output.ToString(Formatting.Indented));
            Console.WriteLine($"wrote {args[1]} ({layout.Rooms.Count} rooms)");
            return 0;
        }

        private static List<RoomTemplateGrid2D> LoadFullTemplates(JArray templatesToken)
        {
            var result = new List<RoomTemplateGrid2D>();
            foreach (var t in templatesToken)
            {
                var points = t["points"]
                    .Select(p => new Vector2Int(p[0].Value<int>(), p[1].Value<int>()))
                    .ToList();
                var outline = new PolygonGrid2D(points);

                IDoorModeGrid2D doorMode;
                if (t["simple_doors"] != null)
                {
                    doorMode = new SimpleDoorModeGrid2D(t["simple_doors"].Value<int>("door_length"),
                        t["simple_doors"].Value<int>("corner_distance"));
                }
                else
                {
                    var doors = t["manual_doors"]
                        .Select(d => new DoorGrid2D(
                            new Vector2Int(d["from"][0].Value<int>(), d["from"][1].Value<int>()),
                            new Vector2Int(d["to"][0].Value<int>(), d["to"][1].Value<int>())))
                        .ToList();
                    doorMode = new ManualDoorModeGrid2D(doors);
                }
                result.Add(new RoomTemplateGrid2D(outline, doorMode));
            }
            return result;
        }

        private static List<RoomTemplateGrid2D> LoadTemplates(JToken templatesToken)
        {
            var result = new List<RoomTemplateGrid2D>();
            if (templatesToken == null)
            {
                return result;
            }

            foreach (var t in templatesToken)
            {
                var rect = t["rect"];
                var outline = PolygonGrid2D.GetRectangle(rect[0].Value<int>(), rect[1].Value<int>());
                var doorMode = new SimpleDoorModeGrid2D(t.Value<int>("door_length"), t.Value<int>("corner_distance"));
                result.Add(new RoomTemplateGrid2D(outline, doorMode));
            }
            return result;
        }
    }
}
