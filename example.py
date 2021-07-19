import tinydbg_py, sys, os, time

tinydbg = tinydbg_py.TinyDbg("test", sys.argv, os.environ, tinydbg_py.TINYDBG_FLAG_NO_ASLR)
tinydbg.cont().join()
time.sleep(1)
print(tinydbg.get_regs().join().rip)

time.sleep(5)
