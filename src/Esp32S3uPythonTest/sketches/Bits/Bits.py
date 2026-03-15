import os, sys, io
item = None

def setup():
    global item
    item = 0
    item = item & 8
    item = ~item
    item = ((item >> 3) & 0x01)
    item = (item | (0x01 << 3))
    item = (item & (~(0x01 << 3)))
    item = (item ^ (0x01 << 3))
    item = int.from_bytes(item, 'big')
    item = int('3.14')
    item = float('3.14')

def loop():
    global item
    item = None

    item = None

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
