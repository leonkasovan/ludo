-- math library — abs, ceil, floor, sin, cos, sqrt, random, etc.

-- Constants
print(math.pi)    --> 3.1415926535898
print(math.huge)  --> inf

-- Rounding
print(math.abs(-7))     --> 7
print(math.ceil(2.3))   --> 3
print(math.floor(2.9))  --> 2

-- Min / Max
print(math.max(1, 5, 3)) --> 5
print(math.min(1, 5, 3)) --> 1

-- Trigonometry (angles in radians)
print(math.sin(math.pi/2))  --> 1.0
print(math.cos(math.pi))    --> -1.0
print(math.tan(math.pi/4))  --> ~1.0
print(math.deg(math.pi))    --> 180
print(math.rad(180))        --> 3.14159
print(math.asin(0.5))       --> ~0.524
print(math.acos(0.5))       --> ~1.047
print(math.atan2(1, 0))     --> ~1.571

-- Hyperbolic
print(math.cosh(0))         --> 1.0
print(math.sinh(0))         --> 0.0
print(math.tanh(1))         --> ~0.762

-- Exponents / Logs
print(math.sqrt(144))       --> 12
print(math.exp(1))          --> 2.71828 (e)
print(math.log(math.exp(1))) --> 1.0
print(math.log(100, 10))    --> 2.0
print(math.pow(2, 10))      --> 1024

-- Fractional
print(math.fmod(10, 3))     --> 1
local m, e = math.frexp(12.5)  -- m * 2^e
print(m, e)                  --> 0.78125  4
print(math.ldexp(0.78125, 4)) --> 12.5
local int, frac = math.modf(3.14159)
print(int, frac)             --> 3  0.14159

-- Degree / Radian helpers
print(math.deg(math.pi))     --> 180
print(math.rad(180))         --> 3.14159

-- Random numbers
math.randomseed(os.time())
print("random:", math.random())        --> [0, 1)
print("int 1-6:", math.random(1, 6))  --> [1, 6]
