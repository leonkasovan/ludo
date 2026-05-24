-- coroutine library — cooperative multitasking

-- Create + Resume + Yield
local co = coroutine.create(function()
    for i = 1, 3 do
        print("  coroutine: yielding", i)
        coroutine.yield(i * 10)
    end
    return "finished"
end)

print("status:", coroutine.status(co))  --> suspended

local ok, val = coroutine.resume(co)
print("resume 1:", ok, val)            --> true    10

ok, val = coroutine.resume(co)
print("resume 2:", ok, val)            --> true    20

ok, val = coroutine.resume(co)
print("resume 3:", ok, val)            --> true    30

ok, val = coroutine.resume(co)
print("resume 4:", ok, val)            --> true    finished

print("status:", coroutine.status(co))  --> dead

-- Wrap (simpler interface)
local cw = coroutine.wrap(function()
    for i = 1, 3 do coroutine.yield(i) end
end)
print(cw())  --> 1
print(cw())  --> 2
print(cw())  --> 3

-- Check if running in main thread
print("running:", coroutine.running())  --> thread, boolean
