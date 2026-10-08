local function run()
    assert(type(package.preload['plugin.geometry2d']) == 'function', 'local loader not installed')
    local g = require('plugin.geometry2d')
    local data = assert(g.util.meshFill({0,0,40,0,0,40}, {aa='none'}))
    assert(#data.vertices == 6)
    local mesh = assert(g.util.meshFill({0,0,40,0,0,40}, {aa='none',output='mesh'}))
    display.remove(mesh)
end
local ok, message = xpcall(run, debug.traceback)
if not ok then print(message) end
print('SIMULATOR_TEST_EXIT: '..(ok and '0' or '1'));io.flush()
