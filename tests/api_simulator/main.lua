local root=system.pathForFile('',system.ResourceDirectory)
package.path=root..'/../../examples/solar2d/?.lua;'..package.path
local failed=false
Runtime:addEventListener('unhandledError',function(e)
    failed=true
    print(tostring(e.errorMessage));print(tostring(e.stackTrace))
    print('SIMULATOR_TEST_EXIT: 1');io.flush()
    return true
end)
local ok,err=xpcall(function() require('assert_tests') end,debug.traceback)
if not ok then print(err);print('SIMULATOR_TEST_EXIT: 1');io.flush()
else
    timer.performWithDelay(1000,function()
        if not failed then print('SIMULATOR_TEST_EXIT: 0');io.flush() end
    end)
end
