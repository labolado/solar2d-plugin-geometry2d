#pragma once

#include "geometry2d_lua.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace Geometry2D {

enum class PathVerb {
    MoveTo,
    LineTo,
    QuadraticTo,
    CubicTo,
    Close,
};

struct PathCommand {
    PathVerb verb = PathVerb::MoveTo;
    std::array<float, 6> values{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
};

struct PathContour {
    std::vector<std::pair<float, float>> points;
    bool closed = false;
};

// Reads either a raw command array or {commands={...}}. Contract violations
// raise a Lua error; false only means the argument was not a table.
bool ReadPathCommands(lua_State *L, int arg, std::vector<PathCommand> &commands);

// Used by retained paths before committing structural edits.
bool ValidatePathCommands(const std::vector<PathCommand> &commands, std::string &error);

// Geometry failures (for example maxCurvePoints) are returned in error.
bool FlattenPathCommands(const std::vector<PathCommand> &commands,
                         const MeshOptions &options,
                         std::vector<PathContour> &contours,
                         std::string &error);

bool GroupPathFillContours(const std::vector<PathContour> &contours,
                           std::vector<Polygon> &groups,
                           std::string &error);

} // namespace Geometry2D
