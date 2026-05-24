-- table library — insert, remove, sort, concat, pack, unpack

local t = { "banana", "apple", "cherry" }

-- Insert / Remove
table.insert(t, "date")
table.insert(t, 2, "blueberry")
print(table.concat(t, ", "))   --> banana, blueberry, apple, cherry, date

table.remove(t, 1)             -- remove first
print(table.concat(t, ", "))   --> blueberry, apple, cherry, date

-- Sort
table.sort(t)
print(table.concat(t, ", "))   --> apple, blueberry, cherry, date

table.sort(t, function(a, b) return #a > #b end)
print(table.concat(t, ", "))   --> blueberry, cherry, apple, date

-- Concat (join array into string)
local csv = table.concat({ "a", "b", "c" }, ",")
print(csv)                     --> a,b,c

-- Pack / Unpack
local packed = table.pack("x", "y", "z")
print(packed.n)                 --> 3
local a, b, c = table.unpack(packed)
print(a, b, c)                  --> x  y  z

-- Advanced: sort a table of records
local data = {
    { name = "Zoe",   age = 25 },
    { name = "Alice", age = 30 },
    { name = "Bob",   age = 20 },
}
table.sort(data, function(a, b) return a.age < b.age end)
for _, p in ipairs(data) do
    print(p.name, p.age)
end
--> Bob   20
--> Zoe   25
--> Alice 30
