import os, sys, io
import math
import random
var1 = None

def setup():
    global var1
    var1 = 0
    var1 = 0 + var1
    var1 = math.sqrt(9)
    var1 = math.sin(math.radians(45))
    var1 = math.pi
    var1 = var1 % 2 == 0
    var1 = round(3.14)
    var1 = var1 % 1
    var1 = min(max(var1, 0), 100)
    var1 = random.randint(0, 100)
    var1 = random.random()
    var1 = None

def loop():
    global var1
    pass

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
