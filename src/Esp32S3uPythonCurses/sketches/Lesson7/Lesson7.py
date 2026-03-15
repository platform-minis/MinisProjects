import os, sys, io

def setup():
    pass

def loop():
    pass

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
