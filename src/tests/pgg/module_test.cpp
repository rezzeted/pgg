// E5 module tests (spec §7.6): imports bind namespaces against the configured
// roots, qualified calls resolve to module defs (and only defs), modules
// import modules, and the module diagnostics fire — E501 (not found), E502
// (cycle), E505 (unknown qualified symbol), E506 (library def without a
// docstring), namespace conflicts (E102), version pinning (stage error).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "pgg/src/eval/expand.h"
#include "pgg/src/eval/modules.h"
#include "test_utils.h"

namespace {

const std::string kLibRoot = std::string(PGG_CORPUS_DIR);

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

pgg::RunResult runWithLib(const std::string& src) {
    pgg::RunParams p;
    p.importRoots.push_back(kLibRoot);
    return pgg::run(src, p);
}

TEST(Module, ImportByPathAndAlias) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "import lib.rocks as rb\n"
        "root = rng_from_seed(1)\n"
        "a = rocks.make_pebble(size = 2.0, rng = root)\n"
        "b = rb.make_pebble(size = 2.0, rng = root)\n"
        "w = rocks.scale_of(g = a)\n"
        "scene = merge(a, b)\n"
        "output scene\n"
        "output w\n");
    pggtest::expectNoErrors(r);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r, "scene"), mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-1.0f));
    pggtest::expectVec3Near(mx, glm::vec3(1.0f));
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "w")), 2.0f);
}

TEST(Module, MultiOutputThroughNamespace) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks as rb\n"
        "root = rng_from_seed(1)\n"
        "first, second = rb.pebble_pair(size = 1.0, rng = root)\n"
        "scene = merge(first, second)\n"
        "output scene\n");
    pggtest::expectNoErrors(r);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r, "scene"), mn, mx);
    pggtest::expectF32Near(mn.x, -0.5f);
    pggtest::expectF32Near(mx.x, 2.5f);
}

TEST(Module, ModuleImportingModule) {
    pgg::RunResult r = runWithLib(
        "import lib.outer\n"
        "x = outer.quadruple_it(x = 2.0)\n"
        "output x\n");
    pggtest::expectNoErrors(r);
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "x")), 8.0f);  // 2 -> 4 -> 8
}

TEST(Module, MissingModuleIsE501) {
    pgg::RunResult r = runWithLib(
        "import lib.no_such_module\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E501"), 1);
    EXPECT_TRUE(hasMessage(r, "E501", "no_such_module"));
}

TEST(Module, ImportWithoutRootsIsE501WithHint) {
    // pgg::run(text) has no implicit root (unlike runFile): imports need
    // explicit RunParams::importRoots.
    pgg::RunResult r = pgg::run(
        "import lib.rocks\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E501"), 1);
    bool hinted = false;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == "E501" && d.hint.find("importRoots") != std::string::npos) hinted = true;
    EXPECT_TRUE(hinted);
}

TEST(Module, ImportCycleIsE502) {
    pgg::RunResult r = runWithLib(
        "import lib.cyc_a\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E502"), 1);
    EXPECT_TRUE(hasMessage(r, "E502", "cyc_"));
}

TEST(Module, UnknownQualifiedSymbolIsE505) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "root = rng_from_seed(1)\n"
        "x = rocks.no_such_def(size = 1.0, rng = root)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E505"), 1);
    // Unknown namespace (never imported) is the same code.
    pgg::RunResult r2 = runWithLib(
        "import lib.rocks\n"
        "root = rng_from_seed(1)\n"
        "x = pebbles.make_pebble(size = 1.0, rng = root)\n"
        "output x\n");
    EXPECT_EQ(countCode(r2, "E505"), 1);
}

TEST(Module, OnlyDefsAreVisibleThroughNamespace) {
    // A module's top-level binding is not exported: calling it is E505.
    pgg::RunResult r = runWithLib(
        "import lib.with_binding\n"
        "x = with_binding.secret()\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E505"), 1);
    EXPECT_TRUE(hasMessage(r, "E505", "secret"));
}

TEST(Module, NamespaceConflictsAreE102) {
    // Two imports binding one namespace.
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "import lib.outer as rocks\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E102"), 1);
    // Namespace colliding with a built-in operation.
    pgg::RunResult r2 = runWithLib(
        "import lib.rocks as box\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r2, "E102"), 1);
    // Namespace colliding with a top-level binding.
    pgg::RunResult r3 = runWithLib(
        "rocks = 1\n"
        "import lib.rocks\n"
        "x = rocks\n"
        "output x\n");
    EXPECT_EQ(countCode(r3, "E102"), 1);
}

TEST(Module, LibraryDefWithoutDocstringIsE506) {
    pgg::RunResult r = runWithLib(
        "import lib.bad_doc\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E506"), 1);
    EXPECT_TRUE(hasMessage(r, "E506", "undocumented"));
}

TEST(Module, SameNamedWrapperDoesNotCollideWithLibraryDef) {
    // inn_hotel/spire_house wrap plan.brick_courses / masonry.clip_arch_hole
    // under the same unqualified name. Instance indices must be per name
    // (wrapper foo[0], library foo[1]), otherwise foo[0].x reads itself
    // and the engine stack-overflows.
    const std::string src =
        "import lib.same_name_inner as inner\n"
        "def foo(x: f32) -> (out: f32) {\n"
        "    out = inner.foo(x = x)\n"
        "}\n"
        "y = foo(x = 10.0)\n"
        "output y\n";
    pgg::RunResult r = runWithLib(src);
    pggtest::expectNoErrors(r);
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "y")), 11.0f);

    pgg::Document doc = pgg::parse(src, "<same-name>");
    for (const pgg::Diagnostic& d : doc.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    ASSERT_TRUE(doc.file);
    std::vector<pgg::Diagnostic> diags;
    pgg::ModuleClosure closure = pgg::loadModuleClosure(*doc.file, {kLibRoot}, diags);
    pgg::FlatProgram flat = pgg::expandProgram(*doc.file, &closure, diags);
    for (const pgg::Diagnostic& d : diags)
        if (!d.isWarning) ADD_FAILURE() << "expand: " << d.code << " " << d.message;
    ASSERT_EQ(flat.instances.size(), 2u);
    EXPECT_EQ(flat.instances[0].name, "foo[0]");
    EXPECT_EQ(flat.instances[0].path, "foo[0]");
    EXPECT_EQ(flat.instances[1].name, "foo[1]");
    EXPECT_EQ(flat.instances[1].path, "foo[0].foo[1]");
}

TEST(Module, VersionPinningIsAStageError) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks @ 1.2\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E201"), 1);
    EXPECT_TRUE(hasMessage(r, "E201", "versioning"));
}

TEST(Module, ProductLibRootFindsShippedLib) {
    const std::string lib = pgg::findProductLibRoot(kLibRoot);
    ASSERT_FALSE(lib.empty()) << "walk-up from corpus should find resources/pgg";
    EXPECT_TRUE(std::filesystem::is_directory(std::filesystem::path(lib) / "lib"));
}

TEST(Module, ProductLibRootLetsLibImportFromElsewhere) {
    const std::string lib = pgg::findProductLibRoot(kLibRoot);
    ASSERT_FALSE(lib.empty());
    pgg::RunParams p;
    p.importRoots.push_back(lib);
    pgg::RunResult r = pgg::run(
        "import lib.parts\n"
        "g = parts.cbox(size = vec3(1.0, 1.0, 1.0), k = 0.1)\n"
        "output g\n",
        p);
    pggtest::expectNoErrors(r);
}

TEST(Module, NestedImportSearchesModuleDirectory) {
    const auto root = std::filesystem::temp_directory_path() / "pgg_mod_sibling";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "pkg", ec);
    {
        std::ofstream f(root / "pkg" / "common.pgg");
        f << "def n() -> (out: f32) {\n    \"\"\"Sibling helper.\"\"\"\n    out = 4.0\n}\n";
    }
    {
        std::ofstream f(root / "pkg" / "item.pgg");
        f << "import common\n"
             "def n() -> (out: f32) {\n    \"\"\"Calls sibling common.\"\"\"\n    out = common.n()\n}\n";
    }
    {
        std::ofstream f(root / "main.pgg");
        f << "import pkg.item as it\nv = it.n()\noutput v\n";
    }
    pgg::RunResult r = pgg::runFile((root / "main.pgg").string());
    pggtest::expectNoErrors(r);
    ASSERT_TRUE(pggtest::outputOf(r, "v"));
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "v")), 4.0f);
    std::filesystem::remove_all(root, ec);
}

}  // namespace
