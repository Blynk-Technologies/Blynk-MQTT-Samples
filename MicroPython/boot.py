import sys

# Allow overriding frozen modules
sys.path.remove(".frozen")
sys.path.append(".frozen")
