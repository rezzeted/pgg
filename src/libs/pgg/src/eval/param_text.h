#pragma once

// Launch-param text parsing shared by PggTool, PggServe and PggViewer:
// scalars (bool/int/f32/vec/string — the historical --param rules) plus
// `@path` file references that load a geo<points> payload (geo_file.h).
// Relative @paths resolve against baseDir (the .pgg file's directory);
// a literal value starting with '@' is written with a doubled '@@'.

#include <string>

#include "value.h"

namespace pgg {

// True for `@path` (a single leading '@' with a non-empty path).
bool isFileParamRef(const std::string& text);

// The scalar --param rules (infallible, no file access): "true"/"false",
// "(x, y[, z[, w]])" vectors, integers, floats, otherwise a string.
Value parseScalarParamText(const std::string& text);

// Full param text: `@path` loads a points file, `@@...` is a literal
// string with one '@' stripped, anything else is parseScalarParamText.
// false + `err` when the file cannot be loaded (err may be null).
bool parseParamText(const std::string& text, const std::string& baseDir, Value& out,
                    std::string* err = nullptr);

}  // namespace pgg
