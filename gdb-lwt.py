import gdb
from collections import defaultdict

# this is based on kazar's version of gdb/amd64-linux-tdep.cc.  Note
# that r8 is stored 0x28 bytes into the saved context ucontext_t, or
# 0x58 bytes into the Thread structure itself.  This labels each 8
# byte starting value in the context.  The ones labelled none
# represent values that aren't actually stored.
SAVED_REGS = ["r8", "r9", "none", "none",
              "r12", "r13", "r14", "r15",
              "rdi", "rsi", "rbp", "rbx",
              "rdx", "none", "rcx", "rsp",
              "rip"]

# This is another way of expressing what's in the context field
# set $r8=$3._ctx.uc_mcontext.gregs[0]
# set $r9=$3._ctx.uc_mcontext.gregs[1]
# set $r12=$3._ctx.uc_mcontext.gregs[4]
# set $r13=$3._ctx.uc_mcontext.gregs[5]
# set $r14=$3._ctx.uc_mcontext.gregs[6]
# set $r15=$3._ctx.uc_mcontext.gregs[7]
# set $rdi=$3._ctx.uc_mcontext.gregs[8]
# set $rsi=$3._ctx.uc_mcontext.gregs[9]
# set $rbp=$3._ctx.uc_mcontext.gregs[10]
# set $rbx=$3._ctx.uc_mcontext.gregs[11]
# set $rdx=$3._ctx.uc_mcontext.gregs[12]
# set $rcx=$3._ctx.uc_mcontext.gregs[14]
# set $rsp=$3._ctx.uc_mcontext.gregs[15]
# set $rip=$3._ctx.uc_mcontext.gregs[16]

# One of these for each load of this script.  ThreadContext maintains
# a copy of the initial register state for the first gdb thread (pthread)
# in saved_regs, and then loads from the Thread package's context
# field when you call the set_machine_regs_from_thread function.
#
# The function restore_machine_regs puts back the saved registers
# so that it is safe to continue from the stopped gdb.  The command
# that triggesr this is 'uthr done'
class ThreadContext:
    # called by set_machine_regs_from_thread to ensure that the
    # machine registers for the current pthread are preserved.
    def save_machine_regs(self):
        if self.tid == 0:
            self.tid = gdb.selected_thread().global_num
            print("Don't forget to 'uthr done' before resuming execution")
            for x in SAVED_REGS:
                if x == "none":
                    continue
                value = gdb.parse_and_eval("$" + x).format_string(format='x')
                self.saved_regs[x] = value

    # save the machine registers if necessary and then restore the register state
    # from the specified thread address into the current pthread's register state
    # so that gdb works with it.
    def set_machine_regs_from_thread(self, ptr):
        # save the pointer and ensure that we have the registers we're
        # going to overwrite saved in saved_regs
        self.ptr = ptr
        self.save_machine_regs()

        t1 = gdb.lookup_type('uint64_t').array(17)
        registers = ptr.dereference().cast(t1)

        # restore the registers
        for i in range(0,17):
            if SAVED_REGS[i] == "none":
                continue
            command = "set $" + SAVED_REGS[i] + "=" + str(registers[i])
            gdb.execute(command)
        
    # restore the saved registers now into the same pthread as we saved the
    # registers from on our first uthr command
    def restore_machine_regs(self):
        if (self.tid == 0):
            return -1
        command = "thread " + str(self.tid)
        gdb.execute(command)
        self.tid = 0
        for x in self.saved_regs:
            command = "set $" + x + "=" + str(self.saved_regs[x])
            gdb.execute(command)
        return 0

    def __init__(self):
        self.tid = 0
        self.saved_regs = {}
        self.thread_regs = {}
        self.saved = 0
        self.thread_addr = 0

class uthread(gdb.Command):
    "python thread command"

    def __init__(self):
        super(uthread, self).__init__("uthread", gdb.COMMAND_USER)
        self.thread_context = ThreadContext()

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) < 1:
            print("usage: uthread <thread address>");
            print("or restore state with: uthread done")
            return

        if (argv[0] == "done"):
            code = self.thread_context.restore_machine_regs()
            if (code == 0):
                print("Register state restored")
            else:
                print("Register state already restored")
            return

        # NB: the 88 is the offset of the general registers in the thread structure
        # when counting sizes don't forget that there's a _vptr at the
        # start of the thread structure
        v = int(argv[0], 0) + 88
        t1 = gdb.lookup_type("ucontext_t").pointer()
        ptr = gdb.Value(v).cast(t1)
        self.thread_context.set_machine_regs_from_thread(ptr)

uthread()
