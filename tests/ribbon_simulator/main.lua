display.setStatusBar(display.HiddenStatusBar)
display.setDefault('background',0,0,0)
local listener,test
local function finish(ok,message)
    if listener then Runtime:removeEventListener('enterFrame',listener) end
    local stage=display.getCurrentStage()
    while stage.numChildren>0 do display.remove(stage[stage.numChildren]) end
    test=nil;collectgarbage('collect')
    if message then print('RIBBON_SIMULATOR ERROR '..tostring(message)) end
    print('SIMULATOR_TEST_EXIT: '..(ok and '0' or '1'));io.flush()
end
test=coroutine.create(function()
    local Ribbon=require('plugin.geometry2d').ribbon
    local Reference=require('lua_reference')
    local effect=require('shader')
    local function waitFrames(n) for _=1,n do coroutine.yield() end end
    require('contracts')(Ribbon)
    require('geometry_compare')(Ribbon,Reference)
    require('pixels')(Ribbon,Reference,effect,waitFrames)
    require('benchmark')(Ribbon,Reference,effect,waitFrames)
    print('RIBBON_SIMULATOR PASS all tests')
end)
listener=function()
    local safe,failure=xpcall(function()
        local response
        while true do
            local ok,request=coroutine.resume(test,response)
            if not ok then finish(false,debug.traceback(test,tostring(request)));return end
            if coroutine.status(test)=='dead' then finish(true);return end
            if type(request)~='table' or not request.sampleX then return end
            response=nil
            display.colorSample(request.sampleX,request.sampleY,function(e)
                response={r=e.r,g=e.g,b=e.b,a=e.a}
            end)
            assert(response,'colorSample callback did not complete on main thread')
        end
    end,debug.traceback)
    if not safe then finish(false,failure) end
end
Runtime:addEventListener('enterFrame',listener)
