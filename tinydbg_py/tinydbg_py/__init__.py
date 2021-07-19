import ctypes
from dataclasses import dataclass

TINYDBG_FLAG_NO_ASLR = 0b1

lib = ctypes.CDLL("./tinydbg_lib")
lib.TinyDbg_start_advanced.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p), ctypes.c_uint]
lib.TinyDbg_start_advanced.restype = ctypes.c_void_p

lib.TinyDbg_free.argtypes = [ctypes.c_void_p]

lib.EventQueue_join.argtypes = [ctypes.c_void_p]
lib.EventQueue_detach.argtypes = [ctypes.c_void_p]

class EventQueue_JoinHandle:
    def __init__(self, handle):
        self.handle = handle

    def join(self):
        lib.EventQueue_join(self.handle)
    def detach(self):
        lib.EventQueue_detach(self.handle)

class EventQueue_join_with_value:
    def __init__(self, value, handle):
        self.value = value
        self.handle = handle

    def join(self):
        lib.EventQueue_join(self.handle)
        return self.value

class user_regs_struct(ctypes.Structure):
    _fields_ = [
        ("r15", ctypes.c_ulonglong),
        ("r14", ctypes.c_ulonglong),
        ("r13", ctypes.c_ulonglong),
        ("r12", ctypes.c_ulonglong),
        ("rbp", ctypes.c_ulonglong),
        ("rbx", ctypes.c_ulonglong),
        ("r11", ctypes.c_ulonglong),
        ("r10", ctypes.c_ulonglong),
        ("r9", ctypes.c_ulonglong),
        ("r8", ctypes.c_ulonglong),
        ("rax", ctypes.c_ulonglong),
        ("rcx", ctypes.c_ulonglong),
        ("rdx", ctypes.c_ulonglong),
        ("rsi", ctypes.c_ulonglong),
        ("rdi", ctypes.c_ulonglong),
        ("orig_rax", ctypes.c_ulonglong),
        ("rip", ctypes.c_ulonglong),
        ("cs", ctypes.c_ulonglong),
        ("eflags", ctypes.c_ulonglong),
        ("rsp", ctypes.c_ulonglong),
        ("ss", ctypes.c_ulonglong),
        ("fs_base", ctypes.c_ulonglong),
        ("gs_base", ctypes.c_ulonglong),
        ("ds", ctypes.c_ulonglong),
        ("es", ctypes.c_ulonglong),
        ("fs", ctypes.c_ulonglong),
        ("gs", ctypes.c_ulonglong),
    ]

class TinyDbg_Breakpoint_struct(ctypes.Structure):
    _fields_ = [("position", ctypes.c_void_p), ("is_once", ctypes.c_bool), ("original", ctypes.c_char)]

class TinyDbg_Event_content_union(ctypes.Union):
    _fields_ = [("stop_code", ctypes.c_int), ("syscall_id", ctypes.c_int), ("breakpoint", TinyDbg_Breakpoint_struct)]

class TinyDbg_Event_struct(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("content", TinyDbg_Event_content_union)]

class EventQueue_Consumer:
    def __init__(handle):
        self.handle = handle

    def consume(self):
        event_content = TinyDbg_Event_struct()
        lib.EventQueue_consume(self.handle, ctypes.byref(event_content))
        return event_content

@dataclass(init=False, eq=False)
class TinyDbg:
    pid: int
    
    """Wrapper for TinyDbg_start"""
    def __init__(self, filename, argv, envp, flags=0):
        new_argv = (ctypes.c_char_p * (len(argv) + 1))()
        new_argv[:] = [x.encode("utf-8") for x in argv] + [ctypes.c_char_p(0)]

        envp = [f"{x[0]}={x[1]}".encode("utf-8") for x in envp.items()]
        new_envp = (ctypes.c_char_p * (len(envp) + 1))()
        new_envp[:] = envp + [ctypes.c_char_p(0)]

        self.handle = lib.TinyDbg_start_advanced(ctypes.create_string_buffer(filename.encode("utf-8")), ctypes.cast(new_argv, ctypes.POINTER(ctypes.c_char_p)), new_envp, flags)
        self.pid = ctypes.cast(self.handle, ctypes.POINTER(ctypes.c_int))[0]

    """Wrapper for TinyDbg_continue"""
    def cont(self) -> EventQueue_JoinHandle:
        return EventQueue_JoinHandle(lib.TinyDbg_continue(self.handle))

    def get_regs(self) -> EventQueue_join_with_value:
        regs = user_regs_struct()
        return EventQueue_join_with_value(regs, lib.TinyDbg_get_registers(self.handle, ctypes.byref(regs)))

    def __del__(self):
        lib.TinyDbg_free(self.handle)
