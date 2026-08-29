-- Keep the Solar2D entry point small: display-oriented examples and API
-- assertions live in separate files so each suite has one clear purpose.
local function runTests()
    require("visual_tests")
    require("assert_tests")
end

local ok, err = xpcall(runTests, debug.traceback)
if not ok then
    print(err)
    error(err, 0)
end
