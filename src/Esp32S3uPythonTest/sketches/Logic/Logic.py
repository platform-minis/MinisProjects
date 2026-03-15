import os, sys, io
item = None

def setup():
    global item
    item = 0
    if item == 0:
        pass
    item = True and not False
    item = None
    item = 2 if item else 3

def loop():
    global item
    pass

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
